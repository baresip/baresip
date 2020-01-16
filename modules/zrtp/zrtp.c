/**
 * @file zrtp.c ZRTP: Media Path Key Agreement for Unicast Secure RTP
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include <zrtp.h>

#include <string.h>

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
 *     https://github.com/juha-h/libzrtp
 *
 * Thanks:
 *
 *   Ingo Feinerer
 *
 * Configuration options:
 *
 \verbatim
  zrtp_hash       {yes,no}   # Enable SDP zrtp-hash (recommended)
 \endverbatim
 *
 */


enum {
	PRESZ = 36  /* Preamble size for TURN/STUN header */
};

struct menc_sess {
	zrtp_session_t *zrtp_session;
	menc_event_h *eventh;
	menc_error_h *errorh;
	void *arg;
	struct tmr abort_timer;
	int err;
};

struct menc_media {
	struct menc_sess *sess;
	struct udp_helper *uh_rtp;
	struct udp_helper *uh_rtcp;
	struct sa raddr;
	void *rtpsock;
	void *rtcpsock;
	zrtp_stream_t *zrtp_stream;
	const struct stream *strm;   /**< pointer to parent */
};


static zrtp_global_t *zrtp_global;
static zrtp_config_t zrtp_config;
static zrtp_zid_t zid;


/* RFC 6189, section 8.1. */
static bool use_sig_hash = true;


enum pkt_type {
	PKT_TYPE_UNKNOWN = 0,
	PKT_TYPE_RTP = 1,
	PKT_TYPE_RTCP = 2,
	PKT_TYPE_ZRTP = 4
};


static enum pkt_type get_packet_type(const struct mbuf *mb)
{
	uint8_t b, pt;
	uint32_t magic;

	if (mbuf_get_left(mb) < 8)
		return PKT_TYPE_UNKNOWN;

	b = mbuf_buf(mb)[0];

	if (127 < b && b < 192) {
		pt = mbuf_buf(mb)[1] & 0x7f;
		if (72 <= pt && pt <= 76)
			return PKT_TYPE_RTCP;
		else
			return PKT_TYPE_RTP;
	}
	else {
		memcpy(&magic, &mbuf_buf(mb)[4], 4);
		magic = ntohl(magic);
		if (magic == ZRTP_PACKETS_MAGIC)
			return PKT_TYPE_ZRTP;
	}

	return PKT_TYPE_UNKNOWN;
}


static void session_destructor(void *arg)
{
	struct menc_sess *st = arg;

	tmr_cancel(&st->abort_timer);

	if (st->zrtp_session)
		zrtp_session_down(st->zrtp_session);
}


static void media_destructor(void *arg)
{
	struct menc_media *st = arg;

	mem_deref(st->uh_rtp);
	mem_deref(st->uh_rtcp);
	mem_deref(st->rtpsock);
	mem_deref(st->rtcpsock);

	if (st->zrtp_stream)
		zrtp_stream_stop(st->zrtp_stream);
}


static void abort_timer_h(void *arg)
{
	struct menc_sess *sess = arg;

	if (sess->errorh) {
		sess->errorh(sess->err, sess->arg);
		sess->errorh = NULL;
	}
}


static void abort_call(struct menc_sess *sess)
{
	if (!sess->err) {
		sess->err = EPIPE;
		tmr_start(&sess->abort_timer, 0, abort_timer_h, sess);
	}
}


static bool drop_packets(const struct menc_media *st)
{
	return (st)? st->sess->err != 0 : true;
}


static bool udp_helper_send(int *err, struct sa *dst,
			    struct mbuf *mb, void *arg)
{
	struct menc_media *st = arg;
	unsigned int length;
	zrtp_status_t s;
	const char *proto_name = "rtp";
	enum pkt_type ptype = get_packet_type(mb);

	if (drop_packets(st))
		return true;

	length = (unsigned int)mbuf_get_left(mb);

	/* only RTP/RTCP packets should be processed */
	if (ptype == PKT_TYPE_RTCP) {
		proto_name = "rtcp";
		s = zrtp_process_rtcp(st->zrtp_stream,
			    (char *)mbuf_buf(mb), &length);
	}
	else if (ptype == PKT_TYPE_RTP) {
		s = zrtp_process_rtp(st->zrtp_stream,
			    (char *)mbuf_buf(mb), &length);
	}
	else
		return false;

	if (s != zrtp_status_ok) {

		if (s == zrtp_status_drop)
			return true;

		warning("zrtp: send(port=%d): zrtp_process_%s failed"
			" (status = %d '%s')\n",
			sa_port(dst), proto_name, s, zrtp_log_status2str(s));
		return false;
	}

	/* make sure target buffer is large enough */
	if (length > mbuf_get_space(mb)) {
		warning("zrtp: zrtp_process_%s: length > space (%u > %u)\n",
			proto_name, length, mbuf_get_space(mb));
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
	const char *proto_name = "srtp";
	enum pkt_type ptype = get_packet_type(mb);

	if (drop_packets(st))
		return true;

	length = (unsigned int)mbuf_get_left(mb);

	if (ptype == PKT_TYPE_RTCP) {
		proto_name = "srtcp";
		s = zrtp_process_srtcp(st->zrtp_stream,
			    (char *)mbuf_buf(mb), &length);
	}
	else if (ptype == PKT_TYPE_RTP || ptype == PKT_TYPE_ZRTP) {
		s = zrtp_process_srtp(st->zrtp_stream,
			    (char *)mbuf_buf(mb), &length);
	}
	else
		return false;

	if (s != zrtp_status_ok) {

		if (s == zrtp_status_drop)
			return true;

		warning("zrtp: recv(port=%d): zrtp_process_%s: %d '%s'\n",
			sa_port(src), proto_name, s, zrtp_log_status2str(s));
		return false;
	}

	mb->end = mb->pos + length;

	return false;
}


static int sig_hash_encode(struct zrtp_stream_t *stream,
                         struct sdp_media *m)
{
	char buf[ZRTP_SIGN_ZRTP_HASH_LENGTH + 1];
	zrtp_status_t s;
	int err = 0;

	s = zrtp_signaling_hash_get(stream, buf, sizeof(buf));
	if (s != zrtp_status_ok) {
		warning("zrtp: zrtp_signaling_hash_get: status = %d\n", s);
		return EINVAL;
	}

	err = sdp_media_set_lattr(m, true, "zrtp-hash", "%s %s",
	                          ZRTP_PROTOCOL_VERSION, buf);
	if (err) {
		warning("zrtp: sdp_media_set_lattr: %d\n", err);
	}

	return err;
}


static void sig_hash_decode(struct zrtp_stream_t *stream,
                           const struct sdp_media *m)
{
	const char *attr_val;
	struct pl major, minor, hash;
	uint32_t version;
	int err;
	zrtp_status_t s;

	attr_val = sdp_media_rattr(m, "zrtp-hash");
	if (!attr_val)
		return;

	err = re_regex(attr_val, strlen(attr_val),
	               "[0-9]+.[0-9]2 [0-9a-f]+",
	               &major, &minor, &hash);
	if (err || hash.l < ZRTP_SIGN_ZRTP_HASH_LENGTH) {
		warning("zrtp: malformed zrtp-hash attribute, ignoring...\n");
		return;
	}

	version = pl_u32(&major) * 100 + pl_u32(&minor);
	/* more version checks? */
	if (version < 110) {
		warning("zrtp: zrtp-hash: version (%d) is too low, "
		        "ignoring...", version);
	}

	s = zrtp_signaling_hash_set(stream, hash.p, (uint32_t)hash.l);
	if (s != zrtp_status_ok)
		warning("zrtp: zrtp_signaling_hash_set: status = %d\n", s);
}


static int session_alloc(struct menc_sess **sessp, struct sdp_session *sdp,
			 bool offerer, menc_event_h *eventh,
			 menc_error_h *errorh, void *arg)
{
	struct menc_sess *st;
	zrtp_status_t s;
	int err = 0;
	(void)offerer;

	if (!sessp || !sdp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), session_destructor);
	if (!st)
		return ENOMEM;

	st->eventh = eventh;
	st->errorh = errorh;
	st->arg = arg;
	st->err = 0;
	tmr_init(&st->abort_timer);

	s = zrtp_session_init(zrtp_global, NULL, zid,
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
		       struct udp_sock *rtpsock, struct udp_sock *rtcpsock,
		       const struct sa *raddr_rtp,
		       const struct sa *raddr_rtcp,
		       struct sdp_media *sdpm,
		       const struct stream *strm)
{
	struct menc_media *st;
	zrtp_status_t s;
	int layer = 10; /* above zero */
	int err = 0;
	(void)raddr_rtp;
	(void)raddr_rtcp;

	if (!stp || !sess)
		return EINVAL;

	st = *stp;
	if (st)
		goto start;

	st = mem_zalloc(sizeof(*st), media_destructor);
	if (!st)
		return ENOMEM;

	st->sess = sess;
	st->strm = strm;
	if (rtpsock) {
		st->rtpsock = mem_ref(rtpsock);
		err |= udp_register_helper(&st->uh_rtp, rtpsock, layer,
				  udp_helper_send, udp_helper_recv, st);
	}
	if (rtcpsock && (rtcpsock != rtpsock)) {
		st->rtcpsock = mem_ref(rtcpsock);
		err |= udp_register_helper(&st->uh_rtcp, rtcpsock, layer,
				  udp_helper_send, udp_helper_recv, st);
	}
	if (err)
		goto out;

	s = zrtp_stream_attach(sess->zrtp_session, &st->zrtp_stream);
	if (s != zrtp_status_ok) {
		warning("zrtp: zrtp_stream_attach failed (status=%d)\n", s);
		err = EPROTO;
		goto out;
	}

	zrtp_stream_set_userdata(st->zrtp_stream, st);

	if (use_sig_hash) {
		err = sig_hash_encode(st->zrtp_stream, sdpm);
		if (err)
			goto out;
	}

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

		if (use_sig_hash)
			sig_hash_decode(st->zrtp_stream, sdpm);

		s = zrtp_stream_start(st->zrtp_stream, rtp_sess_ssrc(rtp));
		if (s != zrtp_status_ok) {
			warning("zrtp: zrtp_stream_start: status = %d\n", s);
		}
	}

	return err;
}


static int on_send_packet(const zrtp_stream_t *stream,
			  char *rtp_packet,
			  unsigned int rtp_packet_length)
{
	struct menc_media *st = zrtp_stream_get_userdata(stream);
	struct mbuf *mb;
	int err;

	if (drop_packets(st))
		return zrtp_status_ok;

	if (!sa_isset(&st->raddr, SA_ALL))
		return zrtp_status_ok;

	mb = mbuf_alloc(PRESZ + rtp_packet_length);
	if (!mb)
		return zrtp_status_alloc_fail;

	mb->pos = PRESZ;
	(void)mbuf_write_mem(mb, (void *)rtp_packet, rtp_packet_length);
	mb->pos = PRESZ;

	err = udp_send_helper(st->rtpsock, &st->raddr, mb, st->uh_rtp);
	if (err) {
		warning("zrtp: udp_send %u bytes (%m)\n",
			rtp_packet_length, err);
	}

	mem_deref(mb);

	return zrtp_status_ok;
}


static void on_zrtp_secure(zrtp_stream_t *stream)
{
	const struct menc_media *st = zrtp_stream_get_userdata(stream);
	const struct menc_sess *sess = st->sess;
	zrtp_session_info_t sess_info;
	char buf[128] = "";

	zrtp_session_get(sess->zrtp_session, &sess_info);
	if (!sess_info.sas_is_verified && sess_info.sas_is_ready) {
		info("zrtp: verify SAS <%s> <%s> for remote peer %w"
		     " (type /zrtp_verify %w to verify)\n",
		     sess_info.sas1.buffer,
		     sess_info.sas2.buffer,
		     sess_info.peer_zid.buffer,
		     (size_t)sess_info.peer_zid.length,
		     sess_info.peer_zid.buffer,
		     (size_t)sess_info.peer_zid.length);
		if (sess->eventh) {
			if (re_snprintf(buf, sizeof(buf), "%s,%s,%w",
					sess_info.sas1.buffer,
					sess_info.sas2.buffer,
					sess_info.peer_zid.buffer,
					(size_t)sess_info.peer_zid.length))
				(sess->eventh)(MENC_EVENT_VERIFY_REQUEST,
					       buf,
					       (struct stream *)st->strm,
					       sess->arg);
			else
				warning("zrtp: failed to print verify "
					" arguments\n");
		}
	}
	else if (sess_info.sas_is_verified) {
		info("zrtp: secure session with verified remote peer %w\n",
		     sess_info.peer_zid.buffer,
		     (size_t)sess_info.peer_zid.length);
		if (sess->eventh) {
			if (re_snprintf(buf, sizeof(buf), "%w",
					sess_info.peer_zid.buffer,
					(size_t)sess_info.peer_zid.length))
				(sess->eventh)(MENC_EVENT_PEER_VERIFIED,
					       buf,
					       (struct stream *)st->strm,
					       sess->arg);
			else
				warning("zrtp: failed to print verified "
					" argument\n");
		}
	}
}


static void on_zrtp_security_event(zrtp_stream_t *stream,
                                   zrtp_security_event_t event)
{
	debug("zrtp: got security_event '%u'\n", event);

	if (event == ZRTP_EVENT_WRONG_SIGNALING_HASH) {
		const struct menc_media *st = zrtp_stream_get_userdata(stream);

		warning("zrtp: Attack detected!!! Signaling hash from the "
		        "zrtp-hash SDP attribute doesn't match the hash of "
		        "the Hello message. Aborting the call.\n");

		/* As this was called from zrtp_process_xxx(), we need
		   a safe shutdown. */
		abort_call(st->sess);
	}
}


static struct menc menc_zrtp = {
	.id        = "zrtp",
	.sdp_proto = "RTP/AVP",
	.sessh     = session_alloc,
	.mediah    = media_alloc
};


static int cmd_sas(int verify, struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	(void)pf;

	if (str_isset(carg->prm)) {
		char rzid[ZRTP_STRING16] = "";
		zrtp_status_t s;
		zrtp_string16_t local_zid = ZSTR_INIT_EMPTY(local_zid);
		zrtp_string16_t remote_zid = ZSTR_INIT_EMPTY(remote_zid);

		zrtp_zstrncpyc(ZSTR_GV(local_zid), (const char*)zid,
			       sizeof(zrtp_zid_t));

		if (str_len(carg->prm) != 24) {
			warning("zrtp: invalid remote ZID (%s)\n", carg->prm);
			return EINVAL;
		}

		(void) str2hex(carg->prm, (int) str_len(carg->prm),
			       rzid, sizeof(rzid));
		zrtp_zstrncpyc(ZSTR_GV(remote_zid), (const char*)rzid,
			       sizeof(zrtp_zid_t));

		s = zrtp_verified_set(zrtp_global, &local_zid, &remote_zid,
				      verify);
		if (s == zrtp_status_ok)
			if (verify)
				info("zrtp: SAS for peer %s verified\n",
				     carg->prm);
			else
				info("zrtp: SAS for peer %s unverified\n",
				     carg->prm);
		else {
			warning("zrtp: zrtp_verified_set"
				" failed (status = %d)\n", s);
			return EINVAL;
		}
	}

	return 0;
}


static int verify_sas(struct re_printf *pf, void *arg)
{
	return cmd_sas(true, pf, arg);
}


static int unverify_sas(struct re_printf *pf, void *arg)
{
	return cmd_sas(false, pf, arg);
}


static void zrtp_log(int level, char *data, int len, int offset)
{
	(void)offset;

	if (level == 1) {
		warning("%b\n", data, len);
	}
	else if (level == 2) {
		info("%b\n", data, len);
	}
	else {
		debug("%b\n", data, len);
	}
}


static const struct cmd cmdv[] = {
	{"zrtp_verify", 0, CMD_PRM, "Verify ZRTP SAS <remote ZID>",
		verify_sas },
	{"zrtp_unverify", 0, CMD_PRM, "Unverify ZRTP SAS <remote ZID>",
		unverify_sas },
};


static int module_init(void)
{
	zrtp_status_t s;
	char config_path[256] = "";
	char zrtp_zid_path[256] = "";
	FILE *f;
	int ret, err;

	(void)conf_get_bool(conf_cur(), "zrtp_hash", &use_sig_hash);

	zrtp_log_set_log_engine(zrtp_log);

	zrtp_config_defaults(&zrtp_config);

	str_ncpy(zrtp_config.client_id, "baresip/zrtp",
		 sizeof(zrtp_config.client_id));

	zrtp_config.lic_mode = ZRTP_LICENSE_MODE_UNLIMITED;

	zrtp_config.cb.misc_cb.on_send_packet = on_send_packet;
	zrtp_config.cb.event_cb.on_zrtp_secure = on_zrtp_secure;
	zrtp_config.cb.event_cb.on_zrtp_security_event =
	        on_zrtp_security_event;

	err = conf_path_get(config_path, sizeof(config_path));
	if (err) {
		warning("zrtp: could not get config path: %m\n", err);
		return err;
	}
	ret = re_snprintf(zrtp_config.def_cache_path.buffer,
			  zrtp_config.def_cache_path.max_length,
			  "%s/zrtp_cache.dat", config_path);
	if (ret < 0) {
		warning("zrtp: could not write cache path\n");
		return ENOMEM;
	}
	zrtp_config.def_cache_path.length = ret;

	if (re_snprintf(zrtp_zid_path,
			sizeof(zrtp_zid_path),
			"%s/zrtp_zid", config_path) < 0)
		return ENOMEM;
	if ((f = fopen(zrtp_zid_path, "rb")) != NULL) {
		if (fread(zid, sizeof(zid), 1, f) != 1) {
			if (feof(f) || ferror(f)) {
				warning("zrtp: invalid zrtp_zid file\n");
			}
		}
	}
	else if ((f = fopen(zrtp_zid_path, "wb")) != NULL) {
		rand_bytes(zid, sizeof(zid));
		if (fwrite(zid, sizeof(zid), 1, f) != 1) {
			warning("zrtp: zrtp_zid file write failed\n");
		}
		info("zrtp: generated new persistent ZID (%s)\n",
		     zrtp_zid_path);
	}
	else {
		err = errno;
		warning("zrtp: fopen() %s (%m)\n", zrtp_zid_path, err);
	}
	if (f)
		(void) fclose(f);

	s = zrtp_init(&zrtp_config, &zrtp_global);
	if (zrtp_status_ok != s) {
		warning("zrtp: zrtp_init() failed (status = %d)\n", s);
		return ENOSYS;
	}

	menc_register(baresip_mencl(), &menc_zrtp);

	debug("zrtp:  cache_file:  %s\n", zrtp_config.def_cache_path.buffer);
	debug("       zid_file:    %s\n", zrtp_zid_path);
	debug("       zid:         %w\n",
	      zid, sizeof(zid));

	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	cmd_unregister(baresip_commands(), cmdv);
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
