/**
 * @file cparam.c  Command parameter parse helpers
 *
 * Copyright (C) 2025 Christian Spielberger - c.spielberger@commend.com
 */

#include <re.h>
#include <baresip.h>


static bool mdir_isvalid(const struct pl *pl)
{
	if (!pl_strcmp(pl, "sendrecv"))
		return true;

	if (!pl_strcmp(pl, "sendonly"))
		return true;

	if (!pl_strcmp(pl, "recvonly"))
		return true;

	if (!pl_strcmp(pl, "inactive"))
		return true;

	return false;
}


static int decode_media_dir(struct pl *pl, enum sdp_dir *dirp,
			    struct re_printf *pf)
{
	if (!pl_isset(pl))
		return 0;

	if (!mdir_isvalid(pl)) {
		re_hprintf(pf, "unknown audio/video direction '%r'\n", pl);
		return EINVAL;
	}

	enum sdp_dir dir = sdp_dir_decode(pl);
	*dirp = dir;
	return 0;
}


/**
 * Decode a command parameter
 *
 * @param prm  Command arguments parameter string
 * @param name Parameter name
 * @param val  Returned parameter value
 *
 * @return 0 for success, otherwise errorcode
 */
int cparam_decode(const char *prm, const char *name, struct pl *val)
{
	struct pl pl;
	pl_set_str(&pl, prm);
	return fmt_param_sep_get(&pl, name, ' ', val) ? 0 : ENOENT;
}


/**
 * Decode call command parameters
 *
 * @param prmp Returned call command parameters
 * @param prm  Command arguments parameter string
 * @param pf   Print handler for debug output
 * @return 0 for success, otherwise errorcode
 */
int cparam_call_decode(struct cparam_call **prmp, const char *prm,
			struct re_printf *pf)
{
	bool set;
	bool one = false;
	int err = 0;

	/* audio/video direction */
	struct cparam_call *cp;
	cp = mem_zalloc(sizeof(*cp), NULL);
	if (!cp)
		return ENOMEM;

	struct pl pla = PL_INIT;
	struct pl plv = PL_INIT;
	struct pl pl  = PL_INIT;

	/* long form */
	set  = cparam_decode(prm, "audio", &pla) == 0;
	set |= cparam_decode(prm, "video", &plv) == 0;
	set |= cparam_decode(prm, "callid", &cp->callid) == 0;
	if (!set) {
		/* short form */
		set = re_regex(prm, str_len(prm),
			"[^ ]*[ \t\r\n]*[^ ]+", &pl, NULL, &cp->callid) == 0;
		pla = plv = pl;
	}

	if (!set) {
		/* only one argument */
		pl_set_str(&pl, prm);
		one = pl_isset(&pl);
		pla = plv = pl_null;
	}

	if (one) {
		if (mdir_isvalid(&pl))
			pla = plv = pl;
		else
			cp->callid = pl;
	}

	cp->mdir = pl_isset(&pla) || pl_isset(&plv);
	if (!pl_isset(&pla))
		pl_set_str(&pla, "sendrecv");

	if (!pl_isset(&plv))
		pl_set_str(&plv, "sendrecv");

	err  = decode_media_dir(&pla, &cp->adir, pf);
	if (err)
		goto out;

	err = decode_media_dir(&plv, &cp->vdir, pf);
	if (err)
		goto out;

	if (cp->adir == SDP_INACTIVE && cp->vdir == SDP_INACTIVE) {
		re_hprintf(pf, "both media directions inactive\n");
		err = EINVAL;
		goto out;
	}
out:
	if (err)
		mem_deref(cp);
	else
		*prmp = cp;

	return err;
}


/**
 * Decode UA command parameters
 *
 * @param prmp Returned UA command parameters
 * @param prm  Command arguments parameter string
 * @param pf   Print handler for debug output
 * @return 0 for success, otherwise errorcode
 */
int cparam_ua_decode(struct cparam_ua **prmp,
		      const char *prm, struct re_printf *pf)
{
	struct cparam_ua *cp;
	int err;

	cp = mem_zalloc(sizeof(*cp), NULL);
	if (!cp)
		return ENOMEM;

	/* with display name */
	err = re_regex(prm, str_len(prm),
		       "[~ \t\r\n<]*[ \t\r\n]*<[^>]+>[ \t\r\n]*",
		       &cp->dname, NULL, &cp->uri, NULL);
	if (err) {
		cp->dname = pl_null;

		/* without display name */
		err = re_regex(prm, str_len(prm), "[^ ]+", &cp->uri);
	}

	bool ok = false;
	if (!err) {
		ok  = re_regex(cp->uri.p, cp->uri.l, ";[^=]+=", NULL) == 0;
		ok |= re_regex(cp->uri.p, cp->uri.l, "=")    != 0;
	}

	if (!ok) {
		re_hprintf(pf, "dial URI missing\n");
		err = EINVAL;
		goto out;
	}

	prm = cp->uri.p + cp->uri.l;
	struct pl pla = PL("sendrecv");
	struct pl plv = PL("sendrecv");
	struct pl pl;
	bool set;
	bool one = false;
	set  = cparam_decode(prm, "audio", &pla) == 0;
	set |= cparam_decode(prm, "video", &plv) == 0;
	set |= cparam_decode(prm, "userdata", &cp->userdata) == 0;
	if (!set) {
		/* short form */
		one = re_regex(prm, str_len(prm), "[^ ]+", &pl) == 0;
	}

	if (one)
		plv = pla = pl;

	err  = decode_media_dir(&pla, &cp->adir, pf);
	err |= decode_media_dir(&plv, &cp->vdir, pf);
	if (err)
		goto out;

	if (cp->adir == SDP_INACTIVE && cp->vdir == SDP_INACTIVE) {
		re_hprintf(pf, "both media directions inactive\n");
		err = EINVAL;
	}

out:
	if (err)
		mem_deref(cp);
	else
		*prmp = cp;

	return err;
}
