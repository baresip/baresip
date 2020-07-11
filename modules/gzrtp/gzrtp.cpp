/**
 * @file gzrtp.cpp  GNU ZRTP: Media Path Key Agreement for Unicast Secure RTP
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
 */
#include <stdint.h>

#include <re.h>
#include <baresip.h>

#include <string.h>

#include <libzrtpcpp/ZRtp.h>

#include "session.h"
#include "stream.h"


/**
 * @defgroup gzrtp gzrtp
 *
 * ZRTP: Media Path Key Agreement for Unicast Secure RTP
 *
 * Experimental support for ZRTP
 *
 *     See http://tools.ietf.org/html/rfc6189
 *
 *
 *     This module is using GNU ZRTP C++ library
 *
 *     https://github.com/wernerd/ZRTPCPP
 *
 * Configuration options:
 *
 \verbatim
  zrtp_parallel   {yes,no}   # Start all streams at once
 \endverbatim
 *
 */


static ZRTPConfig *s_zrtp_config = NULL;


struct menc_sess {
	Session *session;
};


struct menc_media {
	Stream *stream;
	const struct stream *strm;  /**< pointer to parent */
};


static void session_destructor(void *arg)
{
	struct menc_sess *st = (struct menc_sess *)arg;

	delete st->session;
}


static void media_destructor(void *arg)
{
	struct menc_media *st = (struct menc_media *)arg;

	delete st->stream;
}


static int session_alloc(struct menc_sess **sessp, struct sdp_session *sdp,
			 bool offerer, menc_event_h *eventh,
			 menc_error_h *errorh, void *arg)
{
	struct menc_sess *st;
	(void)offerer;
	(void)eventh;
	(void)errorh;
	(void)arg;
	int err = 0;

	if (!sessp || !sdp)
		return EINVAL;

	st = (struct menc_sess *)mem_zalloc(sizeof(*st), session_destructor);
	if (!st)
		return ENOMEM;

	st->session = new Session(*s_zrtp_config);
	if (!st->session)
		err = ENOMEM;

	if (err)
		mem_deref(st);
	else
		*sessp = st;

	return err;
}


static int media_alloc(struct menc_media **stp, struct menc_sess *sess,
                       struct rtp_sock *rtp,
                       struct udp_sock *rtpsock, struct udp_sock *rtcpsock,
 		       const struct sa *raddr_rtp,
		       const struct sa *raddr_rtcp,
		       struct sdp_media *sdpm,
		       const struct stream *strm)
{
	struct menc_media *st;
	int err = 0;
	StreamMediaType med_type;
	const char *med_name;

	if (!stp || !sess || !sess->session)
		return EINVAL;

	st = *stp;
	if (st)
		goto start;

	st = (struct menc_media *)mem_zalloc(sizeof(*st), media_destructor);
	if (!st)
		return ENOMEM;

	med_name = sdp_media_name(sdpm);
	if (str_cmp(med_name, "audio") == 0)
		med_type = MT_AUDIO;
	else if (str_cmp(med_name, "video") == 0)
		med_type = MT_VIDEO;
	else if (str_cmp(med_name, "text") == 0)
		med_type = MT_TEXT;
	else if (str_cmp(med_name, "application") == 0)
		med_type = MT_APPLICATION;
	else if (str_cmp(med_name, "message") == 0)
		med_type = MT_MESSAGE;
	else
		med_type = MT_UNKNOWN;

	st->strm = strm;
	st->stream = sess->session->create_stream(
	                       *s_zrtp_config,
	                       (struct udp_sock *)rtpsock,
	                       (struct udp_sock *)rtcpsock,
	                       rtp_sess_ssrc(rtp), med_type);
	if (!st->stream) {
		err = ENOMEM;
		goto out;
	}

	st->stream->sdp_encode(sdpm);

 out:
	if (err) {
		mem_deref(st);
		return err;
	}
	else
		*stp = st;

 start:
	if (sa_isset(sdp_media_raddr(sdpm), SA_ALL)) {
		st->stream->sdp_decode(sdpm);
		err = sess->session->start_stream(st->stream);
		if (err) {
			warning("zrtp: stream start failed: %d\n", err);
		}
	}

	return err;
}


static struct menc menc_zrtp = {
	.id        = "zrtp",
	.sdp_proto = "RTP/AVP",
	.sessh     = session_alloc,
	.mediah    = media_alloc
};


static const struct cmd cmdv[] = {
	{"zrtp_verify", 0, CMD_PRM, "Verify ZRTP SAS <session ID>",
	        Session::cmd_verify_sas },
	{"zrtp_unverify", 0, CMD_PRM, "Unverify ZRTP SAS <session ID>",
	        Session::cmd_unverify_sas },
};


static int module_init(void)
{
	char config_path[256];
	int err = 0;

	err = conf_path_get(config_path, sizeof(config_path));
	if (err) {
		warning("zrtp: could not get config path: %m\n", err);
		return err;
	}

	s_zrtp_config = new ZRTPConfig(conf_cur(), config_path);
	if (!s_zrtp_config)
		return ENOMEM;

	menc_register(baresip_mencl(), &menc_zrtp);

	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	delete s_zrtp_config;
	s_zrtp_config = NULL;

	cmd_unregister(baresip_commands(), cmdv);

	menc_unregister(&menc_zrtp);

	return 0;
}


extern "C" EXPORT_SYM const struct mod_export DECL_EXPORTS(gzrtp) = {
	"gzrtp",
	"menc",
	module_init,
	module_close
};
