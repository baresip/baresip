/**
 * @file event.c
 *
 * For more information see:
 * https://www.onvif.org/ver10/events/wsdl/event.wsdl
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ifaddrs.h>
#include <time.h>
#include <netpacket/packet.h>
#include <re.h>
#include <baresip.h>

#include "soap.h"
#include "soap_str.h"
#include "event.h"

#define DEBUG_MODULE "onvif_event"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


/**
 * handle GetEventProperties requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int event_GetEventProperties_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b = NULL;

	DEBUG_INFO("TEST\n");

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_events_wsdl, str_uri_events_wsdl)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);

  out:
	if (!b)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}