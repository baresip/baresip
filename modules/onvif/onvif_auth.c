/**
 * @file onvif_auth.c RTSP and SOAP / WS-Security authentification
 *
 * Copyright (C) 2019 Christoph Huber
 */
#include <re.h>
#include <string.h>
#include <baresip.h>

#include "soap.h"
#include "onvif_auth.h"
#include "soap_str.h"

#define DEBUG_MODULE "rtsp & wss auth"
#define DEBUG_LEVEL 6
#include <re_dbg.h>

enum {
	B64DIGEST_LEN = 4 * ((SHA_DIGEST_LENGTH / 3) + 1),
};

/**
 * Writing its own file read function could lead to the following pros + cons:
 * + reading the file line by line would allow us to read the line as a
 * stack variable, since the max length is known by
 * MAXUSERLEN + MAXPASSWDLEN + 1 Char USERLEVEL + 2 * ';' + 1 * '\n'
 * this can save us from fragmentation, but just in case where we do not
 * cache the passwords. If the password is not cached, each time a
 * authentification is requested, we have to read the hole file into the heap.
 * - the acutal file read should be a part of the libre since its a very
 *   lowlevel operation on the host filesystem. Depending on the OS we have
 *   to handle different ways to read the file.
 **/

/*USER LEVEL STRINGS*/
static const char *ul_str[] = {
	"Administrator",
	"Operator",
	"User",
	"Anonymous",
};

/* USER Information struct */
/* i donno, seems pretty stupid to load and hold the password over the hole
 * time */
/* 1) load the users + group */
/* 2) load file again on demand (caching is not the best idea for
 * passwords -.-) */
struct user {
	struct le le;

	char name [MAXUSERLEN];   /** User Name     */
	enum userlevel userlevel;
};


/* User List */
static struct list user_l;


static bool username_cmp(struct le *le, void *arg)
{
	struct pl *uname = arg;
	struct user *u = le->data;

	return (0 == pl_strcmp(uname, u->name));
}


static char *dynusers_path(void)
{
	int n;
	size_t len = onvif_config_path.l + strlen("/users") + 1;
	char *path = NULL;

	path = mem_zalloc(len, NULL);
	if (!path)
		return NULL;

	n = re_snprintf(path, len, "%r%s",
			  &onvif_config_path, "/users");
	if (n != ((int) len) - 1) {
		warning ("%s Can not concat string here -.-\n", __func__);
		path = mem_deref(path);
	}

	return path;
}


/**
 * reads the userfile into a memory buffer
 *
 * This function is called each time a authentification request is received
 * So the password of a user is only as long in memory as needed
 *
 * @param ptruserfile       pointer to a memory buffer which should contain the
 *                          user file content
 *
 * @return          0 if success
 *                  EOVERFLOW if empty line is found
 */
static int onvif_auth_read_userfile(struct mbuf **ptruserfile)
{
	int err = 0;
	char *userpath;
	struct mbuf *userfile = NULL;

	if (!ptruserfile)
		return EINVAL;

	userpath = dynusers_path();
	if (!userpath)
		goto out;

	userfile = mbuf_alloc(512);
	if (!userfile) {
		err = ENOMEM;
		goto out;
	}

	err = load_file(userfile, userpath);
	if (err)
		goto out;

	mbuf_set_pos(userfile, 0);

  out:
	if (err)
		mem_deref(userfile);
	else
		*ptruserfile = userfile;

	mem_deref(userpath);
	return err;
}


/**
 * close the memory buffer of the user data.
 * the buffer contains the hole userfile (including passwords)
 * do not let the passwords in memory -> set the hole buffer to 0
 * then deref the buffer
 *
 * @param userfile          memory buffer containing the userfile
 *
 */
static void onvif_auth_closeclear_userfile(struct mbuf *userfile)
{
	mbuf_set_pos(userfile, 0);
	mbuf_fill(userfile, 0, mbuf_get_left(userfile));
	mem_deref(userfile);
}


/**
 * Get a <userlevel>,<username>,<userpasswd>| line from the file buffer
 *
 * @param mb        memory buffer containing the userfile
 * @param line      pointer to a char, to return the start of the line
 * @param len       pointer to a size_t, to return the lenght of the line
 *
 * @return          0 if success
 *                  EOVERFLOW if empty line is found
 */
static int onvif_auth_getuserentryfromfile(struct mbuf *mb,
	char **line, size_t *len)
{
	char *b = NULL, *e = NULL, tmp;
	size_t l = 0;

	b = (char *)mbuf_buf(mb);
	tmp = (char)mbuf_read_u8(mb);
	for (; tmp != '|' && mbuf_get_left(mb); tmp = (char)mbuf_read_u8(mb));
	e = (char *)mbuf_buf(mb);
	l = e - b - 1;
	if (l <= 0)
		return EOVERFLOW;

	*line = b;
	*len = l;

	return 0;
}


 /**
 * Parse a user from a given @line with @linelen
 *
 * @param u         pointer to a user struct
 * @param line      pointer to the start of the line
 * @param linelen   lenght of the line
 *
 * @return          0 if success
 *                  EOVERFLOW if entry name is too long or out-of-range
 *                      userlevel
 *                  ENOMEM out of memory
 *                  EINVAL invalid argument
 */
static int onvif_auth_parse_user(struct user **u, char *line, size_t linelen)
{
	int err = 0;
	uint8_t groupnb;
	struct pl pluname, plgroupnb;
	struct user *user = NULL;

	if (!u || !line || linelen <= 0)
		return EINVAL;

	err = re_regex(line, linelen, "[0-4]1,[a-z | A-Z | 0-9 | \\_\\^$?.\\*\\+\\-&\\[\\{\\(\\)\\}\\]/!#\\%:;=@~]*,",
		&plgroupnb, &pluname);
	if (err)
		return err;

	if (pluname.l > MAXUSERLEN - 1)
		return EOVERFLOW;

	groupnb = (uint8_t)pl_u32(&plgroupnb);
	if (groupnb > UMAX)
		return EOVERFLOW;

	user = mem_zalloc(sizeof(*user), NULL);
	if (!user) {
		err = ENOMEM;
		goto out;
	}

	err = pl_strcpy(&pluname, user->name, sizeof(user->name));
	if (err)
		goto out;

	user->userlevel = groupnb;

  out:
	if (err)
		mem_deref(user);
	else
		*u = user;

	return err;
}


/**
 * parse the users listed in <onvif_config_path>/users
 * and save them as structs in the list @user_l
 *
 *
 * @param mb        memory buffer contining the users file
 *
 * @return          0 if success, errorcode otherwise
 */
static int onvif_auth_parse_users(struct mbuf *mb)
{
	int err = 0;
	char *b = NULL;
	struct user *u = NULL;
	size_t len = 123;

	while (mbuf_get_left(mb)) {
		err = onvif_auth_getuserentryfromfile(mb, &b, &len);
		if (err && err == EOVERFLOW) {
			err = 0;
			break;
		}

		if (err)
			goto out;

		err = onvif_auth_parse_user(&u, b, len);
		if (err && err == EOVERFLOW) {
			err = 0;
			continue;
		}

		if (err)
			goto out;

		list_append(&user_l, &u->le, u);
	}

  out:
	if (err)
		list_flush(&user_l);

	return err;
}


/**
 * Add userinformation to the soap child @gurc
 *
 * @param gurc          GetUserRespones child which should hold the user infos
 *
 * @return              0 if success, error code otherwise
 */
static int onvif_auth_adduser_to_child(struct soap_child *gurc)
{
	int err = 0;
	struct soap_child *userc, *usc, *ulc;
	struct user *u;
	struct le *le;

	LIST_FOREACH(&user_l, le) {
		u = le->data;
		userc = soap_add_child(gurc->msg, gurc,
			str_pf_device_wsdl, str_gu_user);
		usc = soap_add_child(gurc->msg, userc, str_pf_schema,
				     str_gu_username);
		ulc = soap_add_child(gurc->msg, userc, str_pf_schema,
				     str_gu_userlevel);

		err |= soap_set_value_fmt(usc, "%s", u->name);
		err |= soap_set_value_fmt(ulc, "%s", ul_str[u->userlevel]);
	}

	return err;
}


/**
 * get the password assosiated to user @u
 *
 * @param u         user struct
 * @param passwd    stack string with max size @MAXPASSWDLEN
 *
 */
static void onvif_auth_getuserpasswd(struct user *u, char *passwd)
{
	struct mbuf *userfile = NULL;
	char *line;
	size_t linelen;
	struct pl username, pw;

	if (onvif_auth_read_userfile(&userfile))
		return;

	while (mbuf_get_left(userfile)) {
		if (onvif_auth_getuserentryfromfile(userfile, &line, &linelen))
			break;

		if (re_regex(line, linelen, "[0-4]1,[a-z | A-Z | 0-9 | \\_\\^$?.\\*\\+\\-&\\[\\{\\(\\)\\}\\]/!#\\%:;=@~]*,[^\n]*",
			NULL, &username, &pw))
			break;

		if (0 != pl_strcmp(&username, u->name))
			continue;

		if (pw.l > MAXPASSWDLEN - 1)
			break;

		if (!strncpy(passwd, pw.p, pw.l))
			break;

		break;
	}

	onvif_auth_closeclear_userfile(userfile);
	return;
}


/**
 * create a server side nonce depending on a timestamp and the peer ip address
 * SHA1(32BYTE_Random || timestamp || peer_ipaddress || 32BYTE_Random)
 *
 * @param msg       request msg
 *
 * @return          returns the userlevel, if the user is authenticated
 *
 */
static int create_server_nonce(uint8_t *ptrnonce,
			       const struct sa *peer_address)
{
	int err = 0;
	char timestamp[30];
	char ipaddr[16];
	uint8_t random[64];
	SHA_CTX sha1;

	memset(timestamp, 0 , sizeof(timestamp));
	memset(ipaddr, 0 , sizeof(ipaddr));
	memset(random, 0 , sizeof(random));

	err = re_snprintf(timestamp, sizeof(timestamp), "%H", fmt_gmtime);
	if (err != sizeof(timestamp) - 1)
		return EINVAL;

	err = re_snprintf(ipaddr, sizeof(ipaddr), "%j", peer_address);
	if ((size_t)err > sizeof(ipaddr) - 1)
		return EINVAL;

	rand_bytes(random, 64);

	SHA1_Init(&sha1);
	SHA1_Update(&sha1, random, 32);
	SHA1_Update(&sha1, timestamp, sizeof(timestamp) - 1);
	SHA1_Update(&sha1, ipaddr, sizeof(ipaddr) - 1);
	SHA1_Update(&sha1, random + 32, 32);
	SHA1_Final(ptrnonce, &sha1);

	return 0;
}


/**
 * Initiate the user list of onvif with the data in <onvif_config_path/users>
 *
 * USER FORMAT:
 *      <UserLevel>,<UserName>,<UserPassword>|...
 *      <  NUMBER >,< STRING >,<   STRING   >|...
 *
 * @return          0 if success, errorcode otherwise
 */
int onvif_auth_init_users(void)
{
	int err = 0;
	struct mbuf *userfile = NULL;

	err = onvif_auth_read_userfile(&userfile);
	if (err)
		return err;

	err = onvif_auth_parse_users(userfile);
	onvif_auth_closeclear_userfile(userfile);

	return err;
}


/**
 * Release the user list
 */
void onvif_auth_deinit_users(void)
{
	list_flush(&user_l);
}


/**
 * GetUsers Request Handler
 *
 * @param msg           request message
 * @param ptrresq       pointer for the returned response message
 *
 * @return              0 if success, error code otherwise
 */
int onvif_auth_GetUsers_h(const struct soap_msg *msg,
			  struct soap_msg **ptrresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gurc;

	if (!msg || !ptrresp)
		return EINVAL;


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
	gurc = soap_add_child(resp, b, str_pf_device_wsdl,
			      str_method_get_users_r);

	err |= onvif_auth_adduser_to_child(gurc);

  out:
	if (err)
		mem_deref(resp);
	else
		*ptrresp = resp;

	return err;
}


/**
 * WS-Security authentication check
 *
 * @param msg       request msg
 *
 * @return          returns the userlevel, if the user is authenticated
 *
 */
enum userlevel wss_auth(const struct soap_msg *msg)
{
	int err = 0;
	struct soap_child *h, *sc, *utc, *uc, *pwc, *nc, *cc;
	struct soap_parameter *pwtype;
	struct user *u = NULL;
	struct le *le = NULL;
	char passwd[MAXPASSWDLEN];
	SHA_CTX sha1;
	uint8_t digest[SHA_DIGEST_LENGTH];

	uint8_t *nonce;
	size_t noncelen = 0;

	size_t blen = B64DIGEST_LEN;
	char b64digest[B64DIGEST_LEN];

	if (!msg)
		return UANONYM;

	memset(passwd, 0, sizeof(passwd));
	memset(digest, 0, sizeof(digest));
	memset(b64digest, 0, sizeof(b64digest));

	h = soap_child_has_child(msg->envelope, NULL, str_header);
	if (!h)
		return UANONYM;

	sc = soap_child_has_child(h, NULL, str_wss_security);
	if (!sc)
		return UANONYM;

	utc = soap_child_has_child(sc, NULL, str_wss_usernametoken);
	if (!utc)
		return UANONYM;

	uc = soap_child_has_child(utc, NULL, str_wss_username);
	pwc = soap_child_has_child(utc, NULL, str_wss_password);
	nc = soap_child_has_child(utc, NULL, str_wss_nonce);
	cc = soap_child_has_child(utc, NULL, str_wss_created);
	if (!uc || !pwc || !nc || !cc)
		return UANONYM;

	pwtype = soap_child_has_parameter(pwc, str_uctype);
	if (!pwtype)
		return UANONYM;

	if (0 != pl_strcmp(&pwtype->value, str_uri_passwd_type))
		return UANONYM;

	le = list_apply(&user_l, true, username_cmp, (void*)&uc->value);
	if (!le)
		return UANONYM;

	noncelen = ((nc->value.l/4)) * 3;
	nonce = mem_zalloc(noncelen, NULL);
	if (!nonce)
		return UMAX;

	u = le->data;
	err = base64_decode(nc->value.p, nc->value.l, nonce, &noncelen);
	if (err) {
		mem_deref(nonce);
		return UMAX;
	}

	SHA1_Init(&sha1);
	SHA1_Update(&sha1, nonce, noncelen);
	SHA1_Update(&sha1, cc->value.p, cc->value.l);
	onvif_auth_getuserpasswd(u, passwd);
	SHA1_Update(&sha1, passwd, strlen(passwd));
	memset(passwd, 0, sizeof(passwd));
	SHA1_Final(digest, &sha1);
	err = base64_encode(digest, SHA_DIGEST_LENGTH, b64digest, &blen);
	if (err) {
		mem_deref(nonce);
		return UMAX;
	}

	if (0 != memcmp(pwc->value.p, b64digest, pwc->value.l)) {
		mem_deref(nonce);
		return UANONYM;
	}

	mem_deref(nonce);
	return u->userlevel;
}


/**
 * Create a Digest Challenge for RTSP Auth
 *
 * @param msg       request msg
 * @param ptrchall  pointer to a rtsp_digest_chall object
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int rtsp_digest_auth_chall(const struct rtsp_conn *conn,
	struct rtsp_digest_chall **ptrchall)
{
	int err = 0;
	struct rtsp_digest_chall *chall;
	struct sa peer;
	uint8_t nonce[SHA_DIGEST_LENGTH];

	chall = mem_zalloc(sizeof(struct rtsp_digest_chall), NULL);
	if (!chall)
		return ENOMEM;

	err = tcp_conn_peer_get(rtsp_conn_tcp(conn), &peer);
	err |= create_server_nonce(nonce, &peer);
	if (err)
		goto out;

	re_snprintf(chall->nonce, sizeof(chall->nonce), "%w", nonce,
		    sizeof(nonce));
	rand_str(chall->opaque, 64);
	pl_set_str(&chall->param.realm, str_digest_realm);
	pl_set_str(&chall->param.nonce, chall->nonce);
	pl_set_str(&chall->param.qop, str_digest_qop);
	pl_set_str(&chall->param.algorithm, str_digest_md5sess);
	pl_set_str(&chall->param.opaque, chall->opaque);

  out:
	if (err)
		mem_deref(chall);
	else
		*ptrchall = chall;

	return err;
}


/**
 * Check a Digest Response and return the userlevel if user can be
 * authenticated
 *
 * @param msg       request msg
 *
 * @return          returns the userlevel, if the user is authenticated
 *
 */
enum userlevel rtsp_digest_auth(const struct rtsp_msg *msg)
{
	int err = 0;
	const struct rtsp_hdr *hdr = NULL;
	struct httpauth_digest_resp *resp;
	char passwd[MAXPASSWDLEN];
	uint8_t ha1[MD5_SIZE];
	struct user *u;
	struct le *le;

	memset(passwd, 0, sizeof(passwd));
	memset(ha1, 0, sizeof(ha1));

	resp = mem_zalloc(sizeof(*resp), NULL);
	if (!resp)
		return UMAX;

	hdr = rtsp_msg_hdr(msg, RTSP_HDR_AUTHORIZATION);
	if (!hdr) {
		err = EINVAL;
		goto out;
	}

	err = httpauth_digest_response_decode(resp, &hdr->val);
	if (err)
		goto out;

	le = list_apply(&user_l, true, username_cmp, &resp->username);
	if (!le) {
		err = EINVAL;
		goto out;
	}

	u = le->data;
	onvif_auth_getuserpasswd(u, passwd);
	err = md5_printf(ha1, "%s:%r:%s", u->name, &resp->realm, passwd);
	memset(passwd, 0, sizeof(passwd));
	if (err)
		goto out;

/*        if (pl_isset(&resp->algorithm) &&*/
/*                (0 == pl_strcmp(&resp->algorithm, str_digest_md5sess))) {*/
/*                err = md5_printf(ha1, "%b:%r:%r", ha1, sizeof(ha1),*/
/*                        &resp->nonce, &resp->cnonce);*/
/*                if (err)*/
/*                        goto out;*/
/*        }*/

	err = httpauth_digest_response_auth(resp, &msg->met, ha1);

  out:
	mem_deref(resp);
	if (err)
		return UMAX;

	return u->userlevel;
}
