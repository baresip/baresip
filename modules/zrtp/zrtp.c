/**
 * @file zrtp.c ZRTP: Media Path Key Agreement for Unicast Secure RTP
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include <zrtp.h>


/**
 * @defgroup zrtp zrtp
 *
 * ZRTP: Media Path Key Agreement for Unicast Secure RTP
 *
 * Experimental support for ZRTP
 *
 *     See http://tools.ietf.org/html/rfc6189
 *
 *     Briefly tested with Twinkle 1.4.2 and Jitsi 2.2.4603.9615
 *
 *     This module is using ZRTP implementation in Freeswitch
 *     https://github.com/traviscross/libzrtp
 */


struct menc_sess {
	zrtp_session_t *zrtp_session;
};

struct menc_media {
	const struct menc_sess *sess;
	struct udp_helper *uh;
	struct sa raddr;
	void *rtpsock;
	zrtp_stream_t *zrtp_stream;
};


static zrtp_global_t *zrtp_global;
static zrtp_config_t zrtp_config;


static void session_destructor(void *arg)
{
	struct menc_sess *st = arg;

	if (st->zrtp_session)
		zrtp_session_down(st->zrtp_session);
}


static void media_destructor(void *arg)
{
	struct menc_media *st = arg;

	mem_deref(st->uh);
	mem_deref(st->rtpsock);

	if (st->zrtp_stream)
		zrtp_stream_stop(st->zrtp_stream);
}


static bool udp_helper_send(int *err, struct sa *dst,
			    struct mbuf *mb, void *arg)
{
	struct menc_media *st = arg;
	unsigned int length;
	zrtp_status_t s;
	(void)dst;

	length = (unsigned int)mbuf_get_left(mb);

	s = zrtp_process_rtp(st->zrtp_stream, (char *)mbuf_buf(mb), &length);
	if (s != zrtp_status_ok) {
		warning("zrtp: zrtp_process_rtp failed (status = %d)\n", s);
		return false;
	}

	/* make sure target buffer is large enough */
	if (length > mbuf_get_space(mb)) {
		warning("zrtp: zrtp_process_rtp: length > space (%u > %u)\n",
			length, mbuf_get_space(mb));
		*err = ENOMEM;
	}

	mb->end = mb->pos + length;

	return false;
}


static bool udp_helper_recv(struct sa *src, struct mbuf *mb, void *arg)
{
	struct menc_media *st = arg;
	unsigned int length;
	zrtp_status_t s;
	(void)src;

	length = (unsigned int)mbuf_get_left(mb);

	s = zrtp_process_srtp(st->zrtp_stream, (char *)mbuf_buf(mb), &length);
	if (s != zrtp_status_ok) {

		if (s == zrtp_status_drop)
			return true;

		warning("zrtp: zrtp_process_srtp: %d\n", s);
		return false;
	}

	mb->end = mb->pos + length;

	return false;
}


static int session_alloc(struct menc_sess **sessp, struct sdp_session *sdp,
			 bool offerer, menc_error_h *errorh, void *arg)
{
	struct menc_sess *st;
	zrtp_status_t s;
	int err = 0;
	(void)offerer;
	(void)errorh;
	(void)arg;

	if (!sessp || !sdp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), session_destructor);
	if (!st)
		return ENOMEM;

	s = zrtp_session_init(zrtp_global, NULL,
			      ZRTP_SIGNALING_ROLE_UNKNOWN, &st->zrtp_session);
	if (s != zrtp_status_ok) {
		warning("zrtp: zrtp_session_init failed (status = %d)\n", s);
		err = EPROTO;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*sessp = st;

	return err;
}


static int media_alloc(struct menc_media **stp, struct menc_sess *sess,
		       struct rtp_sock *rtp,
		       int proto, void *rtpsock, void *rtcpsock,
		       struct sdp_media *sdpm)
{
	struct menc_media *st;
	zrtp_status_t s;
	int err = 0;

	if (!stp || !sess || proto != IPPROTO_UDP)
		return EINVAL;

	st = *stp;
	if (st)
		goto start;

	st = mem_zalloc(sizeof(*st), media_destructor);
	if (!st)
		return ENOMEM;

	st->sess = sess;
	st->rtpsock = mem_ref(rtpsock);

	err = udp_register_helper(&st->uh, rtpsock, 0,
				  udp_helper_send, udp_helper_recv, st);
	if (err)
		goto out;

	s = zrtp_stream_attach(sess->zrtp_session, &st->zrtp_stream);
	if (s != zrtp_status_ok) {
		warning("zrtp: zrtp_stream_attach failed (status=%d)\n", s);
		err = EPROTO;
		goto out;
	}

	zrtp_stream_set_userdata(st->zrtp_stream, st);

 out:
	if (err) {
		mem_deref(st);
		return err;
	}
	else
		*stp = st;

 start:
	if (sa_isset(sdp_media_raddr(sdpm), SA_ALL)) {
		st->raddr = *sdp_media_raddr(sdpm);

		s = zrtp_stream_start(st->zrtp_stream, rtp_sess_ssrc(rtp));
		if (s != zrtp_status_ok) {
			warning("zrtp: zrtp_stream_start: status = %d\n", s);
		}
	}

	return err;
}


static int zrtp_send_rtp_callback(const zrtp_stream_t *stream,
				  char *rtp_packet,
				  unsigned int rtp_packet_length)
{
	struct menc_media *st = zrtp_stream_get_userdata(stream);
	struct mbuf *mb;
	int err;

	if (!sa_isset(&st->raddr, SA_ALL))
		return zrtp_status_ok;

	mb = mbuf_alloc(rtp_packet_length);
	if (!mb)
		return zrtp_status_alloc_fail;

	(void)mbuf_write_mem(mb, (void *)rtp_packet, rtp_packet_length);
	mb->pos = 0;

	err = udp_send(st->rtpsock, &st->raddr, mb);
	if (err) {
		warning("zrtp: udp_send %u bytes (%m)\n",
			rtp_packet_length, err);
	}

	mem_deref(mb);

	return zrtp_status_ok;
}


static struct menc menc_zrtp = {
	LE_INIT, "zrtp", "RTP/AVP", session_alloc, media_alloc
};


static int module_init(void)
{
	zrtp_status_t s;
	char config_path[256] = "";
	int err;

	zrtp_config_defaults(&zrtp_config);

	err = conf_path_get(config_path, sizeof(config_path));
	if (err) {
		warning("zrtp: could not get config path: %m\n", err);
		return err;
	}
	if (re_snprintf(zrtp_config.cache_file_cfg.cache_path,
			sizeof(zrtp_config.cache_file_cfg.cache_path),
			"%s/zrtp_cache.dat", config_path) < 0)
	        return ENOMEM;

	rand_bytes(zrtp_config.zid, sizeof(zrtp_config.zid));

	zrtp_config.cb.misc_cb.on_send_packet = zrtp_send_rtp_callback;

	s = zrtp_init(&zrtp_config, &zrtp_global);
	if (zrtp_status_ok != s) {
		warning("zrtp: zrtp_init() failed (status = %d)\n", s);
		return ENOSYS;
	}

	menc_register(&menc_zrtp);

	return 0;
}


static int module_close(void)
{
	menc_unregister(&menc_zrtp);

	if (zrtp_global) {
		zrtp_down(zrtp_global);
		zrtp_global = NULL;
	}

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(zrtp) = {
	"zrtp",
	"menc",
	module_init,
	module_close
};
