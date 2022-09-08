/**
 * @file rtspd.c RTSP server module
 *
 * Copyright (C) 2019 Christoph Huber
 */


#include <re.h>
#include <string.h>
#include <baresip.h>

#include "onvif_auth.h"
#include "filter.h"
#include "rtspd.h"
#include "fakevideo.h"
#include "pl.h"

#define DEBUG_MODULE "rtspd"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static const char uri_audioback[] = "backchannel";
static const char uri_audioplay[] = "trackID=1";
static const char uri_videoplay[] = "trackID=0";

struct rtsp_sock *rtspsock;

static struct list			rtsp_session_l;

static void rtsp_session_destructor(void *arg)
{
	struct rtsp_session *sess = arg;

	tmr_cancel(&sess->timer);
	list_flush(&sess->rtsp_stream_l);
	list_unlink(&sess->le);
}

static void rtsp_stream_destructor(void *arg)
{
	struct rtsp_stream *stream = arg;

	switch (stream->type) {
		case STREAMT_AUDIO:
			onvif_aufilter_audio_send_stop(stream->fs);
			break;

		case STREAMT_ABACK:
			onvif_aufilter_audio_recv_stop(stream->fs);
			break;

		case STREAMT_VIDEO:
			onvif_fakevideo_stop(stream->fvs);
			break;

		default:
			return;
	}

	mem_deref(stream->fs); /* check here for memory problem !!!!! */
	list_unlink(&stream->le);
}


/*UTILITY FUNCTIONS----------------------------------------------------------*/
/**
 * Decode the requested media resource
 * (only supported backchannel, trackID=0, trackID=1)
 *
 * @param msg               message object
 * @param stream            stream object
 *
 * @return                  0 if success, else errorcode
 */
static int decode_resource(const struct rtsp_msg *msg,
	struct rtsp_stream *stream)
{
	if (pl_strstr(&msg->path, uri_audioplay)) {
		stream->type = STREAMT_AUDIO;
	}
	else if (pl_strstr(&msg->path, uri_videoplay)) {
		stream->type = STREAMT_VIDEO;
	}
	else if (pl_strstr(&msg->path, uri_audioback)) {
		stream->type = STREAMT_ABACK;
	}
	else {
		stream->type = STREAMT_MAX;
		return EINVAL;
	}

	return 0;
}


/**
 * Decode the port numbers of the Transport header field.
 *
 * @param pos              start of the port numbers
 * @param end              end of the port number
 * @param port1            ptr for port 1 (RTP)
 * @param port2            prt for port 2 (RTCP)
 *
 * @return                 0 if success, else errorcode
 */
static int decode_transport_ports(const char *pos, const char *end,
	uint16_t *port1, uint16_t *port2)
{
	const char *tmp = pos;
	struct pl port;

	for (; tmp; tmp++) {
		if (*tmp == '-') {
			pl_set_n_str(&port, pos, (tmp - pos));
			pos = tmp + 1;
			*port1 = pl_u32(&port);
		}

		if (*tmp == ';' || *tmp == '\r') {
			pl_set_n_str(&port, pos, (tmp - pos));
			pos = tmp;
			*port2 = pl_u32(&port);
			return 0;
		}

		if (tmp > end)
			break;
	}

	return ENOSTR;
}


/**
 * Decode the Transport header field of @msg to get the information about
 * RTP via UDP / RTP via RTSP|TCP and which ports.
 *
 * @param msg              message object
 * @param stream           stream object
 *
 * @return                 0 if success, else errorcode
 */
static int decode_transport_hdr(const struct rtsp_msg *msg,
	struct rtsp_stream *stream)
{
	int err;
	uint32_t hdr_count;
	uint16_t port1 = 0, port2 = 0;
	const struct rtsp_hdr *hdr;

	hdr_count = rtsp_msg_hdr_count(msg, RTSP_HDR_TRANSPORT);
	while (hdr_count) {
		const char *pos;
		hdr = rtsp_msg_hdr(msg, RTSP_HDR_TRANSPORT);
		if (!hdr)
			return EINVAL;

		pos = pl_strstr(&hdr->val, "interleaved");
		if (pos) {
			/*TCP*/
			pos += 11 + 1;
			err = decode_transport_ports(pos, hdr->val.p +
						     hdr->val.l,
						     &port1, &port2);
			if (err)
				return err;

			stream->proto = IPPROTO_TCP;
			break;
		}

		pos = pl_strstr(&hdr->val, "client_port");
		if (pos) {
			/*UDP*/
			pos += 11 + 1;
			err = decode_transport_ports(pos, hdr->val.p +
						     hdr->val.l,
						     &port1, &port2);
			if (err)
				return err;

			stream->proto = IPPROTO_UDP;
			break;
		}

		--hdr_count;
		hdr = hdr->le.next->data;
	}

	stream->rtp_port = port1;
	stream->rtcp_port = port2;
	return 0;
}


static bool session_cmp(struct le *le, void *arg)
{
	struct rtsp_session *sess = le->data;
	const char *p = arg;

	return (memcmp(&sess->session[0], p, (SESSBYTES - 1)) == 0);
}


static bool stream_cmp_ilch(struct le *le, void *arg)
{
	struct rtsp_stream *stream = le->data;
	uint16_t *ilch = arg;

	return (stream->rtp_port == *ilch);
}


/**
 * Search for a Sesion object which contains the stream with given @ilch
 *
 * @param ilch          interleaved data channel id
 *
 * @return              session data, Null if not found
 */
static struct rtsp_session *get_session_from_ilch(uint16_t ilch)
{
	struct le *le1, *le2;
	struct rtsp_session *sess;

	LIST_FOREACH(&rtsp_session_l, le1) {
		sess = le1->data;
		le2 = list_apply(&sess->rtsp_stream_l, true,
			stream_cmp_ilch, (void*) &ilch);
		if (le2)
			return sess;
	}

	return NULL;
}


/**
 * Search for a Stream object in the Session with given @ilch
 *
 * @param sess          session object
 * @param ilch          interleaved data channel id
 *
 * @return              stream data, Null if not found
 */
static struct rtsp_stream *get_stream_from_ilch(struct rtsp_session *sess,
	uint16_t ilch)
{
	struct le *le1;

	LIST_FOREACH(&sess->rtsp_stream_l, le1) {
		if (stream_cmp_ilch(le1, &ilch))
			return le1->data;
	}

	return NULL;
}


/**
 * Search for a Session containing the given SessionID via @msg
 *
 * @param msg          rtsp message
 *
 * @return             session data, Null if not found
 */
static struct rtsp_session *get_session_from_hdr(const struct rtsp_msg *msg)
{
	const struct rtsp_hdr *hdr;
	struct le *le;

	hdr = rtsp_msg_hdr(msg, RTSP_HDR_SESSION);
	if (!hdr)
		return NULL;

	le = list_apply(&rtsp_session_l, true, session_cmp,
			(void *)hdr->val.p);
	if (!le)
		return NULL;

	return le->data;
}


/**
 * Session Timeout Handler. Free Session
 *
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static void sess_timeout_handler(void *arg)
{
	struct rtsp_session *sess = arg;

	mem_deref(sess);
}


/**
 *  Check the @msg for a Session header field. And renew the timer of the
 *  session
 *
 * @param msg          request message
 */
static void timeout_renewer(const struct rtsp_msg *msg)
{
	struct rtsp_session *session = NULL;

	session = get_session_from_hdr(msg);
	if (!session)
		return;

	tmr_start(&session->timer, (session->timeout * 1000),
		sess_timeout_handler, session);
	return;
}
/*REQUEST HANDLER------------------------------------------------------------*/
/**
 * handle RTSP Options Request messages
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param ver           version number of RTSP
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static int rtsp_req_options_h(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, const int ver, void *arg)
{
	int err;

	(void)arg;

	err = rtsp_reply(conn, ver, 200, "OK",
		"CSeq: %u\r\n"
		"Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN,"
		" GET_PARAMETER, "
		"SET_PARAMETER, REDIRECT, %s\r\n"
		"Server: baresip_onvif module /0.1\r\n"
		"\r\n", msg->cseq, (ver == 1 ? "ANNOUNCE, RECORD" :
					       "PLAY_NOTIFY"));

	return err;
}


/**
 * handle RTSP Announce Request messages
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param ver           version number of RTSP
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static int rtsp_req_announce_h(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, const int ver, void *arg)
{
	int err;
	bool is_sdp;
	struct sdp_session *sdpsess = NULL;
	struct sdp_media   *sdpmedia = NULL;
	const struct list *media_l;
	struct le *media_le;
	struct sa laddr;

	(void)arg;

	is_sdp = msg_ctype_cmp(&msg->ctype, "application", "sdp");
	if (!is_sdp) {
		warning("RTSPD: %s announce request contains not "
			"application/sdp\n", __func__);
		return EINVAL;
	}

	if (msg->clen > mbuf_get_left(msg->mb))
		return EOVERFLOW;

	err =sa_set_str(&laddr, "0.0.0.0", 8554);
	if (err)
		return err;

	err = sdp_session_alloc(&sdpsess, &laddr);
	if (err)
		return err;

	err = sdp_decode(sdpsess, msg->mb, true);
	if (err)
		goto out;

	media_l = sdp_session_medial(sdpsess, false);
	if (list_count(media_l) <= 0) {
		err = EINVAL;
		goto out;
	}

	for (media_le = media_l->head; media_le; media_le = media_le->next) {
		sdpmedia = media_le->data;
		if ((0 == strncmp(sdp_media_name(sdpmedia), "audio",
			strlen("audio"))) &&
			0 == strncmp(sdp_media_proto(sdpmedia), "RTP/AVP",
			strlen("RTP/AVP")) && sdp_media_rport(sdpmedia) == 0) {
			err = 0;
			goto out;
		}
		else {
			warning("RTSPD %s %s not supported\n", __func__,
				sdp_media_name(sdpmedia));
			err = ENOTSUP;
			goto out;
		}
	}

 out:
	if (err) {
		err = rtsp_reply(conn, ver, 404, "Not Found",
			"CSeq: %u\r\n"
			"Server: baresip_onvif module /0.1\r\n"
			"\r\n", msg->cseq);
	}
	else {
		err = rtsp_reply(conn, ver, 200, "OK",
			"CSeq: %u\r\n"
			"Server: baresip_onvif module /0.1\r\n"
			"\r\n", msg->cseq);
	}

	mem_deref(sdpsess);
	return err;
}


/**
 * handle RTSP Describe Request messages
 *
 * Transmit the information about the avialable streams (via SDP)
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param ver           version number of RTSP
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static int rtsp_req_describe_h(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, const int ver, void *arg)
{
	int err;
	struct sdp_session  *sdpsession = NULL;
	struct sdp_media    *media = NULL;
	struct sdp_format   *format = NULL;
	struct mbuf         *sdppackage = NULL;
	const struct sa     *laddr;
	bool fake_video_enabled = true;

	(void)arg;

	if (conf_get_bool(conf_cur(), "onvif_FakeVideoEnabled",
		&fake_video_enabled)) {
		warning("%s: onvif_FakeVideoEnabled field in config not found."
			"Use default: FakeVideo Enabled.\n", DEBUG_MODULE);
	}

	if (!rtsp_msg_hdr_has_value(msg, RTSP_HDR_ACCEPT, "application/sdp")) {
		warning ("%s Accept Header not found or not "
			 "\"application/sdp\"\n", __func__);
		return EINVAL;
	}

	laddr = net_laddr_af(baresip_network(), AF_INET);
	err = sdp_session_alloc(&sdpsession, laddr);
	if (err)
		goto out;

	/* Disable and Enable of the Fake Videosource */
	if (fake_video_enabled) {
		/* RTSP VIDEO Session element */
		err = sdp_media_add(&media, sdpsession, sdp_media_video, 0,
			sdp_proto_rtpavp);
		sdp_media_set_ldir(media, SDP_RECVONLY);
		err |= sdp_media_set_lattr(media, true, "control",
					   uri_videoplay);
		err |= sdp_format_add(&format, media, false, "26", "JPEG",
				      90000, 1, NULL, NULL, NULL, false, NULL);
	}

	/* RTSP AUDIO PLAY */
	err = sdp_media_add(&media, sdpsession, sdp_media_audio, 0,
		sdp_proto_rtpavp);
	sdp_media_set_ldir(media, SDP_RECVONLY);
	err |= sdp_media_set_lattr(media, true, "control",
		uri_audioplay);
	err |= sdp_format_add(&format, media, false, "0", "PCMU", 8000, 1,
			      NULL, NULL, NULL, false, NULL);
	if (err)
		goto out;

	/* RTSP RECORD FUNCTION */
	err = sdp_media_add(&media, sdpsession, sdp_media_audio, 0,
		sdp_proto_rtpavp);
	sdp_media_set_ldir(media, SDP_SENDONLY);
	err |= sdp_media_set_lattr(media, true, "control",
		uri_audioback);
	err |= sdp_format_add(&format, media, false, "0", "PCMU", 8000, 1,
			      NULL, NULL, NULL, false, NULL);
	if (err)
		goto out;

	err = sdp_encode(&sdppackage, sdpsession, true);
	if (err)
		goto out;

	err = rtsp_creply(conn, ver, 200, "OK", "application/sdp", sdppackage,
			  "CSeq: %u\r\n"
			  "Date: %H\r\n", msg->cseq, fmt_gmtime, NULL);

 out:
	mem_deref(sdpsession);
	mem_deref(sdppackage);

	return err;
}


/**
 * handle RTSP Setup Request messages
 *
 * Setup a RTSP streaming session. Allocation of the audio pipeline
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param ver           version number of RTSP
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static int rtsp_req_setup_h(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, const int ver, void *arg)
{
	struct rtsp_session *sess = NULL;
	struct rtsp_session *sess_check = NULL;
	struct rtsp_stream *stream = NULL;
	uint16_t ildch = 0x00;
	int err = 0;

	(void)arg;

	sess = get_session_from_hdr(msg);
	if (!sess)
		sess = mem_zalloc(sizeof(*sess), rtsp_session_destructor);

	if (!sess) {
		err = ENOMEM;
		goto out;
	}

	stream = mem_zalloc(sizeof(*stream), rtsp_stream_destructor);
	if (!stream) {
		err = ENOMEM;
		goto out;
	}

	stream->fs = NULL;
	stream->fvs = NULL;

	err = decode_resource(msg, stream);
	err |= decode_transport_hdr(msg, stream);

	sess_check = get_session_from_ilch(stream->rtp_port);
	ildch = stream->rtp_port;
	while (sess_check) {
		ildch += 2;
		sess_check = get_session_from_ilch(ildch);
	}
	stream->rtp_port = ildch;
	stream->rtcp_port = ildch + 1;

	if (err)
		goto out;

	switch (stream->type) {
		case STREAMT_AUDIO:
			sa_cpy(&stream->tar, rtsp_conn_peer(conn));
			sa_set_port(&stream->tar, stream->rtp_port);
			err = onvif_aufilter_stream_alloc(&stream->fs, 8000, 1,
							  "PCMU");
			break;

		case STREAMT_ABACK:
			sa_cpy(&stream->tar, rtsp_conn_peer(conn));
			sa_set_port(&stream->tar, stream->rtp_port);
			err = onvif_aufilter_stream_alloc(&stream->fs, 8000, 1,
							  "PCMU");
			break;

		case STREAMT_VIDEO:
			sa_cpy(&stream->tar, rtsp_conn_peer(conn));
			sa_set_port(&stream->tar, stream->rtp_port);
			err = onvif_fakevideo_alloc(&stream->fvs, "JPEG");
			break;

		default:
			err = ENOTSUP;
			goto out;
	}

	if (err) {
		warning ("%s: Type (%d), Can not allocate filter stream "
			 "info(%m)\n", DEBUG_MODULE, err);
		goto out;
	}

	if (conf_get_u32(conf_cur(), "rtsp_SessTimeout", &sess->timeout))
		sess->timeout = 60;

	tmr_start(&sess->timer, (sess->timeout * 1000), sess_timeout_handler,
		  sess);
	list_append(&sess->rtsp_stream_l, &stream->le, stream);
	if (!list_contains(&rtsp_session_l, &sess->le)) {
		rand_str(sess->session, SESSBYTES);
		list_append(&rtsp_session_l, &sess->le, sess);
	}

	/*REPLY*/
	err = rtsp_reply(conn, ver, 200, "OK",
		"CSeq: %u\r\n"
		"Date: %H\r\n"
		"Session: %b;timeout=%u\r\n"
		"Transport: %s;%s;%s=%d-%d\r\n"
		"\r\n", msg->cseq, fmt_gmtime, NULL, sess->session,
		(SESSBYTES - 1), sess->timeout,
		stream->proto == IPPROTO_TCP ? "RTP/AVP/TCP" : "RTP/AVP",
		"unicast",
		stream->proto == IPPROTO_TCP? "interleaved" : "client_port",
		stream->rtp_port, stream->rtcp_port);

 out:
	if (err) {
		warning ("%s: steam setup %m\n", __func__, err);
		mem_deref(sess);
		mem_deref(stream);
	}

	return err;
}


/**
 * handle RTSP Play Request messages
 *
 * Start the data transmission over the audio pipeline
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param ver           version number of RTSP
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static int rtsp_req_play_h(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, const int ver, void *arg)
{
	struct rtsp_session *sess = NULL;
	struct rtsp_stream *stream = NULL;
	struct le *le;
	int err;

	(void)arg;

	sess = get_session_from_hdr(msg);
	if (!sess)
		return EINVAL;

	LIST_FOREACH(&sess->rtsp_stream_l, le) {
		stream = le->data;
		switch (stream->type) {
			case STREAMT_AUDIO:
				err = onvif_aufilter_audio_send_start(
					stream->fs,
					&stream->tar, conn, stream->proto);
				break;

			case STREAMT_ABACK:
				sa_set_str(&stream->tar, "0.0.0.0", sa_port(&stream->tar));
				err = onvif_aufilter_audio_recv_start(
					stream->fs, &stream->tar,
					stream->proto);
				break;

			case STREAMT_VIDEO:
				err = onvif_fakevideo_start(stream->fvs,
					stream->proto,
					&stream->tar, conn);
				break;

			default:
				return ENOTSUP;
		}
	}

	err = rtsp_reply(conn, ver, 200, "OK",
		"CSeq: %u\r\n"
		"Date: %H\r\n"
		"Session: %b\r\n"
		"\r\n", msg->cseq, fmt_gmtime, NULL, sess->session,
		(SESSBYTES - 1));

	return err;
}


/**
 * handle RTSP Pause Request messages
 *
 * This message stops the streams of a session currently but the
 * session is still valid
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param ver           version number of RTSP
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static int rtsp_req_pause_h(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, const int ver, void *arg)
{
	struct rtsp_session *sess = NULL;
	struct rtsp_stream *stream = NULL;
	struct le *le;
	int err;

	(void)arg;

	sess = get_session_from_hdr(msg);
	if (!sess)
		return EINVAL;

	LIST_FOREACH(&sess->rtsp_stream_l, le) {
		stream = le->data;
		switch (stream->type) {
			case STREAMT_AUDIO:
				onvif_aufilter_audio_send_stop(stream->fs);
				break;

			case STREAMT_ABACK:
				onvif_aufilter_audio_recv_stop(stream->fs);
				break;

			case STREAMT_VIDEO:
				onvif_fakevideo_stop(stream->fvs);
				break;

			default:
				return ENOTSUP;
		}
	}

	err = rtsp_reply(conn, ver, 200, "OK",
		"CSeq: %u\r\n"
		"Date: %H\r\n"
		"Session: %b\r\n"
		"\r\n", msg->cseq, fmt_gmtime, NULL, sess->session,
		(SESSBYTES - 1));

	return err;
}


/**
 * handle RTSP Teardown Request messages
 *
 * This message shut down a hole session
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param ver           version number of RTSP
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static int rtsp_req_teardown_h(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, const int ver, void *arg)
{
	struct rtsp_session *sess = NULL;
	int err;

	(void)arg;

	sess = get_session_from_hdr(msg);
	if (!sess)
		return EINVAL;

	mem_deref(sess);
	err = rtsp_reply(conn, ver, 200, "OK",
		"CSeq: %u\r\n"
		"Date: %H\r\n"
		"\r\n", msg->cseq, fmt_gmtime, NULL);

	return err;
}


/**
 * handle RTSP GetParameter Request messages
 * This is currently just a dummy function to reset the timeout
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param ver           version number of RTSP
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static int rtsp_req_gparam_h(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, const int ver, void *arg)
{
	int err;
	struct rtsp_session *sess = NULL;

	(void)arg;

	sess = get_session_from_hdr(msg);
	if (!sess)
		return EINVAL;

	err = rtsp_reply(conn, ver, 200, "OK",
		"CSeq: %u\r\n"
		"Date: %H\r\n"
		"Session: %b\r\n"
		"\r\n", msg->cseq, fmt_gmtime, NULL, sess->session,
		(SESSBYTES - 1));

	return err;
}

/**
 * handle RTSP Record Request messages
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param ver           version number of RTSP
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
static int rtsp_record_h(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, const int ver, void *arg)
{
	warning("%s not implemented\n", __func__);

	(void)conn;
	(void)msg;
	(void)ver;
	(void)arg;

	return 0;
}


/**
 * REQUEST HANDLER-------------------------------------------------------------
 * Splits the Request into its different methods
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (request msg)
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 *
 */
static int rtsp_req_handler(const struct rtsp_conn *conn,
	const struct rtsp_msg *msg, void *arg)
{
	int err = 0;
	uint8_t ver = 1;
	struct rtsp_digest_chall *chall;
	enum userlevel ul;
	bool auth_enabled = true;
	const struct sa *laddr;

	if (!conn || !msg)
		return EINVAL;

	if (!pl_strcmp(&msg->ver, "1.0"))
		ver = 1;
	else if (!pl_strcmp(&msg->ver, "2.0"))
		ver = 2;
	else
		err = EBADMSG;

	laddr =  net_laddr_af(baresip_network(), AF_INET);
	if (conf_get_bool(conf_cur(), "rtsp_AuthEnabled", &auth_enabled)) {
		warning("%s: rtsp_AuthEnabled field in config not found."
			"Use default: Auth Enabled.\n", DEBUG_MODULE);
	}

	if (auth_enabled) {
		ul = rtsp_digest_auth(msg);
		if (ul > UUSER) {
			err = EACCES;
			goto out;
		}
	}

	timeout_renewer(msg);

	if (!pl_strcmp(&msg->met, "OPTIONS")) {
		err = rtsp_req_options_h(conn, msg, ver, arg);
	}
	else if (!pl_strcmp(&msg->met, "DESCRIBE")) {
		err = rtsp_req_describe_h(conn, msg, ver, arg);
	}
	else if (!pl_strcmp(&msg->met, "ANNOUNCE")) { /*1.0*/
		err = rtsp_req_announce_h(conn, msg, ver, arg);
	}
	else if (!pl_strcmp(&msg->met, "SETUP")) {
		err = rtsp_req_setup_h(conn, msg, ver, arg);
	}
	else if (!pl_strcmp(&msg->met, "PLAY")) {
		err = rtsp_req_play_h(conn, msg, ver, arg);
	}
	else if (!pl_strcmp(&msg->met, "PLAY_NOTIFY")) { /*2.0*/

	}
	else if (!pl_strcmp(&msg->met, "PAUSE")) {
		err = rtsp_req_pause_h(conn, msg, ver, arg);
	}
	else if (!pl_strcmp(&msg->met, "TEARDOWN")) {
		err = rtsp_req_teardown_h(conn, msg, ver, arg);
	}
	else if (!pl_strcmp(&msg->met, "GET_PARAMETER")) {
		err = rtsp_req_gparam_h(conn, msg, ver, arg);
	}
	else if (!pl_strcmp(&msg->met, "SET_PARAMETER")) {

	}
	else if (!pl_strcmp(&msg->met, "REDIRECT")) {

	}
	else if (!pl_strcmp(&msg->met, "RECORD")) { /*1.0*/
		err = rtsp_record_h(conn, msg, ver, arg);
	}
	else {
		err = ENOTSUP;
	}

  out:
	if (err == EACCES) {
		DEBUG_NOTICE("Create A Response with WWW-Authenticate "
			     "Header\n");
		err = rtsp_digest_auth_chall(conn, &chall);
		err = rtsp_reply(conn, ver, 401, "Unauthorized",
			"CSeq: %u\r\n"
			"WWW-Authenticate: Digest realm=\"%j/%r\","
			"nonce=\"%r\",opaque=\"%r\",algorithm=\"%r\","
			"qop=\"%r\"\r\n\r\n",
			msg->cseq, laddr, &chall->param.realm,
			&chall->param.nonce, &chall->param.opaque,
			&chall->param.algorithm, &chall->param.qop);

		mem_deref(chall);
	}


	return err;
}


/**
 * RESPONSE HANDLER------------------------------------------------------------
 * A response handler is currently not implmented
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (response package)
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 *
 */
static int rtsp_res_handler(struct rtsp_conn *conn, const struct rtsp_msg *msg,
	void *arg)
{
	int err = ENOTSUP;

	(void)conn;
	(void)msg;
	(void)arg;

	warning("%s not implemented\n", __func__);
	return err;
}


/**
 * ILD HANDLER-----------------------------------------------------------------
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (interleaved data package)
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 *
 */
static int rtsp_ild_handler(struct rtsp_conn *conn, const struct rtsp_msg *msg,
	void *arg)
{
	struct rtsp_session *sess;
	struct rtsp_stream *stream;
	int err = 0;

	(void)conn;
	(void)arg;

	sess = get_session_from_ilch(msg->channel);
	if (!sess) {
		warning ("%s Session containing IL channel %d not found\n",
			__func__, msg->channel);
		err = EINVAL;
		goto out;
	}

	stream = get_stream_from_ilch(sess, msg->channel);
	if (!stream) {
		warning ("%s Stream containing IL channel %d not found\n",
			__func__, msg->channel);
		err = EINVAL;
		goto out;
	}

	if (msg->channel == stream->rtp_port)
		onvif_aufilter_rtsp_wrapper(msg->mb, stream->fs);

 out:

	return err;
}


/**
 * MSG HANDLER-----------------------------------------------------------------
 * Decodes the msg type of the rtsp package
 *
 * @param conn          rtsp connection struct
 * @param msg           received rtsp message (interleaved data package)
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 *
 */
void rtsp_msg_handler(struct rtsp_conn *conn, const struct rtsp_msg *msg,
	void *arg)
{
	int err;

	switch (msg->mtype) {
		case RTSP_REQUEST:
			err = rtsp_req_handler(conn, msg, arg);
			break;

		case RTSP_RESPONSE:
			err = rtsp_res_handler(conn, msg, arg);
			break;

		case RTSP_ILD:
			err = rtsp_ild_handler(conn, msg, arg);
			break;

		default:
			err = ENOTSUP;
	}

	if (err)
		warning("%s handle %d err=(%m)\n", __func__, msg->mtype, err);

	return;
}


/**
 * Init RTSP global structs
 */
void rtsp_init(void)
{
}


/**
 * Deinit all RTSP session which are still running
 */
void rtsp_session_deinit(void)
{
	list_flush(&rtsp_session_l);
}
