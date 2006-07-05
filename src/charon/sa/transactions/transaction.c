/**
 * @file transaction.c
 * 
 * @brief Generic contstructor for the different transaction types.
 * 
 */

/*
 * Copyright (C) 2006 Martin Willi
 * Hochschule fuer Technik Rapperswil
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

#include "transaction.h"

#include <sa/child_sa.h>
#include <sa/transactions/ike_sa_init.h>
#include <sa/transactions/ike_auth.h>
#include <sa/transactions/delete_ike_sa.h>
#include <sa/transactions/dead_peer_detection.h>
#include <encoding/payloads/ts_payload.h>
#include <encoding/payloads/sa_payload.h>
#include <encoding/payloads/nonce_payload.h>
#include <encoding/payloads/notify_payload.h>
#include <encoding/payloads/delete_payload.h>
#include <utils/logger_manager.h>


/*
 * see header file
 */
transaction_t *transaction_create(ike_sa_t *ike_sa, message_t *request)
{
	iterator_t *iterator;
	payload_t *current;
	notify_payload_t *notify;
	transaction_t *transaction = NULL;
	u_int32_t message_id;
	
	if (!request->get_request(request))
	{
		return NULL;
	}
	message_id = request->get_message_id(request);
	
	switch (request->get_exchange_type(request))
	{
		case IKE_SA_INIT:
		{
			transaction = (transaction_t*)ike_sa_init_create(ike_sa, message_id);
			break;
		}
		case IKE_AUTH:
		{
			/* IKE_AUTH is always created in IKE_SA_INIT, it never should
			 * appear alone */
			break;
		}
		case CREATE_CHILD_SA:
		{
			/* look for a REKEY_SA notify */
			iterator = request->get_payload_iterator(request);
			while (iterator->has_next(iterator))
			{
				iterator->current(iterator, (void**)&current);
				if (current->get_type(current) != NOTIFY)
				{
					continue;
				}
				notify = (notify_payload_t*)current;
				if (notify->get_notify_type(notify) != REKEY_SA)
				{
					continue;
				}
				switch (notify->get_protocol_id(notify))
				{
					case PROTO_IKE:
						/* TODO: transaction = rekey_ike_sa_create(ike_sa, message_id); */
						break;
					case PROTO_AH:
					case PROTO_ESP:
					{
						/* TODO: transaction = rekey_child_sa_create(ike_sa, message_id); */
						break;
					}
					default:
						break;
				}
				if (transaction)
				{
					break;
				}
			}
			iterator->destroy(iterator);
			break;
		}
		case INFORMATIONAL:
		{
			u_int payload_count = 0;
			iterator = request->get_payload_iterator(request);
			while (iterator->has_next(iterator))
			{
				payload_count++;
				iterator->current(iterator, (void**)&current);
				switch (current->get_type(current))
				{
					case DELETE:
					{
						delete_payload_t *delete_payload;
						delete_payload = (delete_payload_t*)current;
						if (delete_payload->get_protocol_id(delete_payload) == PROTO_IKE)
						{
							transaction = (transaction_t*)
									delete_ike_sa_create(ike_sa, message_id);
							break;
						}
					}
					default:
						break;
				}
				if (transaction)
				{
					break;
				}
			}
			iterator->destroy(iterator);
			if (payload_count == 0)
			{
				transaction = (transaction_t*)
						dead_peer_detection_create(ike_sa, message_id);
			}
			break;
		}
		default:
			break;
	}
	return transaction;	
}
