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
	enum call_state state;    /**< Call state                           */
	int32_t adelay;           /**< Auto answer delay in ms              */
	char *aluri;              /**< Alert-Info URI                       */
	char *local_uri;          /**< Local SIP uri                        */
	char *local_name;         /**< Local display name                   */
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
	time_t time_start;        /**< Time when call started               */
	time_t time_conn;         /**< Time when call initiated             */
	time_t time_stop;         /**< Time when call stopped               */
	bool outgoing;            /**< True if outgoing, false if incoming  */
	bool answered;            /**< True if call has been answered       */
	bool got_offer;           /**< Got SDP Offer from Peer              */
	bool on_hold;             /**< True if call is on hold (local)      */
	bool ans_queued;          /**< True if an (auto) answer is queued   */
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
	char *user_data;           /**< User data related to the call       */
	bool evstop;               /**< UA events stopped flag              */
};


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
	call_event_h *eh = call->eh;
	void *eh_arg = call->arg;
	char buf[256];
	va_list ap;

	if (!eh)
		return;

	va_start(ap, fmt);
	(void)re_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	eh(call, ev, buf, eh_arg);
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

		stream_update(strm);

		if (stream_is_ready(strm)) {

			stream_start_rtcp(strm);
		}
	}

	if (call->acc->mnat && call->acc->mnat->updateh && call->mnats)
		err = call->acc->mnat->updateh(call->mnats);

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


int call_update_media(struct call *call)
{
	int err;

	err = call_apply_sdp(call);
	err |= update_streams(call);

	return err;
}


static int update_media(struct call *call)
{
	debug("call: update media\n");

	ua_event(call->ua, UA_EVENT_CALL_REMOTE_SDP, call,
		 call->got_offer ? "offer" : "answer");

	return call_update_media(call);
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
	mem_deref(call->peer_uri);
	mem_deref(call->peer_name);
	mem_deref(call->replaces);
	mem_deref(call->aluri);
	mem_deref(call->diverter_uri);
	mem_deref(call->audio);
	mem_deref(call->video);
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

	ua_event(call->ua, tx ? UA_EVENT_VU_TX : UA_EVENT_VU_RX,
		 call, "%.2f", lvl);
}


static void audio_error_handler(int err, const char *str, void *arg)
{
	struct call *call = arg;
	MAGIC_CHECK(call);

	if (err) {
		warning("call: audio device error: %m (%s)\n", err, str);

		ua_event(call->ua, UA_EVENT_AUDIO_ERROR, call, "%d,%s",
			err, str);
		call_stream_stop(call);
		call_event_handler(call, CALL_EVENT_CLOSED,
			"%s", str);
	}
	else
		ua_event(call->ua, UA_EVENT_END_OF_FILE, call, "");
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
	int err;
	(void)strm;
	MAGIC_CHECK(call);

	debug("call: mediaenc event '%s' (%s)\n", menc_event_name(event), prm);

	switch (event) {

	case MENC_EVENT_SECURE:
		if (strstr(prm, "audio")) {
			stream_set_secure(audio_strm(call->audio), true);
			stream_start_rtcp(audio_strm(call->audio));
			err = audio_update(call->audio);
			if (err) {
				warning("call: secure: could not"
					" start audio: %m\n", err);
			}
		}
		else if (strstr(prm, "video")) {
			stream_set_secure(video_strm(call->video), true);
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

	ua_event(call->ua, UA_EVENT_CALL_RTPESTAB, call,
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

		ua_event(call->ua, UA_EVENT_CALL_RTCP, call,
			 "%s", sdp_media_name(stream_sdpmedia(strm)));
		break;

	case RTCP_APP:
		ua_event(call->ua, UA_EVENT_CALL_RTCP, call,
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


int call_streams_alloc(struct call *call)
{
	struct account *acc = call->acc;
	struct stream_param strm_prm;
	struct le *le;
	int label = 0;
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
				  call->cfg, call->sdp,
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

		sdp_media_set_lattr(stream_sdpmedia(strm), true,
				    "label", "%d", ++label);

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

	if (msg)
		err |= pl_strdup(&call->peer_uri, &msg->from.auri);

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

		if (pl_isset(&addr.params)) {
			err = re_sdprintf(&call->peer_uri, "%r%r",
					  &addr.auri, &addr.params);
		}
		else {
			err = pl_strdup(&call->peer_uri, &addr.auri);
		}

		if (pl_isset(&addr.dname))
			pl_strdup(&call->peer_name, &addr.dname);

		uri_header_get(&addr.uri.headers, &rname, &rval);
		if (pl_isset(&rval))
			err = re_sdprintf(&call->replaces, "%r",&rval);

	}
	else {
		err = pl_strdup(&call->peer_uri, paddr);
	}
	if (err)
		return err;

	set_state(call, CALL_STATE_OUTGOING);
	call_event_handler(call, CALL_EVENT_OUTGOING, "%s", call->peer_uri);

	/* If we are using asyncronous medianat like STUN/TURN, then
	 * wait until completed before sending the INVITE */
	if (!call->acc->mnat) {
		err = send_invite(call);
	}
	else {
		err = call_streams_alloc(call);
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
	int err;

	if (!call)
		return EINVAL;

	debug("call: modify\n");

	if (call_refresh_allowed(call)) {
		err = call_sdp_get(call, &desc, true);
		if (!err) {
			ua_event(call->ua, UA_EVENT_CALL_LOCAL_SDP, call,
				 "offer");

			err = sipsess_modify(call->sess, desc);
			if (err)
				goto out;
		}
	}

	err = call_update_media(call);

 out:
	mem_deref(desc);

	return err;
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
			(void)sipsess_reject(call->sess, scode, reason, NULL);
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
	struct mbuf *desc;
	int err;

	if (!call)
		return EINVAL;

	tmr_cancel(&call->tmr_inv);

	if (adir != call->estadir || vdir != call->estvdir)
		call_set_mdir(call, adir, vdir);

	err = call_sdp_get(call, &desc, false);
	if (err)
		return err;

	err = sipsess_progress(call->sess, 183, "Session Progress",
			       account_rel100_mode(call->acc),
			       desc, "Allow: %H\r\n%H",
			       ua_print_allowed, call->ua,
			       ua_print_require, call->ua);

	if (err)
		goto out;

	if (call->got_offer) {
		ua_event(call->ua, UA_EVENT_CALL_LOCAL_SDP, call, "answer");
		err = call_update_media(call);
	}

	if (err)
		goto out;

out:
	mem_deref(desc);

	return 0;
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
	struct mbuf *desc;
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

	if (call->got_offer)
		err = call_apply_sdp(call);

	ua_event(call->ua, UA_EVENT_CALL_LOCAL_SDP, call,
		 "%s", !call->got_offer ? "offer" : "answer");

	err = sdp_encode(&desc, call->sdp, !call->got_offer);
	if (err)
		return err;

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

	call->answered = true;
	call->ans_queued = false;

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
	if (!call)
		return EINVAL;

	return sdp_encode(descp, call->sdp, offer);
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
 * @param call  Call object
 *
 * @return Peer URI
 */
const char *call_peeruri(const struct call *call)
{
	return call ? call->peer_uri : NULL;
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


static int sipsess_offer_handler(struct mbuf **descp,
				 const struct sip_msg *msg, void *arg)
{
	const bool got_offer = (0 != mbuf_get_left(msg->mb));
	struct call *call = arg;
	struct sdp_media *vmedia = NULL;
	enum sdp_dir ardir, vrdir;
	int err;

	MAGIC_CHECK(call);

	if (got_offer) {
		const struct sdp_media *m =
			stream_sdpmedia(audio_strm(call->audio));
		bool aurx = sdp_media_dir(m) & SDP_SENDONLY;
		call->got_offer = true;

		/* Decode SDP Offer */
		err = sdp_decode(call->sdp, msg->mb, true);
		if (err) {
			warning("call: reinvite: could not decode SDP offer:"
				" %m\n", err);
			return err;
		}

		if (aurx && !(sdp_media_dir(m) & SDP_SENDONLY))
			ua_event(call->ua, UA_EVENT_CALL_HOLD, call, "");
		else if (!aurx && sdp_media_dir(m) & SDP_SENDONLY)
			ua_event(call->ua, UA_EVENT_CALL_RESUME, call, "");

		err = update_media(call);
		if (err) {
			warning("call: reinvite: could not update media: %m\n",
				err);
			return err;
		}
	}

	ardir = sdp_media_rdir(
		stream_sdpmedia(audio_strm(call_audio(call))));

	vmedia = stream_sdpmedia(video_strm(call_video(call)));
	if (sdp_media_rport(vmedia) == 0 ||
		list_head(sdp_media_format_lst(vmedia, false)) == 0) {
		vrdir = SDP_INACTIVE;
	}
	else {
		vrdir = sdp_media_rdir(vmedia);
	}

	info("call: got %r%s audio-video: %s-%s\n", &msg->met,
	     got_offer ? " (SDP Offer)" : "", sdp_dir_name(ardir),
	     sdp_dir_name(vrdir));

	/* Encode SDP Answer */
	return sdp_encode(descp, call->sdp, !got_offer);
}


static int sipsess_answer_handler(const struct sip_msg *msg, void *arg)
{
	struct call *call = arg;
	int err;

	MAGIC_CHECK(call);

	debug("call: got SDP answer (%zu bytes)\n", mbuf_get_left(msg->mb));

	if (sip_msg_hdr_has_value(msg, SIP_HDR_SUPPORTED, "replaces"))
		call->supported |= REPLACES;

	call->got_offer = false;
	if (!pl_strcmp(&msg->cseq.met, "INVITE") &&
	    msg->scode >= 200 && msg->scode < 300)
		call_event_handler(call, CALL_EVENT_ANSWERED, "%s",
                                   call->peer_uri);

	if (msg_ctype_cmp(&msg->ctyp, "multipart", "mixed"))
		(void)sdp_decode_multipart(&msg->ctyp.params, msg->mb);

	err = sdp_decode(call->sdp, msg->mb, false);
	if (err) {
		warning("call: could not decode SDP answer: %m\n", err);
		return err;
	}

	/* note: before update_media */
	if (call->config_avt.bundle) {

		bundle_sdp_decode(call->sdp, &call->streaml);
	}

	err = update_media(call);
	if (err)
		return err;

	return 0;
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
	else if (!mbuf_get_left(msg->mb)) {
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
	call_event_handler(call, CALL_EVENT_CLOSED, "%s", reason);
}


static void prack_handler(const struct sip_msg *msg, void *arg)
{
	struct call *call = arg;

	if (!msg || !call)
		return;

	if (call->ans_queued && !call->answered)
		(void)call_answer(call, 200, VIDMODE_ON);

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


int call_accept(struct call *call, struct sipsess_sock *sess_sock,
		const struct sip_msg *msg)
{
	const struct sip_hdr *hdr;
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

		err = sdp_decode(call->sdp, msg->mb, true);
		if (err)
			return err;

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

			return 0;
		}

		if (call->config_avt.bundle) {

			bundle_sdp_decode(call->sdp, &call->streaml);
		}
	}

	hdr = sip_msg_hdr(msg, SIP_HDR_REPLACES);
	if (hdr && pl_isset(&hdr->val)) {
		char *rid = NULL;
		struct call *rcall;
		err = pl_strdup(&rid, &hdr->val);
		if (err)
			return err;

		rcall = call_find_id(ua_calls(call->ua), rid);
		call_stream_stop(rcall);
		call_event_handler(rcall, CALL_EVENT_CLOSED,
			"%s replaced", rid);
		mem_deref(rid);
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
	bool media;

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
	if (msg_ctype_cmp(&msg->ctyp, "application", "sdp")
	    && mbuf_get_left(msg->mb)
	    && !sdp_decode(call->sdp, msg->mb, false)) {
		media = true;
	}
	else if (msg_ctype_cmp(&msg->ctyp, "multipart", "mixed") &&
		 !sdp_decode_multipart(&msg->ctyp.params, msg->mb) &&
		 !sdp_decode(call->sdp, msg->mb, false)) {
		media = true;
	}
	else
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
	ua_event(call->ua, UA_EVENT_CALL_REDIRECT, call,
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

	return err;
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

	ua_event(call->ua, UA_EVENT_CALL_LOCAL_SDP, call, "offer");

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
	(void)mbuf_printf(mb, "SIP/2.0 %u %v\n", scode, reason, &ap);
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
		uri2.password = pl_null;
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

	info("transferring call to %s\n", source_call->peer_uri);

	call->sub = mem_deref(call->sub);

	err = sipevent_drefer(&call->sub, uag_sipevent_sock(),
			      sipsess_dialog(call->sess), ua_cuser(call->ua),
			      auth_handler, call->acc, true,
			      sipsub_notify_handler, sipsub_close_handler,
                              call,
			 "Refer-To: <%s?Replaces=%s>\r\nReferred-by: %s\r\n",
                              source_call->peer_uri, source_call->id,
		              account_aor(ua_account(call->ua)));
	if (err) {
		warning("call: sipevent_drefer: %m\n", err);
	}

	return err;
}


int call_af(const struct call *call)
{
	return call ? call->af : AF_UNSPEC;
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
bool call_supported(struct call *call, uint16_t tags)
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
const char *call_user_data(const struct call *call)
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

int call_set_user_data(struct call *call, const char *user_data)
{
	if (!call)
		return EINVAL;

	call->user_data = mem_deref(call->user_data);

	int err = str_dup(&call->user_data, user_data);

	if (err)
		return err;

	return 0;
}


void call_set_evstop(struct call *call, bool stop)
{
	if (!call)
		return;

	call->evstop = stop;
}


bool call_is_evstop(struct call *call)
{
	if (!call)
		return false;

	return call->evstop;
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
