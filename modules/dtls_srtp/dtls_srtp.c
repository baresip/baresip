/**
 * @file dtls_srtp.c DTLS-SRTP media encryption
 *
 * Copyright (C) 2010 Creytiv.com
 */

#if defined (__GNUC__) && !defined (asm)
#define asm __asm__  /* workaround */
#endif
#include <srtp/srtp.h>
#include <re.h>
#include <baresip.h>
#include <string.h>
#include "dtls_srtp.h"


/*
 *            STACK Diagram:
 *
 *                    application
 *                        |
 *                        |
 *            [DTLS]   [SRTP]
 *                \      /
 *                 \    /
 *                  \  /
 *                   \/
 *              ( TURN/ICE )
 *                   |
 *                   |
 *                 socket
 *
 */

struct menc_sess {
	struct sdp_session *sdp;
	bool offerer;
	menc_error_h *errorh;
	void *arg;
};

/* media */
struct dtls_srtp {
	struct sock sockv[2];
	const struct menc_sess *sess;
	struct sdp_media *sdpm;
	struct tmr tmr;
	bool started;
	bool active;
	bool mux;
};

static struct tls *tls;
static const char* srtp_profiles =
	"SRTP_AES128_CM_SHA1_80:"
	"SRTP_AES128_CM_SHA1_32";


static void sess_destructor(void *arg)
{
	struct menc_sess *sess = arg;

	mem_deref(sess->sdp);
}


static void destructor(void *arg)
{
	struct dtls_srtp *st = arg;
	size_t i;

	tmr_cancel(&st->tmr);

	for (i=0; i<2; i++) {
		struct sock *s = &st->sockv[i];

		mem_deref(s->uh_srtp);
		mem_deref(s->dtls);
		mem_deref(s->app_sock);  /* must be freed last */
		mem_deref(s->tx);
		mem_deref(s->rx);
	}

	mem_deref(st->sdpm);
}


static bool verify_fingerprint(const struct sdp_session *sess,
			       const struct sdp_media *media,
			       struct dtls_flow *tc)
{
	struct pl hash;
	char hashstr[32];
	uint8_t md_sdp[64];
	size_t sz_sdp = sizeof(md_sdp);
	struct tls_fingerprint tls_fp;

	if (sdp_fingerprint_decode(sdp_rattr(sess, media, "fingerprint"),
				   &hash, md_sdp, &sz_sdp))
		return false;

	pl_strcpy(&hash, hashstr, sizeof(hashstr));

	if (dtls_get_remote_fingerprint(tc, hashstr, &tls_fp)) {
		warning("dtls_srtp: could not get DTLS fingerprint\n");
		return false;
	}

	if (sz_sdp != tls_fp.len || 0 != memcmp(md_sdp, tls_fp.md, sz_sdp)) {
		warning("dtls_srtp: %s fingerprint mismatch\n", hashstr);
		info("DTLS: %w\n", tls_fp.md, (size_t)tls_fp.len);
		info("SDP:  %w\n", md_sdp, sz_sdp);
		return false;
	}

	info("dtls_srtp: verified %s fingerprint OK\n", hashstr);

	return true;
}


static void dtls_established_handler(int err, struct dtls_flow *flow,
				     const char *profile,
				     const struct key *client_key,
				     const struct key *server_key,
				     void *arg)
{
	struct sock *sock = arg;
	const struct dtls_srtp *ds = sock->ds;

	if (!verify_fingerprint(ds->sess->sdp, ds->sdpm, flow)) {
		warning("dtls_srtp: could not verify remote fingerprint\n");
		if (ds->sess->errorh)
			ds->sess->errorh(EPIPE, ds->sess->arg);
		return;
	}

	sock->negotiated = true;

	info("dtls_srtp: ---> DTLS-SRTP complete (%s/%s) Profile=%s\n",
	     sdp_media_name(ds->sdpm),
	     sock->is_rtp ? "RTP" : "RTCP", profile);

	err |= srtp_stream_add(&sock->tx, profile,
			       ds->active ? client_key : server_key,
			       true);

	err |= srtp_stream_add(&sock->rx, profile,
			       ds->active ? server_key : client_key,
			       false);

	err |= srtp_install(sock);
	if (err) {
		warning("dtls_srtp: srtp_install: %m\n", err);
	}
}


static int session_alloc(struct menc_sess **sessp,
			 struct sdp_session *sdp, bool offerer,
			 menc_error_h *errorh, void *arg)
{
	struct menc_sess *sess;
	int err;

	if (!sessp || !sdp)
		return EINVAL;

	sess = mem_zalloc(sizeof(*sess), sess_destructor);
	if (!sess)
		return ENOMEM;

	sess->sdp    = mem_ref(sdp);
	sess->offerer = offerer;
	sess->errorh = errorh;
	sess->arg    = arg;

	/* RFC 4145 */
	err = sdp_session_set_lattr(sdp, true, "setup",
				    offerer ? "actpass" : "active");
	if (err)
		goto out;

	/* RFC 4572 */
	err = sdp_session_set_lattr(sdp, true, "fingerprint", "SHA-1 %H",
				    dtls_print_sha1_fingerprint, tls);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(sess);
	else
		*sessp = sess;

	return err;
}


static int media_start_sock(struct sock *sock, struct sdp_media *sdpm)
{
	struct sa raddr;
	int err = 0;

	if (!sock->app_sock || sock->negotiated || sock->dtls)
		return 0;

	if (sock->is_rtp)
		raddr = *sdp_media_raddr(sdpm);
	else
		sdp_media_raddr_rtcp(sdpm, &raddr);

	if (sa_isset(&raddr, SA_ALL)) {

		err = dtls_flow_alloc(&sock->dtls, tls, sock->app_sock,
				      dtls_established_handler, sock);
		if (err)
			return err;

		err = dtls_flow_start(sock->dtls, &raddr, sock->ds->active);
	}

	return err;
}


static int media_start(struct dtls_srtp *st, struct sdp_media *sdpm)
{
	int err = 0;

	if (st->started)
		return 0;

	debug("dtls_srtp: media_start: '%s' mux=%d, active=%d\n",
	      sdp_media_name(sdpm), st->mux, st->active);

	if (!sdp_media_has_media(sdpm))
		return 0;

	err = media_start_sock(&st->sockv[0], sdpm);

	if (!st->mux)
		err |= media_start_sock(&st->sockv[1], sdpm);

	if (err)
		return err;

	st->started = true;

	return 0;
}


static void timeout(void *arg)
{
	struct dtls_srtp *st = arg;

	media_start(st, st->sdpm);
}


static int media_alloc(struct menc_media **mp, struct menc_sess *sess,
		       struct rtp_sock *rtp, int proto,
		       void *rtpsock, void *rtcpsock,
		       struct sdp_media *sdpm)
{
	struct dtls_srtp *st;
	const char *setup, *fingerprint;
	int err = 0;
	unsigned i;
	(void)rtp;

	if (!mp || !sess || proto != IPPROTO_UDP)
		return EINVAL;

	st = (struct dtls_srtp *)*mp;
	if (st)
		goto setup;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->sess = sess;
	st->sdpm = mem_ref(sdpm);
	st->sockv[0].app_sock = mem_ref(rtpsock);
	st->sockv[1].app_sock = mem_ref(rtcpsock);

	for (i=0; i<2; i++)
		st->sockv[i].ds = st;

	st->sockv[0].is_rtp = true;
	st->sockv[1].is_rtp = false;

	if (err) {
		mem_deref(st);
		return err;
	}
	else
		*mp = (struct menc_media *)st;

 setup:
	st->mux = (rtpsock == rtcpsock);

	setup = sdp_rattr(st->sess->sdp, st->sdpm, "setup");
	if (setup) {
		st->active = !(0 == str_casecmp(setup, "active"));

		/* note: we need to wait for ICE to settle ... */
		tmr_start(&st->tmr, 100, timeout, st);
	}

	/* SDP offer/answer on fingerprint attribute */
	fingerprint = sdp_rattr(st->sess->sdp, st->sdpm, "fingerprint");
	if (fingerprint) {

		struct pl hash;

		err = sdp_fingerprint_decode(fingerprint, &hash, NULL, NULL);
		if (err)
			return err;

		if (0 == pl_strcasecmp(&hash, "SHA-1")) {
			err = sdp_media_set_lattr(st->sdpm, true,
						  "fingerprint", "SHA-1 %H",
						  dtls_print_sha1_fingerprint,
						  tls);
		}
		else if (0 == pl_strcasecmp(&hash, "SHA-256")) {
			err = sdp_media_set_lattr(st->sdpm, true,
						  "fingerprint", "SHA-256 %H",
						 dtls_print_sha256_fingerprint,
						  tls);
		}
		else {
			info("dtls_srtp: unsupported fingerprint hash `%r'\n",
			     &hash);
			return EPROTO;
		}
	}

	return err;
}


static struct menc dtls_srtp = {
	LE_INIT, "dtls_srtp",  "UDP/TLS/RTP/SAVP", session_alloc, media_alloc
};

static struct menc dtls_srtpf = {
	LE_INIT, "dtls_srtpf", "UDP/TLS/RTP/SAVPF", session_alloc, media_alloc
};

static struct menc dtls_srtp2 = {
	/* note: temp for Webrtc interop */
	LE_INIT, "srtp-mandf", "RTP/SAVPF", session_alloc, media_alloc
};


static int module_init(void)
{
	err_status_t ret;
	int err;

	crypto_kernel_shutdown();
	ret = srtp_init();
	if (err_status_ok != ret) {
		warning("dtls_srtp: srtp_init() failed: ret=%d\n", ret);
		return ENOSYS;
	}

	err = dtls_alloc_selfsigned(&tls, "dtls@baresip", srtp_profiles);
	if (err)
		return err;

	menc_register(&dtls_srtpf);
	menc_register(&dtls_srtp);
	menc_register(&dtls_srtp2);

	debug("DTLS-SRTP ready with profiles %s\n", srtp_profiles);

	return 0;
}


static int module_close(void)
{
	menc_unregister(&dtls_srtp);
	menc_unregister(&dtls_srtpf);
	menc_unregister(&dtls_srtp2);
	tls = mem_deref(tls);
	crypto_kernel_shutdown();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(dtls_srtp) = {
	"dtls_srtp",
	"menc",
	module_init,
	module_close
};
