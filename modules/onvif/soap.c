/**
 * @file soap.c
 *
 * TODO: Something does not work with the parameter decoding!!!
 * check out the WS-Security message in the header
 * <Onvif-TestTool Debug-GetDiscoverableMode with WS-Token>
 *
 *
 * Performance speedup possible: Write wrapper functions for the creation of
 * the children which allow stuff like create child AND set the value or
 * parameter, and so on.
 *
 * Copyright (C) 2018 commend.com - Christoph Huber
 */

/**
 * UNIVERSAL UDP Port    : 3702
 * BROADCAST IPv4		 : 239.255.255.250
 * BROADCAST IPv6		 : FF02::C
 */

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

#include <re.h>
#include <baresip.h>

#include "soap.h"
#include "soap_str.h"
#include "deviceio.h"
#include "wsd.h"
#include "scopes.h"
#include "device.h"
#include "media.h"
#include "ptz.h"
#include "event.h"
#include "fault.h"
#include "onvif_auth.h"
#include "pl.h"

#define DEBUG_MODULE "soap"
#define DEBUG_LEVEL 6
#include <re_dbg.h>

struct udp_sock *udps;
struct http_sock *httpsock;

static void soap_msg_destructor(void *arg)
{
	struct soap_msg *msg = arg;
	struct le *le = msg->l_namespaces.head;
	struct soap_namespace *ns;

	mem_deref(msg->envelope);
	while (le) {
		ns = le->data;
		le = le->next;
		mem_deref(ns);
	}

	list_flush(&msg->l_namespaces);
	mem_deref(msg->mb);
}


static void soap_child_destructor(void *arg)
{
	struct soap_child *c = arg, *child;
	struct soap_parameter *param;
	struct le *le;

	le = c->l_parameters.head;
	while (le) {
		param = le->data;
		le = le->next;
		mem_deref(param);
	}

	list_flush(&c->l_parameters);
	le = c->l_childs.head;
	while (le) {
		child = le->data;
		le = le->next;
		mem_deref(child);
	}
	list_flush(&c->l_childs);

	mem_deref(c->str_value);
	mem_deref(c->ns);
	list_unlink(&c->le);
}


static void soap_namespace_destructor(void *arg)
{
	struct soap_namespace *ns = arg;

	mem_deref(ns->numbered_ns);
	list_unlink(&ns->le);
}


static void soap_parameter_destructor(void *arg)
{
	struct soap_parameter *param = arg;

	mem_deref(param->ns);
	mem_deref(param->str_value);
	list_unlink(&param->le);
}


/*-------------------------------------------------------------------------- */


/**
 * Compares the child key element with a string
 * (list compare function)
 *
 * @param le				pointer to the list element to check
 * @param arg				string argument
 *
 * @return					true if equal, false otherwise
 */
static bool soap_child_key_equals(struct le *le, void *arg)
{
	const struct soap_child *child = le->data;
	const char *str = arg;

	return 0 == pl_strcmp(&child->key, str);
}


/**
 * Compares the parameter key element with a string
 * (list compare function)
 *
 * @param le				pointer to the list element to check
 * @param arg				string argument
 *
 * @return					true if equal, false otherwise
 */
static bool soap_parameter_key_equals(struct le *le, void *arg)
{
	const struct soap_parameter *param = le->data;
	const char *str = arg;

	return 0 == pl_strcmp(&param->key, str);
}


/**
 * search for a child with @key in the list of children of @c
 * if there are children with the same key, recall this function
 * with @last, the last found child
 *
 * @param c                 pointer to the root child of the search request
 * @param last              the last found child, to start for the next child
 * @param key               string key to search
 *
 * @return					true if equal, false otherwise
 */
struct soap_child *soap_child_has_child(const struct soap_child *c,
	const struct soap_child *last, const char *key)
{
	struct le *le;

	if (!c || !key)
		return NULL;

	if (!last) {
		le = list_apply(&c->l_childs, true, soap_child_key_equals,
				(void*)key);
	}
	else {
		le = list_head(&c->l_childs);
		while (le) {
			if (le->data != last) {
				le = le->next;
				continue;
			}

			le = le->next;
			break;
		}

		while (le) {
			if (soap_child_key_equals(le, (void *)key))
				break;

			le = le->next;
		}
	}

	return le ? le->data : NULL;
}

/**
 * search for a parameter with @key in the list of parameters of @c
 *
 * @param c                 pointer to the root child of the search request
 * @param key               string key to search
 *
 * @return					true if equal, false otherwise
 */
struct soap_parameter *soap_child_has_parameter(const struct soap_child *c,
	const char *key)
{
	struct le *le;

	if (!c || !key)
		return NULL;

	le = list_head(&c->l_parameters);
	while (le) {
		if (soap_parameter_key_equals(le, (void *)key))
			break;

		le = le->next;
	}

	return le ? le->data : NULL;
}

/* PRITTY PRINT FUNCTIONS--------------------------------------------------- */
const char str_spaces[] = "                ";

/**
 * Pritty-print parameters of a child element
 *
 * @param l_parameters		list of parameters
 * @param indent
 */
static void soap_parameters_print(struct list *l_parameters, int indent)
{
	struct le *le;
	struct pl spaces;

	if (NULL == list_head(l_parameters))
		return;

	if (indent > 16)
		indent = 16;

	pl_set_n_str(&spaces, str_spaces, indent);
	info("%rparameters: ", &spaces);
	LIST_FOREACH(l_parameters, le) {
		struct soap_parameter *p = le->data;
		info("%r param %r (ns=%r) = %r\n", &spaces, &p->key,
			p->ns ? &p->ns->prefix : &p->xmlns, &p->value);
	}
}


/**
 * Pritty-print namespaces of the msg
 *
 * @param l_parameters		list of namespaces
 * @param indent
 */
static void soap_namespaces_print(struct list *l_namespaces, int indent)
{
	struct le *le;
	struct pl spaces;

	if (NULL == list_head(l_namespaces))
		return;

	if (indent > 16)
		indent = 16;

	pl_set_n_str(&spaces, str_spaces, indent);
	info("%rnamespaces: ", &spaces);
	LIST_FOREACH(l_namespaces, le) {
		struct soap_namespace *ns = le->data;
		info ("%r namespace %r = %r \n", &spaces, &ns->prefix,
		      &ns->uri);
	}
}


/**
 * Pretty print child
 *
 * @param c    pointer to a child
 * @param indent
 */
static void soap_child_print(struct soap_child *c, int indent)
{
	struct pl spaces;
	struct le *le;
	struct pl nil;

	if (!c)
		return;

	pl_set_str(&nil, "nil");
	if (indent > 16)
		indent = 16;

	pl_set_n_str(&spaces, str_spaces, indent);
	info("%rsoap_child: key=%r ns=%r value=%r \n", &spaces, &c->key,
		c->ns ? &c->ns->prefix : &nil, &c->value);
	soap_parameters_print(&c->l_parameters, indent + 1);
	if (!list_head(&c->l_childs))
		return;

	info("%r childs: \n", &spaces);
	LIST_FOREACH(&c->l_childs, le) {
		struct soap_child *cc = le->data;
		soap_child_print(cc, indent + 2);
	}
}


/*-------------------------------------------------------------------------- */


/* NAMESPACE FUNCITONS------------------------------------------------------ */


/**
 * calculates the number of digites of a uint32_t
 *
 * @param num				number to calculate the digits
 *
 * @return					number of digites
 */
static size_t num_digits(const uint32_t num)
{
	uint32_t v = num;
	size_t n = 1;

	while (v) {
		v /= 10;
		if (v)
			++n;
	}

	return n;
}


/**
 * add a new namespace element to the soap message
 *
 * @param msg				pointer to the soap msg
 * @param pf				pointer to pl prefix struct
 * @param uri				pointer to pl uri struct
 *
 * @return			if success: pointer to the namespace element
 *                          NULL otherwise
 */
static struct soap_namespace *soap_msg_add_ns(struct soap_msg *msg,
	struct pl *pf, struct pl *uri)
{
	struct soap_namespace *ns;
	size_t ns_number_len;

	ns = (struct soap_namespace *)mem_zalloc(sizeof(*ns),
		soap_namespace_destructor);
	if (!ns)
		return NULL;

	ns->numbered_ns = NULL;
	if (!pl_isset(pf)) {
		ns_number_len = num_digits(msg->nsnum) + 2 + 1;
		ns->numbered_ns = mem_zalloc(sizeof(char) * ns_number_len,
					     NULL);
		if (!ns->numbered_ns) {
			mem_deref(ns);
			return NULL;
		}

		if (-1 == re_snprintf(ns->numbered_ns, ns_number_len, "ns%u",
			msg->nsnum)) {
			warning("onvif: %s Could not convert uint32 %u to"
				" string.", __func__, msg->nsnum);
			mem_deref(ns);
			return NULL;
		}

		msg->nsnum++;
		pl_set_n_str(&ns->prefix, ns->numbered_ns,
			     (ns_number_len - 1));
	}
	else {
		pl_set_n_str(&ns->prefix, pf->p, pf->l);
	}

	pl_set_n_str(&ns->uri, uri->p, uri->l);
	list_append(&msg->l_namespaces, &ns->le, ns);
	return ns;
}


/**
 * add a new namespace element to the soap message via pointer-length objects
 *
 * @param msg				pointer to the soap msg
 * @param pf				pointer to pl prefix struct
 * @param uri				pointer to pl uri struct
 *
 * @return   if success: pointer to the namespace element
 *                          NULL otherwise
 */
struct soap_namespace *soap_msg_add_ns_pl(struct soap_msg *msg,
	struct pl *prefix, struct pl *uri)
{
	struct le *le;
	struct soap_namespace *ns;

	for (le = list_head(&msg->l_namespaces); le; le = le->next) {
		ns = le->data;

		if (0 == pl_cmp(&ns->uri, uri))
			return ns;
	}

	return soap_msg_add_ns(msg, prefix, uri);
}


/**
 * add a new namespace element to the soap message via strings
 *
 * @param msg				pointer to the soap msg
 * @param pf				pointer to pl prefix struct
 * @param uri				pointer to pl uri struct
 *
 * @return    if success: pointer to the namespace element
 *                          NULL otherwise
 */
struct soap_namespace *soap_msg_add_ns_str(struct soap_msg *msg,
	const char *prefix, const char *uri)
{
	struct pl pl_prefix, pl_uri;
	struct soap_namespace *ns;

	ns = soap_msg_has_ns_uri(msg, uri);
	if (ns)
		return ns;

	pl_set_str(&pl_prefix, prefix);
	pl_set_str(&pl_uri, uri);

	return soap_msg_add_ns(msg, &pl_prefix, &pl_uri);
}


/**
 * add a new namespace element to the soap message via strings
 * and put it into the parameter list of the message envelope
 *
 * @param msg				pointer to the soap msg
 * @param pf				pointer to pl prefix struct
 * @param uri				pointer to pl uri struct
 *
 * @return   0 if success, errorcode otherwise
 */
int soap_msg_add_ns_str_param(struct soap_msg *msg,
	const char *prefix, const char *uri)
{
	if (!soap_msg_add_ns_str(msg, prefix, uri))
		return EINVAL;

	if (soap_add_parameter_str(msg->envelope, str_new_ns,
		prefix, strlen(prefix),uri, strlen(uri)))
		return EINVAL;

	return 0;
}


/**
 * check the decoded soap msg for a namespace with given prefix
 *
 * @param msg				pointer to the soap msg
 * @param pf				pointer to prefix string
 *
 * @return   if success: pointer to the namespace element
 *                          NULL otherwise
 */
struct soap_namespace* soap_msg_has_ns_prefix(struct soap_msg *msg,
	const char *prefix)
{
	struct le *le;
	struct soap_namespace *ns;

	for (le = list_head(&msg->l_namespaces); le; le = le->next) {
		ns = le->data;
		if (0 == pl_strncmp(&ns->prefix, prefix, ns->prefix.l))
			return ns;
	}

	return NULL;
}

/**
 * check the decoded soap msg for a namespace with given uri
 *
 * @param msg				pointer to the soap msg
 * @param uri				pointer to uri string
 *
 * @return   if success: pointer to the namespace element
 *                          NULL otherwise
 */
struct soap_namespace *soap_msg_has_ns_uri(const struct soap_msg *msg,
	const char *uri)
{
	struct le *le;
	struct soap_namespace *ns;

	if (!msg || !uri)
		return NULL;

	for (le = list_head(&msg->l_namespaces); le; le = le->next) {
		ns = le->data;
		if (0 == pl_strncmp(&ns->uri, uri, ns->uri.l))
			return ns;
	}

	return NULL;
}


/*-------------------------------------------------------------------------- */


/* CHILD SET VALUE FUNCITONS------------------------------------------------ */


/**
 * set a string with given length as value of a child
 * ! use this function only for persistent strings in memory !
 *
 * @param c    pointer to child
 * @param v    pointer to string
 * @param len  length of v
 *
 * @return 0 if success, error otherwise
 */
static int soap_set_value_str(struct soap_child *c, const char *v, int len)
{
	if (!c || !v)
		return EINVAL;

	pl_set_n_str(&c->value, v, len);

	return 0;
}


/**
 * write a fmt string with arguments as value for a child object
 * this function will allocate memory and write the string
 * the unused memory in mbuf will be trimmed and freed
 *
 * @param c     pointer to child
 * @param fmt   pointer to fmt string
 * @param ap    variable argument list
 *
 * @return 0 if success, error otherwise
 */
static int soap_vset_value(struct soap_child *c, const char *fmt, va_list ap)
{
	int err;
	struct mbuf *mb;

	mb = mbuf_alloc(128);
	if (!mb)
		return ENOMEM;

	err = mbuf_vprintf(mb, fmt, ap);
	err = mbuf_write_u8(mb, 0);
	if (err)
		goto out;

	mbuf_trim(mb);
	c->str_value = mem_ref(mb->buf);
	pl_set_str(&c->value, c->str_value);

	mem_deref(mb);
  out:
	if (err)
		mem_deref(mb);

	return err;
}

/**
 * write a fmt string with arguments as value for a child object
 *
 * @param c    pointer to child
 * @param fmt  pointer to fmt string
 * @param ...
 *
 * @return 0 if success, error otherwise
 */
int soap_set_value_fmt(struct soap_child *c, const char *fmt, ...)
{
	va_list ap;
	int err;

	if (!c || !fmt)
		return EINVAL;

	va_start(ap, fmt);
	err = soap_vset_value(c, fmt, ap);
	va_end(ap);

	return err;
}


/**
 * set the value of a child to a given string in the heap
 *
 * @param c    pointer to child
 * @param fmt  pointer to string
 *
 * @return 0 if success, error otherwise
 */
int soap_set_value_strref(struct soap_child *c, char *v)
{
	if (!c || !v)
		return EINVAL;

	c->str_value = v;
	pl_set_n_str(&c->value, c->str_value, strlen(c->str_value));
	return 0;
}


/*-------------------------------------------------------------------------- */


/* DECODE FUNCTIONS--------------------------------------------------------- */


/**
 * detect current parameter type
 * SAT_NS_DECL_SIMPLE  ... TYPE 0: xmlns
 * SAT_NS_DECL         ... TYPE 1: xmlns:[Name]
 * SAT_NS_ATTR         ... TYPE 2: [Namespace]:[Name]
 * SAT_ATTR            ... TYPE 3: [Name]
 *
 * @param param  pointer length object to the current parameter
 *
 * @return type if success, SAT_MAX otherwise
 */
static enum soap_attr_type soap_decode_attr_type(struct pl *param)
{
	const char *colon;

	colon = pl_strchr(param, ':');
	if (colon){
		if (0 == pl_strncmp(param, str_new_ns,
				strlen(str_new_ns)))
			return SAT_NS_DECL;
		else
			return SAT_NS_ATTR;
	}
	else {
		if (0 == pl_strncmp(param, str_new_ns,
				strlen(str_new_ns)))
			return SAT_NS_DECL_SIMPLE;
		else
			return SAT_ATTR;
	}

	return SAT_MAX;
}


static int soap_is_endkey(struct soap_msg *msg, bool *endkey)
{
	int err;
	size_t bpos = msg->mb->pos;

	if (!msg)
		return EINVAL;

	err = xml_is_close_key(msg->mb, endkey);
	if (err)
		return err;

	if (!(*endkey)) {
		err = xml_skip_to_end(msg->mb);
		if (err)
			return err;

		mbuf_advance(msg->mb, -2);
		err = xml_is_close_key(msg->mb, endkey);
		if (err)
			return err;
	}

	mbuf_set_pos(msg->mb, bpos);
	return 0;
}


/**
 * SOAP parameter decode function
 * split all parameter in an element and append it as soap_child's parameter
 * list.
 * @param child pointer to soap_child struct
 *
 * @return 0 if success, error otherwise
 */
static int soap_child_parameter_decode(struct soap_child *child)
{
	struct soap_msg *msg;
	int err = 0;
	size_t bpos = 0, epos = 0, tpos = 0;
	struct soap_parameter *parameter = NULL;
	struct pl param;
	struct pl ns;
	struct pl value;
	const char *colon;
	char ns_prefix[11];
	enum soap_attr_type attr_type;
	bool ek;

	if (!child)
		return EINVAL;

	msg = child->msg;
	if (!msg)
		return EINVAL;

	bpos = msg->mb->pos;
	err = xml_skip_to_end(msg->mb);
	if (err)
		return err;

	mbuf_advance(msg->mb, -2);
	soap_is_endkey(msg, &ek);
	mbuf_advance(msg->mb, 2);
	epos = msg->mb->pos;
	if (bpos == (epos - 1))
		return 0;

	if (ek)
		epos -= 2;

	mbuf_set_pos(msg->mb, bpos);
	while (msg->mb->pos < epos) {
		bpos = msg->mb->pos;
		err = xml_goto_value(msg->mb);
		if (err)
			goto out;

		tpos = msg->mb->pos - 1;
		mbuf_set_pos(msg->mb, bpos);
		pl_set_n_str(&param, (const char*)mbuf_buf(msg->mb),
			     (tpos - bpos));
		mbuf_set_pos(msg->mb, tpos + 2);
		bpos = msg->mb->pos;
		err = xml_goto_value_end(msg->mb);
		if (err)
			goto out;

		tpos = msg->mb->pos;
		mbuf_set_pos(msg->mb, bpos);
		pl_set_n_str(&value, (const char*)mbuf_buf(msg->mb),
			     (tpos - bpos));
		mbuf_set_pos(msg->mb, bpos - 1);
		err = xml_skip_to_ws(msg->mb);
		if (err) {
			err = xml_skip_to_end(msg->mb);
		}

		if (err && err != EOF)
			goto out;

		err = 0;
		attr_type = soap_decode_attr_type(&param);
		switch (attr_type) {
			case SAT_NS_DECL_SIMPLE:
				child->ns =
					mem_ref(soap_msg_add_ns_pl(
						child->msg,
						NULL, &value));
				break;

			case SAT_NS_DECL:
				pl_set_n_str(&ns, (param.p +
						   strlen(str_new_ns) + 1),
					(param.l - strlen(str_new_ns) - 1));
				soap_msg_add_ns_pl(child->msg, &ns, &value);
				break;

			case SAT_NS_ATTR:
				parameter = mem_zalloc(sizeof(*parameter),
					soap_parameter_destructor);
				if (!parameter)
					return ENOMEM;

				colon = pl_strchr(&param, ':');
				if ((colon - param.p) > 10) {
					err = EMSGSIZE;
					goto out;
				}

				strncpy(ns_prefix, param.p, (colon - param.p));
				parameter->ns = mem_ref(
					soap_msg_has_ns_prefix(child->msg,
					ns_prefix));
				pl_advance(&param, (colon - param.p));
				pl_set_n_str(&parameter->key, param.p,
					     param.l);
				pl_set_n_str(&parameter->value, value.p,
					     value.l);

				list_append(&child->l_parameters,
					    &parameter->le, parameter);
				break;

			case SAT_ATTR:
				parameter = mem_zalloc(sizeof(*parameter),
					soap_parameter_destructor);
				if (!parameter)
					return ENOMEM;

				pl_set_n_str(&parameter->key,
					     param.p, param.l);
				pl_set_n_str(&parameter->value,
					     value.p, value.l);
				list_append(&child->l_parameters,
					    &parameter->le, parameter);
				break;

			default:
				err = EINVAL;
				goto out;
		}
	}

  out:
	if (err) {
		mem_deref(parameter);
	}

	return err;
}


/**
 * detect current child type
 * SCT_NORMAL_NOPARAM,          TYPE 0: <ns:Key>
 * SCT_NORMAL_PARAM,            TYPE 1: <ns:Key [param]>
 * SCT_END_NORMAL,              TYPE 2: </ns:Key>
 * SCT_IEND_NOPARAM,            TYPE 3: <ns:Key />
 * SCT_IEND_PARAM,              TYPE 4: <ns:Key [param] />
 *
 * @param child  pointer to the soap message
 *
 * @return type if success, SCT_MAX otherwise
 */
static enum soap_childtype soap_decode_child_type(struct soap_msg *msg)
{
	int err = 0;
	bool endkey = false;
	size_t bpos = msg->mb->pos;
	enum soap_childtype type = SCT_MAX;

	err = soap_is_endkey(msg, &endkey);
	if (err) {
		warning("%s Can't detect end key", __func__);
		return SCT_MAX;
	}

	if (*(msg->mb->buf + msg->mb->pos) == '<' &&
		*(msg->mb->buf + msg->mb->pos + 1) == '>') {
		warning("%s Run into a total empty element", __func__);
		return SCT_MAX;
	}

	if (!endkey) {
		/* possible NORMAL TYPES */
		err = xml_skip_to_ws(msg->mb);
		if (err == EOF)
			type = SCT_NORMAL_NOPARAM;
		else if (err)
			type = SCT_MAX;
		else
			type = SCT_NORMAL_PARAM;

	}
	else {
		/* possible [I]END TYPES */
		err = xml_is_close_key(msg->mb, &endkey);
		if (err) {
			type = SCT_MAX;
			goto       out;
		}

		if (endkey) {
			type = SCT_END_NORMAL;
			goto       out;
		}

		err = xml_skip_to_ws(msg->mb);
		if (err) {
			type = SCT_MAX;
			goto       out;
		}

		err = xml_is_close_key(msg->mb, &endkey);
		if (err) {
			type = SCT_MAX;
			goto       out;
		}

		if (endkey)
			type = SCT_IEND_NOPARAM;
		else
			type = SCT_IEND_PARAM;
	}

  out:
	mbuf_set_pos(msg->mb, bpos);
	return type;
}


/**
 * decode the namespace key combination of the child
 *
 * @param child pointer to the soap message
 *
 * @return 0 if success, error otherwise
 */
static int soap_child_nskey_decode(struct soap_child *c,
	enum soap_childtype t)
{
	int err;
	struct pl nskey;
	struct pl nsprefix;
	const char *ctmp;
	size_t bpos = c->msg->mb->pos, epos = 0;

	if (!c)
		return EINVAL;

	switch (t) {
		case SCT_NORMAL_NOPARAM:
			err = xml_skip_to_end(c->msg->mb);
			break;

		case SCT_NORMAL_PARAM:
		case SCT_IEND_NOPARAM:
		case SCT_IEND_PARAM:
			err = xml_skip_to_ws(c->msg->mb);
			break;

		default:
			return EINVAL;
	}

	if (err)
		return err;

	epos = c->msg->mb->pos;
	mbuf_set_pos(c->msg->mb, bpos);
	pl_set_n_str(&nskey, (const char *)mbuf_buf(c->msg->mb),
		     (epos - bpos - 1));
	mbuf_set_pos(c->msg->mb, epos);

	ctmp = pl_strchr(&nskey, ':');
	if (ctmp) {
		pl_set_n_str(&nsprefix, nskey.p, (ctmp++) - nskey.p);
		pl_set_n_str(&c->key, ctmp, nskey.l - (ctmp - nskey.p));
		c->ns = mem_ref(soap_msg_has_ns_prefix(c->msg, nsprefix.p));
	}
	else {
		pl_set_n_str(&c->key, nskey.p, nskey.l);
	}

	return 0;
}


/**
 * detect and decode a possible value
 *
 * @param child  pointer to the soap message
 *
 * @return 0 if success, error otherwise
 */
static int soap_child_value_decode(struct soap_child *c) {
	int err = 0;
	size_t bpos = c->msg->mb->pos, epos;

	if (mbuf_get_left(c->msg->mb) && mbuf_read_u8(c->msg->mb) != '<') {
		err = xml_next_key(c->msg->mb);
		if (err)
			return err;


		epos = c->msg->mb->pos - 1;
		mbuf_set_pos(c->msg->mb, bpos);
		soap_set_value_str(c,
			(const char*)mbuf_buf(c->msg->mb), (epos - bpos));
		mbuf_set_pos(c->msg->mb, epos);
	}
	else {
		mbuf_set_pos(c->msg->mb, bpos);
	}

	return 0;
}


/**
 * SOAP child decode function
 * use a iterative approach to decode the soap message
 *
 * @param msg    pointer to soap message struct
 * @param stack  stack to keep track of the current soap layer
 * @param maxstacksize  max number of soap children on the stack
 *
 * @return 0 if success, error otherwise
 */
static int soap_child_decode(struct soap_msg *msg,
	struct soap_child **stack, const size_t maxstacksize)
{
	int err = 0;
	size_t curstack = 0, bpos;
	struct soap_child *c = NULL;
	enum soap_childtype type;

	while (mbuf_get_left(msg->mb)) {
		type = soap_decode_child_type(msg);
		if (type == SCT_MAX) {
			err = EINVAL;
			break;
		}

		if (type != SCT_END_NORMAL) {
			c = mem_zalloc(sizeof(*c), soap_child_destructor);
			if (!c) {
				err = ENOMEM;
				break;
			}

			c->msg = msg;
			if (curstack >= maxstacksize) {
				err = EOVERFLOW;
				goto out;
			}
		}
		else {
			if (curstack == 1) {
				--curstack;
				err = 0;
				goto out;
			}
			err = xml_next_key(msg->mb);
			if (err)
				goto out;

			--curstack;
			continue;
		}

		if (type == SCT_NORMAL_PARAM || type == SCT_IEND_PARAM) {
			bpos = msg->mb->pos;
			err = xml_skip_to_ws(msg->mb);
			err |= soap_child_parameter_decode(c);
			if (err) {
				warning ("%s Could not decode parameter (%m)",
					 __func__, err);
				goto out;
			}

			mbuf_set_pos(msg->mb, bpos);
		}

		*(stack + curstack) = c;
		if (curstack == 0)
			msg->envelope = c;
		else
			list_append(&(*(stack + curstack - 1))->l_childs,
				&c->le, c);

		err = soap_child_nskey_decode(c, type);
		if (type == SCT_NORMAL_PARAM || type == SCT_IEND_PARAM) {
			err |= xml_skip_to_end(msg->mb);
			if (err) {
				warning ("%s Could not decode namespace and"
					 " key (%m)", __func__, err);
				goto out;
			}
		}

		if (!c->ns) {
			if (curstack == 0) {
				warning ("%s No namespace exists.", __func__);
				err = EINVAL;
				goto out;
			}
			c->ns = mem_ref((*(stack + curstack - 1))->ns);
		}

		if (type == SCT_NORMAL_PARAM || type == SCT_NORMAL_NOPARAM) {
			err = soap_child_value_decode(c);
			if (err)
				goto out;

			++curstack;
		}

		err = xml_next_key(msg->mb);
		if (err)
			goto out;
	}

  out:
	if (curstack != 0 || err) {
		if (c == msg->envelope)
			msg->envelope = NULL;

		c = mem_deref(c);
		warning("suspicious looking soap message");
		err = err ? err : EINVAL;
	}

	return err;
}


/**
 * SOAP message decode function
 * @param msg  pointer to soap message struct
 *
 * @return 0 if success, error otherwise
 */
static int soap_msg_decode(struct soap_msg *msg)
{
	int err = 0;
	struct soap_child **stack = NULL;

	err |= xml_skip_prolog(msg->mb);
	if (err)
		return err;

	pl_set_n_str(&msg->prolog, (const char*) msg->mb->buf, msg->mb->pos);
	err = xml_skip_to_begin(msg->mb);
	if (err)
		return err;

	mbuf_advance(msg->mb, 1);

	stack = mem_zalloc(sizeof(struct soap_child) * SOAP_MAX_STACKSIZE,
			   NULL);
	if (!stack)
		return ENOMEM;

	err = soap_child_decode(msg, stack, SOAP_MAX_STACKSIZE);
	if (err)
		msg->envelope = mem_deref(msg->envelope);

	mem_deref(stack);
	mbuf_set_pos(msg->mb, 0);
	return err;
}


/*-------------------------------------------------------------------------- */


/* GENERATOR FUNCTIONS------------------------------------------------------ */


/**
 * add a parameter object to a @child
 *
 * @param child         pointer to the soap child object
 * @param ns_prefix     namespace prefix, 'xmlns', NULL
 * @param key           key of the parameter
 * @param k_len         size of the key string
 * @param value         value of the parameter
 * @param v_len         size of the value string
 *
 * @return 0 if success, error otherwise
 */
int soap_add_parameter_str(struct soap_child *c, const char *ns_prefix,
	const char *key, const size_t k_len, const char *value,
	const size_t v_len)
{
	int err = 0;
	struct soap_namespace *ns = NULL;
	struct soap_parameter *param;

	if (!c || !key)
		return EINVAL;

	param = mem_zalloc(sizeof(*param), soap_parameter_destructor);
	if (!param)
		return ENOMEM;

	if (!ns_prefix) {
		param->str_value = mem_zalloc(v_len, NULL);
		if (!param->str_value) {
			err = ENOMEM;
			goto out;
		}

		pl_set_n_str(&param->key, key, k_len);
		memcpy(param->str_value, value, v_len);
		pl_set_n_str(&param->value, param->str_value, v_len);
	}
	else if (0 == strncmp(ns_prefix, str_new_ns, strlen(str_new_ns))) {
		pl_set_n_str(&param->xmlns, str_new_ns, strlen(str_new_ns));
		pl_set_n_str(&param->key, key, k_len);
		pl_set_n_str(&param->value, value, v_len);
	}
	else {
		ns = soap_msg_has_ns_prefix(c->msg, ns_prefix);
		if (!ns) {
			err = EINVAL;
			goto out;
		}

		param->str_value = mem_zalloc(v_len, NULL);
		if (!param->str_value) {
			err = ENOMEM;
			goto out;
		}

		param->ns = mem_ref(ns);
		pl_set_n_str(&param->key, key, k_len);
		memcpy(param->str_value, value, v_len);
		pl_set_n_str(&param->value, param->str_value, v_len);
	}

  out:
	if (err) {
		mem_deref(ns);
		mem_deref(param);
	}
	else {
		list_append(&c->l_parameters, &param->le, param);
	}

	return err;
}


/**
 * add a parameter object to a @child
 *
 * @param child         pointer to the soap child object
 * @param ns_prefix     namespace prefix, 'xmlns', NULL
 * @param key           key of the parameter
 * @param k_len         size of the key string
 * @param n             parameter value as number
 *
 * @return				0 if success, error otherwise
 */
int soap_add_parameter_uint(struct soap_child *c, const char* ns_prefix,
	const char *key, const size_t k_len, const uint32_t n)
{
	int err = 0;
	size_t num_len;
	struct soap_namespace *ns = NULL;
	struct soap_parameter *param;


	if (!c || !key)
		return EINVAL;

	ns = soap_msg_has_ns_prefix(c->msg, ns_prefix);
	param = mem_zalloc(sizeof(*param), soap_parameter_destructor);
	if (!param)
		return ENOMEM;

	if (ns)
		param->ns = mem_ref(ns);

	num_len = num_digits(n) + 1;
	param->str_value = mem_zalloc(num_len, NULL);
	if (!param->str_value) {
		err = ENOMEM;
		goto out;
	}

	if (-1 == re_snprintf(param->str_value, num_len, "%u", n)) {
		warning("onvif: %s Could not convert uint32 %u to string.",
			__func__, n);
		err = EINVAL;
		goto out;
	}

	pl_set_n_str(&param->key, key, k_len);
	pl_set_n_str(&param->value, param->str_value, (num_len - 1));

  out:
	if (err) {
		mem_deref(param);
	}
	else {
		list_append(&c->l_parameters, &param->le, param);
	}

	return err;
}


/**
 * add a child to a given parent. The first child ever created
 * (msg->envelope = NULL) has to be the envelope it self.
 *
 * @param msg			pointer to the soap msg
 * @param parent        pointer to the parent of the current child
 * @param ns_prefix     prefix string of the used namespace
 * @param key           key string of the child
 *
 * @return				child prt if success, NULL otherwise
 */
struct soap_child *soap_add_child(struct soap_msg *msg,
	struct soap_child *parent, const char *ns_prefix, const char* key)
{
	struct soap_child *child;
	struct soap_namespace *ns;

	if ((!msg && !parent) || !ns_prefix || !key)
		return NULL;

	ns = soap_msg_has_ns_prefix(msg, ns_prefix);
	if (!ns) {
		warning ("%s: Could not find the namespace with %s\n",
			__func__, ns_prefix);
		return NULL;
	}

	child = mem_zalloc((sizeof *child), soap_child_destructor);
	if (!child)
		return NULL;

	child->msg = msg;
	child->ns = mem_ref(ns);
	child->str_value = NULL;
	list_init(&child->l_parameters);
	list_init(&child->l_childs);

	pl_set_n_str(&child->key, key, strlen(key));

	if (!msg->envelope)
		msg->envelope = child;
	else
		list_append(&parent->l_childs, &child->le, child);

	return child;
}


/**
 * allocate a fresh new soap message without any buffer, for building
 * a soap response message. This generates the envelope child and
 * add the necessary envelope namespace
 *
 * @param prtmsg  pointer for the message as return value
 *
 * @return 0 if success, error otherwise
 */
int soap_alloc_msg(struct soap_msg **ptrmsg)
{
	int err = 0;
	struct soap_msg *msg;

	if (!ptrmsg)
		return EINVAL;

	msg = mem_zalloc(sizeof(*msg), soap_msg_destructor);
	if (!msg)
		return ENOMEM;

	pl_set_n_str(&msg->prolog, str_xmlprolog, strlen(str_xmlprolog));
	if (!soap_msg_add_ns_str(msg, str_pf_envelope, str_uri_envelope)) {
		warning ("%s: Could not add envelope namespace");
		err = EINVAL;
		goto out;
	}

	/* if (!soap_msg_add_ns_str(msg, str_pf_xml_schema, */
	/*     str_uri_xml_schema)) { */
	/*     warning ("%s: Could not add XML Schema"); */
	/*     err = EINVAL; */
	/*     goto out; */
	/* } */

	msg->envelope = soap_add_child(msg, NULL, str_pf_envelope,
				       str_envelope);
	if (!msg->envelope){
		warning ("%s: Could not add envelope child");
		goto out;
	}

	err = soap_add_parameter_str(msg->envelope, str_new_ns,
		str_pf_envelope, strlen(str_pf_envelope),
		str_uri_envelope, strlen(str_uri_envelope));
	/* err |= soap_add_parameter_str(msg->envelope, str_new_ns, */
	/*     str_pf_xml_schema, strlen(str_pf_xml_schema), */
	/*     str_uri_xml_schema, strlen(str_uri_xml_schema)); */

  out:
	if (err)
		mem_deref(msg);
	else
		*ptrmsg = msg;

	return err;
}


/*-------------------------------------------------------------------------- */


/*ENCODE FUNCTIONS---------------------------------------------------------- */


/**
 * calculate the size of a parameter
 * namespace/xmlns + key + value + static symbols
 *
 * @param param  pointer to parameter
 *
 * @return size of the parameter
 */
static size_t soap_param_bufsize(struct soap_parameter *param)
{
	size_t size = 0;

	size += 1;
	if (param->ns) {
		size += param->ns->prefix.l;
		size += 1;
	}
	else if (pl_isset(&param->xmlns)) {
		size += strlen(str_new_ns);
		size += 1;
	}

	size += param->key.l;
	size += 3;
	size += param->value.l;

	return size;
}


/**
 * calculate the size of a child
 * namespace + key + value + parameter + children + static symbols
 *
 * @param param  pointer to child
 *
 * @return size of the child
 */
static size_t soap_child_bufsize(struct soap_child *c)
{
	size_t size = 0;
	struct le *le;
	struct soap_parameter *param;
	struct soap_child *child;

	le = c->l_parameters.head;
	while (le != NULL) {
		param = le->data;
		size += soap_param_bufsize(param);
		le = le->next;
	}

	size += 2;
	size += c->ns ? c->ns->prefix.l + 1 : 0;
	size += c->key.l;
	if (list_isempty(&c->l_childs) && !pl_isset(&c->value)) {
		size += 1;
	}
	else {
		size += pl_isset(&c->value) ? c->value.l : 0;
		size += 3;
		size += c->ns ? c->ns->prefix.l + 1 : 0;
		size += c->key.l;
	}

	le = c->l_childs.head;
	while (le) {
		child = le->data;
		size += soap_child_bufsize(child);
		le = le->next;
	}

	return size;
}


/**
 * calculate the size of the soap message
 * prolog of the xml + envelope
 *
 * @param param  pointer to message
 *
 * @return size of the message
 */
static size_t soap_msg_bufsize(const struct soap_msg *msg)
{
	size_t size = msg->prolog.l;

	size += soap_child_bufsize(msg->envelope);

	return size;
}


/**
 * encode the parameter into the message buffer
 *
 * @param msg    pointer to parameter
 * @param param  pointer to message
 *
 * @return 0 if success, error code otherwise
 */
static int soap_param_encode(const struct soap_msg *msg,
	const struct soap_parameter *param)
{
	int err;

	if (!msg || !param)
		return EINVAL;

	err = mbuf_write_u8(msg->mb, ' ');
	if (param->ns) {
		err |= mbuf_write_pl(msg->mb, &param->ns->prefix);
		err |= mbuf_write_u8(msg->mb, ':');
	}
	else if (pl_isset(&param->xmlns)) {
		err |= mbuf_write_pl(msg->mb, &param->xmlns);
		err |= mbuf_write_u8(msg->mb, ':');
	}

	err |= mbuf_write_pl(msg->mb, &param->key);
	err |= mbuf_write_str(msg->mb, "=\"");
	err |= mbuf_write_pl(msg->mb, &param->value);
	err |= mbuf_write_u8(msg->mb, '\"');

	return err;
}


/**
 * encode the child into the message buffer
 *
 * @param c  pointer to child
 *
 * @return 0 if success, error code otherwise
 */
static int soap_child_encode(const struct soap_child *c)
{
	int err;
	struct le *le;
	struct soap_parameter *param;
	struct soap_child *child;

	if (!c)
		return EINVAL;

	err = mbuf_write_u8(c->msg->mb, '<');
	if (c->ns) {
		err |= mbuf_write_pl(c->msg->mb, &c->ns->prefix);
		err |= mbuf_write_u8(c->msg->mb, ':');
	}

	err |= mbuf_write_pl(c->msg->mb, &c->key);
	le = c->l_parameters.head;
	while (le != NULL) {
		param = le->data;
		err |= soap_param_encode(c->msg, param);
		le = le->next;
	}

	if (list_isempty(&c->l_childs) && !pl_isset(&c->value)) {
		err |= mbuf_write_str(c->msg->mb, "/>");
		goto out;
	}
	else {
		err |= mbuf_write_u8(c->msg->mb, '>');
	}

	if (pl_isset(&c->value)) {
		err |= mbuf_write_pl(c->msg->mb, &c->value);
	}

	le = c->l_childs.head;
	while (le) {
		child = le->data;
		err |= soap_child_encode(child);
		le = le->next;
	}

	err |= mbuf_write_str(c->msg->mb, "</");
	if (c->ns) {
		err |= mbuf_write_pl(c->msg->mb, &c->ns->prefix);
		err |= mbuf_write_u8(c->msg->mb, ':');
	}

	err |= mbuf_write_pl(c->msg->mb, &c->key);
	err |= mbuf_write_u8(c->msg->mb, '>');

  out:
	return err;
}


/**
 * calculate the size of the message and encode the hole
 * datastructure into the message buffer
 *
 * @param msg  pointer to message
 *
 * @return 0 if success, error code otherwise
 */
int soap_msg_encode(struct soap_msg *msg)
{
	int err;
	size_t msg_size;

	if (!msg)
		return EINVAL;

	msg_size = soap_msg_bufsize(msg);
	if (msg_size >= SOAP_MAX_MSG_SIZE) {
		warning("%s: soap message would be to big (%d bytes)\n",
			__func__, msg_size);
		return EINVAL;
	}

	msg->mb = mbuf_alloc(msg_size);
	if (!msg->mb)
		return ENOMEM;

	err = mbuf_write_pl(msg->mb, &msg->prolog);
	err = soap_child_encode(msg->envelope);
	if (err) {
		warning ("%s: soap message does not fit in buffer\n",
			 __func__);
		goto out;
	}

	mbuf_set_pos(msg->mb, 0);

   out:
	return err;
}


/*-------------------------------------------------------------------------- */


/**
 * Pritty-print soap message
 *
 * @param m  pointer to soap msg
 */
void soap_msg_print(struct soap_msg *m)
{
	info("soap msg size:    %d\n", soap_msg_bufsize(m));
	info("soap_msg: prolog=%r\n", &m->prolog);
	soap_namespaces_print(&m->l_namespaces, 1);
	soap_child_print(m->envelope, 1);
}


static int soap_request_handler(const struct soap_msg *msg,
	struct soap_msg **ptr_res)
{
	int err = 0;
	bool unauthorized = false;
	bool auth_enabled = true;
	struct soap_child *header, *body;
	struct soap_msg *response = NULL;
	struct soap_fault f;
	enum userlevel ul;

	fault_clear(&f);

	header = soap_child_has_child(msg->envelope, NULL, str_header);
	body =  soap_child_has_child(msg->envelope, NULL, str_body);

	/*WS-D Stuff*/
	if (soap_child_has_child(body, NULL, str_wsd_probe)) {
		err = wsd_probe(msg, &response);
		goto make_fault;
	}
	else if (soap_child_has_child(body, NULL, str_wsd_resolve)) {
		err = wsd_resolve(msg, &response);
		goto make_fault;
	}
	else if (soap_child_has_child(body, NULL, str_wsd_hello)) {
		goto noresponse;
	}
	else if (soap_child_has_child(body, NULL, str_wsd_bye)) {
		goto noresponse;
	}

	if (conf_get_bool(conf_cur(), "rtsp_AuthEnabled", &auth_enabled)) {
		warning("%s: rtsp_AuthEnabled field in config not found."
			"Use default: Auth Enabled.\n", DEBUG_MODULE);
	}

	/*Called Methods IF-Else ladder*/
	if (auth_enabled)
		ul = wss_auth(msg);
	else
		ul = UADMIN;

	/*ADMIN*/
	/*READ_SYSSTEM_SECRET - CORE*/
	/*GetSystemBackup*/
	/*GetSystemLog*/
	/*GetSystemSupportInformation*/
	/*GetAccessPolicy*/
	if (soap_child_has_child(body, NULL, str_method_get_users)) {
	/*GetUsers*/
		if (ul == UADMIN)
			err = onvif_auth_GetUsers_h(msg, &response);
		else
			unauthorized = true;
	}
	/*WRITE_SYSTEM - CORE*/
	/*SetHostname*/
	/*SetHostnameFromDHCP*/
	/*SetDNS*/
	/*SetNTP*/
	/*SetDynamicDNS*/
	/*SetNetworkInterfaces*/
	/*SetNetworkProtocols*/
	/*SetNetworkDefaultGateway*/
	/*SetZeroConfiguration*/
	/*SetIPAddressFilter*/
	/*AddIPAddressFilter*/
	/*RemoveIPAddressFilter*/
	/*SetSystemDateAndTime*/
	else if (soap_child_has_child(body, NULL, str_method_set_scopes)) {
	/*SetScopes*/
		if (ul == UADMIN)
			err = scope_SetScopes_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_add_scopes)) {
	/*AddScopes*/
		if (ul == UADMIN)
			err = scope_AddScopes_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_remove_scopes)) {
	/*RemoveScopes*/
		if (ul == UADMIN)
			err = scope_RemoveScopes_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	/* }
	 * else if (soap_child_has_child(body, NULL, */
	/* str_method_set_discoverymode)){ */
	/* SetDiscoverable */
	/*	if (ul == UADMIN) */
	/*		err = wsd_SetDiscoverable(msg, &response); */
	/*	else */
	/*		unauthorized = true; */
	/* } */
	/*SetGeoLocation*/
	/*DeleteGeoLocation*/
	/*SetAccessPolicy*/
	/*CreateUsers*/
	/*DeleteUsers*/
	/*SetUsers*/
	/*SetRemoteUsers*/

	/*WRITE_SYSTEM - MEDIA*/
	/*SetVideoSourceMode*/

	/*UNRECOVERABLE*/
	/*RestoreSystem*/
	/*StartSystemRestore*/
	/*SetSystemFactoryDefault*/
	/*UpgradeSystemFirmware*/
	/*StartFirmwareUpgrade*/
	else if (soap_child_has_child(body, NULL, str_method_systemreboot)) {
	/*SystemReboot*/
		if (ul == UADMIN)
			err = device_SystemReboot_h(msg, &response);
		else
			unauthorized = true;
	}

	/*OPERATOR*/
	/*READ_SYSTEM_SENSIVITVE - NOTHING ???*/
	/*ACTUATE - CORE*/
	/*SetRelayOutputSettings*/
	/*SetRelayOutputState*/
	/*SendAuxiliaryCommand*/
	/*CreateStorageConfiguration*/
	/*SetStorageConfiguration*/
	/*DeleteStorageConfiguration*/

	/*ACTUATE - DEVICE-IO*/
	/*SetVideoOutputConfiguration*/
	/*SetRelayOutputSettings*/
	/*SetRelayOutputState*/
	/*SetDigitalInputConfiguration*/
	/*SetSerialPortConfiguration*/
	/*SendReceiveSerialCommand*/

	/*ACTUATE - MEDIA*/
	else if (soap_child_has_child(body, NULL, str_method_create_profile)) {
	/*CreateProfile*/
		if (ul <= UOPERATOR)
			err = media_CreateProfile_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_add_vsc)) {
	/*AddVideoSourceConfiguration*/
		if (ul <= UOPERATOR)
			err = media_AddVideoSourceConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_add_vec)) {
	/*AddVideoEncoderConfiguration*/
		if (ul <= UOPERATOR)
			err = media_AddVideoEncoderConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_add_asc)) {
	/*AddAudioSourceConfiguration*/
		if (ul <= UOPERATOR)
			err = media_AddAudioSourceConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_add_aec)) {
	/*AddAudioEncoderConfiguration*/
		if (ul <= UOPERATOR)
			err = media_AddAudioEncoderConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	/*AddPTZConfiguration*/
	/*AddVideoAnalyticsConfiguration*/
	/*AddMetadataConfiguration*/
	else if (soap_child_has_child(body, NULL, str_method_add_aoc)) {
	/*AddAudioOutputConfiguration*/
		if (ul <= UOPERATOR)
			err = media_AddAudioOutputConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_add_adc)) {
	/*AddAudioDecoderConfiguration*/
		if (ul <= UOPERATOR)
			err = media_AddAudioDecoderConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_remove_vsc)) {
	/*RemoveVideoSourceConfiguration*/
		if (ul <= UOPERATOR)
			err = media_RemoveVideoSourceConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_remove_vec)) {
	/*RemoveVideoEncoderConfiguration*/
		if (ul <= UOPERATOR)
			err = media_RemoveVideoEncoderConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_remove_asc)) {
	/*RemoveAudioSourceConfiguration*/
		if (ul <= UOPERATOR)
			err = media_RemoveAudioSourceConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_remove_aec)) {
	/*RemoveAudioEncoderConfiguration*/
		if (ul <= UOPERATOR)
			err = media_RemoveAudioEncoderConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	/*RemovePTZConfiguration*/
	/*RemoveVideoAnalyticsConfiguration*/
	/*RemoveMetadataConfiguration*/
	else if (soap_child_has_child(body, NULL, str_method_remove_aoc)) {
	/*RemoveAudioOutputConfiguration*/
		if (ul <= UOPERATOR)
			err = media_RemoveAudioOutputConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_remove_adc)) {
	/*RemoveAudioDecoderConfiguration*/
		if (ul <= UOPERATOR)
			err = media_RemoveAudioDecoderConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
					str_method_delete_profile)) {
	/*DeleteProfile*/
		if (ul <= UOPERATOR)
			err = media_DeleteProfile_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
					str_method_set_videosource)) {
	/*SetVidoeSourceConfiguration*/
		if (ul <= UOPERATOR)
			err = media_SetVideoSourceConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
					str_method_set_videoecnoder)) {
	/*SetVidoeEncoderConfiguration*/
		if (ul <= UOPERATOR)
			err = media_SetVideoEncoderConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
					str_method_set_audiosource)) {
	/*SetAudioSourceConfiguration*/
		if (ul <= UOPERATOR)
			err = media_SetAudioSourceConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
					str_method_set_audioecnoder)) {
	/*SetAudioEncoderConfiguration*/
		if (ul <= UOPERATOR)
			err = media_SetAudioEncoderConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	/*SetVidoeVnalyticsConfiguration*/
	/*SetMetadataConfiguration*/
	else if (soap_child_has_child(body, NULL,
				      str_method_set_audiooutput)) {
	/*SetAudioOutputConfiguration*/
		if (ul <= UOPERATOR)
			err = media_SetAudioOutputConfiguration_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	/*SetAudioDecoderConfiguration*/
	/*StartMulticastStreaming*/
	/*StopMulticastStreaming*/
	/*SetSyncronizationPoint*/
	/*CreateOSD*/
	/*DeleteOSD*/
	/*SetOSD*/

	/*USER*/
	/*READ_SYSTEM - CORE*/
	/*GetDNS*/
	/*GetNTP*/
	/*GetDynamicDNS*/
	else if (soap_child_has_child(body, NULL,
				      str_method_get_netinterfaces)) {
	/*GetNetworkInterfaces*/
		if (ul <= UUSER)
			err = device_GetNWI_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_ndg)) {
	/*GetNetworkDefaultGateway*/
		if (ul <= UUSER)
			err = device_GetNetworkDefaultGateway_h(msg,
								&response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
					str_method_get_nprotos)) {
	/*GetNetworkProtocols*/
		if (ul <= UUSER)
			err = device_GetNetworkProtocols_h(msg, &response);
		else
			unauthorized = true;
	}
	/*GetZeroConfiguration*/
	/*GetIPAddressFilter*/
	/*GetDot11Capabilities*/
	/*GetDot11Status*/
	/*ScanAvailableDot11Networks*/
	else if (soap_child_has_child(body, NULL,
				      str_method_get_device_info)) {
	/*GetDeviceInformation*/
		if (ul <= UUSER)
			err = device_GetDeviceInfo_h(msg, &response);
		else
			unauthorized = true;
	}
	/*GetSystemUris*/
	else if (soap_child_has_child(body, NULL, str_method_get_scopes)) {
	/*GetScopes*/
		if (ul <= UUSER)
			err = scope_GetScopes_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
					str_method_get_discoverymode)) {
	/*GetDiscoveryMode*/
		if (ul <= UUSER)
			err = wsd_GetDiscoverable(msg, &response);
		else
			unauthorized = true;
	}
	/*GetGeoLocation*/
	/*GetRemoteUsers*/

	/*READ_SYSTEM - DEVICE-IO*/
	/*GetDigitalInputs*/
	/*GetDigitalInputConfigurationOptions*/
	/*GetSerialPorts*/
	/*GetSerialPortConfiguration*/
	/*GetSerialPortConfigurationOptions*/

	/*READ_SYSTEM - MEDIA*/
	/*GetVideoSourceModes*/

	/*READ_MEDIA - CORE*/
	/*GetRelayOutputs*/
	/*GetStorageConfigurations*/
	/*GetStorageConfiguration*/
	/*CreatePullPointSubscription*/
	/*Renew*/
	/*Unsubscribe*/
	/*Seek*/
	/* else if (soap_child_has_child(body, NULL, */
	/* str_method_get_eventprop)) { */
	/* GetEventProperties */
	/*     if (ul <= UUSER) */
	/*         err = event_GetEventProperties_h(msg, &response); */
	/*     else */
	/*         unauthorized = true; */
	/* } */

	/*READ_MEDIA - DEVICE-IO*/
	/*GetVideoOutputs*/
	/*GetVideoOutputOptions*/
	else if (soap_child_has_child(body, NULL,
				      str_method_get_videosources) &&
		soap_msg_has_ns_uri(msg, str_uri_deviceio_wsdl)) {
	/*GetVideoSources*/
		if (ul <= UUSER)
			err = deviceio_GetVideoSources_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
					str_method_get_audiooutputs) &&
		soap_msg_has_ns_uri(msg, str_uri_deviceio_wsdl)) {
	/*GetAudioOuputs*/
		if (ul <= UUSER)
			err = deviceio_GetAudioOutputs_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
					str_method_get_audiosources) &&
		soap_msg_has_ns_uri(msg, str_uri_deviceio_wsdl)) {
	/*GetAudioSources*/
		if (ul <= UUSER)
			err = deviceio_GetAudioSources_h(msg, &response);
		else
			unauthorized = true;
	}
	/*GetRelayOuputs*/

	/*READ_MEDIA - MEDIA*/
	else if (soap_child_has_child(body, NULL, str_method_get_profiles)) {
	/*GetProfiles*/
		if (ul <= UUSER)
			err = media_GetProfiles_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_profile)) {
	/*GetProfile*/
		if (ul <= UUSER)
			err = media_GetProfile_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
			str_method_get_videosources) &&
			soap_msg_has_ns_uri(msg, str_uri_media_wsdl)) {
	/*GetVideoSources*/
		if (ul <= UUSER)
			err = media_GetVideoSources_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_vscs)) {
	/*GetVideoSourceConfigurations*/
		if (ul <= UUSER)
			err = media_GetVSCS_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_vsc)) {
	/*GetVideoSourceConfiguration*/
		if (ul <= UUSER)
			err = media_GetVSC_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_cvsc)) {
	/*GetCompatibleVideoSourceConfigurations*/
		if (ul <= UUSER)
			err = media_GetCompVideoSourceConfigs_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_vscos)) {
	/*GetVideoSourceConfigurationOptions*/
		if (ul <= UUSER)
			err = media_GetVideoSourceConfigurationOptions_h(msg,
				&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_vecs)) {
	/*GetVideoEncoderConfigurations*/
		if (ul <= UUSER)
			err = media_GetVECS_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_vec)) {
	/*GetVideoEncoderConfiguration*/
		if (ul <= UUSER)
			err = media_GetVEC_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_cvec)) {
	/*GetCompatibleVideoEncoderConfigurations*/
		if (ul <= UUSER)
			err = media_GetCompVideoEncoderConfigs_h(msg,
				&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_vecos)) {
	/*GetVideoEncoderConfigurationOptions*/
		if (ul <= UUSER)
			err = media_GetVideoEncoderConfigurationOptions_h(msg,
				&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_ggnovei)) {
	/*GetGuaranteedNumberOfVideoEncoderInstances*/
		if (ul <= UUSER)
			err = media_GetGuaranteedNumberOfVEInstances_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
			str_method_get_audiosources) &&
			soap_msg_has_ns_uri(msg, str_uri_media_wsdl)) {
	/*GetAudioSources*/
		if (ul <= UUSER)
			err = media_GetAudioSources_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_ascs)) {
	/*GetAudioSourceConfigurations*/
		if (ul <= UUSER)
			err = media_GetASCS_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_asc)) {
	/*GetAudioSourceConfiguratio*/
		if (ul <= UUSER)
			err = media_GetASC_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_casc)) {
	/*GetCompatibleAudioSourceConfigurations*/
		if (ul <= UUSER)
			err = media_GetCompAudioSourceConfigs_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_ascos)) {
	/*GetAudioSourceConfigurationOptions*/
		if (ul <= UUSER)
			err = media_GetAudioSourceConfigurationOptions_h(msg,
				&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_aecs)) {
	/*GetAudioEncoderConfigurations*/
		if (ul <= UUSER)
			err = media_GetAECS_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_aec)) {
	/*GetAudioEncoderConfiguration*/
		if (ul <= UUSER)
			err = media_GetAEC_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_caec)) {
	/*GetCompatibleAudioEncoderConfigurations*/
		if (ul <= UUSER)
			err = media_GetCompAudioEncoderConfigs_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_aecos)) {
	/*GetAudioEncoderConfigurationOptions*/
		if (ul <= UUSER)
			err = media_GetAudioEncoderConfigurationOptions_h(msg,
				&response, &f);
		else
			unauthorized = true;
	}
	/*GetVideoAnalyticsConfigurations*/
	/*GetVideoAnalyticsConfiguration*/
	/*GetCompatibleVideoAnalyticsConfigurations*/
	else if (soap_child_has_child(body, NULL, str_method_get_mdconfigs)) {
	/*GetMetadataConfigurations*/
		if (ul <= UUSER)
			err = media_GetMetadataConfigurations_h(msg,
								&response);
		else
			unauthorized = true;
	}
	/*GetMetadataConfiguration*/
	/*GetCompatibleMetadataConfigurations*/
	/*GetMetadataConfigurationOptions*/
	else if (soap_child_has_child(body, NULL,
			str_method_get_audiooutputs) &&
			soap_msg_has_ns_uri(msg, str_uri_media_wsdl)) {
	/*GetAudioOutputs*/
		if (ul <= UUSER)
			err = media_GetAudioOutputs_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_aocs)) {
	/*GetAudioOutputConfigurations*/
		if (ul <= UUSER)
			err = media_GetAOCS_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_aoc)) {
	/*GetAudioOutputConfiguration*/
		if (ul <= UUSER)
			err = media_GetAOC_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_caoc)) {
	/*GetCompatibleAudioOutputConfiguration*/
		if (ul <= UUSER)
			err = media_GetCompAudioOutputConfigs_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_aocos)) {
	/*GetAudioOutputConfigurationOptions*/
		if (ul <= UUSER)
			err = media_GetAudioOutputConfigurationOptions_h(msg,
				&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_adcs)) {
	/*GetAudioDecoderConfigurations*/
		if (ul <= UUSER)
			err = media_GetADCS_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_adc)) {
	/*GetAudioDecoderConfiguration*/
		if (ul <= UUSER)
			err = media_GetADC_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_cadc)) {
	/*GetCompatibleAudioDecoderConfigurations*/
		if (ul <= UUSER)
			err = media_GetCompAudioDecoderConfigs_h(msg,
							&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_adcos))  {
	/*GetAudioDecoderConfigurationOptions*/
		if (ul <= UUSER)
			err = media_GetAudioDecoderConfigurationOptions_h(msg,
				&response, &f);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL, str_method_get_suri)) {
	/*GetStreamUri*/
		if (ul <= UUSER)
			err = media_GetStreamUri_h(msg, &response, &f);
		else
			unauthorized = true;
	}
	/*GetSnapshotUri*/
	/*GetOSDs*/
	/*GetOSD*/
	/*GetOSDOptions*/

	/*READ_MEDIA - PTZ*/
	else if (soap_child_has_child(body, NULL, str_method_get_nodes)) {
	/*GetNodes*/
		if (ul <= UUSER)
			err = ptz_GetNodes_h(msg, &response);
		else
			unauthorized = true;
	}
	else if (soap_child_has_child(body, NULL,
		str_method_get_configurations)) {
	/*GetConfigurations*/
		if (ul <= UUSER)
			err = ptz_GetConfigurations_h(msg, &response);
		else
			unauthorized = true;
	}

	/*ANON*/
	/*PRE_AUTH - CORE*/

	else if (soap_child_has_child(body, NULL, str_method_get_wsdlurl)) {
	/*GetWsdlUrl*/
		err = device_GetWsdlUrl_h(msg, &response);
	}
	else if (soap_child_has_child(body, NULL, str_method_get_services)) {
	/*GetServices*/
		err = device_GetServices_h(msg, &response);
	}
	else if (soap_child_has_child(body, NULL,
					str_method_get_service_cap)) {
	/*GetServiceCapabilities*/
		err = device_GetServiceCapabilities_h(msg, &response);
	}
	else if (soap_child_has_child(body, NULL,
					str_method_get_capabilities)) {
	/*GetCapabilities*/
		err = device_GetCapabilities_h(msg, &response, &f);
	}
	else if (soap_child_has_child(body, NULL, str_method_get_hostname)) {
	/*GetHostname*/
		err = device_GetHostname_h(msg, &response);
	}
	else if (soap_child_has_child(body, NULL, str_method_get_systime)) {
	/*GetSystemDateAndTime*/
		err = device_GetSystemDateAndTime_h(msg, &response);
	}
	/*GetEndpointReference*/

	/*PRE_AUTH - DEVICE-IO*/
	/*GetRelayOutputOptions*/

	else {
		fault_set(&f, FC_Sender, FS_UnknownAction, FS_MAX,
			"Requested method not implementetd");
		DEBUG_WARNING("WTF are u doing???\n");
		goto make_fault;
	}

	if (unauthorized) {
		fault_set(&f, FC_Sender, FS_NotAuthorized, FS_MAX,
			"Sender not Authorized");
		DEBUG_WARNING("U can't reach this method with our level\n");
		goto make_fault;
	}

  make_fault:
	if (f.is_set) {
		/* create fault msg */
		err = fault_create(msg, &response, &f);
		fault_clear(&f);
	}
	else if (err) {
		/* program error */
		goto out;
	}

	info ("\n######## SOAP RESPONSE ########\n");
	if (!response)
		goto out;

	soap_msg_print(response);
	err = soap_msg_encode(response);

	(void) header;
	goto out;

  out:
	if (err) {
		DEBUG_WARNING("Here should stand your personal SOAP error"
			      " message :P - "
			"(%m)\n", err);
		/*build error message */
	}
	else {
		*ptr_res = response;
	}

  noresponse:
	info ("\n######## SOAP RESPONSE DONE ########\n");
	return err;
}


/**
 *	UDP receive handler
 */
void soap_udp_recv_handler(const struct sa *src, struct mbuf *mb,
	void *arg)
{
	int err = 0;
	struct soap_msg *msg = NULL;
	struct soap_msg *res = NULL;

	(void) arg;
	DEBUG_INFO("%s Connection from %J\n", __func__, src);

	msg = (struct soap_msg *) mem_zalloc(sizeof(*msg),
					     soap_msg_destructor);
	if (!msg)
		return;

	msg->envelope = NULL;
	msg->mb = mem_ref(mb);
	list_init(&msg->l_namespaces);
	msg->nsnum = 0;

	err = soap_msg_decode(msg);
	if (err) {
		warning("%s Got unsupported xml. err=(%m)", __func__, err);
		goto out;
	}

	if (!msg->envelope)
		goto out;

	info ("\n######## UDP Request ########\n");
	soap_msg_print(msg);

	err = soap_request_handler(msg, &res);
	if (!err && res) {
		udp_send(udps, src, res->mb);
	}

  out:
	mem_deref(msg);
	mem_deref(res);

	return;
}


/**
 *	SOAP HTTP/XML receive handler
 */
void http_req_handler(struct http_conn *conn,
		const struct http_msg *http_msg, void *arg)
{
	int err = 0;
	struct soap_msg *msg = NULL;
	struct soap_msg *res = NULL;

	(void) arg;
	if (pl_strcmp(&http_msg->ctyp.type, "application") ||
			pl_strcmp(&http_msg->ctyp.subtype, "soap+xml"))
		return;

	DEBUG_INFO("%s Connection from %J\n", __func__, http_conn_peer(conn));

	msg = (struct soap_msg *) mem_zalloc(sizeof(*msg),
					     soap_msg_destructor);
	if (!msg)
		return;

	msg->envelope = NULL;
	msg->mb = mem_ref(http_msg->mb);
	list_init(&msg->l_namespaces);
	msg->nsnum = 0;

	err = soap_msg_decode(msg);
	if (err) {
		warning("%s Got unsupported xml. err=(%m)", __func__, err);
		goto out;
	}

	info ("\n######## HTTP Request ########\n");
	soap_msg_print(msg);

	err = soap_request_handler(msg, &res);

	if (!err && res && soap_msg_has_ns_prefix(res, str_pf_error)) {
		http_creply(conn, 400, "Bad Request", str_http_ctype, "%b",
			mbuf_buf(res->mb), mbuf_get_left(res->mb));
	}
	else if (!err && res) {
		http_creply(conn, 200, "OK", str_http_ctype, "%b",
			mbuf_buf(res->mb), mbuf_get_left(res->mb));
	}

  out:
	mem_deref(msg);
	mem_deref(res);

	return;
}
