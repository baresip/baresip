/**
 * @file src/account.c  User-Agent account
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"
#include <ctype.h>


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
	for (i=0; i<RE_ARRAY_SIZE(acc->outboundv); i++)
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
	mem_deref(acc->vidsrc_mod);
	mem_deref(acc->vidsrc_dev);
	mem_deref(acc->viddisp_mod);
	mem_deref(acc->viddisp_dev);
	mem_deref(acc->cert);
	mem_deref(acc->extra);
	mem_deref(acc->uas_user);
	mem_deref(acc->uas_pass);
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


static int param_bool(bool *v, const struct pl *params, const char *name)
{
	struct pl pl;

	if (msg_param_decode(params, name, &pl))
		return 0;

	return pl_bool(v, &pl);
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
	struct uri uri;
	int err;

	if (!acc || !aor)
		return EINVAL;

	memset(&uri, 0, sizeof(uri));
	if (0 == msg_param_decode(&aor->params, "stunserver", &srv)) {

		info("using stunserver: '%r'\n", &srv);

		err = uri_decode(&uri, &srv);
		if (err) {
			warning("account: decode '%r' failed (%m)\n",
				&srv, err);
			return err;
		}

		err = stunuri_decode_uri(&acc->stun_host, &uri);
		if (err) {
			return err;
		}
	}

	err = 0;

	if (0 == msg_param_exists(&aor->params, "stunuser", &tmp))
		err |= param_dstr(&acc->stun_user, &aor->params, "stunuser");
	else if (pl_isset(&uri.user))
		err |= re_sdprintf(&acc->stun_user, "%H",
					uri_user_unescape, &uri.user);

	if (0 == msg_param_exists(&aor->params, "stunpass", &tmp))
		err |= param_dstr(&acc->stun_pass, &aor->params, "stunpass");

	return err;
}


/* Decode media parameters */
static int media_decode(struct account *acc, const struct pl *prm)
{
	int err = 0;

	if (!acc || !prm)
		return EINVAL;

	err |= param_dstr(&acc->mencid,   prm, "mediaenc");
	err |= param_dstr(&acc->mnatid,   prm, "medianat");
	err |= param_u32(&acc->ptime,     prm, "ptime");
	err |= param_bool(&acc->rtcp_mux, prm, "rtcp_mux");
	err |= param_bool(&acc->pinhole, prm,  "natpinhole");

	return err;
}


/* Decode cert parameter */
static int cert_decode(struct account *acc, const struct pl *prm)
{
	int err = 0;

	if (!acc || !prm)
		return EINVAL;

	err = param_dstr(&acc->cert, prm, "cert");
	if (err)
		return err;

	if (!str_isset(acc->cert))
		return 0;

	if (!fs_isfile(acc->cert)) {
		warning("account: certificate %s not found\n", acc->cert);
		err = errno;
	}

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
	struct pl amode, adelay;

	prm->answermode = ANSWERMODE_MANUAL;

	if (0 == msg_param_decode(pl, "answermode", &amode)) {

		if (0 == pl_strcasecmp(&amode, "manual")) {
			prm->answermode = ANSWERMODE_MANUAL;
		}
		else if (0 == pl_strcasecmp(&amode, "early")) {
			prm->answermode = ANSWERMODE_EARLY;
		}
		else if (0 == pl_strcasecmp(&amode, "early-video")) {
			prm->answermode = ANSWERMODE_EARLY_VIDEO;
		}
		else if (0 == pl_strcasecmp(&amode, "early-audio")) {
			prm->answermode = ANSWERMODE_EARLY_AUDIO;
		}
		else if (0 == pl_strcasecmp(&amode, "auto")) {
			prm->answermode = ANSWERMODE_AUTO;
		}
		else {
			warning("account: answermode unknown (%r)\n", &amode);
		}
	}

	if (0 == msg_param_decode(pl, "answerdelay", &adelay))
		prm->adelay = pl_u32(&adelay);
}


static void rel100_decode(struct account *prm, const struct pl *pl)
{
	struct pl rmode;

	prm->rel100_mode = REL100_DISABLED;

	if (0 == msg_param_decode(pl, "100rel", &rmode)) {

		if (0 == pl_strcasecmp(&rmode, "no")) {
			prm->rel100_mode = REL100_DISABLED;
		}
		else if (0 == pl_strcasecmp(&rmode, "yes")) {
			prm->rel100_mode = REL100_ENABLED;
		}
		else if (0 == pl_strcasecmp(&rmode, "required")) {
			prm->rel100_mode = REL100_REQUIRED;
		}
		else {
			warning("account: 100rel mode unknown (%r)\n", &rmode);
		}
	}
}


static void autoanswer_decode(struct account *prm, const struct pl *pl)
{
	struct pl v;

	if (0 == msg_param_decode(pl, "sip_autoanswer", &v)) {
		if (0 == pl_strcasecmp(&v, "yes")) {
			prm->sipans = true;
		}
	}

	if (0 == msg_param_decode(pl, "sip_autoanswer_beep", &v)) {
		if (0 == pl_strcasecmp(&v, "on")) {
			prm->sipansbeep = SIPANSBEEP_ON;
		}
		else if (0 == pl_strcasecmp(&v, "off")) {
			prm->sipansbeep = SIPANSBEEP_OFF;
		}
		else if (0 == pl_strcasecmp(&v, "local")) {
			prm->sipansbeep = SIPANSBEEP_LOCAL;
		}
	}
}


/* Decode dtmfmode parameter */
static void dtmfmode_decode(struct account *prm, const struct pl *pl)
{
	struct pl dtmfmode;

	if (0 == msg_param_decode(pl, "dtmfmode", &dtmfmode)) {

		if (0 == pl_strcasecmp(&dtmfmode, "info")) {
			prm->dtmfmode = DTMFMODE_SIP_INFO;
		}
		else if (0 == pl_strcasecmp(&dtmfmode, "auto")) {
			prm->dtmfmode = DTMFMODE_AUTO;
		}
		else {
			prm->dtmfmode = DTMFMODE_RTP_EVENT;
		}
	}
}


static void inreq_mode_decode(struct account *prm, const struct pl *pl)
{
	struct pl mode;

	prm->inreq_mode = INREQ_MODE_ON;

	if (0 == msg_param_decode(pl, "inreq_allowed", &mode)) {

		if (0 == pl_strcasecmp(&mode, "no")) {
			prm->inreq_mode = INREQ_MODE_OFF;
		}
		else if (0 == pl_strcasecmp(&mode, "yes")) {
			prm->inreq_mode = INREQ_MODE_ON;
		}
		else {
			warning("account: inreq_allowed mode unknown (%r)\n",
				&mode);
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

			if (i >= RE_ARRAY_SIZE(acc->acv))
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

		acc->videoen = false;
		if (msg_param_decode(prm, "video_codecs", &vcs))
			return 0;

		while (0 == csl_parse(&vcs, cname, sizeof(cname))) {
			struct le *le;

			for (le=list_head(vidcodecl); le; le=le->next) {
				struct vidcodec *vc = le->data;

				if (0 != str_casecmp(cname, vc->name))
					continue;

				/* static list with references to vidcodec */
				list_append(&acc->vidcodecl, &acc->vcv[i++],
						vc);

				acc->videoen = true;
				if (i >= RE_ARRAY_SIZE(acc->vcv))
					return 0;
			}
		}
	}

	return 0;
}


static void uasauth_decode(struct account *acc, const struct pl *prm)
{
	struct pl val;

	if (!acc || !prm)
		return;

	if (msg_param_decode(prm, "uas_user", &val))
		return;

	(void)pl_strdup(&acc->uas_user, &val);

	if (msg_param_decode(prm, "uas_pass", &val))
		return;

	(void)pl_strdup(&acc->uas_pass, &val);
}


static int sip_params_decode(struct account *acc, const struct sip_addr *aor)
{
	struct pl auth_user, tmp;
	size_t i;
	uint32_t u32;
	int err = 0;
	char *value;

	if (!acc || !aor)
		return EINVAL;

	acc->regint = REG_INTERVAL + (rand_u32()&0xff);
	err |= param_u32(&acc->regint, &aor->params, "regint");
	err |= param_u32(&acc->prio, &aor->params, "prio");
	err |= param_u32(&acc->rwait, &aor->params, "rwait");
	if (acc->rwait > 95)
		acc->rwait = 95;

	if (acc->rwait && acc->rwait < 5)
		acc->rwait = 5;

	err |= param_u32(&acc->fbregint, &aor->params, "fbregint");
	acc->pubint = 0;
	err |= param_u32(&acc->pubint, &aor->params, "pubint");
	u32  = 0;
	err |= param_u32(&u32, &aor->params, "tcpsrcport");
	if (u32) {
		if (u32 <= 65535)
			acc->tcpsrcport = u32;
		else
			warning("account: invalid tcpsrcport\n");
	}

	err |= param_dstr(&acc->regq, &aor->params, "regq");

	for (i=0; i<RE_ARRAY_SIZE(acc->outboundv); i++) {

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

	value = NULL;
	err |= param_dstr(&value, &aor->params, "sipnat");
	(void)account_set_sipnat(acc, value);
	mem_deref(value);

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

	if (0 != msg_param_decode(&aor->params, "sip_autoredirect", &tmp))
		acc->autoredirect = false;
	else
		acc->autoredirect = pl_strcasecmp(&tmp, "yes") == 0;

	return err;
}


static int encode_uri_user(struct re_printf *pf, const struct uri *uri)
{
	struct uri uuri = *uri;

	uuri.params = uuri.headers = pl_null;

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

	acc->sipansbeep = SIPANSBEEP_ON;
	acc->videoen = true;
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

	err = re_sdprintf(&acc->aor, "%H", encode_uri_user, &acc->luri);
	if (err)
		goto out;

	/* Decode parameters */
	acc->ptime = 20;
	err |= sip_params_decode(acc, &acc->laddr);
	       rel100_decode(acc, &acc->laddr.params);
	       answermode_decode(acc, &acc->laddr.params);
	       autoanswer_decode(acc, &acc->laddr.params);
	       dtmfmode_decode(acc, &acc->laddr.params);
	       uasauth_decode(acc, &acc->laddr.params);
	       inreq_mode_decode(acc, &acc->laddr.params);
	err |= audio_codecs_decode(acc, &acc->laddr.params);
	err |= video_codecs_decode(acc, &acc->laddr.params);
	err |= media_decode(acc, &acc->laddr.params);
	err |= param_bool(&acc->catchall, &acc->laddr.params, "catchall");
	if (err)
		goto out;

	param_u32(&acc->autelev_pt, &acc->laddr.params, "autelev_pt");
	err  = decode_pair(&acc->ausrc_mod, &acc->ausrc_dev,
			   &acc->laddr.params, "audio_source");
	err |= decode_pair(&acc->auplay_mod, &acc->auplay_dev,
			   &acc->laddr.params, "audio_player");
	if (err) {
		warning("account: audio_source/audio_player parse error\n");
		goto out;
	}

	err  = decode_pair(&acc->vidsrc_mod, &acc->vidsrc_dev,
			   &acc->laddr.params, "video_source");
	err |= decode_pair(&acc->viddisp_mod, &acc->viddisp_dev,
			   &acc->laddr.params, "video_display");
	if (err) {
		warning("account: video_source/video_display parse error\n");
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

	err |= cert_decode(acc, &acc->laddr.params);
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
	if (!acc || ix >= RE_ARRAY_SIZE(acc->outboundv))
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

	if (sipnat)
		if (0 == str_casecmp(sipnat, "outbound")) {
			acc->sipnat = mem_deref(acc->sipnat);
			return str_dup(&acc->sipnat, sipnat);
		}
		else {
			warning("account: unknown sipnat value: '%s'\n",
				sipnat);
			return EINVAL;
		}
	else
		acc->sipnat = mem_deref(acc->sipnat);

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
 * Set the Audio Source Device for a SIP account
 *
 * @param acc  User-Agent account
 * @param dev  Device string
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_ausrc_dev(struct account *acc, const char *dev)
{
	if (!acc)
		return EINVAL;

	acc->ausrc_dev = mem_deref(acc->ausrc_dev);

	if (dev)
		return str_dup(&acc->ausrc_dev, dev);

	return 0;
}


/**
 * Set the Audio Playout Device for a SIP account
 *
 * @param acc  User-Agent account
 * @param dev  Device string
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_auplay_dev(struct account *acc, const char *dev)
{
	if (!acc)
		return EINVAL;

	acc->auplay_dev = mem_deref(acc->auplay_dev);

	if (dev)
		return str_dup(&acc->auplay_dev, dev);

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
 * Sets MWI on (value true) or off (value false)
 *
 * @param acc      User-Agent account
 * @param value    true or false
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_mwi(struct account *acc, bool value)
{
	if (!acc)
		return EINVAL;

	acc->mwi = value;

	return 0;
}


/**
 * Sets call transfer on (value true) or off (value false)
 *
 * @param acc      User-Agent account
 * @param value    true or false
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_call_transfer(struct account *acc, bool value)
{
	if (!acc)
		return EINVAL;

	acc->refer = value;

	return 0;
}


/**
 * Sets rtcp_mux on (value true) or off (value false)
 *
 * @param acc      User-Agent account
 * @param value    true or false
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_rtcp_mux(struct account *acc, bool value)
{
	if (!acc)
		return EINVAL;

	acc->rtcp_mux = value;

	return 0;
}


/**
 * Sets catchall flag for the User-Agent account. A catch all account catches
 * all inbound SIP requests
 *
 * @param acc      User-Agent account
 * @param value    true or false
 */
void account_set_catchall(struct account *acc, bool value)
{
	if (!acc)
		return;

	acc->catchall = value;
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
 * @return List of video codecs (struct vidcodec), NULL if video is disabled
 */
struct list *account_vidcodecl(const struct account *acc)
{
	if (acc && !acc->videoen)
		return NULL;

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
 * Get the registration interval of an account
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
 * Get the fallback registration interval of an account
 *
 * @param acc User-Agent account
 *
 * @return Registration interval in [seconds]
 */
uint32_t account_fbregint(const struct account *acc)
{
	return acc ? acc->fbregint : 0;
}


/**
 * Get the priority of an account. Priority 0 is default.
 *
 * @param acc User-Agent account
 *
 * @return Priority
 */
uint32_t account_prio(const struct account *acc)
{
	return acc ? acc->prio : 0;
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
	    (mode != ANSWERMODE_AUTO) && (mode != ANSWERMODE_EARLY_VIDEO) &&
	    (mode != ANSWERMODE_EARLY_AUDIO)) {
		warning("account: invalid answermode : `%d'\n", mode);
		return EINVAL;
	}

	acc->answermode = mode;

	return 0;
}


/**
 * Get the 100rel mode of an account
 *
 * @param acc User-Agent account
 *
 * @return 100rel mode
 */
enum rel100_mode account_rel100_mode(const struct account *acc)
{
	return acc ? acc->rel100_mode : REL100_ENABLED;
}


/**
 * Set the 100rel mode of an account
 *
 * @param acc  User-Agent account
 * @param mode 100rel mode
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_rel100_mode(struct account *acc, enum rel100_mode mode)
{
	if (!acc)
		return EINVAL;

	if ((mode != REL100_DISABLED) && (mode != REL100_ENABLED) &&
	    (mode != REL100_REQUIRED)) {
		warning("account: invalid 100rel mode : `%d'\n", mode);
		return EINVAL;
	}

	acc->rel100_mode = mode;

	return 0;
}


/**
 * Get the dtmfmode of an account
 *
 * @param acc User-Agent account
 *
 * @return Dtmfmode
 */
enum dtmfmode account_dtmfmode(const struct account *acc)
{
	return acc ? acc->dtmfmode : DTMFMODE_RTP_EVENT;
}


/**
 * Set the dtmfmode of an account
 *
 * @param acc  User-Agent account
 * @param mode Dtmfmode
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_dtmfmode(struct account *acc, enum dtmfmode mode)
{
	if (!acc)
		return EINVAL;

	if ((mode != DTMFMODE_RTP_EVENT) &&
	    (mode != DTMFMODE_SIP_INFO) &&
	    (mode != DTMFMODE_AUTO)) {
		warning("account: invalid dtmfmode : `%d'\n", mode);
		return EINVAL;
	}

	acc->dtmfmode = mode;

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
	if (!acc || ix >= RE_ARRAY_SIZE(acc->outboundv))
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


int32_t account_answerdelay(const struct account *acc)
{
	return acc ? acc->adelay : 0;
}


void account_set_answerdelay(struct account *acc, int32_t adelay)
{
	if (!acc)
		return;

	acc->adelay = adelay;
}


/**
 * Returns if SIP auto answer is allowed for the account
 *
 * @param acc User-Agent account
 * @return true if allowed, otherwise false
 */
bool account_sip_autoanswer(const struct account *acc)
{
	return acc ? acc->sipans : false;
}


void account_set_sip_autoanswer(struct account *acc, bool allow)
{
	if (!acc)
		return;

	acc->sipans = allow;
}


/**
 * Returns if SIP autoredirect on 3xx response is allowed for the account
 *
 * @param acc User-Agent account
 * @return true if allowed, otherwise false
 */
bool account_sip_autoredirect(const struct account *acc)
{
	return acc ? acc->autoredirect : false;
}


void account_set_sip_autoredirect(struct account *acc, bool allow)
{
	if (!acc)
		return;

	acc->autoredirect = allow;
}


/**
 * Returns the beep mode for a SIP auto answer call
 *
 * - SIPANSBEEP_ON    ... The beep is played before the call is answered
 *                         automatically. The locally configured audio file can
 *                         be overwritten with the Alert-Info header URL.
 *                         This is the default value.
 *
 * - SIPANSBEEP_OFF   ... No beep is played.
 *
 * - SIPANSBEEP_LOCAL ... The local configured beep tone is played.
 *
 * @param acc User-Agent account
 * @return Beep mode
 */
enum sipansbeep account_sipansbeep(const struct account *acc)
{
	return acc ? acc->sipansbeep : SIPANSBEEP_ON;
}


void account_set_sipansbeep(struct account *acc, enum sipansbeep beep)
{
	if (!acc)
		return;

	acc->sipansbeep = beep;
}


/**
 * Sets the audio payload type for telephone-events
 *
 * @param acc User-Agent account
 * @param pt  Payload type
 */
void account_set_autelev_pt(struct account *acc, uint32_t pt)
{
	if (!acc)
		return;

	acc->autelev_pt = pt;
}


/**
 * Returns the audio payload type for telephone-events
 *
 * @param acc User-Agent account
 *
 * @return Telephone-event payload type
 */
uint32_t account_autelev_pt(struct account *acc)
{
	return acc ? acc->autelev_pt : 0;
}


static const char *answermode_str(enum answermode mode)
{
	switch (mode) {

	case ANSWERMODE_MANUAL:       return "manual";
	case ANSWERMODE_EARLY:        return "early";
	case ANSWERMODE_AUTO:         return "auto";
	case ANSWERMODE_EARLY_AUDIO:  return "early-audio";
	case ANSWERMODE_EARLY_VIDEO:  return "early-video";
	default: return "???";
	}
}


static const char *rel100_mode_str(enum rel100_mode mode)
{
	switch (mode) {

	case REL100_ENABLED:    return "yes";
	case REL100_DISABLED:   return "no";
	case REL100_REQUIRED:   return "required";
	default: return "???";
	}
}


static const char *dtmfmode_str(enum dtmfmode mode)
{
	switch (mode) {

	case DTMFMODE_RTP_EVENT: return "rtpevent";
	case DTMFMODE_SIP_INFO:  return "info";
	case DTMFMODE_AUTO: 	 return "auto";
	default: return "???";
	}
}


static const char *sipansbeep_str(enum sipansbeep beep)
{
	switch (beep) {

	case SIPANSBEEP_OFF:   return "off";
	case SIPANSBEEP_ON:    return "on";
	case SIPANSBEEP_LOCAL: return "local";
	default: return "???";
	}
}


static const char *inreq_mode_str(enum inreq_mode mode)
{
	switch (mode) {

	case INREQ_MODE_OFF:      return "no";
	case INREQ_MODE_ON:       return "yes";
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
 * @return true or false
 */
bool account_mwi(const struct account *acc)
{
	if (!acc)
		return false;

	return acc->mwi;
}


/**
 * Get call transfer capability of an account
 *
 * @param acc User-Agent account
 *
 * @return true or false
 */
bool account_call_transfer(const struct account *acc)
{
	if (!acc)
		return false;

	return acc->refer;
}


/**
 * Get rtcp_mux capability of an account
 *
 * @param acc User-Agent account
 *
 * @return true or false
 */
bool account_rtcp_mux(const struct account *acc)
{
	if (!acc)
		return false;

	return acc->rtcp_mux;
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
 * Auto complete a SIP uri, add scheme and domain if missing
 *
 * @param acc User-Agent account
 * @param buf Target buffer to print SIP uri
 * @param uri Input SIP uri
 *
 * @return 0 if success, otherwise errorcode
 */
int account_uri_complete(const struct account *acc, struct mbuf *buf,
			 const char *uri)
{
	char *str;
	struct pl pl;
	int err;

	pl_set_str(&pl, uri);
	err = account_uri_complete_strdup(acc, &str, &pl);
	if (err)
		return err;

	err = mbuf_write_str(buf, str);
	mem_deref(str);

	return err;
}


/**
 * Auto complete a SIP uri, add scheme and domain if missing
 *
 * @param acc  User-Agent account
 * @param strp Pointer to destination string; allocated and set
 * @param uri  Input SIP uri pointer-length string
 *
 * @return 0 if success, otherwise errorcode
 */
int account_uri_complete_strdup(const struct account *acc, char **strp,
				const struct pl *uri)
{
	struct sa sa_addr;
	bool uri_is_ip;
	char *uridup;
	char *host;
	char *c;
	bool complete = false;
	struct mbuf *mb;
	struct pl trimmed;
	int err = 0;

	if (!strp || !pl_isset(uri))
		return EINVAL;

	/* Skip initial whitespace */
	trimmed = *uri;
	err = pl_ltrim(&trimmed);
	if (err)
		return err;

	mb = mbuf_alloc(64);
	if (!mb)
		return ENOMEM;

	/* Append sip: scheme if missing */
	if (re_regex(trimmed.p, trimmed.l, "sip:"))
		err |= mbuf_printf(mb, "sip:");
	else
		complete = true;

	err |= mbuf_write_pl(mb, &trimmed);
	if (!acc || err)
		goto out;

	if (complete)
		goto out;

	/* Append domain if missing and uri is not IP address */

	/* check if uri is valid IP address */
	err = pl_strdup(&uridup, &trimmed);
	if (err)
		goto out;

	if (!strncmp(uridup, "sip:", 4))
		host = uridup + 4;
	else
		host = uridup;

	c = strchr(host, ';');
	if (c)
		*c = 0;

	uri_is_ip =
		!sa_decode(&sa_addr, host, strlen(host)) ||
		!sa_set_str(&sa_addr, host, 0);
	if (!uri_is_ip && host[0] == '[' && (c = strchr(host, ']'))) {
		*c = 0;
		uri_is_ip = !sa_set_str(&sa_addr, host+1, 0);
	}

	mem_deref(uridup);

	if (!uri_is_ip &&
	    0 != re_regex(trimmed.p, trimmed.l, "[^@]+@[^]+", NULL, NULL)) {
		if (AF_INET6 == acc->luri.af)
			err |= mbuf_printf(mb, "@[%r]",
					   &acc->luri.host);
		else
			err |= mbuf_printf(mb, "@%r",
					   &acc->luri.host);

		/* Also append port if specified and not 5060 */
		switch (acc->luri.port) {

		case 0:
		case SIP_PORT:
			break;

		default:
			err |= mbuf_printf(mb, ":%u", acc->luri.port);
			break;
		}
	}

out:
	if (!err) {
		mbuf_set_pos(mb, 0);
		err = mbuf_strdup(mb, strp, mbuf_get_left(mb));
	}

	mem_deref(mb);
	return err;
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
	err |= re_hprintf(pf, " 100rel:       %s\n",
			  rel100_mode_str(acc->rel100_mode));
	err |= re_hprintf(pf, " answermode:   %s\n",
			  answermode_str(acc->answermode));
	err |= re_hprintf(pf, " autoredirect:   %s\n",
			  acc->autoredirect ? "yes" : "no");
	err |= re_hprintf(pf, " sipans:       %s\n",
			  acc->sipans ? "yes" : "no");
	err |= re_hprintf(pf, " sipansbeep:   %s\n",
			  sipansbeep_str(acc->sipansbeep));
	err |= re_hprintf(pf, " dtmfmode:     %s\n",
			  dtmfmode_str(acc->dtmfmode));
	if (!list_isempty(&acc->aucodecl)) {
		err |= re_hprintf(pf, " audio_codecs:");
		for (le = list_head(&acc->aucodecl); le; le = le->next) {
			const struct aucodec *ac = le->data;
			err |= re_hprintf(pf, " %s/%u/%u",
					  ac->name, ac->srate, ac->ch);
		}
		err |= re_hprintf(pf, "\n");
	}
	err |= re_hprintf(pf, " autelev_pt:   %u\n", acc->autelev_pt);
	err |= re_hprintf(pf, " auth_user:    %s\n", acc->auth_user);
	err |= re_hprintf(pf, " mediaenc:     %s\n",
			  acc->mencid ? acc->mencid : "none");
	err |= re_hprintf(pf, " medianat:     %s\n",
			  acc->mnatid ? acc->mnatid : "none");
	err |= re_hprintf(pf, " natpinhole:   %s\n",
			  acc->pinhole ? "yes" : "no");
	for (i=0; i<RE_ARRAY_SIZE(acc->outboundv); i++) {
		if (acc->outboundv[i]) {
			err |= re_hprintf(pf, " outbound%zu:    %s\n",
					  i+1, acc->outboundv[i]);
		}
	}
	err |= re_hprintf(pf, " mwi:          %s\n",
			  account_mwi(acc) ? "yes" : "no");
	err |= re_hprintf(pf, " ptime:        %u\n", acc->ptime);
	err |= re_hprintf(pf, " regint:       %u\n", acc->regint);
	err |= re_hprintf(pf, " prio:         %u\n", acc->prio);
	err |= re_hprintf(pf, " pubint:       %u\n", acc->pubint);
	err |= re_hprintf(pf, " regq:         %s\n", acc->regq);
	err |= re_hprintf(pf, " inreq_allowed:%s\n",
			  inreq_mode_str(acc->inreq_mode));
	err |= re_hprintf(pf, " sipnat:       %s\n", acc->sipnat);
	err |= re_hprintf(pf, " stunuser:     %s\n", acc->stun_user);
	err |= re_hprintf(pf, " stunserver:   %H\n",
			  stunuri_print, acc->stun_host);
	err |= re_hprintf(pf, " rtcp_mux:     %s\n",
			  acc->rtcp_mux ? "yes" : "no");

	if (!list_isempty(&acc->vidcodecl)) {
		err |= re_hprintf(pf, " video_codecs:");
		for (le = list_head(&acc->vidcodecl); le; le = le->next) {
			const struct vidcodec *vc = le->data;
			err |= re_hprintf(pf, " %s", vc->name);
		}
		err |= re_hprintf(pf, "\n");
	}
	err |= re_hprintf(pf, " call_transfer:%s\n",
			  account_call_transfer(acc) ? "yes" : "no");
	err |= re_hprintf(pf, " catchall:%s\n", acc->catchall ? "yes" : "no");
	err |= re_hprintf(pf, " cert:         %s\n", acc->cert);
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
	size_t i;

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
	for (i=0; i<RE_ARRAY_SIZE(acc->outboundv); i++) {
		if (acc->outboundv[i]) {
			err |= odict_entry_add(obn, "outbound", ODICT_STRING,
					acc->outboundv[i]);
		}
	}
	err |= odict_entry_add(odcfg, "sip_nat_outbound", ODICT_ARRAY, obn);

	const char *stunhost =
		account_stun_host(acc) ? account_stun_host(acc) : "";
	err |= odict_entry_add(odcfg, "stun_host", ODICT_STRING, stunhost);
	err |= odict_entry_add(odcfg, "stun_port", ODICT_INT,
			account_stun_port(acc));
	if (acc->stun_user) {
		err |= odict_entry_add(odcfg, "stun_user", ODICT_STRING,
				acc->stun_user);
	}

	err |= odict_entry_add(odcfg, "rel100_mode", ODICT_STRING,
			rel100_mode_str(acc->rel100_mode));
	err |= odict_entry_add(odcfg, "answer_mode", ODICT_STRING,
			answermode_str(acc->answermode));
	err |= odict_entry_add(odcfg, "inreq_allowed", ODICT_STRING,
			inreq_mode_str(acc->inreq_mode));
	err |= odict_entry_add(odcfg, "call_transfer", ODICT_BOOL, acc->refer);

	err |= odict_entry_add(odcfg, "packet_time", ODICT_INT,
			account_ptime(acc));

	mem_deref(obn);
	return err;
}


const char* account_uas_user(const struct account *acc)
{
	if (!acc)
		return NULL;

	return acc->uas_user;
}


const char* account_uas_pass(const struct account *acc)
{
	if (!acc)
		return NULL;

	return acc->uas_pass;
}


bool account_uas_isset(const struct account *acc)
{
	if (!acc)
		return false;

	return acc->uas_user || acc->uas_pass;
}


/**
 * Get the incoming out-of-dialog request mode of an account
 *
 * @param acc User-Agent account
 *
 * @return inreq_mode
 */
enum inreq_mode account_inreq_mode(const struct account *acc)
{
	return acc ? acc->inreq_mode : INREQ_MODE_ON;
}


/**
 * Set the incoming out-of-dialog request mode of an account
 *
 * @param acc  User-Agent account
 * @param mode Incoming request mode
 *
 * @return 0 if success, otherwise errorcode
 */
int account_set_inreq_mode(struct account *acc, enum inreq_mode mode)
{
	if (!acc)
		return EINVAL;

	if ((mode != INREQ_MODE_OFF) && (mode != INREQ_MODE_ON)) {
		warning("account: invalid inreq_allowed : '%d'\n", mode);
		return EINVAL;
	}

	acc->inreq_mode = mode;

	return 0;
}
