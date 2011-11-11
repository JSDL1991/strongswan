/*
 * Copyright (C) 2011 Sansar Choinyambuu
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "imv_attestation_state.h"

#include <utils/lexparser.h>
#include <utils/linked_list.h>
#include <debug.h>

typedef struct private_imv_attestation_state_t private_imv_attestation_state_t;
typedef struct file_meas_request_t file_meas_request_t;
typedef struct comp_evid_request_t comp_evid_request_t;

/**
 * PTS File/Directory Measurement request entry
 */
struct file_meas_request_t {
	u_int16_t id;
	int file_id;
	bool is_dir;
};

/**
 * Functional Component Evidence Request entry
 */
struct comp_evid_request_t {
	u_int32_t vendor_id;
	pts_qualifier_t qualifier;
	pts_ita_funct_comp_name_t name;
};

/**
 * Private data of an imv_attestation_state_t object.
 */
struct private_imv_attestation_state_t {

	/**
	 * Public members of imv_attestation_state_t
	 */
	imv_attestation_state_t public;

	/**
	 * TNCCS connection ID
	 */
	TNC_ConnectionID connection_id;

	/**
	 * TNCCS connection state
	 */
	TNC_ConnectionState state;
	
	/**
	 * IMV Attestation handshake state
	 */
	imv_attestation_handshake_state_t handshake_state;

	/**
	 * IMV action recommendation
	 */
	TNC_IMV_Action_Recommendation rec;

	/**
	 * IMV evaluation result
	 */
	TNC_IMV_Evaluation_Result eval;

	/**
	 * File Measurement Request counter
	 */
	u_int16_t file_meas_request_counter;

	/**
	 * List of PTS File/Directory Measurement requests
	 */
	linked_list_t *file_meas_requests;

	/**
	 * List of Functional Component Evidence requests
	 */
	linked_list_t *comp_evid_requests;

	/**
	 * PTS object
	 */
	pts_t *pts;

	/**
	 * Measurement error
	 */
	bool measurement_error;

};

typedef struct entry_t entry_t;

/**
 * Define an internal reason string entry
 */
struct entry_t {
	char *lang;
	char *string;
};

/**
 * Table of multi-lingual reason string entries 
 */
static entry_t reasons[] = {
	{ "en", "IMV Attestation: Non-matching file measurement/s or invalid TPM Quote signature" },
	{ "mn", "IMV Attestation: Файлуудын хэмжилт зөрсөн эсвэл буруу TPM Quote гарын үсэг" },
	{ "de", "IMV Attestation: Falsche Datei Messung/en oder TPM Quote Unterschrift ist ungültig" },
};

METHOD(imv_state_t, get_connection_id, TNC_ConnectionID,
	private_imv_attestation_state_t *this)
{
	return this->connection_id;
}

METHOD(imv_state_t, change_state, void,
	private_imv_attestation_state_t *this, TNC_ConnectionState new_state)
{
	this->state = new_state;
}

METHOD(imv_state_t, get_recommendation, void,
	private_imv_attestation_state_t *this, TNC_IMV_Action_Recommendation *rec,
									TNC_IMV_Evaluation_Result *eval)
{
	*rec = this->rec;
	*eval = this->eval;
}

METHOD(imv_state_t, set_recommendation, void,
	private_imv_attestation_state_t *this, TNC_IMV_Action_Recommendation rec,
									TNC_IMV_Evaluation_Result eval)
{
	this->rec = rec;
	this->eval = eval;
}

METHOD(imv_state_t, get_reason_string, bool,
	private_imv_attestation_state_t *this, chunk_t preferred_language,
	chunk_t *reason_string, chunk_t *reason_language)
{
	chunk_t pref_lang, lang;
	u_char *pos;
	int i;

	while (eat_whitespace(&preferred_language))
	{
		if (!extract_token(&pref_lang, ',', &preferred_language))
		{
			/* last entry in a comma-separated list or single entry */
			pref_lang = preferred_language;
		}

		/* eat trailing whitespace */
		pos = pref_lang.ptr + pref_lang.len - 1;
		while (pref_lang.len && *pos-- == ' ')
		{
			pref_lang.len--;
		}

		for (i = 0 ; i < countof(reasons); i++)
		{
			lang = chunk_create(reasons[i].lang, strlen(reasons[i].lang));
			if (chunk_equals(lang, pref_lang))
			{
				*reason_language = lang;
				*reason_string = chunk_create(reasons[i].string,
										strlen(reasons[i].string));
				return TRUE;
			}
		}
	}

	/* no preferred language match found - use the default language */
	*reason_string =   chunk_create(reasons[0].string,
									strlen(reasons[0].string));
	*reason_language = chunk_create(reasons[0].lang,
									strlen(reasons[0].lang));
	return TRUE;
}

METHOD(imv_state_t, destroy, void,
	private_imv_attestation_state_t *this)
{
	this->file_meas_requests->destroy_function(this->file_meas_requests, free);
	this->comp_evid_requests->destroy_function(this->comp_evid_requests, free);
	this->pts->destroy(this->pts);
	free(this);
}

METHOD(imv_attestation_state_t, get_handshake_state,
	   imv_attestation_handshake_state_t, private_imv_attestation_state_t *this)
{
	return this->handshake_state;
}

METHOD(imv_attestation_state_t, set_handshake_state, void,
	private_imv_attestation_state_t *this,
	imv_attestation_handshake_state_t new_state)
{
	this->handshake_state = new_state;
}

METHOD(imv_attestation_state_t, get_pts, pts_t*,
	private_imv_attestation_state_t *this)
{
	return this->pts;
}

METHOD(imv_attestation_state_t, add_file_meas_request, u_int16_t,
	private_imv_attestation_state_t *this, int file_id, bool is_dir)
{
	file_meas_request_t *request;

	request = malloc_thing(file_meas_request_t);
	request->id = ++this->file_meas_request_counter;
	request->file_id = file_id;
	request->is_dir = is_dir;
	this->file_meas_requests->insert_last(this->file_meas_requests, request);

	return this->file_meas_request_counter;
}

METHOD(imv_attestation_state_t, check_off_file_meas_request, bool,
	private_imv_attestation_state_t *this, u_int16_t id, int *file_id,
	bool* is_dir)
{
	enumerator_t *enumerator;
	file_meas_request_t *request;
	bool found = FALSE;
	
	enumerator = this->file_meas_requests->create_enumerator(this->file_meas_requests);
	while (enumerator->enumerate(enumerator, &request))
	{
		if (request->id == id)
		{
			found = TRUE;
			*file_id = request->file_id;
			*is_dir = request->is_dir;
			this->file_meas_requests->remove_at(this->file_meas_requests, enumerator);
			free(request);
			break;
		}
	}
	enumerator->destroy(enumerator);
	return found;
}

METHOD(imv_attestation_state_t, get_file_meas_request_count, int,
	private_imv_attestation_state_t *this)
{
	return this->file_meas_requests->get_count(this->file_meas_requests);
}

METHOD(imv_attestation_state_t, add_comp_evid_request, void,
	private_imv_attestation_state_t *this, u_int32_t vendor_id,
	pts_qualifier_t qualifier, pts_ita_funct_comp_name_t comp_name)
{
	comp_evid_request_t *request;

	request = malloc_thing(comp_evid_request_t);
	request->vendor_id = vendor_id;
	request->qualifier = qualifier;
	request->name = comp_name;
	this->comp_evid_requests->insert_last(this->comp_evid_requests, request);
}

METHOD(imv_attestation_state_t, check_off_comp_evid_request, bool,
	private_imv_attestation_state_t *this, u_int32_t vendor_id,
	pts_qualifier_t qualifier, pts_ita_funct_comp_name_t comp_name)
{
	enumerator_t *enumerator;
	comp_evid_request_t *request;
	bool found = FALSE;

	enumerator = this->comp_evid_requests->create_enumerator(this->comp_evid_requests);
	while (enumerator->enumerate(enumerator, &request))
	{
		if (request->vendor_id == vendor_id &&
			request->qualifier.kernel == qualifier.kernel &&
			request->qualifier.sub_component == qualifier.sub_component &&
			request->qualifier.type == qualifier.type &&
			request->name == comp_name)
		{
			found = TRUE;
			this->comp_evid_requests->remove_at(this->comp_evid_requests, enumerator);
			free(request);
			break;
		}
	}
	enumerator->destroy(enumerator);
	return found;
}

METHOD(imv_attestation_state_t, get_comp_evid_request_count, int,
	private_imv_attestation_state_t *this)
{
	return this->comp_evid_requests->get_count(this->comp_evid_requests);
}

METHOD(imv_attestation_state_t, get_measurement_error, bool,
	private_imv_attestation_state_t *this)
{
	return this->measurement_error;
}

METHOD(imv_attestation_state_t, set_measurement_error, void,
	private_imv_attestation_state_t *this)
{
	this->measurement_error = TRUE;
}

/**
 * Described in header.
 */
imv_state_t *imv_attestation_state_create(TNC_ConnectionID connection_id)
{
	private_imv_attestation_state_t *this;
	char *platform_info;

	INIT(this,
		.public = {
			.interface = {
				.get_connection_id = _get_connection_id,
				.change_state = _change_state,
				.get_recommendation = _get_recommendation,
				.set_recommendation = _set_recommendation,
				.get_reason_string = _get_reason_string,
				.destroy = _destroy,
			},
			.get_handshake_state = _get_handshake_state,
			.set_handshake_state = _set_handshake_state,
			.get_pts = _get_pts,
			.add_file_meas_request = _add_file_meas_request,
			.check_off_file_meas_request = _check_off_file_meas_request,
			.get_file_meas_request_count = _get_file_meas_request_count,
			.add_comp_evid_request = _add_comp_evid_request,
			.check_off_comp_evid_request = _check_off_comp_evid_request,
			.get_comp_evid_request_count = _get_comp_evid_request_count,
			.get_measurement_error = _get_measurement_error,
			.set_measurement_error = _set_measurement_error,
		},
		.connection_id = connection_id,
		.state = TNC_CONNECTION_STATE_CREATE,
		.handshake_state = IMV_ATTESTATION_STATE_INIT,
		.rec = TNC_IMV_ACTION_RECOMMENDATION_NO_RECOMMENDATION,
		.eval = TNC_IMV_EVALUATION_RESULT_DONT_KNOW,
		.file_meas_requests = linked_list_create(),
		.comp_evid_requests = linked_list_create(),
		.pts = pts_create(FALSE),
	);

	platform_info = lib->settings->get_str(lib->settings,
						 "libimcv.plugins.imv-attestation.platform_info", NULL);
	if (platform_info)
	{
		this->pts->set_platform_info(this->pts, platform_info);
	}
	
	return &this->public.interface;
}
