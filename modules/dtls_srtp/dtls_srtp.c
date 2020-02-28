/**
 * @file dtls_srtp.c DTLS-SRTP media encryption
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <string.h>
#include "dtls_srtp.h"


/**
 * @defgroup dtls_srtp dtls_srtp
 *
 * DTLS-SRTP media encryption module
 *
 * This module implements end-to-end media encryption using DTLS-SRTP
 * which is now mandatory for WebRTC endpoints.
 *
 * DTLS-SRTP can be enabled in ~/.baresip/accounts:
 *
 \verbatim
  <sip:user@domain.com>;mediaenc=dtls_srtp
 \endverbatim
 *
 *
 * Internally the protocol stack diagram looks something like this:
 *
 \verbatim
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
 *                [socket]
 \endverbatim
 *
 */

struct menc_sess {
	struct sdp_session *sdp;
	bool offerer;
	menc_event_h *eventh;
	menc_error_h *errorh;
	void *arg;
};

/* media */
struct dtls_srtp {
	struct comp compv[2];
	const struct menc_sess *sess;
	struct sdp_media *sdpm;
	const struct stream *strm;   /**< pointer to parent */
	bool started;
	bool active;
	bool mux;
};

static struct tls *tls;
static const char* srtp_profiles =
	"SRTP_AES128_CM_SHA1_80:"
	"SRTP_AES128_CM_SHA1_32:"
	"SRTP_AEAD_AES_128_GCM:"
	"SRTP_AEAD_AES_256_GCM";


static void sess_destructor(void *arg)
{
	struct menc_sess *sess = arg;

	mem_deref(sess->sdp);
}


static void destructor(void *arg)
{
	struct dtls_srtp *st = arg;
	size_t i;

	for (i=0; i<2; i++) {
		struct comp *c = &st->compv[i];

		mem_deref(c->uh_srtp);
		mem_deref(c->tls_conn);
		mem_deref(c->dtls_sock);
		mem_deref(c->app_sock);  /* must be freed last */
		mem_deref(c->tx);
		mem_deref(c->rx);
	}

	mem_deref(st->sdpm);
}


static bool verify_fingerprint(const struct sdp_session *sess,
			       const struct sdp_media *media,
			       struct tls_conn *tc)
{
	struct pl hash;
	uint8_t md_sdp[32], md_dtls[32];
	size_t sz_sdp = sizeof(md_sdp);
	size_t sz_dtls;
	enum tls_fingerprint type;
	int err;

	if (sdp_fingerprint_decode(sdp_media_session_rattr(media, sess,
							   "fingerprint"),
				   &hash, md_sdp, &sz_sdp))
		return false;

	if (0 == pl_strcasecmp(&hash, "sha-256")) {
		type = TLS_FINGERPRINT_SHA256;
		sz_dtls = 32;
	}
	else {
		warning("dtls_srtp: unknown fingerprint '%r'\n", &hash);
		return false;
	}

	err = tls_peer_fingerprint(tc, type, md_dtls, sizeof(md_dtls));
	if (err) {
		warning("dtls_srtp: could not get DTLS fingerprint (%m)\n",
			err);
		return false;
	}

	if (sz_sdp != sz_dtls || 0 != memcmp(md_sdp, md_dtls, sz_sdp)) {
		warning("dtls_srtp: %r fingerprint mismatch\n", &hash);
		info("SDP:  %w\n", md_sdp, sz_sdp);
		info("DTLS: %w\n", md_dtls, sz_dtls);
		return false;
	}

	info("dtls_srtp: verified %r fingerprint OK\n", &hash);

	return true;
}


static int session_alloc(struct menc_sess **sessp,
			 struct sdp_session *sdp, bool offerer,
			 menc_event_h *eventh, menc_error_h *errorh,
			 void *arg)
{
	struct menc_sess *sess;
	int err;

	if (!sessp || !sdp)
		return EINVAL;

	sess = mem_zalloc(sizeof(*sess), sess_destructor);
	if (!sess)
		return ENOMEM;

	sess->sdp     = mem_ref(sdp);
	sess->offerer = offerer;
	sess->eventh  = eventh;
	sess->errorh  = errorh;
	sess->arg     = arg;

	/* RFC 4145 */
	err = sdp_session_set_lattr(sdp, true, "setup",
				    offerer ? "actpass" : "active");
	if (err)
		goto out;

	/* RFC 4572 */
	err = sdp_session_set_lattr(sdp, true, "fingerprint", "SHA-256 %H",
				    dtls_print_sha256_fingerprint, tls);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(sess);
	else
		*sessp = sess;

	return err;
}


static size_t get_master_keylen(enum srtp_suite suite)
{
	switch (suite) {

		case SRTP_AES_CM_128_HMAC_SHA1_32: return 16+14;
		case SRTP_AES_CM_128_HMAC_SHA1_80: return 16+14;
		case SRTP_AES_128_GCM:             return 16+12;
		case SRTP_AES_256_GCM:             return 32+12;
		default: return 0;
	}
}


static void dtls_estab_handler(void *arg)
{
	struct comp *comp = arg;
	const struct dtls_srtp *ds = comp->ds;
	enum srtp_suite suite;
	uint8_t cli_key[32+12], srv_key[32+12];
	char buf[32] = "";
	size_t keylen;
	int err;

	if (!verify_fingerprint(ds->sess->sdp, ds->sdpm, comp->tls_conn)) {
		warning("dtls_srtp: could not verify remote fingerprint\n");
		if (ds->sess->errorh)
			ds->sess->errorh(EPIPE, ds->sess->arg);
		return;
	}

	err = tls_srtp_keyinfo(comp->tls_conn, &suite,
			       cli_key, sizeof(cli_key),
			       srv_key, sizeof(srv_key));
	if (err) {
		warning("dtls_srtp: could not get SRTP keyinfo (%m)\n", err);
		return;
	}

	comp->negotiated = true;

	info("dtls_srtp: ---> DTLS-SRTP complete (%s/%s) Profile=%s\n",
	     sdp_media_name(ds->sdpm),
	     comp->is_rtp ? "RTP" : "RTCP", srtp_suite_name(suite));

	keylen = get_master_keylen(suite);

	err |= srtp_stream_add(&comp->tx, suite,
			       ds->active ? cli_key : srv_key, keylen, true);
	err |= srtp_stream_add(&comp->rx, suite,
			       ds->active ? srv_key : cli_key, keylen, false);
	if (err)
		return;

	err |= srtp_install(comp);
	if (err) {
		warning("dtls_srtp: srtp_install: %m\n", err);
	}

	if (ds->sess->eventh) {
		if (re_snprintf(buf, sizeof(buf), "%s,%s",
				sdp_media_name(ds->sdpm),
				comp->is_rtp ? "RTP" : "RTCP"))
			ds->sess->eventh(MENC_EVENT_SECURE, buf,
					 (struct stream *)ds->strm,
					 ds->sess->arg);
		else
			warning("dtls_srtp: failed to print secure"
				" event arguments\n");
	}
}


static void dtls_close_handler(int err, void *arg)
{
	struct comp *comp = arg;

	info("dtls_srtp: dtls-connection closed (%m)\n", err);

	comp->tls_conn = mem_deref(comp->tls_conn);

	if (!comp->negotiated) {

		if (comp->ds->sess->errorh)
			comp->ds->sess->errorh(err, comp->ds->sess->arg);
	}
}


static void dtls_conn_handler(const struct sa *peer, void *arg)
{
	struct comp *comp = arg;
	int err;
	(void)peer;

	info("dtls_srtp: incoming DTLS connect from %J\n", peer);

	if (comp->tls_conn) {
		warning("dtls_srtp: dtls already accepted (peer = %J)\n",
			dtls_peer(comp->tls_conn));
		return;
	}

	err = dtls_accept(&comp->tls_conn, tls, comp->dtls_sock,
			  dtls_estab_handler, NULL, dtls_close_handler, comp);
	if (err) {
		warning("dtls_srtp: dtls_accept failed (%m)\n", err);
		return;
	}
}


static int component_start(struct comp *comp, const struct sa *raddr)
{
	int err = 0;

	debug("dtls_srtp: component start: %s [raddr=%J]\n",
	      comp->is_rtp ? "RTP" : "RTCP", raddr);

	if (!comp->app_sock || comp->negotiated || comp->dtls_sock)
		return 0;

	err = dtls_listen(&comp->dtls_sock, NULL,
			  comp->app_sock, 2, LAYER_DTLS,
			  dtls_conn_handler, comp);
	if (err) {
		warning("dtls_srtp: dtls_listen failed (%m)\n", err);
		return err;
	}

	if (sa_isset(raddr, SA_ALL)) {

		if (comp->ds->active && !comp->tls_conn) {

			info("dtls_srtp: '%s,%s' dtls connect to %J\n",
			     sdp_media_name(comp->ds->sdpm),
			     comp->is_rtp ? "RTP" : "RTCP",
			     raddr);

			err = dtls_connect(&comp->tls_conn, tls,
					   comp->dtls_sock, raddr,
					   dtls_estab_handler, NULL,
					   dtls_close_handler, comp);
			if (err) {
				warning("dtls_srtp: dtls_connect()"
					" failed (%m)\n", err);
				return err;
			}
		}
	}

	return err;
}


static int media_start(struct dtls_srtp *st, struct sdp_media *sdpm,
		       const struct sa *raddr_rtp,
		       const struct sa *raddr_rtcp)
{
	int err = 0;

	if (st->started)
		return 0;

	info("dtls_srtp: media=%s -- start DTLS %s\n",
	     sdp_media_name(sdpm), st->active ? "client" : "server");

	if (!sdp_media_has_media(sdpm))
		return 0;

	err = component_start(&st->compv[0], raddr_rtp);

	if (!st->mux)
		err |= component_start(&st->compv[1], raddr_rtcp);

	if (err)
		return err;

	st->started = true;

	return 0;
}


static int media_alloc(struct menc_media **mp, struct menc_sess *sess,
		       struct rtp_sock *rtp,
		       struct udp_sock *rtpsock, struct udp_sock *rtcpsock,
		       const struct sa *raddr_rtp,
		       const struct sa *raddr_rtcp,
		       struct sdp_media *sdpm, const struct stream *strm)
{
	struct dtls_srtp *st;
	const char *setup, *fingerprint;
	int err = 0;
	unsigned i;
	(void)rtp;

	if (!mp || !sess)
		return EINVAL;

	st = (struct dtls_srtp *)*mp;
	if (st)
		goto setup;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->sess = sess;
	st->sdpm = mem_ref(sdpm);
	st->strm = strm;
	st->compv[0].app_sock = mem_ref(rtpsock);
	st->compv[1].app_sock = mem_ref(rtcpsock);

	for (i=0; i<2; i++)
		st->compv[i].ds = st;

	st->compv[0].is_rtp = true;
	st->compv[1].is_rtp = false;

	err = sdp_media_set_alt_protos(st->sdpm, 4,
				       "RTP/SAVP",
				       "RTP/SAVPF",
				       "UDP/TLS/RTP/SAVP",
				       "UDP/TLS/RTP/SAVPF");
	if (err)
		goto out;

 out:
	if (err) {
		mem_deref(st);
		return err;
	}
	else
		*mp = (struct menc_media *)st;

 setup:
	st->mux = (rtpsock == rtcpsock) || (rtcpsock == NULL);

	setup = sdp_media_session_rattr(st->sdpm, st->sess->sdp, "setup");
	if (setup) {
		st->active = !(0 == str_casecmp(setup, "active"));

		err = media_start(st, st->sdpm, raddr_rtp, raddr_rtcp);
		if (err)
			return err;
	}

	/* SDP offer/answer on fingerprint attribute */
	fingerprint = sdp_media_session_rattr(st->sdpm, st->sess->sdp,
					      "fingerprint");
	if (fingerprint) {

		struct pl hash;

		err = sdp_fingerprint_decode(fingerprint, &hash, NULL, NULL);
		if (err)
			return err;

		if (0 == pl_strcasecmp(&hash, "SHA-256")) {
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
	.id        = "dtls_srtp",
	.sdp_proto = "UDP/TLS/RTP/SAVPF",
	.wait_secure = true,
	.sessh     = session_alloc,
	.mediah    = media_alloc
};


static int module_init(void)
{
	struct list *mencl = baresip_mencl();
	int err;

	err = tls_alloc(&tls, TLS_METHOD_DTLSV1, NULL, NULL);
	if (err) {
		warning("dtls_srtp: failed to create DTLS context (%m)\n",
			err);
		return err;
	}

	err = tls_set_selfsigned(tls, "dtls@baresip");
	if (err) {
		warning("dtls_srtp: failed to self-sign certificate (%m)\n",
			err);
		return err;
	}

	tls_set_verify_client(tls);

	err = tls_set_srtp(tls, srtp_profiles);
	if (err) {
		warning("dtls_srtp: failed to enable SRTP profile (%m)\n",
			err);
		return err;
	}

	menc_register(mencl, &dtls_srtp);

	debug("DTLS-SRTP ready with profiles %s\n", srtp_profiles);

	return 0;
}


static int module_close(void)
{
	menc_unregister(&dtls_srtp);
	tls = mem_deref(tls);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(dtls_srtp) = {
	"dtls_srtp",
	"menc",
	module_init,
	module_close
};
