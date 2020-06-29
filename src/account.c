/**
 * @file src/account.c  User-Agent account
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


enum {
	REG_INTERVAL    = 3600,
};


static void destructor(void *arg)
{
	struct account *acc = arg;
	size_t i;

	list_clear(&acc->aucodecl);
	list_clear(&acc->vidcodecl);
	mem_deref(acc->auth_user);
	mem_deref(acc->auth_pass);
	for (i=0; i<ARRAY_SIZE(acc->outboundv); i++)
		mem_deref(acc->outboundv[i]);
	mem_deref(acc->regq);
	mem_deref(acc->sipnat);
	mem_deref(acc->stun_user);
	mem_deref(acc->stun_pass);
	mem_deref(acc->stun_host);
	mem_deref(acc->mnatid);
	mem_deref(acc->mencid);
	mem_deref(acc->aor);
	mem_deref(acc->dispname);
	mem_deref(acc->buf);
	mem_deref(acc->ausrc_mod);
	mem_deref(acc->ausrc_dev);
	mem_deref(acc->auplay_mod);
	mem_deref(acc->auplay_dev);
	mem_deref(acc->extra);
}


static int param_dstr(char **dstr, const struct pl *params, const char *name)
{
	struct pl pl;

	if (msg_param_decode(params, name, &pl))
		return 0;

	return pl_strdup(dstr, &pl);
}


static int param_u32(uint32_t *v, const struct pl *params, const char *name)
{
	struct pl pl;

	if (msg_param_decode(params, name, &pl))
		return 0;

	*v = pl_u32(&pl);

	return 0;
}


/*
 * Decode STUN parameters, inspired by RFC 7064
 *
 * See RFC 3986:
 *
 *     Use of the format "user:password" in the userinfo field is
 *     deprecated.
 *
 */
static int stunsrv_decode(struct account *acc, const struct sip_addr *aor)
{
	struct pl srv, tmp;
	int err;

	if (!acc || !aor)
		return EINVAL;

	if (0 == msg_param_decode(&aor->params, "stunserver", &srv)) {

		info("using stunserver: '%r'\n", &srv);

		err = stunuri_decode(&acc->stun_host, &srv);
		if (err) {
			warning("account: decode '%r' failed: %m\n",
				&srv, err);
		}
	}

	err = 0;

	if (0 == msg_param_exists(&aor->params, "stunuser", &tmp))
		err |= param_dstr(&acc->stun_user, &aor->params, "stunuser");
	else
		err |= pl_strdup(&acc->stun_user, &aor->uri.user);

	if (0 == msg_param_exists(&aor->params, "stunpass", &tmp))
		err |= param_dstr(&acc->stun_pass, &aor->params, "stunpass");
	else if (acc->auth_pass)
		err |= str_dup(&acc->stun_pass, acc->auth_pass);

	return err;
}


/* Decode media parameters */
static int media_decode(struct account *acc, const struct pl *prm)
{
	int err = 0;

	if (!acc || !prm)
		return EINVAL;

	err |= param_dstr(&acc->mencid,  prm, "mediaenc");
	err |= param_dstr(&acc->mnatid,  prm, "medianat");
	err |= param_u32(&acc->ptime,    prm, "ptime"   );

	return err;
}


/* Decode extra parameter */
static int extra_decode(struct account *acc, const struct pl *prm)
{
	int err = 0;

	if (!acc || !prm)
		return EINVAL;

	err |= param_dstr(&acc->extra, prm, "extra");

	return err;
}


static int decode_pair(char **val1, char **val2,
		       const struct pl *params, const char *name)
{
	struct pl val, pl1, pl2;
	int err = 0;

	if (0 == msg_param_decode(params, name, &val)) {

		/* note: second value may be quoted */
		err = re_regex(val.p, val.l, "[^,]+,[~]*", &pl1, &pl2);
		if (err)
			return err;

		err  = pl_strdup(val1, &pl1);
		err |= pl_strdup(val2, &pl2);
		if (err)
			return err;
	}

	return 0;
}


/* Decode answermode parameter */
static void answermode_decode(struct account *prm, const struct pl *pl)
{
	struct pl amode;

	prm->answermode = ANSWERMODE_MANUAL;

	if (0 == msg_param_decode(pl, "answermode", &amode)) {

		if (0 == pl_strcasecmp(&amode, "manual")) {
			prm->answermode = ANSWERMODE_MANUAL;
		}
		else if (0 == pl_strcasecmp(&amode, "early")) {
			prm->answermode = ANSWERMODE_EARLY;
		}
		else if (0 == pl_strcasecmp(&amode, "auto")) {
			prm->answermode = ANSWERMODE_AUTO;
		}
		else {
			warning("account: answermode unknown (%r)\n", &amode);
		}
	}
}


static int csl_parse(struct pl *pl, char *str, size_t sz)
{
	struct pl ws = PL_INIT, val, ws2 = PL_INIT, cma = PL_INIT;
	int err;

	err = re_regex(pl->p, pl->l, "[ \t]*[^, \t]+[ \t]*[,]*",
		       &ws, &val, &ws2, &cma);
	if (err)
		return err;

	pl_advance(pl, ws.l + val.l + ws2.l + cma.l);

	(void)pl_strcpy(&val, str, sz);

	return 0;
}


static int audio_codecs_decode(struct account *acc, const struct pl *prm)
{
	struct list *aucodecl = baresip_aucodecl();
	struct pl tmp;

	if (!acc || !prm)
		return EINVAL;

	list_init(&acc->aucodecl);

	if (0 == msg_param_exists(prm, "audio_codecs", &tmp)) {
		struct pl acs;
		char cname[64];
		unsigned i = 0;

		if (msg_param_decode(prm, "audio_codecs", &acs))
			return 0;

		while (0 == csl_parse(&acs, cname, sizeof(cname))) {
			struct aucodec *ac;
			struct pl pl_cname, pl_srate, pl_ch = PL_INIT;
			uint32_t srate = 8000;
			uint8_t ch = 1;

			/* Format: "codec/srate/ch" */
			if (0 == re_regex(cname, str_len(cname),
					  "[^/]+/[0-9]+[/]*[0-9]*",
					  &pl_cname, &pl_srate,
					  NULL, &pl_ch)) {
				(void)pl_strcpy(&pl_cname, cname,
						sizeof(cname));
				srate = pl_u32(&pl_srate);
				if (pl_isset(&pl_ch))
					ch = pl_u32(&pl_ch);
			}

			ac = (struct aucodec *)aucodec_find(aucodecl,
							    cname, srate, ch);
			if (!ac) {
				warning("account: audio codec not found:"
					" %s/%u/%d\n",
					cname, srate, ch);
				continue;
			}

			/* NOTE: static list with references to aucodec */
			list_append(&acc->aucodecl, &acc->acv[i++], ac);

			if (i >= ARRAY_SIZE(acc->acv))
				break;
		}
	}

	return 0;
}


static int video_codecs_decode(struct account *acc, const struct pl *prm)
{
	struct list *vidcodecl = baresip_vidcodecl();
	struct pl tmp;

	if (!acc || !prm)
		return EINVAL;

	list_init(&acc->vidcodecl);

	if (0 == msg_param_exists(prm, "video_codecs", &tmp)) {
		struct pl vcs;
		char cname[64];
		unsigned i = 0;

		if (msg_param_decode(prm, "video_codecs", &vcs))
			return 0;

		while (0 == csl_parse(&vcs, cname, sizeof(cname))) {
			struct vidcodec *vc;

			vc = (struct vidcodec *)vidcodec_find(vidcodecl,
							      cname, NULL);
			if (!vc) {
				warning("account: video codec not found: %s\n",
					cname);
				continue;
			}

			/* NOTE: static list with references to vidcodec */
			list_append(&acc->vidcodecl, &acc->vcv[i++], vc);

			if (i >= ARRAY_SIZE(acc->vcv))
				break;
		}
	}

	return 0;
}


static int sip_params_decode(struct account *acc, const struct sip_addr *aor)
{
	struct pl auth_user, tmp;
	size_t i;
	int err = 0;

	if (!acc || !aor)
		return EINVAL;

	acc->regint = REG_INTERVAL + (rand_u32()&0xff);
	err |= param_u32(&acc->regint, &aor->params, "regint");

	acc->pubint = 0;
	err |= param_u32(&acc->pubint, &aor->params, "pubint");

	err |= param_dstr(&acc->regq, &aor->params, "regq");

	for (i=0; i<ARRAY_SIZE(acc->outboundv); i++) {

		char expr[16] = "outbound";

		expr[8] = (char)(i + 1 + 0x30);
		expr[9] = '\0';

		err |= param_dstr(&acc->outboundv[i], &aor->params, expr);
	}

	/* backwards compat */
	if (!acc->outboundv[0]) {
		err |= param_dstr(&acc->outboundv[0], &aor->params,
				  "outbound");
	}

	err |= param_dstr(&acc->sipnat, &aor->params, "sipnat");

	if (0 == msg_param_decode(&aor->params, "auth_user", &auth_user))
		err |= pl_strdup(&acc->auth_user, &auth_user);

	if (pl_isset(&aor->dname))
		err |= pl_strdup(&acc->dispname, &aor->dname);

	if (0 != msg_param_decode(&aor->params, "mwi", &tmp))
		acc->mwi = true;
	else
		acc->mwi = pl_strcasecmp(&tmp, "no") != 0;

	if (0 != msg_param_decode(&aor->params, "call_transfer", &tmp))
		acc->refer = true;
	else
		acc->refer = pl_strcasecmp(&tmp, "no") != 0;

	return err;
}


static int encode_uri_user(struct re_printf *pf, const struct uri *uri)
{
	struct uri uuri = *uri;

	uuri.password = uuri.params = uuri.headers = pl_null;

	return uri_encode(pf, &uuri);
}


/**
 * Create a SIP account from a sip address string
 *
 * @param accp     Pointer to allocated SIP account object
 * @param sipaddr  SIP address with parameters
 *
 * @return 0 if success, otherwise errorcode
 */
int account_alloc(struct account **accp, const char *sipaddr)
{
	struct account *acc;
	struct pl pl;
	int err = 0;

	if (!accp || !sipaddr)
		return EINVAL;

	acc = mem_zalloc(sizeof(*acc), destructor);
	if (!acc)
		return ENOMEM;

	err = str_dup(&acc->buf, sipaddr);
	if (err)
		goto out;

	pl_set_str(&pl, acc->buf);
	err = sip_addr_decode(&acc->laddr, &pl);
	if (err) {
		warning("account: error parsing SIP address: '%r'\n", &pl);
		goto out;
	}

	acc->luri = acc->laddr.uri;
	acc->luri.password = pl_null;

	err = re_sdprintf(&acc->aor, "%H", encode_uri_user, &acc->luri);
	if (err)
		goto out;

	/* Decode parameters */
	acc->ptime = 20;
	err |= sip_params_decode(acc, &acc->laddr);
	       answermode_decode(acc, &acc->laddr.params);
	err |= audio_codecs_decode(acc, &acc->laddr.params);
	err |= video_codecs_decode(acc, &acc->laddr.params);
	err |= media_decode(acc, &acc->laddr.params);
	if (err)
		goto out;

	err  = decode_pair(&acc->ausrc_mod, &acc->ausrc_dev,
			   &acc->laddr.params, "audio_source");
	err |= decode_pair(&acc->auplay_mod, &acc->auplay_dev,
			   &acc->laddr.params, "audio_player");
	if (err) {
		warning("account: audio_source/player parse error\n");
		goto out;
	}

	/* optional password prompt */
	if (0 == msg_param_decode(&acc->laddr.params, "auth_pass", &pl)) {
		err = pl_strdup(&acc->auth_pass, &pl);
		if (err)
			goto out;
	}

	err = stunsrv_decode(acc, &acc->laddr);
	if (err)
		goto out;

	if (acc->mnatid) {
		acc->mnat = mnat_find(baresip_mnatl(), acc->mnatid);
		if (!acc->mnat) {
			warning("account: medianat not found: '%s'\n",
				acc->mnatid);
		}
	}

	if (acc->mencid) {
		acc->menc = menc_find(baresip_mencl(), acc->mencid);
		if (!acc->menc) {
			warning("account: mediaenc not found: '%s'\n",
				acc->mencid);
		}
	}

	err |= extra_decode(acc, &acc->laddr.params);

 out:
	if (err)
		mem_deref(acc);
	else
		*accp = acc;

	return err;
}


/**
 * Set the authentication user for a SIP account
 *
 * @param acc   User-Agent account
 * @param user  Authentication username (NULL to reset)
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_auth_user(struct account *acc, const char *user)
{
	if (!acc)
		return EINVAL;

	acc->auth_user = mem_deref(acc->auth_user);

	if (user)
		return str_dup(&acc->auth_user, user);

	return 0;
}


/**
 * Set the authentication password for a SIP account
 *
 * @param acc   User-Agent account
 * @param pass  Authentication password (NULL to reset)
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_auth_pass(struct account *acc, const char *pass)
{
	if (!acc)
		return EINVAL;

	acc->auth_pass = mem_deref(acc->auth_pass);

	if (pass)
		return str_dup(&acc->auth_pass, pass);

	return 0;
}


/**
 * Set an outbound proxy for a SIP account
 *
 * @param acc  User-Agent account
 * @param ob   Outbound proxy
 * @param ix   Index of outbound proxy
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_outbound(struct account *acc, const char *ob, unsigned ix)
{
	if (!acc || ix >= ARRAY_SIZE(acc->outboundv))
		return EINVAL;

	acc->outboundv[ix] = mem_deref(acc->outboundv[ix]);

	if (ob)
		return str_dup(&(acc->outboundv[ix]), ob);

	return 0;
}


/**
 * Set the SIP nat protocol for a SIP account
 *
 * @param acc     User-Agent account
 * @param sipnat  SIP nat protocol
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_sipnat(struct account *acc, const char *sipnat)
{
	if (!acc)
		return EINVAL;

	acc->sipnat = mem_deref(acc->sipnat);

	if (sipnat)
		return str_dup(&acc->sipnat, sipnat);

	return 0;
}


/**
 * Set the SIP registration interval for a SIP account
 *
 * @param acc     User-Agent account
 * @param regint  Registration interval in [seconds]
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_regint(struct account *acc, uint32_t regint)
{
	if (!acc)
		return EINVAL;

	acc->regint = regint;

	return 0;
}


/**
 * Set the STUN server URI for a SIP account
 *
 * @param acc   User-Agent account
 * @param uri   STUN server URI (NULL to reset)
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_stun_uri(struct account *acc, const char *uri)
{
	struct pl pl;
	int err;

	if (!acc)
		return EINVAL;

	acc->stun_host = mem_deref(acc->stun_host);

	if (!uri)
		return 0;

	pl_set_str(&pl, uri);
	err = stunuri_decode(&acc->stun_host, &pl);
	if (err)
		warning("account: decode '%r' failed: %m\n",
			&pl, err);

	return err;
}


/**
 * Set the stun host for a SIP account
 *
 * @param acc   User-Agent account
 * @param host  Stun host (NULL to reset)
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_stun_host(struct account *acc, const char *host)
{
	if (!acc)
		return EINVAL;

	if (acc->stun_host)
		return stunuri_set_host(acc->stun_host, host);

	return 0;
}


/**
 * Set the port of the STUN host of a SIP account
 *
 * @param acc     User-Agent account
 * @param port    Port number
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_stun_port(struct account *acc, uint16_t port)
{
	if (!acc)
		return EINVAL;

	if (acc->stun_host)
		return stunuri_set_port(acc->stun_host, port);

	return 0;
}


/**
 * Set the STUN user for a SIP account
 *
 * @param acc   User-Agent account
 * @param user  STUN username (NULL to reset)
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_stun_user(struct account *acc, const char *user)
{
	if (!acc)
		return EINVAL;

	acc->stun_user = mem_deref(acc->stun_user);

	if (user)
		return str_dup(&acc->stun_user, user);

	return 0;
}


/**
 * Set the STUN password for a SIP account
 *
 * @param acc   User-Agent account
 * @param pass  STUN password (NULL to reset)
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_stun_pass(struct account *acc, const char *pass)
{
	if (!acc)
		return EINVAL;

	acc->stun_pass = mem_deref(acc->stun_pass);

	if (pass)
		return str_dup(&acc->stun_pass, pass);

	return 0;
}


/**
 * Set the media encryption for a SIP account
 *
 * @param acc     User-Agent account
 * @param mencid  Media encryption id
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_mediaenc(struct account *acc, const char *mencid)
{
	const struct menc *menc = NULL;
	if (!acc)
		return EINVAL;

	if (mencid) {
		menc = menc_find(baresip_mencl(), mencid);
		if (!menc) {
			warning("account: mediaenc not found: `%s'\n",
				mencid);
			return EINVAL;
		}
	}

	acc->mencid = mem_deref(acc->mencid);
	acc->menc = NULL;

	if (mencid) {
		acc->menc = menc;
		return str_dup(&acc->mencid, mencid);
	}

	return 0;
}


/**
 * Set the media NAT handling for a SIP account
 *
 * @param acc     User-Agent account
 * @param mnatid  Media NAT handling id
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_medianat(struct account *acc, const char *mnatid)
{
	const struct mnat *mnat = NULL;

	if (!acc)
		return EINVAL;

	if (mnatid) {
		mnat = mnat_find(baresip_mnatl(), mnatid);
		if (!mnat) {
			warning("account: medianat not found: `%s'\n",
				mnatid);
			return EINVAL;
		}
	}

	acc->mnatid = mem_deref(acc->mnatid);
	acc->mnat = NULL;

	if (mnatid) {
		acc->mnat = mnat;
		return str_dup(&acc->mnatid, mnatid);
	}

	return 0;
}


/**
 * Sets audio codecs
 *
 * @param acc      User-Agent account
 * @param codecs   Comma separated list of audio codecs (NULL to disable)
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_audio_codecs(struct account *acc, const char *codecs)
{
	char buf[256];
	struct pl pl;

	if (!acc)
		return EINVAL;

	list_clear(&acc->aucodecl);

	if (codecs) {
		re_snprintf(buf, sizeof(buf), ";audio_codecs=%s", codecs);
		pl_set_str(&pl, buf);
		return audio_codecs_decode(acc, &pl);
	}

	return 0;
}


/**
 * Sets video codecs
 *
 * @param acc      User-Agent account
 * @param codecs   Comma separated list of video codecs (NULL to disable)
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_video_codecs(struct account *acc, const char *codecs)
{
	char buf[256];
	struct pl pl;

	if (!acc)
		return EINVAL;

	list_clear(&acc->vidcodecl);

	if (codecs) {
		re_snprintf(buf, sizeof(buf), ";video_codecs=%s", codecs);
		pl_set_str(&pl, buf);
		return video_codecs_decode(acc, &pl);
	}

	return 0;
}


/**
 * Sets the displayed name. Pass null in dname to disable display name
 *
 * @param acc      User-Agent account
 * @param dname    Display name (NULL to disable)
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_display_name(struct account *acc, const char *dname)
{
	if (!acc)
		return EINVAL;

	acc->dispname = mem_deref(acc->dispname);

	if (dname)
		return str_dup(&acc->dispname, dname);

	return 0;
}


/**
 * Sets MWI on (value "yes") or off (value "no")
 *
 * @param acc      User-Agent account
 * @param value    "yes" or "no"
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_mwi(struct account *acc, const char *value)
{
	if (!acc)
		return EINVAL;

	if (0 == str_casecmp(value, "yes"))
		acc->mwi = true;
	else
		if (0 == str_casecmp(value, "no"))
			acc->mwi = false;
		else {
			warning("account: unknown mwi value: %r\n",
				value);
			return EINVAL;
		}

	return 0;
}


/**
 * Sets call transfer on (value "yes") or off (value "no")
 *
 * @param acc      User-Agent account
 * @param value    "yes" or "no"
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_call_transfer(struct account *acc, const char *value)
{
	if (!acc)
		return EINVAL;

	if (0 == str_casecmp(value, "yes"))
		acc->refer = true;
	else
		if (0 == str_casecmp(value, "no"))
			acc->refer = false;
		else {
			warning("account: unknown call transfer: %r\n",
				value);
			return EINVAL;
		}

	return 0;
}


/**
 * Authenticate a User-Agent (UA)
 *
 * @param acc      User-Agent account
 * @param username Pointer to allocated username string
 * @param password Pointer to allocated password string
 * @param realm    Realm string
 *
 * @return 0 if success, otherwise errorcode
 */
int account_auth(const struct account *acc, char **username, char **password,
		 const char *realm)
{
	int err = 0;

	if (!acc)
		return EINVAL;

	(void)realm;

	if (acc->auth_user)
		*username = mem_ref(acc->auth_user);
	else
		err = pl_strdup(username, &(acc->luri.user));

	*password = mem_ref(acc->auth_pass);

	return err;
}


/**
 * Get the audio codecs of an account
 *
 * @param acc User-Agent account
 *
 * @return List of audio codecs (struct aucodec)
 */
struct list *account_aucodecl(const struct account *acc)
{
	return (acc && !list_isempty(&acc->aucodecl))
		? (struct list *)&acc->aucodecl : baresip_aucodecl();
}


/**
 * Get the video codecs of an account
 *
 * @param acc User-Agent account
 *
 * @return List of video codecs (struct vidcodec)
 */
struct list *account_vidcodecl(const struct account *acc)
{
	return (acc && !list_isempty(&acc->vidcodecl))
		? (struct list *)&acc->vidcodecl : baresip_vidcodecl();
}


/**
 * Get the SIP address of an account
 *
 * @param acc User-Agent account
 *
 * @return SIP address
 */
struct sip_addr *account_laddr(const struct account *acc)
{
	return acc ? (struct sip_addr *)&acc->laddr : NULL;
}


/**
 * Get the decoded AOR URI of an account
 *
 * @param acc User-Agent account
 *
 * @return Decoded URI
 */
struct uri *account_luri(const struct account *acc)
{
	return acc ? (struct uri *)&acc->luri : NULL;
}


/**
 * Get the Registration interval of an account
 *
 * @param acc User-Agent account
 *
 * @return Registration interval in [seconds]
 */
uint32_t account_regint(const struct account *acc)
{
	return acc ? acc->regint : 0;
}


/**
 * Get the Publication interval of an account
 *
 * @param acc User-Agent account
 *
 * @return Publication interval in [seconds]
 */
uint32_t account_pubint(const struct account *acc)
{
	return acc ? acc->pubint : 0;
}


/**
 * Get the answermode of an account
 *
 * @param acc User-Agent account
 *
 * @return Answermode
 */
enum answermode account_answermode(const struct account *acc)
{
	return acc ? acc->answermode : ANSWERMODE_MANUAL;
}


/**
 * Set the answermode of an account
 *
 * @param acc  User-Agent account
 * @param mode Answermode
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_answermode(struct account *acc, enum answermode mode)
{
	if (!acc)
		return EINVAL;

	if ((mode != ANSWERMODE_MANUAL) && (mode != ANSWERMODE_EARLY) &&
	    (mode != ANSWERMODE_AUTO)) {
		warning("account: invalid answermode : `%d'\n", mode);
		return EINVAL;
	}

	acc->answermode = mode;

	return 0;
}


/**
 * Get the SIP Display Name of an account
 *
 * @param acc User-Agent account
 *
 * @return SIP Display Name
 */
const char *account_display_name(const struct account *acc)
{
	return acc ? acc->dispname : NULL;
}


/**
 * Get the SIP Address-of-Record (AOR) of an account
 *
 * @param acc User-Agent account
 *
 * @return SIP Address-of-Record (AOR)
 */
const char *account_aor(const struct account *acc)
{
	return acc ? acc->aor : NULL;
}


/**
 * Get the authentication username of an account
 *
 * @param acc User-Agent account
 *
 * @return Authentication username
 */
const char *account_auth_user(const struct account *acc)
{
	return acc ? acc->auth_user : NULL;
}


/**
 * Get the SIP authentication password of an account
 *
 * @param acc User-Agent account
 *
 * @return Authentication password
 */
const char *account_auth_pass(const struct account *acc)
{
	return acc ? acc->auth_pass : NULL;
}


/**
 * Get the outbound SIP server of an account
 *
 * @param acc User-Agent account
 * @param ix  Index starting at zero
 *
 * @return Outbound SIP proxy, NULL if not configured
 */
const char *account_outbound(const struct account *acc, unsigned ix)
{
	if (!acc || ix >= ARRAY_SIZE(acc->outboundv))
		return NULL;

	return acc->outboundv[ix];
}


/**
 * Get sipnat protocol of an account
 *
 * @param acc User-Agent account
 *
 * @return sipnat protocol or NULL if not set
 */
const char *account_sipnat(const struct account *acc)
{
	return acc ? acc->sipnat : NULL;
}


/**
 * Get the audio packet-time (ptime) of an account
 *
 * @param acc User-Agent account
 *
 * @return Packet-time (ptime)
 */
uint32_t account_ptime(const struct account *acc)
{
	return acc ? acc->ptime : 0;
}


/**
 * Get the STUN username of an account
 *
 * @param acc User-Agent account
 *
 * @return STUN username
 */
const char *account_stun_user(const struct account *acc)
{
	return acc ? acc->stun_user : NULL;
}


/**
 * Get the STUN password of an account
 *
 * @param acc User-Agent account
 *
 * @return STUN password
 */
const char *account_stun_pass(const struct account *acc)
{
	return acc ? acc->stun_pass : NULL;
}


/**
 * Get the STUN server URI of an account
 *
 * @param acc User-Agent account
 *
 * @return STUN server URI
 */
const struct stun_uri *account_stun_uri(const struct account *acc)
{
	if (!acc)
		return NULL;

	return acc->stun_host ? acc->stun_host : NULL;
}


/**
 * Get the STUN hostname of an account
 *
 * @param acc User-Agent account
 *
 * @return STUN hostname
 */
const char *account_stun_host(const struct account *acc)
{
	if (!acc)
		return NULL;

	return acc->stun_host ? acc->stun_host->host : NULL;
}


/**
 * Get the port of the STUN host of an account
 *
 * @param acc User-Agent account
 *
 * @return Port number or 0 if not set
 */
uint16_t account_stun_port(const struct account *acc)
{
	if (!acc)
		return 0;

	return acc->stun_host ? acc->stun_host->port : 0;
}


static const char *answermode_str(enum answermode mode)
{
	switch (mode) {

	case ANSWERMODE_MANUAL: return "manual";
	case ANSWERMODE_EARLY:  return "early";
	case ANSWERMODE_AUTO:   return "auto";
	default: return "???";
	}
}


/**
 * Get the media encryption of an account
 *
 * @param acc User-Agent account
 *
 * @return Media encryption id or NULL if not set
 */
const char *account_mediaenc(const struct account *acc)
{
	return acc ? acc->mencid : NULL;
}


/**
 * Get the media NAT handling of an account
 *
 * @param acc User-Agent account
 *
 * @return Media NAT handling id or NULL if not set
 */
const char *account_medianat(const struct account *acc)
{
	return acc ? acc->mnatid : NULL;
}


/**
 * Get MWI capability of an account
 *
 * @param acc User-Agent account
 *
 * @return "yes" or "no"
 */
const char *account_mwi(const struct account *acc)
{
	if (!acc)
		return "no";

	return acc->mwi ? "yes" : "no";
}


/**
 * Get call transfer capability of an account
 *
 * @param acc User-Agent account
 *
 * @return "yes" or "no"
 */
const char *account_call_transfer(const struct account *acc)
{
	if (!acc)
		return "no";

	return acc->refer ? "yes" : "no";
}


/**
 * Get extra parameter value of an account
 *
 * @param acc User-Agent account
 *
 * @return extra parameter value
 */
const char *account_extra(const struct account *acc)
{
	return acc ? acc->extra : NULL;
}


/**
 * Print the account debug information
 *
 * @param pf  Print function
 * @param acc User-Agent account
 *
 * @return 0 if success, otherwise errorcode
 */
int account_debug(struct re_printf *pf, const struct account *acc)
{
	struct le *le;
	size_t i;
	int err = 0;

	if (!acc)
		return 0;

	err |= re_hprintf(pf, "\nAccount:\n");

	err |= re_hprintf(pf, " address:      %s\n", acc->buf);
	err |= re_hprintf(pf, " luri:         %H\n",
			  uri_encode, &acc->luri);
	err |= re_hprintf(pf, " aor:          %s\n", acc->aor);
	err |= re_hprintf(pf, " dispname:     %s\n", acc->dispname);
	err |= re_hprintf(pf, " answermode:   %s\n",
			  answermode_str(acc->answermode));
	if (!list_isempty(&acc->aucodecl)) {
		err |= re_hprintf(pf, " audio_codecs:");
		for (le = list_head(&acc->aucodecl); le; le = le->next) {
			const struct aucodec *ac = le->data;
			err |= re_hprintf(pf, " %s/%u/%u",
					  ac->name, ac->srate, ac->ch);
		}
		err |= re_hprintf(pf, "\n");
	}
	err |= re_hprintf(pf, " auth_user:    %s\n", acc->auth_user);
	err |= re_hprintf(pf, " mediaenc:     %s\n",
			  acc->mencid ? acc->mencid : "none");
	err |= re_hprintf(pf, " medianat:     %s\n",
			  acc->mnatid ? acc->mnatid : "none");
	for (i=0; i<ARRAY_SIZE(acc->outboundv); i++) {
		if (acc->outboundv[i]) {
			err |= re_hprintf(pf, " outbound%d:    %s\n",
					  i+1, acc->outboundv[i]);
		}
	}
	err |= re_hprintf(pf, " mwi:          %s\n", account_mwi(acc));
	err |= re_hprintf(pf, " ptime:        %u\n", acc->ptime);
	err |= re_hprintf(pf, " regint:       %u\n", acc->regint);
	err |= re_hprintf(pf, " pubint:       %u\n", acc->pubint);
	err |= re_hprintf(pf, " regq:         %s\n", acc->regq);
	err |= re_hprintf(pf, " sipnat:       %s\n", acc->sipnat);
	err |= re_hprintf(pf, " stunuser:     %s\n", acc->stun_user);
	err |= re_hprintf(pf, " stunserver:   %H\n",
			  stunuri_print, acc->stun_host);

	if (!list_isempty(&acc->vidcodecl)) {
		err |= re_hprintf(pf, " video_codecs:");
		for (le = list_head(&acc->vidcodecl); le; le = le->next) {
			const struct vidcodec *vc = le->data;
			err |= re_hprintf(pf, " %s", vc->name);
		}
		err |= re_hprintf(pf, "\n");
	}
	err |= re_hprintf(pf, " call_transfer:%s\n",
			  account_call_transfer(acc));
	err |= re_hprintf(pf, " extra:        %s\n",
			  acc->extra ? acc->extra : "none");

	return err;
}


/**
 * Print the account information in JSON
 *
 * @param od  Account dict
 * @param odcfg  Configuration dict
 * @param acc User-Agent account
 *
 * @return 0 if success, otherwise errorcode
 */
int account_json_api(struct odict *od, struct odict *odcfg,
		const struct account *acc)
{
	int err = 0;
	struct odict *obn = NULL;
	const char *stunhost = "";

	if (!acc)
		return 0;

	/* account */
	err |= odict_entry_add(od, "aor", ODICT_STRING, acc->aor);
	if (acc->dispname) {
		err |= odict_entry_add(od, "display_name", ODICT_STRING,
				acc->dispname);
	}

	/* config */
	if (acc->sipnat) {
		err |= odict_entry_add(odcfg, "sip_nat", ODICT_STRING,
				acc->sipnat);
	}

	err |= odict_alloc(&obn, 8);
	for (size_t i=0; i<ARRAY_SIZE(acc->outboundv); i++) {
		if (acc->outboundv[i]) {
			err |= odict_entry_add(obn, "outbound", ODICT_STRING,
					acc->outboundv[i]);
		}
	}
	err |= odict_entry_add(odcfg, "sip_nat_outbound", ODICT_ARRAY, obn);

	stunhost = account_stun_host(acc) ? account_stun_host(acc) : "";
	err |= odict_entry_add(odcfg, "stun_host", ODICT_STRING, stunhost);
	err |= odict_entry_add(odcfg, "stun_port", ODICT_INT,
			account_stun_port(acc));
	if (acc->stun_user) {
		err |= odict_entry_add(odcfg, "stun_user", ODICT_STRING,
				acc->stun_user);
	}

	err |= odict_entry_add(odcfg, "answer_mode", ODICT_STRING,
			answermode_str(acc->answermode));
	err |= odict_entry_add(odcfg, "call_transfer", ODICT_BOOL, acc->refer);

	err |= odict_entry_add(odcfg, "packet_time", ODICT_INT,
			account_ptime(acc));

	mem_deref(obn);
	return err;
}
