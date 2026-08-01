/**
 * @file src/call.c  Call Control
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


/** Magic number */
#define MAGIC 0xca11ca11
#include "magic.h"


#define FOREACH_STREAM						\
	for (le = call->streaml.head; le; le = le->next)


/** SIP Call Control object */
struct call {
	MAGIC_DECL                /**< Magic number for debugging           */
	struct le le;             /**< Linked list element                  */
	const struct config *cfg; /**< Global configuration                 */
	struct ua *ua;            /**< SIP User-agent                       */
	struct account *acc;      /**< Account (ref.)                       */
	struct sipsess *sess;     /**< SIP Session                          */
	struct sdp_session *sdp;  /**< SDP Session                          */
	struct sipsub *sub;       /**< Call transfer REFER subscription     */
	struct sipnot *not;       /**< REFER/NOTIFY client                  */
	struct call *xcall;       /**< Cross ref Transfer call              */
	struct list streaml;      /**< List of mediastreams (struct stream) */
	struct audio *audio;      /**< Audio stream                         */
	struct video *video;      /**< Video stream                         */
#ifdef USE_DATACHANNEL
	struct data_context *data; /**< SCTP data-channel transport         */
	struct sdp_media *data_reject; /**< Rejected remote data m-line     */
	call_datachannel_h *data_channelh; /**< Incoming data channel        */
	void *data_channel_arg; /**< Incoming data-channel argument        */
	struct tmr data_notify_tmr; /**< Deferred negotiated-channel event */
#endif
	enum call_state state;    /**< Call state                           */
	int32_t adelay;           /**< Auto answer delay in ms              */
	char *aluri;              /**< Alert-Info URI                       */
	char *local_uri;          /**< Local SIP uri                        */
	char *local_name;         /**< Local display name                   */
	char *contact_uri;        /**< Peer Contact SIP Address             */
	char *peer_uri;           /**< Peer SIP Address                     */
	char *peer_name;          /**< Peer display name                    */
	struct sa msg_src;        /**< Peer message source address          */
	char *diverter_uri;       /**< Diverter SIP Address                 */
	char *id;                 /**< Cached session call-id               */
	char *replaces;           /**< Replaces parameter                   */
	uint16_t supported;       /**< Supported header tags                */
	struct tmr tmr_inv;       /**< Timer for incoming calls             */
	struct tmr tmr_dtmf;      /**< Timer for incoming DTMF events       */
	struct tmr tmr_answ;      /**< Timer for delayed answer             */
	struct tmr tmr_reinv;     /**< Timer for outgoing re-INVITES        */
	bool remote_hold;         /**< True if remote has placed us on hold */
	time_t time_start;        /**< Time when call started               */
	time_t time_conn;         /**< Time when call initiated             */
	time_t time_stop;         /**< Time when call stopped               */
	bool outgoing;            /**< True if outgoing, false if incoming  */
	bool answered;            /**< True if call has been answered       */
	bool got_offer;           /**< Got SDP Offer from Peer              */
	bool on_hold;             /**< True if call is on hold (local)      */
	bool ans_queued;          /**< True if an (auto) answer is queued   */
	bool progr_queued;        /**< True if a progress response is queued*/
	struct mnat_sess *mnats;  /**< Media NAT session                    */
	bool mnat_wait;           /**< Waiting for MNAT to establish        */
	struct menc_sess *mencs;  /**< Media encryption session state       */
	int af;                   /**< Preferred Address Family             */
	uint16_t scode;           /**< Termination status code              */
	call_event_h *eh;         /**< Event handler                        */
	call_dtmf_h *dtmfh;       /**< DTMF handler                         */
	void *arg;                /**< Handler argument                     */

	struct config_avt config_avt;    /**< AVT config                    */
	struct config_call config_call;  /**< Call config                   */

	uint32_t rtp_timeout_ms;  /**< RTP Timeout in [ms]                  */
	uint32_t linenum;         /**< Line number from 1 to N              */
	struct list custom_hdrs;  /**< List of custom headers if any        */

	enum sdp_dir estadir;      /**< Established audio direction         */
	enum sdp_dir estvdir;      /**< Established video direction         */
	bool use_video;
	bool use_rtp;
	struct pl *user_data;      /**< User data related to the call       */
	call_sip_info_h *sip_infoh;/**< SIP INFO handler for this call      */
	void *sip_info_arg;        /**< SIP INFO handler argument           */
};


#ifdef USE_DATACHANNEL
struct call_stream_description_state {
	struct le le;
	struct stream_jsep_state *state;
};


struct call_description_state {
	struct sdp_session_state *sdp;
	struct list streams;
	bool had_data;
	bool had_data_reject;
	bool data_started;
	bool data_committed;
	bool data_prepared;
	bool provisional;
	bool got_offer;
	bool remote_hold;
};
#endif


static int send_invite(struct call *call);
static int send_dtmf_info(struct call *call, char key);


static const char *state_name(enum call_state st)
{
	switch (st) {

	case CALL_STATE_IDLE:		 return "IDLE";
	case CALL_STATE_INCOMING:	 return "INCOMING";
	case CALL_STATE_OUTGOING:	 return "OUTGOING";
	case CALL_STATE_RINGING:	 return "RINGING";
	case CALL_STATE_EARLY:		 return "EARLY";
	case CALL_STATE_ESTABLISHED:	 return "ESTABLISHED";
	case CALL_STATE_TERMINATED:	 return "TERMINATED";
	case CALL_STATE_TRANSFER:	 return "TRANSFER";
	case CALL_STATE_UNKNOWN:	 return "UNKNOWN";
	default:			 return "???";
	}
}


static void set_state(struct call *call, enum call_state st)
{
	call->state = st;
}


static const struct sdp_format *sdp_media_rcodec(const struct sdp_media *m)
{
	const struct list *lst;
	struct le *le;

	if (!m || !sdp_media_rport(m))
		return NULL;

	lst = sdp_media_format_lst(m, false);

	for (le=list_head(lst); le; le=le->next) {

		const struct sdp_format *fmt = le->data;

		if (!fmt->sup)
			continue;

		if (!fmt->data)
			continue;

		return fmt;
	}

	return NULL;
}


static void call_timer_start(struct call *call)
{
	debug("call: timer started\n");
	tmr_cancel(&call->tmr_inv);
	call->time_start = time(NULL);
}


static void call_stream_stop(struct call *call)
{
	if (!call)
		return;

	call->time_stop = time(NULL);

	/* Audio */
	audio_stop(call->audio);

	/* Video */
	video_stop(call->video);

	tmr_cancel(&call->tmr_inv);
}


static void call_event_handler(struct call *call, enum call_event ev,
			       const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	if (!call || !call->eh)
		return;

	va_start(ap, fmt);
	(void)re_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	call->eh(call, ev, buf, call->arg);
}


static void invite_timeout(void *arg)
{
	struct call *call = arg;

	info("%s: Local timeout after %u seconds\n",
	     call->peer_uri, call->config_call.local_timeout);

	call_event_handler(call, CALL_EVENT_CLOSED, "Local timeout");
}


/* Called when all media streams are established */
static void mnat_handler(int err, uint16_t scode, const char *reason,
			 void *arg)
{
	struct call *call = arg;
	MAGIC_CHECK(call);

	if (err) {
		warning("call: medianat '%s' failed: %m\n",
			call->acc->mnatid, err);
		call_event_handler(call, CALL_EVENT_CLOSED, "%m", err);
		return;
	}
	else if (scode) {
		warning("call: medianat failed: %u %s\n", scode, reason);
		call_event_handler(call, CALL_EVENT_CLOSED, "%u %s",
				   scode, reason);
		return;
	}

	info("call: media-nat '%s' established/gathered\n",
	     call->acc->mnatid);

	/* Re-INVITE */
	if (!call->mnat_wait) {
		info("call: medianat established -- sending Re-INVITE\n");
		(void)call_modify(call);
		return;
	}

	call->mnat_wait = false;

	switch (call->state) {

	case CALL_STATE_OUTGOING:
		(void)send_invite(call);
		break;

	case CALL_STATE_INCOMING:
		call_event_handler(call, CALL_EVENT_INCOMING, "%s",
                                   call->peer_uri);
		break;

	default:
		break;
	}
}


static int call_apply_sdp(struct call *call)
{
	struct le *le;
	int err = 0;

	if (!call)
		return EINVAL;

	audio_sdp_attr_decode(call->audio);

	if (call->video)
		video_sdp_attr_decode(call->video);

	/* Update each stream */
	FOREACH_STREAM {
		struct stream *strm = le->data;

		err = stream_update(strm);
		if (err) {
			warning("call: stream '%s' update failed (%m)\n",
				stream_name(strm), err);
			return err;
		}

		if (stream_is_ready(strm)) {

			stream_start_rtcp(strm);
		}
	}

	if (call->acc->mnat && call->acc->mnat->updateh && call->mnats)
		err = call->acc->mnat->updateh(call->mnats);
#ifdef USE_DATACHANNEL
	if (!err && call->data)
		err = data_context_start(call->data);
#endif

	return err;
}


static int update_streams(struct call *call)
{
	int err = 0;

	if (!call)
		return EINVAL;

	if (stream_is_ready(audio_strm(call->audio)))
		err |= audio_update(call->audio);
	else
		audio_stop(call->audio);

	if (stream_is_ready(video_strm(call->video)))
		err |= video_update(call->video, call->peer_uri);
	else
		video_stop(call->video);

	return err;
}


#ifdef USE_DATACHANNEL
static void data_notify_handler(void *arg)
{
	struct call *call = arg;

	mem_ref(call);
	if (call->data)
		data_context_notify_channels(call->data, true);
	mem_deref(call);
}
#endif


int call_update_media(struct call *call)
{
	int err;

	err = call_apply_sdp(call);
	if (!err)
		err = update_streams(call);
#ifdef USE_DATACHANNEL
	if (!err && call->data)
		tmr_start(&call->data_notify_tmr, 0, data_notify_handler, call);
#endif

	return err;
}


static void print_summary(const struct call *call)
{
	uint32_t dur = call_duration(call);
	if (!dur)
		return;

	info("%s: Call with %s terminated (duration: %H)\n",
	     call->local_uri, call->peer_uri, fmt_human_time, &dur);
}


static void call_destructor(void *arg)
{
	struct call *call = arg;

	if (call->state != CALL_STATE_IDLE)
		print_summary(call);

	call_stream_stop(call);
	list_unlink(&call->le);
	tmr_cancel(&call->tmr_dtmf);
	tmr_cancel(&call->tmr_answ);
	tmr_cancel(&call->tmr_reinv);

	mem_deref(call->sess);
	mem_deref(call->id);
	mem_deref(call->local_uri);
	mem_deref(call->local_name);
	mem_deref(call->contact_uri);
	mem_deref(call->peer_uri);
	mem_deref(call->peer_name);
	mem_deref(call->replaces);
	mem_deref(call->aluri);
	mem_deref(call->diverter_uri);
	mem_deref(call->audio);
	mem_deref(call->video);
#ifdef USE_DATACHANNEL
	tmr_cancel(&call->data_notify_tmr);
	mem_deref(call->data);
	mem_deref(call->data_reject);
#endif
	mem_deref(call->sdp);
	mem_deref(call->mnats);
	mem_deref(call->mencs);
	mem_deref(call->sub);
	mem_deref(call->not);
	mem_deref(call->acc);
	mem_deref(call->user_data);

	list_flush(&call->custom_hdrs);
}


static void audio_event_handler(int key, bool end, void *arg)
{
	struct call *call = arg;
	MAGIC_CHECK(call);

	info("received in-band DTMF event: '%c' (end=%d)\n", key, end);

	if (call->dtmfh)
		call->dtmfh(call, end ? KEYCODE_REL : key, call->arg);
}


static void audio_level_handler(bool tx, double lvl, void *arg)
{
	struct call *call = arg;
	MAGIC_CHECK(call);

	bevent_call_emit(tx ? BEVENT_VU_TX : BEVENT_VU_RX, call,
			 "%.2f", lvl);
}


static void audio_error_handler(int err, const char *str, void *arg)
{
	struct call *call = arg;
	MAGIC_CHECK(call);

	if (err) {
		warning("call: audio device error: %m (%s)\n", err, str);

		bevent_call_emit(BEVENT_AUDIO_ERROR, call,
				 "%d,%s", err, str);

		call_stream_stop(call);
		call_event_handler(call, CALL_EVENT_CLOSED,
			"%s", str);
	}
	else
		bevent_call_emit(BEVENT_END_OF_FILE, call, "");
}


static void video_error_handler(int err, const char *str, void *arg)
{
	struct call *call = arg;
	MAGIC_CHECK(call);

	warning("call: video device error: %m (%s)\n", err, str);

	call_stream_stop(call);
	call_event_handler(call, CALL_EVENT_CLOSED, "%s", str);
}


static void menc_event_handler(enum menc_event event,
			       const char *prm, struct stream *strm, void *arg)
{
	struct call *call = arg;
	struct stream *audio_stream;
	bool legacy_base = false;
	int err;
	(void)strm;
	MAGIC_CHECK(call);

	debug("call: mediaenc event '%s' (%s)\n", menc_event_name(event), prm);

	bool secure_rtcp = NULL != strstr(prm, "RTCP");

	switch (event) {

	case MENC_EVENT_SECURE:
		if (strstr(prm, "audio")) {
			audio_stream = audio_strm(call->audio);
			legacy_base =
				bundle_state(stream_bundle(audio_stream)) ==
					BUNDLE_BASE &&
				!stream_has_menc_transport(audio_stream);
			stream_set_secure(audio_stream, true);
			if (secure_rtcp)
				stream_start_rtcp(audio_stream);
			err = audio_update(call->audio);
			if (err) {
				warning("call: secure: could not"
					" start audio: %m\n", err);
			}
			if (call->video && legacy_base) {
				stream_set_secure(
					video_strm(call->video), true);
				stream_start_rtcp(
					video_strm(call->video));
				err = video_update(call->video,
						   call->peer_uri);
				if (err) {
					warning("call: secure: could not"
						" start bundled video: %m\n",
						err);
				}
			}
		}
		else if (strstr(prm, "video")) {
			stream_set_secure(video_strm(call->video), true);
			if (secure_rtcp)
				stream_start_rtcp(video_strm(call->video));
			err = video_update(call->video, call->peer_uri);
			if (err) {
				warning("call: secure: could not"
					" start video: %m\n", err);
			}
		}
		else {
			info("call: mediaenc: no match for stream (%s)\n",
			     prm);
		}
		break;

	default:
		break;
	}

	if (str_isset(prm))
		call_event_handler(call, CALL_EVENT_MENC, "%u,%s", event,
				   prm);
	else
		call_event_handler(call, CALL_EVENT_MENC, "%u", event);
}


static void menc_error_handler(int err, void *arg)
{
	struct call *call = arg;
	MAGIC_CHECK(call);

	warning("call: mediaenc '%s' error: %m\n", call->acc->mencid, err);

	call_stream_stop(call);
	call_event_handler(call, CALL_EVENT_CLOSED, "mediaenc failed");
}


static void stream_mnatconn_handler(struct stream *strm, void *arg)
{
	struct call *call = arg;
	int err;
	MAGIC_CHECK(call);

	if (call->mencs) {
		err = stream_start_mediaenc(strm);
		if (err) {
			call_event_handler(call, CALL_EVENT_CLOSED,
					   "mediaenc failed %m", err);
		}
#ifdef USE_DATACHANNEL
		else if (call->data) {
			err = data_context_start(call->data);
			if (err)
				call_event_handler(call, CALL_EVENT_CLOSED,
						   "data transport failed %m",
						   err);
		}
#endif
	}
	else if (stream_is_ready(strm)) {

		stream_start_rtcp(strm);

		switch (stream_type(strm)) {

		case MEDIA_AUDIO:
			err = audio_update(call->audio);
			if (err) {
				warning("call: mnatconn: could not"
					" start audio: %m\n", err);
			}
			break;

		case MEDIA_VIDEO:
			err = video_update(call->video, call->peer_uri);
			if (err) {
				warning("call: mnatconn: could not"
					" start video: %m\n", err);
			}
			break;
		}
	}
}


static void stream_rtpestab_handler(struct stream *strm, void *arg)
{
	struct call *call = arg;
	MAGIC_CHECK(call);

	bevent_call_emit(BEVENT_CALL_RTPESTAB, call,
			 "%s", sdp_media_name(stream_sdpmedia(strm)));
}


static void stream_rtcp_handler(struct stream *strm,
				struct rtcp_msg *msg, void *arg)
{
	struct call *call = arg;

	MAGIC_CHECK(call);

	switch (msg->hdr.pt) {

	case RTCP_SR:
		if (call->config_avt.rtp_stats)
			call_set_xrtpstat(call);

		bevent_call_emit(BEVENT_CALL_RTCP, call,
				 "%s", sdp_media_name(stream_sdpmedia(strm)));
		break;

	case RTCP_APP:
		bevent_call_emit(BEVENT_CALL_RTCP, call,
				 "%s", sdp_media_name(stream_sdpmedia(strm)));
		break;
	}
}


static void stream_error_handler(struct stream *strm, int err, void *arg)
{
	struct call *call = arg;
	MAGIC_CHECK(call);

	info("call: error in \"%s\" rtp stream (%m)\n",
		sdp_media_name(stream_sdpmedia(strm)), err);

	call->scode = 701;
	set_state(call, CALL_STATE_TERMINATED);

	call_stream_stop(call);
	call_event_handler(call, CALL_EVENT_CLOSED, "rtp stream error");
}


static int assign_linenum(uint32_t *linenum, const struct list *lst)
{
	uint32_t num;

	for (num=CALL_LINENUM_MIN; num<CALL_LINENUM_MAX; num++) {

		if (!call_find_linenum(lst, num)) {
			*linenum = num;
			return 0;
		}
	}

	return ENOENT;
}


/**
 * Decode the SIP-Header for RFC 5373 auto answer of incoming call
 *
 * @param call Call object
 * @param msg  SIP message
 * @param name SIP header name
 */
static void call_rfc5373_autoanswer(struct call *call,
		const struct sip_msg *msg, const char *name)
{
	const struct sip_hdr *hdr;
	struct pl v1;

	hdr = sip_msg_xhdr(msg, name);
	if (!hdr || pl_strcasecmp(&hdr->val, "Auto"))
		return;

	if (!msg_param_exists(&hdr->val, "require", &v1) &&
			!account_sip_autoanswer(call->acc)) {

		warning("call: rejected, since %s is not allowed\n", name);
		call_hangup(call, 0, NULL);
		return;
	}

	call->adelay = 0;
}


/**
 * Decodes given SIP header for auto answer options of incoming call
 *
 * @param call Call object
 * @param hdr  SIP header (Call-Info or Alert-Info)
 * @return true if success, otherwise false
 */
static bool call_hdr_dec_sip_autoanswer(struct call *call,
		const struct sip_hdr *hdr)
{
	struct pl v1, v2;
	if (!call || !hdr)
		return false;

	if (!msg_param_decode(&hdr->val, "answer-after", &v1)) {
		call->adelay = pl_u32(&v1) * 1000;
		return true;
	}

	if (!msg_param_decode(&hdr->val, "info", &v1) &&
			!msg_param_decode(&hdr->val, "delay", &v2)) {
		if (!pl_strcmp(&v1, "alert-autoanswer")) {
			call->adelay = pl_u32(&v2) * 1000;
			return true;
		}
	}

	if (!msg_param_decode(&hdr->val, "info", &v1)) {
		if (!pl_strcmp(&v1, "alert-autoanswer")) {
			call->adelay = 0;
			return true;
		}
	}

	return false;
}


static void call_decode_diverter(struct call *call, const struct sip_msg *msg)
{
	const struct sip_hdr *hdr;
	struct sip_addr addr;
	int err;

	if (!call || !msg)
		return;

	hdr = sip_msg_hdr(msg, SIP_HDR_HISTORY_INFO);
	if (!hdr)
		hdr = sip_msg_xhdr(msg, "Diversion");
	if (!hdr)
		return;

	err = sip_addr_decode(&addr, &hdr->val);
	if (err) {
		warning("call: error parsing diverter address: %r\n",
			&hdr->val);
		return;
	}

	err = pl_strdup(&call->diverter_uri, &addr.auri);

	if (err) {
		warning("call: could not extract diverter uri");
		return;
	}
}


/**
 * Decode the SIP message for auto answer options of incoming call
 *
 * @param call Call object
 * @param msg  SIP message
 */
static void call_decode_sip_autoanswer(struct call *call,
		const struct sip_msg *msg)
{
	const struct sip_hdr *hdr;
	struct pl v;
	int err = 0;

	call->adelay = -1;

	/* polycom (HDA50), avaya, grandstream, snom, gigaset, yealink */
	hdr = sip_msg_hdr(msg, SIP_HDR_CALL_INFO);
	if (call_hdr_dec_sip_autoanswer(call, hdr))
		return;

	hdr = sip_msg_hdr(msg, SIP_HDR_ALERT_INFO);
	if (call_hdr_dec_sip_autoanswer(call, hdr)) {
		if (!re_regex(hdr->val.p, hdr->val.l, "<[^<>]*>", &v))
			err = pl_strdup(&call->aluri, &v);

		if (err) {
			warning("call: could not extract Alert-Info URI\n");
			return;
		}

		return;
	}

	/* RFC 5373 */
	call_rfc5373_autoanswer(call, msg, "Answer-Mode");
	call_rfc5373_autoanswer(call, msg, "Priv-Answer-Mode");
}


#ifdef USE_DATACHANNEL
static void call_data_error_handler(int err, void *arg)
{
	warning("call: data transport failed without closing RTP media (%m)\n",
		err);
	(void)arg;
}


static uint32_t call_data_dispatch_refs(void *arg)
{
	return mem_nrefs(arg);
}


static bool sdp_has_data_mline(const struct mbuf *mb)
{
	static const char prefix[] = "m=application ";
	const uint8_t *cursor;
	const uint8_t *limit;
	size_t left;

	if (!mb)
		return false;
	cursor = mb->buf + mb->pos;
	limit = mb->buf + mb->end;
	while (cursor < limit) {
		const uint8_t *start;
		const uint8_t *end;

		left = (size_t)(limit - cursor);
		start = memmem(cursor, left, prefix, sizeof(prefix) - 1);
		if (!start)
			return false;
		end = memchr(start, '\n', (size_t)(limit - start));
		left = end ? (size_t)(end - start)
			   : (size_t)(limit - start);
		if (memmem(start, left, "UDP/DTLS/SCTP", 13))
			return true;
		if (!end)
			return false;
		cursor = end + 1;
	}
	return false;
}


static bool sdp_section_has_rtcp_mux(const uint8_t *section, size_t len)
{
	static const char attr[] = "a=rtcp-mux";
	const uint8_t *line = section;
	const uint8_t *end = section + len;

	while (line < end) {
		const uint8_t *next = memchr(line, '\n',
					     (size_t)(end - line));
		size_t line_len = next ? (size_t)(next - line)
				      : (size_t)(end - line);

		if (line_len && line[line_len - 1] == '\r')
			--line_len;
		if (line_len == sizeof(attr) - 1 &&
		    !memcmp(line, attr, line_len))
			return true;
		if (!next)
			break;
		line = next + 1;
	}
	return false;
}


static bool sdp_rtp_mlines_muxed(const struct mbuf *mb)
{
	const uint8_t *line;
	const uint8_t *end;

	if (!mb)
		return false;
	line = mb->buf + mb->pos;
	end = mb->buf + mb->end;

	while (line < end) {
		const uint8_t *next = memchr(line, '\n',
					     (size_t)(end - line));
		const uint8_t *section_end = end;
		const uint8_t *port_start;
		const uint8_t *port_end;
		uint32_t port = 0;
		size_t line_len = next ? (size_t)(next - line)
				      : (size_t)(end - line);

		if (line_len && line[line_len - 1] == '\r')
			--line_len;
		if ((line_len < 8 || memcmp(line, "m=audio ", 8)) &&
		    (line_len < 8 || memcmp(line, "m=video ", 8)))
			goto next_line;

		port_start = line + 8;
		port_end = memchr(port_start, ' ',
				  line_len - (size_t)(port_start - line));
		if (!port_end || port_end == port_start)
			return false;
		for (const uint8_t *p = port_start; p < port_end; ++p) {
			uint32_t digit;

			if (*p < '0' || *p > '9')
				return false;
			digit = (uint32_t)(*p - '0');
			if (port > (UINT16_MAX - digit) / 10)
				return false;
			port = port * 10 + digit;
		}
		if (!port)
			goto next_line;

		const uint8_t *search = next ? next + 1 : end;
		const uint8_t *next_media =
			memmem(search, (size_t)(end - search), "\nm=", 3);
		if (next_media)
			section_end = next_media + 1;
		if (!sdp_section_has_rtcp_mux(
			    search, (size_t)(section_end - search)))
			return false;

 next_line:
		if (!next)
			break;
		line = next + 1;
	}
	return true;
}


static bool call_data_supported(const struct call *call)
{
	return call && call->acc->rtcp_mux && call->acc->mnat &&
		call->acc->menc && call->acc->menc->transporth &&
		call->mnats && call->mencs;
}


static bool call_remote_rtcp_mux(const struct call *call)
{
	struct le *le;

	if (!call->acc->rtcp_mux)
		return false;
	for (le = call->streaml.head; le; le = le->next) {
		struct stream *stream = le->data;
		struct sdp_media *media = stream_sdpmedia(stream);

		if (sdp_media_has_media(media) &&
		    !sdp_media_rattr(media, "rtcp-mux"))
			return false;
	}
	return true;
}


static int call_data_reject_ensure(struct call *call)
{
	int err;

	if (call->data || call->data_reject)
		return 0;
	err = sdp_media_add(&call->data_reject, call->sdp, "application", 0,
			    "UDP/DTLS/SCTP");
	if (!err)
		err = sdp_format_add(NULL, call->data_reject, false,
				     "webrtc-datachannel", NULL, 0, 0,
				     NULL, NULL, NULL, false, NULL);
	return err;
}


static int call_data_ensure(struct call *call, bool bundle)
{
	struct stream *base = NULL;
	int err;

	if (call->data)
		return 0;
	if (!call_data_supported(call))
		return ENOTSUP;
	if (list_isempty(&call->streaml))
		return EAGAIN;

	if (bundle && call->cfg->avt.bundle)
		base = call->streaml.head->data;
	err = data_context_alloc(&call->data, call->sdp, call->acc->mnat,
				 call->mnats, call->acc->menc, call->mencs,
				 base, &call->streaml, call->af,
				 !call->got_offer, call_data_error_handler,
				 call);
	if (!err)
		data_context_set_dispatch_refs(
			call->data, call_data_dispatch_refs, call);
	if (!err)
		err = data_context_set_handler(
			call->data, call->data_channelh,
			call->data_channel_arg);
	if (!err && base)
		err = data_context_bundle_encode(call->data,
						 &call->streaml);
	return err;
}


static int call_prepare_remote_data(struct call *call, const struct mbuf *sdp)
{
	int err;

	if (!sdp_has_data_mline(sdp))
		return 0;
	if (!call->acc->rtcp_mux || !sdp_rtp_mlines_muxed(sdp))
		return call_data_reject_ensure(call);
	err = call_data_ensure(call, true);
	if (err == ENOTSUP) {
		err = call_data_reject_ensure(call);
	}
	return err;
}


static int call_update_remote_data(struct call *call, bool offer)
{
	int err;

	if (!call->data) {
		const char *mid;

		if (!call->data_reject)
			return 0;
		mid = sdp_media_rattr(call->data_reject, "mid");
		if (!str_isset(mid))
			return EPROTO;
		sdp_media_set_lport(call->data_reject, 0);
		return sdp_media_set_lattr(call->data_reject, true,
					   "mid", "%s", mid);
	}
	if (!call_remote_rtcp_mux(call))
		return data_context_set_rejected(call->data, true);

	err = data_context_set_rejected(call->data, false);
	if (!err)
		err = data_context_remote_update(call->data, offer);
	if (err)
		warning("call: remote data update failed (%m)\n", err);
	return err;
}


static void call_stream_description_state_destructor(void *arg)
{
	struct call_stream_description_state *state = arg;

	list_unlink(&state->le);
	mem_deref(state->state);
}


static void call_description_state_destructor(void *arg)
{
	struct call_description_state *state = arg;

	list_flush(&state->streams);
	mem_deref(state->sdp);
}


static void call_description_abort(struct call_description_state *state,
				   struct call *call);


static int call_description_begin(struct call_description_state **statep,
				  struct call *call, const struct mbuf *sdp)
{
	struct call_description_state *state;
	const struct le *le;
	int err;

	if (!statep || !call)
		return EINVAL;
	state = mem_zalloc(sizeof(*state), call_description_state_destructor);
	if (!state)
		return ENOMEM;
	state->had_data = call->data != NULL;
	state->had_data_reject = call->data_reject != NULL;
	state->got_offer = call->got_offer;
	state->remote_hold = call->remote_hold;
	err = sdp_session_state_save(&state->sdp, call->sdp);
	if (err)
		goto out;
	for (le = list_head(&call->streaml); le; le = le->next) {
		struct call_stream_description_state *stream_state;

		stream_state = mem_zalloc(
			sizeof(*stream_state),
			call_stream_description_state_destructor);
		if (!stream_state) {
			err = ENOMEM;
			goto out;
		}
		err = stream_jsep_state_save(&stream_state->state, le->data);
		if (err) {
			mem_deref(stream_state);
			goto out;
		}
		list_append(&state->streams, &stream_state->le, stream_state);
	}
	if (sdp) {
		err = call_prepare_remote_data(call, sdp);
		if (err)
			goto out;
	}
	if (call->data) {
		err = data_context_description_begin(call->data);
		if (err)
			goto out;
		state->data_started = true;
	}
	*statep = state;
	return 0;

out:
	call_description_abort(state, call);
	mem_deref(state);
	return err;
}


static void call_description_abort(struct call_description_state *state,
				   struct call *call)
{
	struct le *le;

	if (!state || !call)
		return;
	if (state->data_started && call->data) {
		if (state->data_committed && state->provisional)
			data_context_rollback(call->data);
		else if (!state->data_committed)
			data_context_description_abort(call->data);
	}
	for (le = list_head(&state->streams); le; le = le->next) {
		struct call_stream_description_state *stream_state = le->data;

		stream_jsep_state_restore(stream_state->state);
	}
	/* Release stream-held candidate transports before dropping a context
	 * created by this operation; otherwise the callback ownership cycle can
	 * survive into the immediate retry. */
	if (!state->had_data)
		call->data = mem_deref(call->data);
	if (!state->had_data_reject)
		call->data_reject = mem_deref(call->data_reject);
	call->got_offer = state->got_offer;
	call->remote_hold = state->remote_hold;
	sdp_session_state_restore(state->sdp);
	state->sdp = NULL;
}


static int call_description_prepare(struct call_description_state *state,
				    struct call *call, bool provisional)
{
	int err;

	if (!state || !call)
		return EINVAL;
	err = state->data_started
		? data_context_description_prepare(call->data, provisional) : 0;
	if (err)
		return err;
	state->data_prepared = state->data_started;
	state->provisional = provisional;
	return 0;
}


static void call_description_publish(struct call_description_state *state,
				     struct call *call)
{
	if (!state || !call)
		return;
	if (state->data_started)
		data_context_description_publish(call->data, state->provisional);
	state->data_committed = state->data_started;
	if (state->data_started)
		data_context_description_finalize(call->data, state->provisional);
	if (state->data_started && !state->provisional)
		tmr_start(&call->data_notify_tmr, 0, data_notify_handler, call);
}


static int call_description_commit(struct call_description_state *state,
				   struct call *call, bool provisional)
{
	int err = call_description_prepare(state, call, provisional);

	if (err)
		return err;
	call_description_publish(state, call);
	return 0;
}


int call_set_datachannel_handler(struct call *call,
				 call_datachannel_h *channelh, void *arg)
{
	if (!call)
		return EINVAL;
	call->data_channelh = channelh;
	call->data_channel_arg = arg;
	if (!call->data)
		return 0;
	return data_context_set_handler(call->data, channelh, arg);
}


int call_create_datachannel(struct call *call, const char *label,
			    const struct data_channel_config *cfg,
			    struct data_channel **dcp)
{
	bool renegotiate;
	int err;

	if (!call)
		return EINVAL;
	err = data_context_channel_validate(label, cfg, dcp);
	if (err)
		return err;
	if (!call->acc->rtcp_mux)
		return ENOTSUP;
	if (list_isempty(&call->streaml)) {
		err = call_streams_alloc(call);
		if (err)
			return err;
	}
	if (call->state == CALL_STATE_ESTABLISHED &&
	    !call_remote_rtcp_mux(call))
		return ENOTSUP;

	renegotiate = call->state == CALL_STATE_ESTABLISHED &&
		      (!call->data || (cfg && cfg->negotiated));
	err = call_data_ensure(call, true);
	if (err)
		return err;
	err = data_context_channel_create(call->data, label, cfg, dcp);
	if (!err && renegotiate) {
		err = call_modify(call);
		if (err) {
			(void)datachannel_close(*dcp);
			*dcp = NULL;
		}
	}
	return err;
}
#endif


int call_streams_alloc(struct call *call)
{
	if (!call)
		return EINVAL;

	struct account *acc = call->acc;
	struct stream_param strm_prm;
	struct le *le;
	int err;

	memset(&strm_prm, 0, sizeof(strm_prm));
	strm_prm.use_rtp  = call->use_rtp;
	strm_prm.af	  = call->af;
	strm_prm.cname	  = call->local_uri;
	strm_prm.peer	  = call->peer_uri;
	strm_prm.rtcp_mux = call->acc->rtcp_mux;

	/* Audio stream */
	err = audio_alloc(&call->audio, &call->streaml, &strm_prm,
			  call->cfg, acc, call->sdp,
			  acc->mnat, call->mnats, acc->menc, call->mencs,
			  acc->ptime, account_aucodecl(call->acc),
			  !call->got_offer,
			  audio_event_handler, audio_level_handler,
			  audio_error_handler, call);
	if (err)
		return err;

	/* Video stream */
	if (call->use_video) {
		err = video_alloc(&call->video, &call->streaml, &strm_prm,
				  call->cfg, acc, call->sdp,
				  acc->mnat, call->mnats,
				  acc->menc, call->mencs,
				  "main",
				  account_vidcodecl(call->acc),
				  baresip_vidfiltl(), !call->got_offer,
				  video_error_handler, call);
		if (err)
			return err;
	}

	FOREACH_STREAM {
		struct stream *strm = le->data;

		stream_set_session_handlers(strm, stream_mnatconn_handler,
					    stream_rtpestab_handler,
					    stream_rtcp_handler,
					    stream_error_handler, call);

		stream_enable_natpinhole(strm, acc->pinhole);
	}

	if (call->cfg->avt.bundle) {

		FOREACH_STREAM {
			struct stream *strm = le->data;

			err = stream_bundle_init(strm, !call->got_offer);
			if (err)
				return err;
		}

		err = bundle_sdp_encode(call->sdp, &call->streaml);
		if (err)
			return err;
	}

	if (call->cfg->audio.level && !call->got_offer) {
		err = audio_enable_level(call->audio);
		if (err)
			return err;
	}

	return 0;
}


/**
 * Allocate a new Call state object
 *
 * @param callp       Pointer to allocated Call state object
 * @param cfg         Global configuration
 * @param lst         List of call objects
 * @param local_name  Local display name (optional)
 * @param local_uri   Local SIP uri
 * @param acc         Account parameters
 * @param ua          User-Agent
 * @param prm         Call parameters
 * @param msg         SIP message for incoming calls
 * @param xcall       Optional call to inherit properties from
 * @param dnsc        DNS Client
 * @param eh          Call event handler
 * @param arg         Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int call_alloc(struct call **callp, const struct config *cfg, struct list *lst,
	       const char *local_name, const char *local_uri,
	       struct account *acc, struct ua *ua, const struct call_prm *prm,
	       const struct sip_msg *msg, struct call *xcall,
	       struct dnsc *dnsc,
	       call_event_h *eh, void *arg)
{
	struct call *call;
	enum vidmode vidmode = prm ? prm->vidmode : VIDMODE_OFF;
	int err = 0;

	if (!cfg || !local_uri || !acc || !ua || !prm)
		return EINVAL;

	debug("call: alloc with params laddr=%j, af=%s, use_rtp=%d\n",
	      &prm->laddr, net_af2name(prm->af), prm->use_rtp);

	call = mem_zalloc(sizeof(*call), call_destructor);
	if (!call)
		return ENOMEM;

	MAGIC_INIT(call);

	call->config_avt = cfg->avt;
	call->config_call = cfg->call;

	tmr_init(&call->tmr_inv);
	tmr_init(&call->tmr_answ);
	tmr_init(&call->tmr_reinv);
#ifdef USE_DATACHANNEL
	tmr_init(&call->data_notify_tmr);
#endif

	call->cfg    = cfg;
	call->acc    = mem_ref(acc);
	call->ua     = ua;
	call->state  = CALL_STATE_IDLE;
	call->eh     = eh;
	call->arg    = arg;
	call->af     = prm->af;
	call->estadir = SDP_SENDRECV;
	call->estvdir = SDP_SENDRECV;
	call->use_rtp = prm->use_rtp;
	call_decode_sip_autoanswer(call, msg);
	call_decode_diverter(call, msg);

	err = str_dup(&call->local_uri, local_uri);
	if (local_name)
		err |= str_dup(&call->local_name, local_name);

	if (msg) {
		struct sip_addr addr;
		const struct sip_hdr *hdr = sip_msg_hdr(msg, SIP_HDR_CONTACT);
		if (hdr && 0 == sip_addr_decode(&addr, &hdr->val))
			err |= pl_strdup(&call->contact_uri, &addr.auri);

		err |= pl_strdup(&call->peer_uri, &msg->from.auri);
	}

	if (err)
		goto out;

	if (sip_msg_hdr_has_value(msg, SIP_HDR_SUPPORTED, "replaces"))
		call->supported |= REPLACES;

	/* Init SDP info */
	err = sdp_session_alloc(&call->sdp, &prm->laddr);
	if (err)
		goto out;

	/* Check for incoming SDP Offer */
	if (msg && mbuf_get_left(msg->mb))
		call->got_offer = true;

	/* Initialise media NAT handling */
	if (acc->mnat) {
		err = acc->mnat->sessh(&call->mnats, acc->mnat,
				       dnsc, call->af,
				       acc->stun_host,
				       acc->stun_user, acc->stun_pass,
				       call->sdp, !call->got_offer,
				       mnat_handler, call);
		if (err) {
			warning("call: medianat session: %m\n", err);
			goto out;
		}
	}
	call->mnat_wait = true;

	/* Media encryption */
	if (acc->menc) {
		if (acc->menc->sessh) {
			err = acc->menc->sessh(&call->mencs, call->sdp,
					       !call->got_offer,
					       menc_event_handler,
					       menc_error_handler, call);
			if (err) {
				warning("call: mediaenc session: %m\n", err);
				goto out;
			}
		}
	}

	/* We require at least one video codec, and at least one
	   video source or video display */
	call->use_video = (vidmode != VIDMODE_OFF)
		&& (list_head(account_vidcodecl(call->acc)) != NULL)
		&& (NULL != vidsrc_find(baresip_vidsrcl(), NULL)
		    || NULL != vidisp_find(baresip_vidispl(), NULL));

	debug("call: use_video=%d\n", call->use_video);
	if (!call->use_video)
		call->estvdir = SDP_INACTIVE;

	/* inherit certain properties from original call */
	if (xcall) {
		call->not = mem_ref(xcall->not);
		call->xcall = xcall;
	}

	if (cfg->avt.rtp_timeout) {
		call_enable_rtp_timeout(call, cfg->avt.rtp_timeout*1000);
	}

	err = assign_linenum(&call->linenum, lst);
	if (err) {
		warning("call: could not assign linenumber\n");
		goto out;
	}

	/* NOTE: The new call must always be added to the tail of list,
	 *       which indicates the current call.
	 */
	list_append(lst, &call->le, call);

 out:
	if (err) {
		mem_deref(call);
	}
	else if (callp) {
		*callp = call;
		if (xcall)
			xcall->xcall = call;
	}

	return err;
}


void call_set_sip_info_handler(struct call *call,
			       call_sip_info_h *handler, void *arg)
{
	if (!call)
		return;

	call->sip_infoh = handler;
	call->sip_info_arg = arg;
}


void call_set_custom_hdrs(struct call *call, const struct list *hdrs)
{
	struct le *le;

	if (!call)
		return;

	list_flush(&call->custom_hdrs);

	LIST_FOREACH(hdrs, le) {
		struct sip_hdr *hdr = le->data;
		char *buf = NULL;

		if (re_sdprintf(&buf, "%r", &hdr->name))
			return;

		if (custom_hdrs_add(&call->custom_hdrs, buf,
				    "%r", &hdr->val)) {
			mem_deref(buf);
			return;
		}

		mem_deref(buf);
	}
}


/**
 * Get the list of custom SIP headers
 *
 * @param call Call object
 *
 * @return List of custom SIP headers (struct sip_hdr)
 */
const struct list *call_get_custom_hdrs(const struct call *call)
{
	if (!call)
		return NULL;

	return &call->custom_hdrs;
}


/**
 * Connect an outgoing call to a given SIP uri
 *
 * @param call  Call Object
 * @param paddr SIP address or uri to connect to
 *
 * @return 0 if success, otherwise errorcode
 */
int call_connect(struct call *call, const struct pl *paddr)
{
	struct sip_addr addr;
	struct pl rname = PL("Replaces");
	struct pl rval = PL_INIT;
	int err;

	if (!call || !paddr)
		return EINVAL;

	info("call: connecting to '%r'..\n", paddr);

	call->outgoing = true;
	err = str_x64dup(&call->id, rand_u64());
	if (err)
		return err;

	/* if the peer-address is a full SIP address then we need
	 * to parse it and extract the SIP uri part.
	 */
	call->peer_uri = mem_deref(call->peer_uri);
	if (0 == sip_addr_decode(&addr, paddr)) {

		uri_header_get(&addr.uri.headers, &rname, &rval);
		if (pl_isset(&rval))
			err = re_sdprintf(&call->replaces, "%H",
					  uri_header_unescape, &rval);

		addr.uri.headers.l = 0;

		if (pl_isset(&addr.params)) {
			err |= re_sdprintf(&call->peer_uri, "%H%r", uri_encode,
					   &addr.uri, &addr.params);
		}
		else {
			err |= re_sdprintf(&call->peer_uri, "%H", uri_encode,
					   &addr.uri);
		}

		if (pl_isset(&addr.dname))
			pl_strdup(&call->peer_name, &addr.dname);

	}
	else {
		err = pl_strdup(&call->peer_uri, paddr);
	}
	if (err)
		return err;

	set_state(call, CALL_STATE_OUTGOING);
	call_event_handler(call, CALL_EVENT_OUTGOING, "%s", call->peer_uri);

	/* If we are using asynchronous medianat like STUN/TURN, then
	 * wait until completed before sending the INVITE */
	if (!call->acc->mnat) {
		err = send_invite(call);
	}
	else {
		err = list_isempty(&call->streaml)
			      ? call_streams_alloc(call) : 0;
		if (err)
			return err;

		call_set_mdir(call, call->estadir, call->estvdir);
	}

	return err;
}


/**
 * Update the current call by sending Re-INVITE or UPDATE
 *
 * @param call Call object
 *
 * @return 0 if success, otherwise errorcode
 */
int call_modify(struct call *call)
{
	struct mbuf *desc = NULL;
#ifdef USE_DATACHANNEL
	struct call_description_state *description = NULL;
	bool data_offer_published = false;
#endif
	int err;

	if (!call)
		return EINVAL;

	debug("call: modify\n");

	if (call_refresh_allowed(call)) {
#ifdef USE_DATACHANNEL
		if (call->data) {
			/* A terminal non-2xx re-INVITE response has no sipsess
			 * description callback.  Once refresh is allowed again, discard
			 * the previous local-offer candidate before taking the retry
			 * snapshot.  Successful answers already clear this state. */
			data_context_rollback(call->data);
			err = call_description_begin(&description, call, NULL);
			if (err)
				return err;
		}
#endif
		err = bevent_call_emit(BEVENT_CALL_LOCAL_SDP, call, "offer");
		if (err)
			goto out;

		err = call_sdp_get(call, &desc, true);
		if (err)
			goto out;

#ifdef USE_DATACHANNEL
		if (description) {
			err = call_description_prepare(description, call, true);
			if (err)
				goto out;
		}
#endif
		err = sipsess_modify(call->sess, desc);
		if (err)
			goto out;
#ifdef USE_DATACHANNEL
		if (description) {
			call_description_publish(description, call);
			data_offer_published = true;
			/* Signaling and provisional publication are irreversible.  A
			 * subsequent runtime-media error must not roll back the offer that
			 * the peer has already received. */
			description = mem_deref(description);
		}
#endif
	}

#ifdef USE_DATACHANNEL
	if (data_offer_published) {
		err = call_apply_sdp(call);
		if (!err)
			err = update_streams(call);
	}
	else
#endif
		err = call_update_media(call);

 out:
#ifdef USE_DATACHANNEL
	if (err && description)
		call_description_abort(description, call);
	mem_deref(description);
#endif
	mem_deref(desc);

	return err;
}


/**
 * Send a re-INVITE without SDP offer
 *
 * @param call Call object
 *
 * @return 0 if success, otherwise errorcode
 */
int call_modify_nosdp(struct call *call)
{
	if (!call || !call->sess)
		return EINVAL;

	return sipsess_modify(call->sess, NULL);
}


/**
 * Hangup the call
 *
 * @param call   Call to hangup
 * @param scode  Optional status code
 * @param reason Optional reason
 * @param fmt    Formatted headers
 * @param ...    Variable arguments
 */
void call_hangupf(struct call *call, uint16_t scode, const char *reason,
		 const char *fmt, ...)
{
	if (!call)
		return;

	if (call->config_avt.rtp_stats)
		call_set_xrtpstat(call);

	if (call->state == CALL_STATE_INCOMING) {
		if (call->answered) {
			info("call: abort call '%s' with %s\n",
			     sip_dialog_callid(sipsess_dialog(call->sess)),
			     call->peer_uri);
			sipsess_abort(call->sess);
		}
		else {
			if (!scode)
				scode = 486;

			if (!str_isset(reason))
				reason = "Busy Here";

			info("call: rejecting incoming call from %s (%u %s)\n",
			     call->peer_uri, scode, reason);
			va_list ap;
			va_start(ap, fmt);
			(void)sipsess_reject(call->sess, scode, reason,
					     fmt ? "%v" : NULL, fmt, &ap);
			va_end(ap);
		}
	}
	else {
		info("call: terminate call '%s' with %s\n",
		     sip_dialog_callid(sipsess_dialog(call->sess)),
		     call->peer_uri);

		if (call->not)
			call_notify_sipfrag(call, 487, "Request Terminated");

		call->sess = mem_deref(call->sess);
	}

	set_state(call, CALL_STATE_TERMINATED);

	call_stream_stop(call);
}


/**
 * Hangup the call
 *
 * @param call   Call to hangup
 * @param scode  Optional status code
 * @param reason Optional reason
 */
void call_hangup(struct call *call, uint16_t scode, const char *reason)
{
	call_hangupf(call, scode, reason, NULL);
}


/**
 * Send a SIP 183 Session Progress with configured media
 *
 * @param call Call to answer
 *
 * @return 0 if success, otherwise errorcode
 */
int call_progress(struct call *call)
{
	enum answermode m;
	enum sdp_dir adir;
	enum sdp_dir vdir;

	if (!call)
		return EINVAL;

	m = account_answermode(call->acc);

	adir = m == ANSWERMODE_EARLY ? SDP_SENDRECV :
			    m == ANSWERMODE_EARLY_AUDIO ? SDP_RECVONLY :
			    SDP_INACTIVE;
	vdir = m == ANSWERMODE_EARLY ? SDP_SENDRECV :
			    m == ANSWERMODE_EARLY_VIDEO ? SDP_RECVONLY :
			    SDP_INACTIVE;
	enum sdp_dir ladir = SDP_SENDRECV;
	enum sdp_dir lvdir = SDP_SENDRECV;
	call_get_mdir(call, &ladir, &lvdir);
	adir &= ladir;
	vdir &= lvdir;

	return call_progress_dir(call, adir, vdir);
}


/**
 * Send a SIP 183 Session Progress with given audio/video direction
 *
 * @param call Call to answer
 * @param adir Audio direction
 * @param vdir Video direction
 *
 * @return 0 if success, otherwise errorcode
 */
int call_progress_dir(struct call *call, enum sdp_dir adir, enum sdp_dir vdir)
{
	struct mbuf *desc = NULL;
#ifdef USE_DATACHANNEL
	struct call_description_state *description = NULL;
#endif
	int err;

	if (!call)
		return EINVAL;

	enum sdp_dir ardir = sdp_media_rdir(stream_sdpmedia(audio_strm(
				call_audio(call))));
	enum sdp_dir vrdir = sdp_media_rdir(stream_sdpmedia(video_strm(
				call_video(call))));
	if ((adir & ardir) == SDP_INACTIVE &&
	    (vdir & vrdir) == SDP_INACTIVE) {
		debug("call: progress: both audio and video would become "
		      "inactive\n");
		return EINVAL;
	}

	tmr_cancel(&call->tmr_inv);

	if (adir != call->estadir || vdir != call->estvdir)
		call_set_mdir(call, adir, vdir);

#ifdef USE_DATACHANNEL
	if (call->got_offer) {
		err = call_description_begin(&description, call, NULL);
		if (err)
			return err;
	}
#endif
	err = call_sdp_get(call, &desc, false);
	if (err)
		goto out;
	if (call->got_offer) {
		err = bevent_call_emit(BEVENT_CALL_LOCAL_SDP, call, "answer");
		if (err)
			goto out;
		err = call_apply_sdp(call);
		if (!err)
			err = update_streams(call);
		if (err)
			goto out;
	}
#ifdef USE_DATACHANNEL
	if (description) {
		err = call_description_prepare(description, call, true);
		if (err)
			goto out;
	}
#endif

	err = sipsess_progress(call->sess, 183, "Session Progress",
			       account_rel100_mode(call->acc),
			       desc, "Allow: %H\r\n%H",
			       ua_print_allowed, call->ua,
			       ua_print_require, call->ua);

	call->progr_queued = (err == EAGAIN);
	if (err)
		goto out;

#ifdef USE_DATACHANNEL
	if (description)
		call_description_publish(description, call);
#endif

out:
#ifdef USE_DATACHANNEL
	if (err && description)
		call_description_abort(description, call);
	mem_deref(description);
#endif
	mem_deref(desc);

	return err;
}


static bool call_need_modify(const struct call *call)
{
	enum sdp_dir adir;
	enum sdp_dir vdir;

	if (!call)
		return false;

	adir = stream_ldir(audio_strm(call_audio(call)));
	vdir = stream_ldir(video_strm(call_video(call)));
	return adir != call->estadir || vdir != call->estvdir;
}


/**
 * Answer an incoming call
 *
 * @param call  Call to answer
 * @param scode Status code
 * @param vmode Wanted video mode
 *
 * @return 0 if success, otherwise errorcode
 */
int call_answer(struct call *call, uint16_t scode, enum vidmode vmode)
{
	struct mbuf *desc = NULL;
#ifdef USE_DATACHANNEL
	struct call_description_state *description = NULL;
#endif
	int err;

	if (!call || !call->sess)
		return EINVAL;

	tmr_cancel(&call->tmr_answ);

	if (CALL_STATE_INCOMING != call->state) {
		info("call: answer: call is not in incoming state (%s)\n",
		     state_name(call->state));
		return EINVAL;
	}

	if (sipsess_awaiting_prack(call->sess)) {
		info("call: answer: can not answer because we are awaiting a "
		     "PRACK to a 1xx response with SDP\n");
		return EAGAIN;
	}

	if (vmode == VIDMODE_OFF)
		call->video = mem_deref(call->video);

	info("call: answering call on line %u from %s with %u\n",
			call->linenum, call->peer_uri, scode);

#ifdef USE_DATACHANNEL
	if (call->got_offer) {
		err = call_description_begin(&description, call, NULL);
		if (err)
			return err;
	}
#endif
	err = bevent_call_emit(BEVENT_CALL_LOCAL_SDP, call,
			       "%s", !call->got_offer ? "offer" : "answer");
	if (err)
		goto out;
	if (call->got_offer) {
		err = call_apply_sdp(call);
		if (err)
			goto out;
	}
	err = call_sdp_get(call, &desc, !call->got_offer);
	if (err)
		goto out;

#ifdef USE_DATACHANNEL
	if (description) {
		err = call_description_prepare(description, call, false);
		if (err)
			goto out;
	}
#endif

	if (scode >= 200 && scode < 300) {
		err = sipsess_answer(call->sess, scode, "Answering", desc,
				"Allow: %H\r\n"
				"%H", ua_print_allowed, call->ua,
				ua_print_supported, call->ua);
	}
	else {
		err = sipsess_answer(call->sess, scode, "Answering", desc,
				"Allow: %H\r\n", ua_print_allowed, call->ua);
	}

	if (err)
		goto out;
#ifdef USE_DATACHANNEL
	if (description)
		call_description_publish(description, call);
#endif
	call->answered = true;
	call->ans_queued = false;

	if (call->got_offer && stream_is_ready(video_strm(call->video)))
		(void)video_update(call->video, call->peer_uri);

out:
#ifdef USE_DATACHANNEL
	if (err && description)
		call_description_abort(description, call);
	mem_deref(description);
#endif
	mem_deref(desc);
	return err;
}


/**
 * Check if the current call has an active audio stream
 *
 * @param call  Call object
 *
 * @return True if active stream, otherwise false
 */
bool call_has_audio(const struct call *call)
{
	if (!call)
		return false;

	return sdp_media_has_media(stream_sdpmedia(audio_strm(call->audio)));
}


/**
 * Check if the current call has an active video stream
 *
 * @param call  Call object
 *
 * @return True if active stream, otherwise false
 */
bool call_has_video(const struct call *call)
{
	if (!call)
		return false;

	return sdp_media_has_media(stream_sdpmedia(video_strm(call->video)));
}


/**
 * Put the current call on hold/resume
 *
 * @param call  Call object
 * @param hold  True to hold, false to resume
 *
 * @return 0 if success, otherwise errorcode
 */
int call_hold(struct call *call, bool hold)
{
	struct le *le;

	if (!call || !call->sess)
		return EINVAL;

	if (hold == call->on_hold)
		return 0;

	info("call: %s %s\n", hold ? "hold" : "resume", call->peer_uri);

	call->on_hold = hold;

	FOREACH_STREAM
		stream_hold(le->data, hold);

	return call_modify(call);
}


/**
 * Sets the audio local direction of the given call
 *
 * @param call  Call object
 * @param dir   SDP media direction
 */
void call_set_audio_ldir(struct call *call, enum sdp_dir dir)
{
	if (!call)
		return;

	stream_set_ldir(audio_strm(call_audio(call)), dir);
}


/**
 * Sets the video local direction of the given call
 *
 * @param call  Call object
 * @param dir   SDP media direction
 */
void call_set_video_ldir(struct call *call, enum sdp_dir dir)
{
	if (!call)
		return;

	stream_set_ldir(video_strm(call_video(call)), dir);
}


/**
 * Sets the video direction of the given call
 *
 * @param call  Call object
 * @param dir   SDP media direction
 *
 * @return 0 if success, otherwise errorcode
 */
int call_set_video_dir(struct call *call, enum sdp_dir dir)
{
	if (!call)
		return EINVAL;

	call->estvdir = dir;
	stream_set_ldir(video_strm(call_video(call)), dir);
	return call_modify(call);
}


int call_sdp_get(const struct call *call, struct mbuf **descp, bool offer)
{
	int err;

	if (!call)
		return EINVAL;

#ifdef USE_DATACHANNEL
	if (call->data) {
		err = data_context_bundle_encode(call->data, &call->streaml);
		if (err)
			return err;
	}
	else
#endif
	if (call->config_avt.bundle) {
		err = bundle_sdp_encode(call->sdp, &call->streaml);
		if (err)
			return err;
	}
	err = sdp_encode(descp, call->sdp, offer);
#ifdef USE_DATACHANNEL
	if (!err && call->data)
		err = data_context_local_description(call->data, offer);
#endif
	return err;
}


/**
 * Check if a target refresh (re-INVITE or UPDATE) is currently allowed
 *
 * @param call  Call object
 *
 * @return True if a target refresh is currently allowed, otherwise false
 */
bool call_refresh_allowed(const struct call *call)
{
	return call ? sipsess_refresh_allowed(call->sess) : false;
}


/**
 * Check if the local SIP Session expects an ACK as answer to a SIP Session
 * Reply
 *
 * @param call  Call object
 *
 * @return True if an ACK is pending, false if not
 */
bool call_ack_pending(const struct call *call)
{
	return call ? sipsess_ack_pending(call->sess) : false;
}


/**
 * Get the session call-id for the call
 *
 * @param call Call object
 *
 * @return Session call-id
 */
const char *call_id(const struct call *call)
{
	return call ? call->id : NULL;
}


/**
 * Get the URI of the peer
 *
 * For incoming calls, this is the From header URI of the incoming INVITE.
 *
 * @param call  Call object
 *
 * @return Peer URI
 */
const char *call_peeruri(const struct call *call)
{
	return call ? call->peer_uri : NULL;
}


/**
 * Get the Contact URI of the peer
 *
 * For outgoing calls, this is the same as call_peeruri.
 * For incoming calls, this is the Contact header URI of the incoming INVITE.
 *
 * @param call  Call object
 *
 * @return Peer Contact URI
 */
const char *call_contacturi(const struct call *call)
{
	if (!call)
		return NULL;

	return call->outgoing ? call->peer_uri : call->contact_uri;
}


/**
 * Get the local URI of the call
 *
 * @param call  Call object
 *
 * @return Local URI
 */
const char *call_localuri(const struct call *call)
{
	return call ? call->local_uri : NULL;
}


/**
 * Get the name of the peer
 *
 * @param call  Call object
 *
 * @return Peer name
 */
const char *call_peername(const struct call *call)
{
	return call ? call->peer_name : NULL;
}


/**
 * Get the diverter URI of the call
 *
 * @param call  Call object
 *
 * @return Diverter URI
 */
const char *call_diverteruri(const struct call *call)
{
	return call ? call->diverter_uri : NULL;
}


/**
 * Get the Alert-Info URI of the call
 *
 * @param call  Call object
 *
 * @return Alert-Info URI
 */
const char *call_alerturi(const struct call *call)
{
	return call ? call->aluri : NULL;
}


/**
 * Get the state as string
 *
 * @param call  Call object
 *
 * @return State name
 */
const char *call_statename(const struct call *call)
{
	return call ? state_name(call->state) : NULL;
}


/**
 * Print the call debug information
 *
 * @param pf   Print function
 * @param call Call object
 *
 * @return 0 if success, otherwise errorcode
 */
int call_debug(struct re_printf *pf, const struct call *call)
{
	int err;

	if (!call)
		return 0;

	err = re_hprintf(pf, "===== Call debug (%s) =====\n",
			 state_name(call->state));

	/* SIP Session debug */
	err |= re_hprintf(pf,
			  " local_uri: %s <%s>\n"
			  " peer_uri:  %s <%s>\n"
			  " af=%s id=%s\n"
			  " autoanswer delay: %d\n",
			  call->local_name, call->local_uri,
			  call->peer_name, call->peer_uri,
			  net_af2name(call->af), call->id,
			  call->adelay);
	err |= re_hprintf(pf, " direction: %s\n",
			  call->outgoing ? "Outgoing" : "Incoming");

	/* SDP debug */
	err |= sdp_session_debug(pf, call->sdp);

	return err;
}


static int print_duration(struct re_printf *pf, const struct call *call)
{
	const uint32_t dur = call_duration(call);
	const uint32_t sec = dur%60%60;
	const uint32_t min = dur/60%60;
	const uint32_t hrs = dur/60/60;

	return re_hprintf(pf, "%u:%02u:%02u", hrs, min, sec);
}


/**
 * Print the call status
 *
 * @param pf   Print function
 * @param call Call object
 *
 * @return 0 if success, otherwise errorcode
 */
int call_status(struct re_printf *pf, const struct call *call)
{
	struct le *le;
	int err;

	if (!call)
		return EINVAL;

	switch (call->state) {

	case CALL_STATE_EARLY:
	case CALL_STATE_ESTABLISHED:
		break;
	default:
		return 0;
	}

	err = re_hprintf(pf, "\r[%H]", print_duration, call);

	FOREACH_STREAM
		err |= stream_print(pf, le->data);

	err |= re_hprintf(pf, " (bit/s)");

	if (call->video)
		err |= video_print(pf, call->video);

	/* remove old junk */
	err |= re_hprintf(pf, "    ");

	return err;
}


int call_info(struct re_printf *pf, const struct call *call)
{
	if (!call)
		return 0;

	return re_hprintf(pf, "[line %u, id %s]  %H  %9s  %s  %s",
			  call->linenum, call->id,
			  print_duration, call,
			  state_name(call->state),
			  call->on_hold ? "(on hold)" : "         ",
			  call->peer_uri);
}


/**
 * Send a DTMF digit to the peer
 *
 * @param call  Call object
 * @param key   DTMF digit to send (KEYCODE_REL for key release)
 *
 * @return 0 if success, otherwise errorcode
 */
int call_send_digit(struct call *call, char key)
{
	int err = 0;
	const struct sdp_format *fmt;
	bool info = true;

	if (!call)
		return EINVAL;

	switch (account_dtmfmode(call->acc)) {
		case DTMFMODE_SIP_INFO:
			info = true;
			break;
		case DTMFMODE_AUTO:
			fmt = sdp_media_rformat(
				stream_sdpmedia(audio_strm(call->audio)),
				telev_rtpfmt);
			info = fmt == NULL;
			break;
		case DTMFMODE_RTP_EVENT:
		default:
			info = false;
			break;
	}

	if (info) {
		if (key != KEYCODE_REL) {
			err = send_dtmf_info(call, key);
		}
	}
	else {
		err = audio_send_digit(call->audio, key);
	}

	return err;
}


/**
 * Get the User-Agent for the call
 *
 * @param call Call object
 *
 * @return User-Agent
 */
struct ua *call_get_ua(const struct call *call)
{
	return call ? call->ua : NULL;
}


struct account *call_account(const struct call *call)
{
	return call ? call->acc : NULL;
}


static int auth_handler(char **username, char **password,
			const char *realm, void *arg)
{
	struct account *acc = arg;
	return account_auth(acc, username, password, realm);
}


/*
 * Returns true, if a hold or resume event is detected.
 *
 * If a hold or resume is detected, ev is set to BEVENT_CALL_HOLD,
 * or BEVENT_CALL_RESUME.
 *
 * Detects hold/resume transitions per RFC 6337 section 5.3. Only detect
 * transitions in established calls, not during initial call setup.
 *
 * When remote transitions from sendrecv or recvonly to sendonly or inactive,
 * we're placed on hold.
 * When remote transitions from sendonly or inactive to sendrecv or recvonly,
 * we're resumed.
 */
static bool detect_hold_resume(enum bevent_ev *ev, struct call *call,
			       enum sdp_dir old_rdir, bool is_offer)
{
	bool emit_event = false;

	if (!ev || !call || call->state != CALL_STATE_ESTABLISHED)
		return false;

	const struct sdp_media *m = stream_sdpmedia(audio_strm(call->audio));
	const enum sdp_dir new_rdir = sdp_media_rdir(m);

	bool before_on_hold = (old_rdir == SDP_RECVONLY ||
			       old_rdir == SDP_INACTIVE);
	bool now_on_hold = (new_rdir == SDP_RECVONLY ||
			    new_rdir == SDP_INACTIVE);

	/* For offers, detect based on rdir changes.
	 * For answers, use the tracked remote_hold state because rdir
	 * may have changed when we created an offer. */
	if (is_offer) {
		if (now_on_hold && !before_on_hold) {
			*ev = BEVENT_CALL_HOLD;
			call->remote_hold = true;
			emit_event = true;
		}
		else if (before_on_hold && !now_on_hold) {
			*ev = BEVENT_CALL_RESUME;
			call->remote_hold = false;
			emit_event = true;
		}
	}
	else if (call->remote_hold && !now_on_hold) {
		/* Only detect resume events for answers, not hold events. */
		*ev = BEVENT_CALL_RESUME;
		call->remote_hold = false;
		emit_event = true;
	}

	return emit_event;
}


static int sipsess_offer_handler(struct mbuf **descp,
				 const struct sip_msg *msg, void *arg)
{
	const bool got_offer = (0 != mbuf_get_left(msg->mb));
	struct call *call = arg;
#ifdef USE_DATACHANNEL
	struct call_description_state *description = NULL;
#endif
	enum sdp_dir ardir, vrdir;
	enum sdp_dir old_rdir = SDP_INACTIVE;
	enum bevent_ev hold_ev = BEVENT_CALL_HOLD;
	bool emit_hold = false;
	int err;

	MAGIC_CHECK(call);

	if (got_offer) {
		const struct sdp_media *m =
			stream_sdpmedia(audio_strm(call->audio));
		old_rdir = sdp_media_rdir(m);
#ifdef USE_DATACHANNEL
		err = call_description_begin(&description, call, msg->mb);
		if (err)
			goto out;
#endif
		call->got_offer = true;
		/* Decode SDP Offer */
		err = sdp_decode(call->sdp, msg->mb, true);
		if (err) {
			warning("call: reinvite: could not decode SDP offer:"
				" %m\n", err);
			goto out;
		}
#ifdef USE_DATACHANNEL
		err = call_update_remote_data(call, true);
		if (err)
			goto out;
#endif
		if (call->config_avt.bundle) {
			err = bundle_sdp_decode(call->sdp, &call->streaml);
			if (err) {
				warning("call: re-INVITE BUNDLE update failed (%m)\n",
					err);
				goto out;
			}
		}

	}

	ardir = sdp_media_rdir(stream_sdpmedia(audio_strm(call_audio(call))));
	vrdir = sdp_media_rdir(stream_sdpmedia(video_strm(call_video(call))));

	info("call: got %r%s audio-video: %s-%s\n", &msg->met,
	     got_offer ? " (SDP Offer)" : "",
	     sdp_dir_name(ardir), sdp_dir_name(vrdir));

	err = bevent_call_emit(BEVENT_CALL_LOCAL_SDP, call, "%s",
			       got_offer ? "answer" : "offer");
	if (err)
		goto out;
	if (got_offer) {
		err = bevent_call_emit(BEVENT_CALL_REMOTE_SDP, call, "offer");
		if (err)
			goto out;
		err = call_apply_sdp(call);
		if (!err)
			err = update_streams(call);
		if (err) {
			warning("call: reinvite - update media failed/rejected"
				" (%m)\n", err);
			goto out;
		}
	}
	/* Encode only after vetoes and media preparation succeed. */
	err = call_sdp_get(call, descp, !got_offer);
	if (err)
		goto out;
#ifdef USE_DATACHANNEL
	if (description)
		err = call_description_prepare(description, call, false);
#endif
	if (err)
		goto out;
#ifdef USE_DATACHANNEL
	if (description)
		call_description_publish(description, call);
#endif
	if (got_offer) {
		emit_hold = detect_hold_resume(&hold_ev, call, old_rdir, true);
		if (emit_hold)
			(void)bevent_call_emit(hold_ev, call, "");
	}

out:
#ifdef USE_DATACHANNEL
	if (err && description)
		call_description_abort(description, call);
	mem_deref(description);
#endif
	if (err && descp)
		*descp = mem_deref(*descp);
	return err;
}


static int sipsess_answer_handler(const struct sip_msg *msg, void *arg)
{
	struct call *call = arg;
#ifdef USE_DATACHANNEL
	struct call_description_state *description = NULL;
#endif
	int err;
	enum bevent_ev hold_ev;
	const struct sdp_media *m = stream_sdpmedia(audio_strm(call->audio));
	const enum sdp_dir old_rdir = sdp_media_rdir(m);

	MAGIC_CHECK(call);

	debug("call: got SDP answer (%zu bytes)\n", mbuf_get_left(msg->mb));

	if (sip_msg_hdr_has_value(msg, SIP_HDR_SUPPORTED, "replaces"))
		call->supported |= REPLACES;

	if (msg_ctype_cmp(&msg->ctyp, "multipart", "mixed"))
		(void)sdp_decode_multipart(&msg->ctyp.params, msg->mb);

#ifdef USE_DATACHANNEL
	err = call_description_begin(&description, call, msg->mb);
	if (err)
		goto out;
#endif
	call->got_offer = false;
	err = sdp_decode(call->sdp, msg->mb, false);
	if (err) {
		warning("call: could not decode SDP answer: %m\n", err);
		goto out;
	}
#ifdef USE_DATACHANNEL
	err = call_update_remote_data(call, false);
	if (err)
		goto out;
#endif

	/* note: before update_media */
	if (call->config_avt.bundle) {
		err = bundle_sdp_decode(call->sdp, &call->streaml);
		if (err) {
			warning("call: answer BUNDLE update failed (%m)\n", err);
			goto out;
		}
	}

	err = bevent_call_emit(BEVENT_CALL_REMOTE_SDP, call, "answer");
	if (err)
		goto out;
	err = call_apply_sdp(call);
	if (!err)
		err = update_streams(call);
	if (err)
		goto out;
#ifdef USE_DATACHANNEL
	err = call_description_prepare(description, call, msg->scode < 200);
#endif
	if (err)
		goto out;
#ifdef USE_DATACHANNEL
	call_description_publish(description, call);
#endif
	if (detect_hold_resume(&hold_ev, call, old_rdir, false))
		(void)bevent_call_emit(hold_ev, call, "");
	if (!pl_strcmp(&msg->cseq.met, "INVITE") &&
	    msg->scode >= 200 && msg->scode < 300 &&
	    call_state(call) != CALL_STATE_ESTABLISHED)
		call_event_handler(call, CALL_EVENT_ANSWERED, "%s",
				   call->peer_uri);

out:
#ifdef USE_DATACHANNEL
	if (err && description)
		call_description_abort(description, call);
	mem_deref(description);
#endif
	return err;
}


static void set_established_mdir(void *arg)
{
	struct call *call = arg;
	if (!call)
		return;
	MAGIC_CHECK(call);

	if (call_need_modify(call)) {
		call_set_mdir(call, call->estadir, call->estvdir);
		call_modify(call);
	}
}


static uint32_t randwait(uint32_t minwait, uint32_t maxwait)
{

	return minwait + rand_u16() % (maxwait - minwait);
}


static bool call_emit_sip_info(struct call *call, const struct sip_msg *msg)
{
	struct pl body;
	char content_type[256] = "";

	if (!call || !msg || !call->sip_infoh)
		return false;

	if (pl_isset(&msg->ctyp.type) && pl_isset(&msg->ctyp.subtype)) {
		re_snprintf(
			content_type,
			sizeof(content_type),
			"%r/%r%s%r",
			&msg->ctyp.type,
			&msg->ctyp.subtype,
			pl_isset(&msg->ctyp.params) ? ";" : "",
			&msg->ctyp.params
		);
	}

	pl_set_mbuf(&body, msg->mb);

	return call->sip_infoh(
		call,
		content_type[0] ? content_type : NULL,
		(const uint8_t *)body.p,
		body.l,
		msg,
		call->sip_info_arg
	);
}


static void sipsess_estab_handler(const struct sip_msg *msg, void *arg)
{
	struct call *call = arg;
	uint32_t wait;
	(void)msg;

	MAGIC_CHECK(call);

	if (call->state == CALL_STATE_ESTABLISHED)
		return;

	set_state(call, CALL_STATE_ESTABLISHED);

	if (call->got_offer)
		(void)update_streams(call);

	call_timer_start(call);

	if (call->rtp_timeout_ms) {

		struct le *le;

		FOREACH_STREAM {
			struct stream *strm = le->data;
			stream_enable_rtp_timeout(strm, call->rtp_timeout_ms);
		}
	}

	/* the transferor will hangup this call */
	if (call->not) {
		(void)call_notify_sipfrag(call, 200, "OK");
	}

	wait = call_is_outgoing(call) ? 150 : 0;
	wait += randwait(50, 150);

	/* modify call after call_event_established handlers are executed */
	tmr_start(&call->tmr_reinv, wait, set_established_mdir, call);

	/* must be done last, the handler might deref this call */
	call_event_handler(call, CALL_EVENT_ESTABLISHED, "%s", call->peer_uri);
}


static void dtmfend_handler(void *arg)
{
	struct call *call = arg;

	if (call->dtmfh)
		call->dtmfh(call, KEYCODE_REL, call->arg);
}


static void sipsess_send_info_handler(int err, const struct sip_msg *msg,
				void *arg)
{
	(void)arg;

	if (err)
		warning("call: sending DTMF INFO failed (%m)", err);
	else if (msg && msg->scode != 200)
		warning("call: sending DTMF INFO failed (scode: %d)",
				msg->scode);
}


static void sipsess_info_handler(struct sip *sip, const struct sip_msg *msg,
				 void *arg)
{
	struct call *call = arg;

	if (msg_ctype_cmp(&msg->ctyp, "application", "dtmf-relay")) {

		struct pl body, sig, dur;
		int err;

		pl_set_mbuf(&body, msg->mb);

		err  = re_regex(body.p, body.l,
		       "Signal=[ ]*[0-9*#a-d]+", NULL, &sig);
		err |= re_regex(body.p, body.l,
		       "Duration=[ ]*[0-9]+", NULL, &dur);

		if (err || !pl_isset(&sig) || sig.l == 0) {
			(void)sip_reply(sip, msg, 400, "Bad Request");
		}
		else {
			char s = toupper(sig.p[0]);
			uint32_t duration = pl_u32(&dur);

			info("call: received SIP INFO DTMF: '%c' "
			     "(duration=%r)\n", s, &dur);

			(void)sip_reply(sip, msg, 200, "OK");

			if (call->dtmfh) {
				tmr_start(&call->tmr_dtmf, duration,
					  dtmfend_handler, call);
				call->dtmfh(call, s, call->arg);
			}
		}
	}
	else if (call_emit_sip_info(call, msg) || !mbuf_get_left(msg->mb)) {
		(void)sip_reply(sip, msg, 200, "OK");
	}
	else {
		(void)sip_reply(sip, msg, 488, "Not Acceptable Here");
	}
}


static void sipnot_close_handler(int err, const struct sip_msg *msg,
				 void *arg)
{
	struct call *call = arg;

	call->not = mem_deref(call->not);

	if (err)
		call_event_handler(call, CALL_EVENT_TRANSFER_FAILED,
				   "%m", err);
	else if (msg && msg->scode >= 300)
		call_event_handler(call, CALL_EVENT_TRANSFER_FAILED,
				   "%u %r", msg->scode, &msg->reason);
}


static void sipsess_refer_handler(struct sip *sip, const struct sip_msg *msg,
				  void *arg)
{
	struct call *call = arg;
	const struct sip_hdr *hdr;
	int err;

	/* get the transfer target */
	hdr = sip_msg_hdr(msg, SIP_HDR_REFER_TO);
	if (!hdr) {
		warning("call: bad REFER request from %r\n", &msg->from.auri);
		(void)sip_reply(sip, msg, 400, "Missing Refer-To header");
		return;
	}

	/* The REFER creates an implicit subscription.
	 * Reply 202 to the REFER request
	 */
	call->not = mem_deref(call->not);
	err = sipevent_accept(&call->not, uag_sipevent_sock(), msg,
			      sipsess_dialog(call->sess), NULL,
			      202, "Accepted", 60, 60, 60,
			      ua_cuser(call->ua), "message/sipfrag",
			      auth_handler, call->acc, true,
			      sipnot_close_handler, call,
			      "Allow: %H\r\n", ua_print_allowed, call->ua);
	if (err) {
		warning("call: refer: sipevent_accept failed: %m\n", err);
		return;
	}

	(void)call_notify_sipfrag(call, 100, "Trying");

	set_state(call, CALL_STATE_TRANSFER);
	call_event_handler(call, CALL_EVENT_TRANSFER, "%r", &hdr->val);
}


static void xfer_cleanup(struct call *call, char *reason)
{
	if (call->xcall->state == CALL_STATE_TRANSFER) {
		set_state(call->xcall, CALL_STATE_ESTABLISHED);
		call_event_handler(call->xcall, CALL_EVENT_TRANSFER_FAILED,
                                   "%s", reason);
	}

	call->xcall->xcall = NULL;
}


static void sipsess_close_handler(int err, const struct sip_msg *msg,
				  void *arg)
{
	struct call *call = arg;
	char reason[128] = "";
	const struct sip_hdr *reason_hdr = NULL;

	MAGIC_CHECK(call);

	if (err) {
		info("%s: session closed: %m\n", call->peer_uri, err);

		(void)re_snprintf(reason, sizeof(reason), "%m", err);

		if (call->not) {
			(void)call_notify_sipfrag(call, 500, "%m", err);
		}
	}
	else if (msg) {

		call->scode = msg->scode;

		(void)re_snprintf(reason, sizeof(reason), "%u %r",
				  msg->scode, &msg->reason);

		info("%s: session closed: %u %r\n",
		     call->peer_uri, msg->scode, &msg->reason);

		if (call->not) {
			(void)call_notify_sipfrag(call, msg->scode,
						  "%r", &msg->reason);
		}
	}
	else {
		info("%s: session closed\n", call->peer_uri);
	}

	if (call->xcall)
		xfer_cleanup(call, reason);

	call_stream_stop(call);

	if (msg)
		reason_hdr = sip_msg_hdr(msg, SIP_HDR_REASON);

	if (reason_hdr) {
		info("Cancel reason: %r\n", &reason_hdr->val);
		call_event_handler(call, CALL_EVENT_CLOSED, "%s,%r",
						   reason, &reason_hdr->val);
	}
	else {
		call_event_handler(call, CALL_EVENT_CLOSED, "%s", reason);
	}
}


static void prack_handler(const struct sip_msg *msg, void *arg)
{
	struct call *call = arg;

	if (!msg || !call)
		return;

	if (call->ans_queued && !call->answered)
		(void)call_answer(call, 200, VIDMODE_ON);
	else if (call->progr_queued && !call->answered)
		(void)call_progress(call);

	return;
}


static bool have_common_audio_codecs(const struct call *call)
{
	const struct sdp_format *sc;
	struct aucodec *ac;

	sc = sdp_media_rcodec(stream_sdpmedia(audio_strm(call->audio)));
	if (!sc)
		return false;

	ac = sc->data;  /* note: this will exclude telephone-event */

	return ac != NULL;
}


static bool have_common_video_codecs(const struct call *call)
{
	const struct sdp_format *sc;
	struct vidcodec *vc;

	sc = sdp_media_rcodec(stream_sdpmedia(video_strm(call->video)));
	if (!sc)
		return false;

	vc = sc->data;

	return vc != NULL;
}


static bool valid_addressfamily(struct call *call, const struct stream *strm)
{
	struct sdp_media *m;
	const struct sa *raddr;
	m = stream_sdpmedia(strm);
	raddr = sdp_media_raddr(m);

	if (sa_isset(raddr, SA_ADDR) &&  sa_af(raddr) != call->af) {
		info("call: incompatible address-family for %s"
				" (local=%s, remote=%s)\n",
				sdp_media_name(m),
				net_af2name(call->af),
				net_af2name(sa_af(raddr)));

		return false;
	}

	return true;
}


/**
 * Find a call given the value of a Replaces header
 *
 * @param calls   List of calls
 * @param hdr     Value of Replaces header
 *
 * @return Call object if found, NULL if not found
 */
static struct call *call_find_replaces(const struct list *calls,
				       const struct pl *hdr)
{
	struct le *le;
	struct sip_msg msg = { 0 };
	struct call *ret = NULL;

	if (!calls || !pl_isset(hdr))
		return NULL;

	msg.req = true;
	msg.callid.p = hdr->p;
	msg.callid.l = hdr->l;

	if (pl_strchr(&msg.callid, ';')) {
		msg.callid.l = (size_t)(pl_strchr(&msg.callid, ';')
				- msg.callid.p);
	}

	if (!pl_isset(&msg.callid))
		return NULL;

	(void)re_regex(hdr->p, hdr->l, ";to-tag=[^; ]+", &msg.to.tag);
	(void)re_regex(hdr->p, hdr->l, ";from-tag=[^; ]+", &msg.from.tag);

	for (le = list_head(calls); le; le = le->next) {
		struct call *call = le->data;

		if (pl_isset(&msg.from.tag) && pl_isset(&msg.to.tag) &&
		    sip_dialog_cmp(sipsess_dialog(call->sess), &msg)) {
			ret = call;
			break;
		}
		else if (pl_isset(&msg.to.tag) &&
			 sip_dialog_cmp_half(sipsess_dialog(call->sess),
					     &msg)) {
			ret = call;
			break;
		}
		else if (0 == pl_strcmp(&msg.callid, call->id)) {
			ret = call;
			break;
		}
	}

	return ret;
}


int call_accept(struct call *call, struct sipsess_sock *sess_sock,
		const struct sip_msg *msg)
{
	const struct sip_hdr *hdr;
#ifdef USE_DATACHANNEL
	struct call_description_state *description = NULL;
#endif
	int err;

	if (!call || !msg)
		return EINVAL;

	call->outgoing = false;
	if (pl_isset(&msg->from.dname)) {
		err = pl_strdup(&call->peer_name, &msg->from.dname);
		if (err)
			return err;
	}

	err = call_streams_alloc(call);
	if (err)
		return err;

	if (call->got_offer) {

#ifdef USE_DATACHANNEL
		err = call_description_begin(&description, call, msg->mb);
		if (err)
			return err;
#endif
		err = sdp_decode(call->sdp, msg->mb, true);
		if (err) {
#ifdef USE_DATACHANNEL
			call_description_abort(description, call);
			mem_deref(description);
#endif
			return err;
		}
#ifdef USE_DATACHANNEL
		err = call_update_remote_data(call, true);
		if (err) {
			call_description_abort(description, call);
			mem_deref(description);
			return err;
		}
#endif

		/*
		 * Each media description in the SDP answer MUST
		 * use the same network type as the corresponding
		 * media description in the offer.
		 *
		 * See RFC 6157
		 */
		if (!valid_addressfamily(call, audio_strm(call->audio)) ||
		    !valid_addressfamily(call, video_strm(call->video))) {
			sip_treply(NULL, uag_sip(), msg, 488,
				   "Not Acceptable Here");

			call_event_handler(call, CALL_EVENT_CLOSED,
					   "Wrong address family");
#ifdef USE_DATACHANNEL
			call_description_abort(description, call);
			mem_deref(description);
#endif
			return 0;
		}

		/* Check if we have any common audio or video codecs, after
		 * the SDP offer has been parsed
		 */

		if (!have_common_audio_codecs(call) &&
			!have_common_video_codecs(call)) {
			info("call: no common audio or video codecs "
				"- rejected\n");

			sip_treply(NULL, uag_sip(), msg,
				   488, "Not Acceptable Here");

			call_event_handler(call, CALL_EVENT_CLOSED,
					   "No common audio or video codecs");
#ifdef USE_DATACHANNEL
			call_description_abort(description, call);
			mem_deref(description);
#endif

			return 0;
		}

			if (call->config_avt.bundle) {
				err = bundle_sdp_decode(call->sdp,
						       &call->streaml);
				if (err) {
					warning("call: remote BUNDLE update failed"
						" (%m)\n", err);
#ifdef USE_DATACHANNEL
					call_description_abort(description, call);
					mem_deref(description);
#endif
					return err;
				}
			}

#ifdef USE_DATACHANNEL
		err = call_description_commit(description, call, true);
		if (err) {
			call_description_abort(description, call);
			mem_deref(description);
			return err;
		}
		description = mem_deref(description);
#endif

		bevent_call_emit(BEVENT_CALL_REMOTE_SDP, call, "offer");
	}

	hdr = sip_msg_hdr(msg, SIP_HDR_REPLACES);
	if (hdr && pl_isset(&hdr->val)) {
		struct call *rcall = call_find_replaces(ua_calls(call->ua),
							&hdr->val);
		if (!rcall) {
			info("call: Replaces header present, but could not "
				"find matching call %r\n", &hdr->val);

			sip_treply(NULL, uag_sip(), msg,
				   481, "Call/Transaction Does Not Exist");

			call_event_handler(call, CALL_EVENT_CLOSED,
				"Replaces header without matching dialog.");

			return 0;
		}
		else {
			call_stream_stop(rcall);
			call_event_handler(rcall, CALL_EVENT_CLOSED,
				"%r replaced", &hdr->val);
		}
	}

	err = sipsess_accept(&call->sess, sess_sock, msg, 180, "Ringing",
			     account_rel100_mode(call->acc),
			     ua_cuser(call->ua), "application/sdp", NULL,
			     auth_handler, call->acc, true,
			     sipsess_offer_handler, sipsess_answer_handler,
			     sipsess_estab_handler, sipsess_info_handler,
			     call->acc->refer ? sipsess_refer_handler : NULL,
			     sipsess_close_handler,
			     call, "Allow: %H\r\n%H",
			     ua_print_allowed, call->ua,
			     ua_print_require, call->ua);

	if (err) {
		warning("call: sipsess_accept: %m\n", err);
		return err;
	}

	err = str_dup(&call->id,
		      sip_dialog_callid(sipsess_dialog(call->sess)));
	if (err)
		return err;

	set_state(call, CALL_STATE_INCOMING);

	err = sipsess_set_prack_handler(call->sess, prack_handler);
	if (err)
		return err;

	/* New call */
	if (call->config_call.local_timeout) {
		tmr_start(&call->tmr_inv, call->config_call.local_timeout*1000,
			  invite_timeout, call);
	}

	call->msg_src = msg->src;

	call->estadir = stream_ldir(audio_strm(call_audio(call)));
	call->estvdir = stream_ldir(video_strm(call_video(call)));
	if (!call->acc->mnat)
		call_event_handler(call, CALL_EVENT_INCOMING, "%s",
                                   call->peer_uri);

	return 0;
}


bool call_sess_cmp(const struct call *call, const struct sip_msg *msg)
{
	if (!call || !msg)
		return false;

	return sipsess_msg(call->sess) == msg;
}


static void delayed_answer_handler(void *arg)
{
	struct call *call = arg;

	if (sipsess_awaiting_prack(call->sess))
		call->ans_queued = true;
	else
		(void)call_answer(call, 200, VIDMODE_ON);
}


static void sipsess_progr_handler(const struct sip_msg *msg, void *arg)
{
	struct call *call = arg;
#ifdef USE_DATACHANNEL
	struct call_description_state *description = NULL;
#endif
	bool media;
	int err = 0;

	MAGIC_CHECK(call);

	info("call: SIP Progress: %u %r (%r/%r)\n",
	     msg->scode, &msg->reason, &msg->ctyp.type, &msg->ctyp.subtype);

	call->msg_src = msg->src;

	if (msg->scode <= 100)
		return;

	/* check for 18x and content-type
	 *
	 * 1. start media-stream if application/sdp
	 * 2. play local ringback tone if not
	 *
	 * we must also handle changes to/from 180 and 183,
	 * so we reset the media-stream/ringback each time.
	 */
	media = msg_ctype_cmp(&msg->ctyp, "application", "sdp") &&
		mbuf_get_left(msg->mb);
	if (!media && msg_ctype_cmp(&msg->ctyp, "multipart", "mixed")) {
		err = sdp_decode_multipart(&msg->ctyp.params, msg->mb);
		media = !err && mbuf_get_left(msg->mb);
	}
#ifdef USE_DATACHANNEL
	if (media)
		err = call_description_begin(&description, call, msg->mb);
#endif
	if (media && !err)
		err = sdp_decode(call->sdp, msg->mb, false);
	if (err)
		media = false;

	switch (msg->scode) {

	case 180:
		set_state(call, CALL_STATE_RINGING);
		break;

	case 183:
		set_state(call, CALL_STATE_EARLY);

		break;
	}

	if (media) {
#ifdef USE_DATACHANNEL
		err = call_update_remote_data(call, false);
		if (err) {
			warning("call: progress data update failed"
				" (%m)\n", err);
			media = false;
		}
#endif
		if (media && call->config_avt.bundle) {
			err = bundle_sdp_decode(call->sdp, &call->streaml);

			if (err) {
				warning("call: progress BUNDLE update failed"
					" (%m)\n", err);
				media = false;
			}
		}
	}

#ifdef USE_DATACHANNEL
	if (media && description) {
		err = call_description_commit(description, call, true);
		if (err)
			media = false;
	}
	if (!media && description)
		call_description_abort(description, call);
	mem_deref(description);
#endif

	if (media) {
		mem_ref(call);
		call_event_handler(call, CALL_EVENT_PROGRESS, "%s",
                                   call->peer_uri);
		mem_deref(call);
	}
	else {
		call_stream_stop(call);
		call_event_handler(call, CALL_EVENT_RINGING, "%s",
                                   call->peer_uri);
	}
}


static void redirect_handler(const struct sip_msg *msg, const char *uri,
	void *arg)
{
	struct call *call = arg;

	info("call: redirect to %s\n", uri);
	bevent_call_emit(BEVENT_CALL_REDIRECT, call,
			 "%d,%s", msg->scode, uri);
	return;
}


static int sipsess_desc_handler(struct mbuf **descp, const struct sa *src,
				const struct sa *dst, void *arg)
{
	struct call *call = arg;
	int err;
	(void) dst;

	MAGIC_CHECK(call);
	call->af     = sa_af(src);
	if (!call->acc->mnat)
		sdp_session_set_laddr(call->sdp, src);

	if (list_isempty(&call->streaml)) {
		err = call_streams_alloc(call);
		if (err)
			return err;

		call_set_mdir(call, call->estadir, call->estvdir);
		err = bevent_call_emit(BEVENT_CALL_LOCAL_SDP, call, "offer");
		if (err)
			return err;
	}

	err = call_sdp_get(call, descp, true);
	if (err)
		return err;
#if 0
	info("- - - - - S D P - O f f e r - - - - -\n"
	     "%b"
	     "- - - - - - - - - - - - - - - - - - -\n",
	     (*descp)->buf, (*descp)->end);
#endif

	return 0;
}


static int call_print_replaces(struct re_printf *pf, const struct call *call) {
	int err = 0;

	if (!call || !call->replaces)
		return 0;

	err = re_hprintf(pf, "Replaces: %s\r\n", call->replaces);

	return err;
}


static int send_invite(struct call *call)
{
	const char *routev[1];
	int err;

	routev[0] = account_outbound(call->acc, 0);

	if (!list_isempty(&call->streaml)) {
		err = bevent_call_emit(BEVENT_CALL_LOCAL_SDP, call, "offer");
		if (err)
			return err;
	}

	err = sipsess_connect(&call->sess, uag_sipsess_sock(),
			      call->peer_uri,
			      call->local_name,
			      call->local_uri,
			      ua_cuser(call->ua),
			      routev[0] ? routev : NULL,
			      routev[0] ? 1 : 0,
			      "application/sdp",
			      auth_handler, call->acc, true,
			      call->id,
			      sipsess_desc_handler,
			      sipsess_offer_handler, sipsess_answer_handler,
			      sipsess_progr_handler, sipsess_estab_handler,
			      sipsess_info_handler,
			      call->acc->refer ? sipsess_refer_handler : NULL,
			      sipsess_close_handler, call,
			      "Allow: %H\r\n%H%H%H%H",
			      ua_print_allowed, call->ua,
			      ua_print_supported, call->ua,
			      ua_print_require, call->ua,
			      call_print_replaces, call,
			      custom_hdrs_print, &call->custom_hdrs);
	if (err) {
		warning("call: sipsess_connect: %m\n", err);
		return err;
	}

	err = sipsess_set_redirect_handler(call->sess, redirect_handler);
	if (err)
		return err;

	err = sipsess_set_prack_handler(call->sess, prack_handler);
	if (err)
		return err;

	/* save call setup timer */
	call->time_conn = time(NULL);

	return 0;
}


static int send_dtmf_info(struct call *call, char key)
{
	struct mbuf *body;
	int err;

	if ((key < '0' || key > '9') &&
	    (key < 'a' || key > 'd') &&
	    (key < 'A' || key > 'D') &&
	    (key != '*') &&
	    (key != '#'))
		return EINVAL;

	body = mbuf_alloc(25);
	mbuf_printf(body, "Signal=%c\r\nDuration=250\r\n", key);
	mbuf_set_pos(body, 0);

	err = sipsess_info(call->sess, "application/dtmf-relay", body,
			   sipsess_send_info_handler, call);
	if (err) {
		warning("call: sipsess_info for DTMF failed (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(body);

	return err;
}


/**
 * Find the peer capabilites of early video in the remote SDP
 *
 * @param call Call object
 *
 * @return True if peer accepts early video, otherwise false
 */
bool call_early_video_available(const struct call *call)
{
	struct le *le;
	struct sdp_media *v;

	if (!call)
		return false;

	LIST_FOREACH(sdp_session_medial(call->sdp, false), le) {
		v = le->data;
		if (0 == str_cmp(sdp_media_name(v), "video") &&
			(sdp_media_rdir(v) & SDP_RECVONLY))
			return true;
	}

	return false;
}


/**
 * Get the current call duration in seconds
 *
 * @param call  Call object
 *
 * @return Duration in seconds
 */
uint32_t call_duration(const struct call *call)
{
	if (!call || !call->time_start)
		return 0;

	return (uint32_t)(time(NULL) - call->time_start);
}


/**
 * Get the current call setup time in seconds
 *
 * @param call  Call object
 *
 * @return Call setup in seconds
 */
uint32_t call_setup_duration(const struct call *call)
{
	if (!call || !call->time_conn || call->time_conn <= 0 )
		return 0;

	return (uint32_t)(call->time_start - call->time_conn);
}


/**
 * Get the audio object for the current call
 *
 * @param call  Call object
 *
 * @return Audio object
 */
struct audio *call_audio(const struct call *call)
{
	return call ? call->audio : NULL;
}


/**
 * Get the video object for the current call
 *
 * @param call  Call object
 *
 * @return Video object
 */
struct video *call_video(const struct call *call)
{
	return call ? call->video : NULL;
}


/**
 * Get the list of media streams for the current call
 *
 * @param call  Call object
 *
 * @return List of media streams
 */
struct list *call_streaml(const struct call *call)
{
	return call ? (struct list *)&call->streaml : NULL;
}


int call_reset_transp(struct call *call, const struct sa *laddr)
{
	if (!call)
		return EINVAL;

	sdp_session_set_laddr(call->sdp, laddr);

	return call_modify(call);
}


const struct sa *call_laddr(const struct call *call)
{
	if (!call)
		return NULL;

	return sdp_session_laddr(call->sdp);
}


/**
 * Send a SIP NOTIFY with a SIP message fragment
 *
 * @param call   Call object
 * @param scode  SIP Status code
 * @param reason Formatted SIP Reason phrase
 * @param ...    Variable arguments
 *
 * @return 0 if success, otherwise errorcode
 */
int call_notify_sipfrag(struct call *call, uint16_t scode,
			const char *reason, ...)
{
	struct mbuf *mb;
	va_list ap;
	int err;

	if (!call)
		return EINVAL;

	mb = mbuf_alloc(512);
	if (!mb)
		return ENOMEM;

	va_start(ap, reason);
	(void)mbuf_printf(mb, "SIP/2.0 %u %v\r\n", scode, reason, &ap);
	va_end(ap);

	mb->pos = 0;

	if (scode >= 200) {
		err = sipevent_notify(call->not, mb, SIPEVENT_TERMINATED,
				      SIPEVENT_NORESOURCE, 0);

		call->not = mem_deref(call->not);
	}
	else {
		err = sipevent_notify(call->not, mb, SIPEVENT_ACTIVE,
				      SIPEVENT_NORESOURCE, 0);
	}

	mem_deref(mb);

	return err;
}


static void sipsub_notify_handler(struct sip *sip, const struct sip_msg *msg,
				  void *arg)
{
	struct call *call = arg;
	struct pl scode, reason;
	uint32_t sc;

	if (re_regex((char *)mbuf_buf(msg->mb), mbuf_get_left(msg->mb),
		     "SIP/2.0 [0-9]+ [^\r\n]+", &scode, &reason)) {
		(void)sip_reply(sip, msg, 400, "Bad sipfrag");
		return;
	}

	(void)sip_reply(sip, msg, 200, "OK");

	sc = pl_u32(&scode);

	if (sc >= 300) {
		info("call: transfer failed: %u %r\n", sc, &reason);
		call_event_handler(call, CALL_EVENT_TRANSFER_FAILED,
				   "%u %r", sc, &reason);
	}
	else if (sc >= 200) {
		call_event_handler(call, CALL_EVENT_CLOSED, "Call transfered");
	}
}


static void sipsub_close_handler(int err, const struct sip_msg *msg,
				 const struct sipevent_substate *substate,
				 void *arg)
{
	struct call *call = arg;

	(void)substate;

	call->sub = mem_deref(call->sub);

	if (err) {
		info("call: subscription closed: %m\n", err);
	}
	else if (msg && msg->scode >= 300) {
		info("call: transfer failed: %u %r\n",
		     msg->scode, &msg->reason);
		call_event_handler(call, CALL_EVENT_TRANSFER_FAILED,
				   "%u %r", msg->scode, &msg->reason);
	}
}


static int normalize_uri(char **out, const char *uri, const struct uri *luri)
{
	struct uri uri2;
	struct pl pl;
	int err;

	if (!out || !uri || !luri)
		return EINVAL;

	pl_set_str(&pl, uri);

	if (0 == uri_decode(&uri2, &pl)) {

		err = str_dup(out, uri);
	}
	else {
		uri2 = *luri;

		uri2.user     = pl;
		uri2.params   = pl_null;

		err = re_sdprintf(out, "%H", uri_encode, &uri2);
	}

	return err;
}


/**
 * Transfer the call to a target SIP uri
 *
 * @param call  Call object
 * @param uri   Target SIP uri
 *
 * @return 0 if success, otherwise errorcode
 */
int call_transfer(struct call *call, const char *uri)
{
	char *nuri;
	int err;

	if (!call || !uri)
		return EINVAL;

	err = normalize_uri(&nuri, uri, &call->acc->luri);
	if (err)
		return err;

	info("transferring call to %s\n", nuri);

	call->sub = mem_deref(call->sub);
	err = sipevent_drefer(&call->sub, uag_sipevent_sock(),
			      sipsess_dialog(call->sess), ua_cuser(call->ua),
			      auth_handler, call->acc, true,
			      sipsub_notify_handler, sipsub_close_handler,
			      call,
		              "Refer-To: %s\r\nReferred-by: %s\r\n",
			      nuri, account_aor(ua_account(call->ua)));
	if (err) {
		warning("call: sipevent_drefer: %m\n", err);
	}

	mem_deref(nuri);

	return err;
}


/**
 * Transfer the call to a target SIP uri and replace the source call
 *
 * @param call        Call object
 * @param source_call Source call object
 *
 * @return 0 if success, otherwise errorcode
 */
int call_replace_transfer(struct call *call, struct call *source_call)
{
	int err;

	if (!call || !source_call)
		return EINVAL;

	info("transferring call to %s\n", source_call->peer_uri);

	call->sub = mem_deref(call->sub);

	err = sipevent_drefer(&call->sub, uag_sipevent_sock(),
		sipsess_dialog(call->sess), ua_cuser(call->ua),
		auth_handler, call->acc, true,
		sipsub_notify_handler, sipsub_close_handler, call,
	"Refer-To: <%s?Replaces=%s%%3Bto-tag%%3D%s%%3Bfrom-tag%%3D%s>\r\n"
		"Referred-By: %s\r\n",
		source_call->peer_uri,
		source_call->id,
		sip_dialog_rtag(sipsess_dialog(source_call->sess)),
		sip_dialog_ltag(sipsess_dialog(source_call->sess)),
		account_aor(ua_account(call->ua)));

	if (err) {
		warning("call: sipevent_drefer: %m\n", err);
	}

	return err;
}


/**
 * Get the SIP status code for the outgoing call
 *
 * @param call Call object
 *
 * @return SIP Status code
 */
uint16_t call_scode(const struct call *call)
{
	return call ? call->scode : 0;
}


/**
 * Get state of the call
 *
 * @param call Call object
 *
 * @return Call state or CALL_STATE_UNKNOWN if call object is NULL
 */
enum call_state call_state(const struct call *call)
{
	if (!call)
		return CALL_STATE_UNKNOWN;

	return call->state;
}


/**
 * Set the callback handlers for a call object
 *
 * @param call  Call object
 * @param eh    Event handler
 * @param dtmfh DTMF handler
 * @param arg   Handler argument
 */
void call_set_handlers(struct call *call, call_event_h *eh,
		       call_dtmf_h *dtmfh, void *arg)
{
	if (!call)
		return;

	if (eh)
		call->eh    = eh;

	if (dtmfh)
		call->dtmfh = dtmfh;

	if (arg)
		call->arg   = arg;
}


void call_set_xrtpstat(struct call *call)
{
	if (!call)
		return;

	sipsess_set_close_headers(call->sess,
				  "X-RTP-Stat: %H\r\n",
				  rtpstat_print, call);
}


/**
 * Check if a call is locally on hold
 *
 * @param call Call object
 *
 * @return True if on hold (local), otherwise false
 */
bool call_is_onhold(const struct call *call)
{
	return call ? call->on_hold : false;
}


/**
 * Check if a call has direction outgoing
 *
 * @param call Call object
 *
 * @return True if outgoing, otherwise false
 */
bool call_is_outgoing(const struct call *call)
{
	return call ? call->outgoing : false;
}


/**
 * Enable RTP timeout for a call
 *
 * @param call       Call object
 * @param timeout_ms RTP Timeout in [milliseconds]
 */
void call_enable_rtp_timeout(struct call *call, uint32_t timeout_ms)
{
	if (!call)
		return;

	call->rtp_timeout_ms = timeout_ms;
}


/**
 * Get the line number for this call
 *
 * @param call Call object
 *
 * @return Line number from 1 to N
 */
uint32_t call_linenum(const struct call *call)
{
	return call ? call->linenum : 0;
}


/**
 * Get the answer delay of this call
 *
 * @param call Call object
 *
 * @return answer delay in ms
 */
int32_t call_answer_delay(const struct call *call)
{
	return call ? call->adelay : -1;
}


/**
 * Set/override the answer delay of call
 *
 * @param call    Call object
 * @param adelay  Answer delay in ms. A value of -1 means auto answer is
 *                disabled
 *
 */
void call_set_answer_delay(struct call *call, int32_t adelay)
{
	if (!call)
		return;

	call->adelay = adelay;
}


/**
 * Find the call with a given line number
 *
 * @param calls   List of calls
 * @param linenum Line number from 1 to N
 *
 * @return Call object if found, NULL if not found
 */
struct call *call_find_linenum(const struct list *calls, uint32_t linenum)
{
	struct le *le;

	for (le = list_head(calls); le; le = le->next) {
		struct call *call = le->data;

		if (linenum == call->linenum)
			return call;
	}

	return NULL;
}


/**
 * Find a call by call-id
 *
 * @param calls   List of calls
 * @param id      Call-id pointer-length string
 *
 * @return Call object if found, NULL if not found
 */
struct call *call_find_id_pl(const struct list *calls, const struct pl *id)
{
	struct le *le;

	for (le = list_head(calls); le; le = le->next) {
		struct call *call = le->data;

		if (0 == pl_strcmp(id, call->id))
			return call;
	}

	return NULL;
}


/**
 * Find a call by call-id
 *
 * @param calls   List of calls
 * @param id      Call-id string
 *
 * @return Call object if found, NULL if not found
 */
struct call *call_find_id(const struct list *calls, const char *id)
{
	struct le *le;

	for (le = list_head(calls); le; le = le->next) {
		struct call *call = le->data;

		if (0 == str_cmp(id, call->id))
			return call;
	}

	return NULL;
}


/**
 * Set the current call
 *
 * @param calls List of calls
 * @param call  Call to set as current
 */
void call_set_current(struct list *calls, struct call *call)
{
	if (!calls || !call)
		return;

	list_unlink(&call->le);
	list_append(calls, &call->le, call);
}


/**
 * Set stream sdp media line direction attribute and established media dir
 *
 * @param call Call object
 * @param a    Audio SDP direction
 * @param v    Video SDP direction if video available
 */
void call_set_media_direction(struct call *call, enum sdp_dir a,
			      enum sdp_dir v)
{
	if (!call)
		return;

	call_set_media_estdir(call, a, v);
	call_set_mdir(call, a, v);
}


/**
 * Set stream sdp media line direction attribute
 *
 * @param call Call object
 * @param a    Audio SDP direction
 * @param v    Video SDP direction if video available
 */
void call_set_mdir(struct call *call, enum sdp_dir a, enum sdp_dir v)
{
	if (!call)
		return;

	stream_set_ldir(audio_strm(call_audio(call)), a);

	if (video_strm(call_video(call))) {
		if (vidisp_find(baresip_vidispl(), NULL) == NULL)
			stream_set_ldir(video_strm(
				call_video(call)), v & SDP_SENDONLY);
		else
			stream_set_ldir(video_strm(call_video(call)), v);

	}
}


/**
 * Returns local audio and video directions
 *
 * @param call Call object
 * @param ap   Pointer for returning local audio direction
 * @param vp   Pointer for returning local video direction
 */
void call_get_mdir(struct call *call, enum sdp_dir *ap, enum sdp_dir *vp)
{
	struct stream *strm;

	if (!call)
		return;

	strm = audio_strm(call_audio(call));
	if (strm && ap)
		*ap = stream_ldir(strm);

	strm = video_strm(call_video(call));
	if (strm && vp)
		*vp = stream_ldir(strm);
}


/**
 * Returns audio and video directions for the established state
 *
 * @param call Call object
 * @param ap   Pointer for returning audio direction
 * @param vp   Pointer for returning video direction
 */
void call_get_media_estdir(struct call *call,
			   enum sdp_dir *ap, enum sdp_dir *vp)
{
	if (!call)
		return;

	if (ap)
		*ap = call->estadir;

	if (vp)
		*vp = call->estvdir;
}


/**
 * Set audio/video direction during pre-established for the established state
 *
 * @param call Call object
 * @param a    Audio SDP direction
 * @param v    Video SDP direction if video available
 */
void call_set_media_estdir(struct call *call, enum sdp_dir a, enum sdp_dir v)
{
	if (!call)
		return;

	call->estadir = a;
	call->estvdir = call->use_video ? v : SDP_INACTIVE;
}


void call_start_answtmr(struct call *call, uint32_t ms)
{
	if (!call)
		return;

	tmr_start(&call->tmr_answ, ms, delayed_answer_handler, call);
}


/**
 * Checks if given Supported header tags are supported in the call
 *
 * @param call Call object
 * @param tags tags
 *
 * @return true if check succeeds, false otherwise
 */
bool call_supported(const struct call *call, uint16_t tags)
{
	if (!call)
		return false;

	return (call->supported & tags) == tags;
}


/**
 * Get the user data for the call
 *
 * @param call Call object
 *
 * @return Call's user data
 */
const struct pl *call_user_data(const struct call *call)
{
	return call ? call->user_data : NULL;
}


/**
 * Set the user data of the call
 *
 * @param call Call object
 * @param user_data User data to be set
 * @return int
 */
int call_set_user_data(struct call *call, const struct pl *user_data)
{
	if (!call)
		return EINVAL;

	call->user_data = mem_deref(call->user_data);
	if (!pl_isset(user_data))
		return 0;

	call->user_data = pl_alloc_dup(user_data);
	return call->user_data ? 0 : ENOMEM;
}


/**
 * Get the message source address of the peer
 *
 * @param call Call object
 * @param sa   Pointer to sa object. Will be set on return.
 *
 * @return 0 on success, non-zero otherwise
 */
int call_msg_src(const struct call *call, struct sa *sa)
{
	if (!call || !sa)
		return EINVAL;

	*sa = call->msg_src;

	return 0;
}


/**
 * Get the SIP transport protocol used for this call
 *
 * @param call Call object
 *
 * @return Transport protocol
 */
enum sip_transp call_transp(const struct call *call)
{
	return call ? sip_dialog_tp(sipsess_dialog(call->sess))
		: SIP_TRANSP_NONE;
}


/*
 * Get the SDP negotiation state of the call
 *
 * @param call Call object
 *
 * @return SDP negotiation state
 */
enum sdp_neg_state call_sdp_neg_state(const struct call *call)
{
	return call ? sipsess_sdp_neg_state(call->sess) : SDP_NEG_NONE;
}


/**
 * Check if an SDP change is allowed currently
 *
 * @param call Call object
 *
 * @return true if SDP change is currently allowed, false otherwise
 */
bool call_sdp_change_allowed(const struct call *call)
{
	if (!call)
		return false;

	enum sdp_neg_state sdp_state = call_sdp_neg_state(call);

	return (call->state == CALL_STATE_ESTABLISHED
		&& sdp_state == SDP_NEG_DONE)
		|| (sdp_state == SDP_NEG_NONE
		|| sdp_state == SDP_NEG_REMOTE_OFFER);
}
