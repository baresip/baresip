/**
 * @file onvif_auth.h RTSP and SOAP / WS-Security authentification
 *
 * Copyright (C) 2019 Christoph Huber
 */

#ifndef _ONVIF_AUTH_H_
#define _ONVIF_AUTH_H_

#define MAXUSERLEN 32 + 1
#define MAXPASSWDLEN 64 + 1

#include <openssl/sha.h>

struct soap_msg;
struct rtsp_conn;
struct rtsp_msg;

enum userlevel {            /** User Level    */
	UADMIN,                 // 0
	UOPERATOR,              // 1
	UUSER,                  // 2
	UANONYM,                // 3

	UMAX,                   // Fault entries
};


struct rtsp_digest_chall {  /** RTSP (HTTP) Digest Wrapperstruct*/
	struct le le;

	char nonce[SHA_DIGEST_LENGTH * 2 + 1];
	char opaque[64 + 1];
	struct httpauth_digest_chall param;
};

int onvif_auth_init_users(void);
void onvif_auth_deinit_users(void);

int onvif_auth_GetUsers_h(const struct soap_msg *msg,
	struct soap_msg **ptrresp);
enum userlevel wss_auth(const struct soap_msg *msg);

int rtsp_digest_auth_chall(const struct rtsp_conn *conn,
	struct rtsp_digest_chall **ptrchall);
enum userlevel rtsp_digest_auth(const struct rtsp_msg *msg);

#endif /* _ONVIF_AUTH_H_ */
