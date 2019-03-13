/* @file scopes.h
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */

#ifndef _ONVIF_SCOPES_H_
#define _ONVIF_SCOPES_H_

#define MAXDYNSCOPES 16

struct soap_msg;
struct soap_fault;

int scope_init(void);
void scope_deinit(void);

int scope_GetScopes_h(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *f);
int scope_SetScopes_h(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *f);
int scope_AddScopes_h(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *f);
int scope_RemoveScopes_h(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *f);
int scope_add_all_scopes(const struct soap_msg *req, struct soap_msg *response,
	struct soap_child *c, bool as_child);


#endif /* _ONVIF_SCOPES_H_ */

