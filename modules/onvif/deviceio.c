/**
 * @file media.c
 *
 * For more information see:
 * https://www.onvif.org/ver10/media/wsdl/media.wsdl
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "deviceio.h"
#include "soap.h"
#include "fault.h"
#include "soap_str.h"
#include "wsd.h"
#include "media.h"
#include "rtspd.h"

#define DEBUG_MODULE "onvif_deviceio"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


/**
 * !IN CASE OF NS=DEVICEIO @media_GetVideoSources_h IS JUST A WRAPPER FUNCTION!
 *
 * function to handle GetVideoSources in different namespace (DeviceIO)
 * @media_GetVideoSources_h function defines in which namespace the method
 * should be handeld
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int deviceio_GetVideoSources_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gvsrc, *tokenc;
	struct media_config *cfg;
	struct le *le;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_deviceio_wsdl, str_uri_deviceio_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gvsrc = soap_add_child(resp, b, str_pf_deviceio_wsdl,
		str_method_get_videosources_r);

	le = vs_l.head;
	while (le) {
		cfg = le->data;
		tokenc = soap_add_child(resp, gvsrc, str_pf_deviceio_wsdl, str_uctoken);
		err |= soap_set_value_fmt(tokenc, "%s", cfg->t.vs.sourcetoken);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * !IN CASE OF NS=DEVICEIO @media_GetAudioSources_h IS JUST A WRAPPER FUNCTION!
 *
 * function to handle GetAudioSources in different namespace (DeviceIO)
 * @media_GetAudioSources_h function defines in which namespace the method
 * should be handeld
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int deviceio_GetAudioSources_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gvsrc, *tokenc;
	struct media_config *cfg;
	struct le *le;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_deviceio_wsdl, str_uri_deviceio_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gvsrc = soap_add_child(resp, b, str_pf_deviceio_wsdl,
		str_method_get_audiosources_r);

	le = as_l.head;
	while (le) {
		cfg = le->data;
		tokenc = soap_add_child(resp, gvsrc, str_pf_deviceio_wsdl, str_uctoken);
		err |= soap_set_value_fmt(tokenc, "%s", cfg->t.as.sourcetoken);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * !IN CASE OF NS=DEVICEIO @media_GetAudioOutput_h IS JUST A WRAPPER FUNCTION!
 *
 * function to handle GetAudioOutput in different namespace (DeviceIO)
 * @media_GetAudioOutput_h function defines in which namespace the method
 * should be handeld
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int deviceio_GetAudioOutputs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gaosc, *tokenc;
	struct media_config *cfg;
	struct le *le;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_deviceio_wsdl, str_uri_deviceio_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gaosc = soap_add_child(resp, b, str_pf_deviceio_wsdl,
		str_method_get_audiooutputs_r);

	le = ao_l.head;
	while (le) {
		cfg = le->data;
		tokenc = soap_add_child(resp, gaosc, str_pf_deviceio_wsdl, str_uctoken);
		err |= soap_set_value_fmt(tokenc, "%s", cfg->t.ao.outputtoken);
		le = le->next;
	}


  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}