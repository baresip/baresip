/**
 * @file scopes.c
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "soap.h"
#include "wsd.h"
#include "fault.h"
#include "scopes.h"
#include "device.h"
#include "soap_str.h"

#define DEBUG_MODULE "onvif_scopes"
#define DEBUG_LEVEL 6
#include <re_dbg.h>

struct list dynscope_l;

struct scope {
	struct le le;

	char *scope_str;
};


static void scope_destructor(void *arg)
{
	struct scope *s = arg;

	mem_deref(s->scope_str);
	list_unlink(&s->le);
}


static bool scope_cmp(struct le *le, void *arg)
{
	struct scope *s = le->data;
	struct pl *str = (struct pl *)arg;

	return (0 == pl_strcmp(str, s->scope_str));
}


/**
 * Parse the dynamic scopes from the file into a list
 *
 * @param mb            memory buffer containing the dynamic scopes
 *
 * @return              0 if success, error code otherwise
 *
 * TODO: Lock the access of the dynscopes variable
 */
static int scope_parse(struct mbuf *mb)
{
	int err = 0;
	struct scope *s;
	struct pl pls;

	mbuf_set_pos(mb, 0);
	while(mbuf_get_left(mb)) {
		pls.p = (char *)mbuf_buf(mb);
		while(mbuf_get_left(mb) && mbuf_read_u8(mb) != '|') {
				pls.l = ((char *)mbuf_buf(mb) - pls.p);
		}

		s = mem_zalloc(sizeof(*s), scope_destructor);
		if (!s) {
			err = ENOMEM;
			goto out;
		}

		s->scope_str = mem_zalloc(pls.l + 1, NULL);
		if (!s->scope_str) {
			mem_deref(s);
			err = ENOMEM;
			goto out;
		}

		memcpy(s->scope_str, pls.p, pls.l);
		list_append(&dynscope_l, &s->le, s);
	}

  out:
	if (err)
		list_flush(&dynscope_l);

	return 0;
}


/**
 * Load dynamic scopes from config file
 *
 * @return              0 if success, error code otherwise
 *
 * TODO: Lock the access of the dynscopes variable
 */
static int scope_read_dynscopes(void)
{
	int err = 0;
	struct mbuf *mb = NULL;
	char dynscopepath [onvif_config_path.l + strlen("/scopes") + 1];

	err = re_snprintf(dynscopepath, sizeof(dynscopepath), "%r%s",
	&onvif_config_path, "/scopes");
	if (err != (int)sizeof(dynscopepath) - 1) {
		warning ("Can not concat string here -.-");
		goto out;
	}

	mb = mbuf_alloc(512);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	err = load_file(mb, dynscopepath);
	if (err)
		goto out;

	err = scope_parse(mb);

  out:
	mem_deref(mb);
	return err;
}


/**
 * Write the dynamic scopes back to file
 *
 * @return              0 if success, error code otherwise
 *
 * TODO: Lock the access of the dynscopes variable
 */
static int soap_write_dynscopes(void)
{
	int err = 0;
	size_t bufsize = 0;
	struct le *le;
	struct scope *s;
	struct mbuf *mb = NULL;

	char dynscopepath [onvif_config_path.l + strlen("/scopes") + 1];

	err = re_snprintf(dynscopepath, sizeof(dynscopepath), "%r%s",
	&onvif_config_path, "/scopes");
	if (err != (int)sizeof(dynscopepath) - 1) {
		warning ("Can not concat string here -.-");
		goto out;
	}

	LIST_FOREACH(&dynscope_l, le){
		s = le->data;
		bufsize += strlen(s->scope_str) + 1;
	}

	mb = mbuf_alloc(bufsize);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	LIST_FOREACH(&dynscope_l, le){
		s = le->data;
		mbuf_write_str(mb, s->scope_str);
		mbuf_write_u8(mb, '|');
	}

	mbuf_set_pos(mb, 0);
	err = save_file(mb, dynscopepath);

  out:
	mem_deref(mb);
	return err;
}


/**
 * remove the scopes definded in @rsc from the dynamic scopes list
 *
 * @param rsc           remove scope child
 *
 * @return              0 if success, error code otherwise
 *
 */
static int scope_removedynamicscopes(struct soap_child *rsc)
{
	int err = 0;
	struct soap_child *sic;
	struct scope *s;
	struct le *le, *sle;

	LIST_FOREACH(&rsc->l_childs, le) {
		sic = le->data;
		sle = list_apply(&dynscope_l, true, scope_cmp, &sic->value);
		if (sle) {
			s = sle->data;
			mem_deref(s);
		}
	}

	return err;
}


/**
 * replace all dynamic scopes
 *
 * @param ssc           set scope child
 *
 * @return              0 if success, error code otherwise
 *
 */
static int scope_replace_dynamicscope(const struct soap_child *ssc)
{
	int err = 0;
	struct le *le;
	struct soap_child *sic;
	struct scope *s;

	if (!ssc)
		return EINVAL;

	list_flush(&dynscope_l);
	LIST_FOREACH(&ssc->l_childs, le) {
		sic = le->data;

		s = mem_zalloc(sizeof(*s), scope_destructor);
		if (!s) {
			err = ENOMEM;
			goto out;
		}

		s->scope_str = mem_zalloc(sic->value.l + 1, NULL);
		if (!s->scope_str) {
			mem_deref(s);
			err = ENOMEM;
			goto out;
		}

		memcpy(s->scope_str, sic->value.p, sic->value.l);
		list_append(&dynscope_l, &s->le, s);
	}

  out:
	if (err)
		list_flush(&dynscope_l);

	return err;
}


/**
 * calculate the size of the memory buffer to send all scopes via a buffer
 * in case of WS-Discovery the scopes must be send via a memory buffer
 *
 * @return              0 if success, error code otherwise
 *
 */
static size_t scope_total_mbufsize(void)
{
	int err = 0;
	size_t scope_sizes = 0;
	struct scope *s;
	struct le *le;
	struct pl value;

	err = conf_get(conf_cur(), str_scope_name, &value);
	scope_sizes += value.l + 1;
	err |= conf_get(conf_cur(), str_scope_hardware, &value);
	scope_sizes += value.l + 1;
	err |= conf_get(conf_cur(), str_scope_manufacturer, &value);
	scope_sizes += value.l + 1;
	err |= conf_get(conf_cur(), str_scope_profstreaming, &value);
	scope_sizes += value.l + 1;

	LIST_FOREACH(&dynscope_l, le) {
		s = le->data;
		scope_sizes += strlen(s->scope_str) + 1;
	}

	if (err)
		scope_sizes = 0;

	return scope_sizes;
}


/**
 * add all elements of the AddScopes child
 *
 * @return              0 if success, error code otherwise
 *
 */
static int scope_add_dynscopes(struct soap_child *asc)
{
	int err = 0;
	struct soap_child *sic;
	struct scope *s;
	struct le *le;

	LIST_FOREACH(&asc->l_childs, le) {
		sic = le->data;
		s = mem_zalloc(sizeof(*s), scope_destructor);
		if (!s) {
			err = ENOMEM;
			goto out;
		}

		s->scope_str = mem_zalloc(sic->value.l + 1, NULL);
		if (!s->scope_str) {
			mem_deref(s);
			err = ENOMEM;
			goto out;
		}

		memcpy(s->scope_str, sic->value.p, sic->value.l);
		list_append(&dynscope_l, &s->le, s);
	}

  out:
	return err;
}


/**
 * Add a scope element with the following XML style
 * <Scopes>
 *   <ScopeDef> Fixed/Configurable</ScopeDef>
 *   <ScopeItem> [SCOPE URL] <ScopeItem>
 * <Scopes>
 *
 * @param c             pointer to current child where this elements should be
 *                      inserted
 * @param str_scope     name of the scope as string
 *
 * @return              0 if success, error code otherwise
 */
static int scope_add_scope_onvif(struct soap_child *c, const char *str_scope)
{
	int err = 0;
	struct pl value;
	struct soap_child *cc, *cscope;

	if (!str_scope)
		return EINVAL;

	err = conf_get(conf_cur(), str_scope, &value);
	if (err)
		return err;

	cscope = soap_add_child(c->msg, c, str_pf_device_wsdl, str_scope_scopes);
	LAST_CHILD(cscope);

	cc = soap_add_child(c->msg, cscope, str_pf_schema, str_scope_scopedef);
	err |= soap_set_value_fmt(cc, str_scope_fixed);

	cc = soap_add_child(c->msg, cscope, str_pf_schema, str_scope_scopeitem);
	err |= soap_set_value_fmt(cc, "%r", &value);

	return err;
}


/**
 * Add all configurable scope element with the following XML style
 * <Scopes>
 *   <ScopeDef> Fixed/Configurable</ScopeDef>
 *   <ScopeItem> [SCOPE URL] <ScopeItem>
 * <Scopes>
 *
 * @param c             pointer to current child where this elements should be
 *                      inserted
 *
 * @return              0 if success, error code otherwise
 *
 * TODO 1: Lock the access of the read operation!
 */
static int scope_add_scope_onvifdyn(struct soap_child *c)
{
	int err = 0;
	struct le *le;
	struct scope *s;
	struct soap_child *cc, *cscope;

	LIST_FOREACH(&dynscope_l, le) {
		s = le->data;

		cscope = soap_add_child(c->msg, c,
			str_pf_device_wsdl, str_scope_scopes);
		LAST_CHILD(cscope);

		cc = soap_add_child(c->msg, cscope,
			str_pf_schema, str_scope_scopedef);
		err |= soap_set_value_fmt(cc, str_scope_configurable);

		cc = soap_add_child(c->msg, cscope,
			str_pf_schema, str_scope_scopeitem);

		err |= soap_set_value_fmt(cc, "%s", s->scope_str);
		if (err)
			return err;
	}

	return err;
}


/**
 * Add a scope element as string value for a child
 *
 * @param mb            memory buffer which should hold the scopes
 * @param str_scope     name of the scope as string
 *
 * @return              0 if success, error code otherwise
 */
static int scope_add_scope_value(struct mbuf *mb, const char *str_scope)
{
	int err = 0;
	struct pl value;

	if (!str_scope || !mb)
		return EINVAL;

	err = conf_get(conf_cur(), str_scope, &value);
	if (err)
		return err;

	err = mbuf_printf(mb, "%r ", &value);

	return err;
}


/**
 * Add all configurable scope element as string value for a child
 *
 * @param mb            memory buffer which should hold the scopes
 *
 * @return              0 if success, error code otherwise
 *
 */
static int scope_add_scope_valuedyn(struct mbuf *mb)
{
	int err = 0;
	struct scope *s;
	struct le *le;

	LIST_FOREACH(&dynscope_l, le) {
		s = le->data;

		err |= mbuf_printf(mb, "%s ", s->scope_str);
	}

	return err;
}


/**
 * check the request if the scope entries are valid
 * @param c             pointer to current child
 *
 * @return              true, false
 */
static bool scope_req_validity_fixed(const struct soap_child *c)
{
	int err = 0;
	const char *start, *end;
	struct pl tmp, search;
	struct pl name, hardware, manufacturer, profile;

	tmp.p = c->value.p;
	tmp.l = c->value.l;

	err = conf_get(conf_cur(), str_scope_name, &name);
	err |= conf_get(conf_cur(), str_scope_hardware, &hardware);
	err |= conf_get(conf_cur(), str_scope_manufacturer, &manufacturer);
	err |= conf_get(conf_cur(), str_scope_profstreaming, &profile);
	if (err)
		return false;

	start = tmp.p;
	end = pl_strchr(&tmp, ' ');
	if (!end)
		end = tmp.p + tmp.l;

	do {
		search.p = start;
		search.l = end - start;

		if (0 == memcmp(search.p, name.p, search.l) ||
			0 == memcmp(search.p, hardware.p, search.l) ||
			0 == memcmp(search.p, manufacturer.p, search.l) ||
			0 == memcmp(search.p, profile.p, search.l))
				return true;

		pl_advance(&tmp, search.l + 1);
		start = tmp.p;
		end = pl_strchr(&tmp, ' ');
	} while (end);

	if (0 == memcmp(tmp.p, name.p, tmp.l) ||
		0 == memcmp(tmp.p, hardware.p, tmp.l) ||
		0 == memcmp(tmp.p, manufacturer.p, tmp.l) ||
		0 == memcmp(tmp.p, profile.p, tmp.l))
			return true;

	return false;
}


/**
 * check the request if the scope entries are valid (configruable scopes)
 *
 * @param c             pointer to current child
 *
 * @return              true, false
 */
static bool scope_req_validity_dynamics(const struct soap_child *c)
{
	const char *start, *end;
	struct pl search, tmp;
	struct le *le;
	struct scope *s;

	LIST_FOREACH(&dynscope_l, le) {
		s = le->data;

		tmp.p = c->value.p;
		tmp.l = c->value.l;

		start = tmp.p;
		end = pl_strchr(&tmp, ' ');
		if (!end)
			end = tmp.p + tmp.l;

		do {
			search.p = start;
			search.l = end - start;
			if (0 == memcmp(search.p, s->scope_str, search.l))
				return true;

			pl_advance(&tmp, search.l + 1);

			start = tmp.p;
			end = pl_strchr(&tmp, ' ');
		} while (end);

		if (0 == memcmp(tmp.p, s->scope_str, tmp.l))
			return true;
	}

	return false;
}


/**
 * check the request if the scope entries are valid (fixed and configurable)
 *
 * @param c             pointer to current child
 *
 * @return              true nothing found or valid scope found
 *                      false invalid scopes found
 */
static bool scope_req_validity(const struct soap_msg *req)
{
	struct soap_child *b, *c;
	bool v = false;

	b = soap_child_has_child(req->envelope, NULL, str_body);
	if (!b)
		return false;

	c = soap_child_has_child(b, NULL, str_method_get_scopes);
	if (c) {
		if (pl_isset(&c->value))
			goto check;
		else
			return true;
	}


	c = soap_child_has_child(b, NULL, str_wsd_probe);
	if (!c)
		return false;

	c = soap_child_has_child(c, NULL, str_wsd_scopes);
	if (!c)
		return true;

	if (!pl_isset(&c->value))
		return true;

  check:
	v = scope_req_validity_fixed(c);
	if (!v)
		v = scope_req_validity_dynamics(c);

	return v;
}


/**
 * Add all scopes as child or as value depending on @as_child
 * TODO: In case of @req is a Resolve message, the scope_req_validity
 * check should be skipped since a WSD-Resolve message in @req does not contain
 * a scope child.
 *
 * @param req           pointer to request (requested scope check!)
 * @param response      pointer to response msg
 * @param c             pointer to current child
 * @param as_child      defines the style of the scopes
 *
 * @return              0 if success, error code otherwise
 */
int scope_add_all_scopes(const struct soap_msg *req, struct soap_msg *response,
	struct soap_child *c, bool as_child)
{
	int err = 0;
	struct mbuf *mb;
	size_t scope_sizes = 0;

	if (!response || !c)
		return EINVAL;

	if (req && !scope_req_validity(req)) {
		err = EINVAL;
		goto out;
	}

	if (as_child) {
		err = scope_add_scope_onvif(c, str_scope_manufacturer);
		err |= scope_add_scope_onvif(c, str_scope_hardware);
		err |= scope_add_scope_onvif(c, str_scope_name);
		err |= scope_add_scope_onvif(c, str_scope_profstreaming);
		err |= scope_add_scope_onvifdyn(c);

	} else {
		scope_sizes = scope_total_mbufsize();
		if (!scope_sizes) {
			err = EINVAL;
			goto out;
		}

		mb = mbuf_alloc(scope_sizes);
		if (!mb)
			return ENOMEM;

		err = scope_add_scope_value(mb, str_scope_manufacturer);
		err |= scope_add_scope_value(mb, str_scope_hardware);
		err |= scope_add_scope_value(mb, str_scope_name);
		err |= scope_add_scope_value(mb, str_scope_profstreaming);
		err |= scope_add_scope_valuedyn(mb);

		err |= mbuf_write_u8(mb, '\0');
		if (err) {
			mem_deref(mb);
			return err;
		}

		mbuf_trim(mb);
		err = soap_set_value_strref(c, mem_ref(mb->buf));
		mem_deref(mb);
	}

  out:

	return err;
}


/**
 * GetScopes Request Handler
 *
 * @param msg           request message
 * @param ptrresq       pointer for the returned response message
 * @param f             soap fault
 *
 * @return              0 if success, error code otherwise
 */
int scope_GetScopes_h(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *response;
	struct soap_child *b, *c;

	if (!msg || !ptrresp)
		return EINVAL;

	err = soap_alloc_msg(&response);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		response, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		response, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	soap_add_child(response, response->envelope,
		str_pf_envelope, str_header);
	b = soap_add_child(response, response->envelope,
		str_pf_envelope, str_body);
	c = soap_add_child(response, b, str_pf_device_wsdl,
		str_method_get_scopes_r);


	err = scope_add_all_scopes(msg, response, c, true);

	if (list_isempty(&c->l_childs)) {
		fault_set(f, FC_Receiver, FS_Action, FS_EmptyScope,
			str_fault_scopeempty);
		err = EINVAL;
		goto out;
	}

  out:
	if (err)
		mem_deref(response);
	else
		*ptrresp = response;

	return err;
}


/**
 * SetScopes Request Handler
 *
 * @param msg           request message
 * @param ptrresq       pointer for the returned response message
 * @param f             soap fault
 *
 * @return              0 if success, error code otherwise
 */
int scope_SetScopes_h(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *ssc;

	if (!msg || !ptrresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	ssc = soap_child_has_child(b, NULL, str_method_set_scopes);
	if (list_count(&ssc->l_childs) > MAXDYNSCOPES) {
		fault_set(f, FC_Receiver, FS_Action, FS_TooManyScopes,
			str_fault_toomanyscopes);
		err = EINVAL;
		goto out;
	}

	err = scope_replace_dynamicscope(ssc);
	if (err)
		goto out;

	err = soap_write_dynscopes();
	if (err)
		goto out;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_device_wsdl, str_method_set_scopes_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*ptrresp = resp;

	return err;
}


/**
 * AddScopes Request Handler
 *
 * @param msg           request message
 * @param ptrresq       pointer for the returned response message
 * @param f             soap fault
 *
 * @return              0 if success, error code otherwise
 */
int scope_AddScopes_h(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *asc;
	size_t count = 0;

	if (!msg || !ptrresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	asc = soap_child_has_child(b, NULL, str_method_add_scopes);

	count = list_count(&asc->l_childs);
	count += list_count(&dynscope_l);
	if (count > MAXDYNSCOPES) {
		fault_set(f, FC_Receiver, FS_Action, FS_TooManyScopes,
			str_fault_toomanyscopes);
		return EINVAL;
	}

	err = scope_add_dynscopes(asc);
	if (err)
		goto out;

	err = soap_write_dynscopes();
	if (err)
		goto out;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_device_wsdl, str_method_add_scopes_r);

	wsd_init();

  out:
	if (err)
		mem_deref(resp);
	else
		*ptrresp = resp;

	return err;
}


/**
 * RemoveScopes Request Handler
 *
 * @param msg           request message
 * @param ptrresq       pointer for the returned response message
 * @param f             soap fault
 *
 * @return              0 if success, error code otherwise
 */

int scope_RemoveScopes_h(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp = NULL;
	struct soap_child *b, *rsc, *rsrc, *sic, *tmp;
	struct le *le;


	if (!msg || !ptrresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	rsc = soap_child_has_child(b, NULL, str_method_remove_scopes);

	LIST_FOREACH(&rsc->l_childs, le) {
		sic = le->data;
		if (scope_req_validity_fixed(sic)) {
			fault_set(f, FC_Sender, FS_OperationProhibited, FS_FixedScope,
				str_fault_delfixedscope);
			return EINVAL;
		}

		if (!scope_req_validity_dynamics(sic)) {
			fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoScope,
				str_fault_noscope);
			return EINVAL;
		}
	}

	err = scope_removedynamicscopes(rsc);
	if (err)
		goto out;

	err = soap_write_dynscopes();
	if (err)
		goto out;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	rsrc = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_remove_scopes_r);
	LIST_FOREACH(&rsc->l_childs, le) {
		tmp = le->data;

		sic = soap_add_child(resp, rsrc, str_pf_device_wsdl,
			str_scope_scopeitem);
		err |= soap_set_value_fmt(sic, "%r", &tmp->value);
	}

	wsd_init();

  out:
	if (err)
		mem_deref(resp);
	else
		*ptrresp = resp;

	return err;
}


/**
 * Load the dynamic scopes
 *
 * @return              0 if success, error code otherwise
 */
int scope_init(void)
{
	return scope_read_dynscopes();
}


/**
 * Release the dynamic scopes
 */
void scope_deinit(void)
{
	list_flush(&dynscope_l);
}