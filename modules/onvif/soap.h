/* @file soap.h  XML
 *
 * Copyright (C) 2018 commend.com - Christoph Huber
 */

#ifndef _SOAP_H_
#define _SOAP_H_

#define SOAP_MAX_MSG_SIZE 1024 * 10
#define SOAP_MAX_STACKSIZE 10

#define LAST_CHILD(x) while(x->le.next){x = x->le.next->data;}

extern struct pl onvif_config_path;

extern struct udp_sock *udps;
extern struct http_sock *httpsock;

enum soap_childtype {
	SCT_NORMAL_NOPARAM,         // TYPE 0: <ns:Key>
	SCT_NORMAL_PARAM,           // TYPE 1: <ns:Key [param]>
	SCT_END_NORMAL,             // TYPE 2: </ns:Key>
	SCT_IEND_NOPARAM,           // TYPE 3: <ns:Key />
	SCT_IEND_PARAM,             // TYPE 4: <ns:Key [param] />

	SCT_MAX,
};

enum soap_attr_type {
	SAT_NS_DECL_SIMPLE,         // TYPE 0: xmlns
	SAT_NS_DECL,                // TYPE 1: xmlns:[Name]
	SAT_NS_ATTR,                // TYPE 2: [Namespace]:[Name]
	SAT_ATTR,                   // TYPE 3: [Name]

	SAT_MAX,
};


struct soap_parameter {       // 96 bytes
	struct le le;

	struct pl key;
	struct pl xmlns;
	struct soap_namespace *ns;
	struct pl value;
	char *str_value;
};


struct soap_namespace {       // 72 bytes
	struct le le;

	struct pl prefix;
	struct pl uri;
	char      *numbered_ns;
};


struct soap_child {          // 120 bytes
	struct le   le;

	struct soap_namespace *ns;
	struct pl   key;
	struct pl   value;
	char        *str_value;

	struct list l_parameters;
	struct list l_childs;

	struct soap_msg *msg;
};


struct soap_msg {             //56 bytes
	struct mbuf *mb;
	struct pl prolog;
	struct soap_child *envelope;

	struct list l_namespaces;
	uint8_t nsnum;
};


const char *str_response_has_no_body;
const char *str_response_has_no_header;

void soap_udp_recv_handler(const struct sa *src, struct mbuf *mb, void *arg);
void http_req_handler(struct http_conn *conn,
	const struct http_msg *http_msg, void *arg);

void soap_msg_print(struct soap_msg *m);

struct soap_namespace *soap_msg_add_ns_pl(struct soap_msg *msg,
	struct pl *prefix, struct pl *uri);
struct soap_namespace *soap_msg_add_ns_str(struct soap_msg *msg,
	const char *prefix, const char *uri);
int soap_msg_add_ns_str_param(struct soap_msg *msg,
	const char *prefix, const char *uri);
struct soap_namespace *soap_msg_has_ns_prefix(struct soap_msg *msg,
	const char *prefix);
struct soap_namespace *soap_msg_has_ns_uri(const struct soap_msg *msg,
	const char *uri);

struct soap_child *soap_child_has_child(const struct soap_child *c,
	const struct soap_child *last, const char *key);
struct soap_parameter *soap_child_has_parameter(const struct soap_child *c,
	const char *key);

int soap_set_value_strref(struct soap_child *c, char *v);
int soap_set_value_fmt(struct soap_child *c, const char *fmt, ...);

int soap_add_parameter_str(struct soap_child *c, const char *ns_prefix,
	const char *key, const size_t k_len, const char *value, const size_t v_len);
int soap_add_parameter_uint(struct soap_child *c, const char* ns_prefix,
	const char *key, const size_t k_len, const uint32_t n);
struct soap_child *soap_add_child(struct soap_msg *msg,
	struct soap_child *parent, const char *ns_prefix, const char* key);
int soap_alloc_msg(struct soap_msg **ptrmsg);

int soap_msg_encode(struct soap_msg *msg);

#endif /* _SOAP_H_ */

