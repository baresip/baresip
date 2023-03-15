/**
 * @file wsd.c
 *
 * Copyright (C) 2018 commend.com - Christian Spielberger
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "soap.h"
#include "wsd.h"
#include "scopes.h"
#include "soap_str.h"
#include "device.h"

#define DEBUG_MODULE "wsd"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static uint32_t message_number = 0;
static uint32_t instance_id = 0;
static uint32_t metadata_version = 0;

/**
 * decode the type of the wsd request message
 *
 * @param type              pointer to the type child of the message
 *
 * @return   if success: pointer to the string with the type
 *                          NULL otherwise
 */
static const char *wsd_decode_type(struct soap_child *type)
{
	const char *type_start;
	const char spacer = ':';
	size_t len;

	if (!type)
		return NULL;

	type_start = pl_strchr(&type->value, spacer);
	if (!type_start)
		return NULL;

	len = type->value.l - (++type_start - type->value.p);
	if (0 == strncmp(type_start, str_type_nvt, len))
		return str_type_nvt;
	else if (0 == strncmp(type_start, str_type_dev, len))
		return str_type_dev;
	else
		return NULL;

	return NULL;
}


/**
 * create a webservice discovery hello or bye message
 *
 * @param ptrmsg        pointer as return value of the soap message
 * @param action        string defining a hello or bye message (hello / bye)
 *
 * @return   if success: pointer to the namespace element
 *                          NULL otherwise
 */
static int wsd_send_hello_bye(struct soap_msg **ptrmsg, const char *action)
{
	int err = 0;
	char tb_uuid[UUID_TB_SIZE];
	struct soap_msg *msg;
	struct soap_child *c, *cc, *h, *b, *eprc, *mdvc, *sc, *tc, *xaddrc;
	const struct sa *laddr;

	if (!ptrmsg || !action)
		return EINVAL;

	err = generate_timebased_uuid(tb_uuid, UUID_TB_SIZE);
	if (err)
		return err;

	err = soap_alloc_msg(&msg);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		msg, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		msg, str_pf_network_wsdl, str_uri_network_wsdl) ||
		soap_msg_add_ns_str_param(
		msg, str_pf_addressing, str_uri_xml_soap_addressing) ||
		soap_msg_add_ns_str_param(
		msg, str_pf_discovery, str_uri_discovery)){
		err = EINVAL;
		goto out;
	}

	h = soap_add_child(msg, msg->envelope, str_pf_envelope, str_header);
	b = soap_add_child(msg, msg->envelope, str_pf_envelope, str_body);

	c = soap_add_child(msg, h, str_pf_addressing, str_wsd_action);
	err |= soap_set_value_fmt(c, "%s/%s", str_wsd_action_url, action);

	c = soap_add_child(msg, h, str_pf_addressing, str_wsd_messageid);
	err |= soap_set_value_fmt(c, "uuid:%s", tb_uuid);

	c = soap_add_child(msg, h, str_pf_addressing, str_wsd_to);
	err |= soap_set_value_fmt(c, "%s", str_wsd_to_value);

	c = soap_add_child(msg, h, str_pf_discovery, str_wsd_appsequence);
	err |= soap_add_parameter_uint(c, NULL, str_wsd_instanceid,
		strlen(str_wsd_instanceid), instance_id);
	err |= soap_add_parameter_uint(c, NULL, str_wsd_messagenumber,
		strlen(str_wsd_messagenumber), message_number++);

	c = soap_add_child(msg, b, str_pf_discovery, action);
	eprc = soap_add_child(msg, c, str_pf_addressing,
			      str_wsd_endpointreference);
	tc = soap_add_child(msg, c, str_pf_discovery, str_wsd_types);
	sc = soap_add_child(msg, c, str_pf_discovery, str_wsd_scopes);
	xaddrc = soap_add_child(msg, c, str_pf_discovery, str_wsd_xaddrs);
	mdvc = soap_add_child(msg, c, str_pf_discovery,
			      str_wsd_meadataversion);

	err |= soap_set_value_fmt(tc, "%s:%s %s:%s",
		str_pf_network_wsdl, str_type_nvt, str_pf_device_wsdl,
		str_type_dev);

	cc = soap_add_child(msg, eprc, str_pf_addressing, str_wsd_address);
	err |= soap_set_value_fmt(cc, "urn:uuid:%s", conf_config()->sip.uuid);

	err |= scope_add_all_scopes(NULL, msg, sc, false);
	err |= soap_set_value_fmt(mdvc, "%u", metadata_version);

	laddr =  net_laddr_af(baresip_network(), AF_INET);
	if (!laddr) {
		warning("onvif: %s Could not get local IP address.", __func__);
		return EINVAL;
	}

	err |= soap_set_value_fmt(xaddrc, "http://%j:%u%s",
		laddr, DEFAULT_ONVIF_PORT, str_device_uri);

  out:
	if (err)
		mem_deref(msg);
	else
		*ptrmsg = msg;

	return 0;
}


/**
 * create a webservice discovery probe / resolve match
 *
 * @param msg               original request message
 * @param ptrmsg            pointer as return value of the soap message
 * @param action            string defining a probe match or
 *                          resolve match message (ProbeMatch, ResolveMatch)
 * @return   if success: pointer to the namespace element
 *                          NULL otherwise
 */
static int wsd_answer_probe_resolve(const struct soap_msg *msg,
	struct soap_msg **presponse, const char *action)
{
	int err = 0;
	char tb_uuid[UUID_TB_SIZE];
	const struct sa *laddr;
	struct soap_msg *response = NULL;
	struct soap_child *typec, *c = NULL, *cc, *c_msg_id, *c_msg_reply;
	struct soap_child *h, *b, *eprc;
	const char *str_type;

	if (!msg || !presponse || !action)
		return EINVAL;

	/* check if the request searchs for a NetworkVideoTransmitter */
	typec = soap_child_has_child(soap_child_has_child(
			soap_child_has_child(msg->envelope, NULL, str_body),
			NULL, str_wsd_probe), NULL, str_wsd_types);

	if (typec && pl_isset(&typec->value)) {
		str_type = wsd_decode_type(typec);
		if (!str_type)
			goto out;
	}

	c_msg_reply = soap_child_has_child(msg->envelope,NULL, str_header);
	c_msg_reply = soap_child_has_child(c_msg_reply, NULL,
					   str_wsd_reply_to);
	c_msg_reply = soap_child_has_child(c_msg_reply, NULL, str_wsd_address);

	err = generate_timebased_uuid(tb_uuid, UUID_TB_SIZE);
	if (err)
		return err;

	c_msg_id = soap_child_has_child(
		soap_child_has_child(msg->envelope, NULL, str_header),
		NULL, str_wsd_messageid);
	if (!c_msg_id)
		return EINVAL;

	err = soap_alloc_msg(&response);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		response, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		response, str_pf_addressing, str_uri_xml_soap_addressing) ||
		soap_msg_add_ns_str_param(
		response, str_pf_discovery, str_uri_discovery) ||
		soap_msg_add_ns_str_param(
		response, str_pf_network_wsdl, str_uri_network_wsdl) ||
		soap_msg_add_ns_str_param(
		response, str_pf_schema, str_uri_schema)){
		err = EINVAL;
		goto out;
	}

	h = soap_add_child(response, response->envelope,
		str_pf_envelope, str_header);
	b = soap_add_child(response, response->envelope,
		str_pf_envelope, str_body);
	cc = soap_add_child(response, h, str_pf_addressing, str_wsd_action);
	if (0 == strncmp(action, str_wsd_probe_match, strlen(action)))
		err |= soap_set_value_fmt(cc, "%s/%s", str_wsd_action_url,
			str_wsd_probe_matches);
	else if (0 == strncmp(action, str_wsd_resolve_match, strlen(action)))
		err |= soap_set_value_fmt(cc, "%s/%s", str_wsd_action_url,
			str_wsd_resolve_matches);

	cc = soap_add_child(response, h, str_pf_addressing, str_wsd_messageid);
	err |= soap_set_value_fmt(cc, "uuid:%s", tb_uuid);

	cc = soap_add_child(response, h, str_pf_addressing,
			    str_wsd_relates_to);
	err |= soap_set_value_fmt(cc, "%r", &c_msg_id->value);

	if (!c_msg_reply || (c_msg_reply && 0 == pl_strcmp(&c_msg_reply->value,
		str_wsd_addressing_role_anon))) {
		cc = soap_add_child(response, h, str_pf_addressing,
				    str_wsd_to);
		err |= soap_set_value_fmt(cc, "%s",
					  str_wsd_addressing_role_anon);
	}

	cc = soap_add_child(response, h, str_pf_discovery,
			    str_wsd_appsequence);
	err |= soap_add_parameter_uint(cc, NULL, str_wsd_instanceid,
		strlen(str_wsd_instanceid), instance_id);
	err |= soap_add_parameter_uint(cc, NULL, str_wsd_messagenumber,
		strlen(str_wsd_messagenumber), message_number++);
	if (err)
		goto out;

	if (0 == strncmp(action, str_wsd_probe_match, strlen(action))) {
		c = soap_add_child(response, b,
			str_pf_discovery, str_wsd_probe_matches);
		c = soap_add_child(response, c,
			str_pf_discovery, str_wsd_probe_match);
	}
	else if (0 == strncmp(action, str_wsd_resolve_match,
				strlen(action))) {
		c = soap_add_child(response, b,
			str_pf_discovery, str_wsd_resolve_matches);
		c = soap_add_child(response, c,
			str_pf_discovery, str_wsd_resolve_match);
	}

	eprc = soap_add_child(response, c, str_pf_addressing,
		str_wsd_endpointreference);
	cc = soap_add_child(response, eprc, str_pf_addressing,
			    str_wsd_address);
	err = soap_set_value_fmt(cc, "urn:uuid:%s", conf_config()->sip.uuid);

	cc = soap_add_child(response, c, str_pf_discovery, str_wsd_types);
	err |= soap_set_value_fmt(cc, "%s:%s %s:%s",
		str_pf_network_wsdl, str_type_nvt, str_pf_device_wsdl,
		str_type_dev);

	cc = soap_add_child(response, c, str_pf_discovery, str_wsd_scopes);
	err |= scope_add_all_scopes(msg, response, cc, false);

	cc = soap_add_child(response, c, str_pf_discovery, str_wsd_xaddrs);
	laddr =  net_laddr_af(baresip_network(), AF_INET);
	if (!laddr) {
		warning("onvif: %s Could not get local IP address.", __func__);
		return EINVAL;
	}

	err |= soap_set_value_fmt(cc, "http://%j:%u%s",
		laddr, DEFAULT_ONVIF_PORT, str_device_uri);

	cc = soap_add_child(response, c, str_pf_discovery,
		str_wsd_meadataversion);
	err |= soap_set_value_fmt(cc, "%u", metadata_version);

  out:
	if (err)
		mem_deref(response);
	else if (!response)
		*presponse = NULL;
	else
		*presponse = response;

	return err;
}


/**
 * initialize the webservice discovery service.
 *
 * @return   if success 0, otherwise errorcode
 */
int wsd_init(void)
{
	int err = 0;
	struct sa laddr;
	struct soap_msg *msg = NULL;
	bool discoverable = true;

	conf_get_bool(conf_cur(), str_wsd_discoverableconf, &discoverable);
	if (!instance_id)
		instance_id = (tmr_jiffies() / 1000);

	if (!metadata_version)
		metadata_version = instance_id;

	if (discoverable) {
		err = wsd_send_hello_bye(&msg, str_wsd_hello);
		if (err)
			return err;

		err = soap_msg_encode(msg);
		soap_msg_print(msg);

		err = sa_set_str(&laddr, SOAP_BC_IP4, SOAP_BC_PORT);
		err = wsd_udp_send_anon(&laddr, msg->mb);
	}

	if (err)
		warning("onvif: %s Could not send Hello. Detail: %m, laddr=%J",
				__func__, err, &laddr);

	mem_deref(msg);

	return 0;
}


/**
 * deinitialize the webservice discovery service.
 *
 * @return   if success 0, otherwise errorcode
 */
int wsd_deinit(void)
{
	int err = 0;
	struct sa laddr;
	struct soap_msg *msg = NULL;
	bool discoverable = true;

	conf_get_bool(conf_cur(), str_wsd_discoverableconf, &discoverable);

	if (discoverable) {
		err = wsd_send_hello_bye(&msg, str_wsd_bye);
		if (err)
			return err;

		err = soap_msg_encode(msg);
		soap_msg_print(msg);

		err = sa_set_str(&laddr, SOAP_BC_IP4, SOAP_BC_PORT);
		err = wsd_udp_send_anon(&laddr, msg->mb);
	}

	if (err)
		warning("onvif: %s Could not send Bye. Detail: %m, laddr=%J",
				__func__, err, &laddr);

	mem_deref(msg);

	return 0;
}

/**
 * wrapper function to create a probe match response
 *
 * @param msg               request message
 * @param presponse         pointer to the response message struct
 *
 * @return   if success 0, otherwise errorcode
 */
int wsd_probe(const struct soap_msg *msg, struct soap_msg **presponse)
{
	bool discoverable = true;

	conf_get_bool(conf_cur(), str_wsd_discoverableconf, &discoverable);
	if (!discoverable)
		return 0;

	return wsd_answer_probe_resolve(msg, presponse, str_wsd_probe_match);
}


/**
 * wrapper function to create a resolve match response
 *
 * @param msg               request message
 * @param presponse         pointer to the response message struct
 *
 * @return   if success 0, otherwise errorcode
 */
int wsd_resolve(const struct soap_msg *msg, struct soap_msg **presponse)
{
	bool discoverable = true;

	conf_get_bool(conf_cur(), str_wsd_discoverableconf, &discoverable);
	if (!discoverable)
		return 0;

	return wsd_answer_probe_resolve(msg, presponse, str_wsd_resolve_match);
}


/**
 * GetDiscoverableMode handler
 *
 * @param msg               request message
 * @param presponse         pointer to the response message struct
 *
 * @return   if success 0, otherwise errorcode
 */
int wsd_GetDiscoverable(const struct soap_msg *msg,
			struct soap_msg **presponse)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *c;
	bool discoverable = true;

	if (!msg || !presponse)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)){
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	if (err || !b) {
		err = err ? EINVAL : err;
		goto out;
	}

	conf_get_bool(conf_cur(), str_wsd_discoverableconf, &discoverable);
	c = soap_add_child(resp, b, str_pf_schema,
		str_method_get_discoverymode_r);
	err |= soap_set_value_fmt(c, "%s",
		discoverable ? str_wsd_discoverable : str_wsd_nondiscoverable);

  out:
	if (err)
		mem_deref(resp);
	else
		*presponse = resp;

	return err;
}


/**
 * SetDiscoverableMode handler
 *
 * @param msg               request message
 * @param presponse         pointer to the response message struct
 *
 * @return   if success 0, otherwise errorcode
 */
int wsd_SetDiscoverable(const struct soap_msg *msg,
			struct soap_msg **presponse)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *c;

	if (!msg || !presponse)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	c = soap_child_has_child(b, NULL, str_method_set_discoverymode);
	c = soap_child_has_child(c, NULL, str_wsd_discoverymode);
	if (!c)
		return EINVAL;

	/* if (0 == pl_strcmp(&c->value, str_wsd_discoverable)) */
	/*	discoverable = true; */
	/* else if (0 == pl_strcmp(&c->value, str_wsd_nondiscoverable)) */
	/*	discoverable = false; */
	/* else */
	/*	return EINVAL; */

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)){
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	if (err || !b) {
		err = err ? EINVAL : err;
		goto out;
	}

	c = soap_add_child(resp, b, str_pf_schema,
		str_method_get_discoverymode_r);

  out:
	if (err || !c)
		mem_deref(resp);
	else
		*presponse = resp;

	return err;
}


int wsd_udp_send_anon(const struct sa *dst, struct mbuf *mb) {
	struct udp_sock *us;
	int err;

	if (!dst || !mb)
		return EINVAL;

	err = udp_listen(&us, NULL, NULL, NULL);
	if (err)
		return err;

	err = udp_send(us, dst, mb);
	mem_deref(us);

	return err;
}