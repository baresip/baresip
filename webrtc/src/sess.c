/**
 * @file sess.c  Baresip WebRTC demo -- session
 *
 * Copyright (C) 2010 - 2022 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


static void destructor(void *data)
{
	struct session *sess = data;

	list_unlink(&sess->le);
	mem_deref(sess->conn_pending);
	mem_deref(sess->pc);
}


static void peerconnection_gather_handler(void *arg)
{
	struct session *sess = arg;
	struct mbuf *mb_sdp = NULL;
	struct odict *od = NULL;
	enum sdp_type type = SDP_NONE;
	int err;

	switch (peerconnection_signaling(sess->pc)) {

	case SS_STABLE:
		type = SDP_OFFER;
		break;

	case SS_HAVE_LOCAL_OFFER:
		warning("demo: illegal state HAVE_LOCAL_OFFER\n");
		type = SDP_OFFER;
		break;

	case SS_HAVE_REMOTE_OFFER:
		type = SDP_ANSWER;
		break;
	}

	info("demo: session gathered -- send sdp '%s'\n", sdptype_name(type));

	if (type == SDP_OFFER)
		err = peerconnection_create_offer(sess->pc, &mb_sdp);
	else
		err = peerconnection_create_answer(sess->pc, &mb_sdp);
	if (err)
		goto out;

	err = session_description_encode(&od, type, mb_sdp);
	if (err)
		goto out;

	err = http_reply_json(sess->conn_pending, sess->id, od);
	if (err) {
		warning("demo: reply error: %m\n", err);
		goto out;
	}

	if (type == SDP_ANSWER) {

		err = peerconnection_start_ice(sess->pc);
		if (err) {
			warning("demo: failed to start ice (%m)\n", err);
			goto out;
		}
	}

 out:
	mem_deref(mb_sdp);
	mem_deref(od);

	if (err)
		session_close(sess, err);
}


static void peerconnection_estab_handler(struct media_track *media, void *arg)
{
	struct session *sess = arg;
	int err = 0;

	info("demo: stream established: '%s'\n",
	     media_kind_name(mediatrack_kind(media)));

	switch (mediatrack_kind(media)) {

	case MEDIA_KIND_AUDIO:
		err = mediatrack_start_audio(media, baresip_ausrcl(),
					     baresip_aufiltl());
		if (err) {
			warning("demo: could not start audio (%m)\n", err);
		}
		break;

	case MEDIA_KIND_VIDEO:
		err = mediatrack_start_video(media);
		if (err) {
			warning("demo: could not start video (%m)\n", err);
		}
		break;

	default:
		break;
	}

	if (err) {
		session_close(sess, err);
		return;
	}

	stream_enable(media_get_stream(media), true);
}


static void peerconnection_close_handler(int err, void *arg)
{
	struct session *sess = arg;

	warning("demo: session closed (%m)\n", err);

	session_close(sess, err);
}


int session_start(struct session *sess,
		  const struct rtc_configuration *pc_config,
		  const struct mnat *mnat, const struct menc *menc)
{
	const struct config *config = conf_config();
	int err;

	if (!sess)
		return EINVAL;

	if (sess->pc)
		return EALREADY;

	err = peerconnection_new(&sess->pc, pc_config, mnat, menc,
				 peerconnection_gather_handler,
				 peerconnection_estab_handler,
				 peerconnection_close_handler, sess);
	if (err) {
		warning("demo: session alloc failed (%m)\n", err);
		return err;
	}

	err = peerconnection_add_audio_track(sess->pc, config,
					     baresip_aucodecl(), SDP_SENDRECV);
	if (err) {
		warning("demo: add_audio failed (%m)\n", err);
		return err;
	}

	err = peerconnection_add_video_track(
		sess->pc, config, baresip_vidcodecl(), SDP_SENDRECV);
	if (err) {
		warning("demo: add_video failed (%m)\n", err);
		return err;
	}

	return 0;
}


int session_new(struct list *sessl, struct session **sessp)
{
	struct session *sess;

	info("demo: create session\n");

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

	/* generate a unique session id */
	rand_str(sess->id, sizeof(sess->id));


	list_append(sessl, &sess->le, sess);

	*sessp = sess;

	return 0;
}


struct session *session_lookup(const struct list *sessl,
			       const struct http_msg *msg)
{
	const struct http_hdr *hdr;

	hdr = http_msg_xhdr(msg, "Session-ID");
	if (!hdr) {
		warning("demo: no Session-ID header\n");
		return NULL;
	}

	for (struct le *le = sessl->head; le; le = le->next) {

		struct session *sess = le->data;

		if (0 == pl_strcasecmp(&hdr->val, sess->id))
			return sess;
	}

	warning("demo: session not found (%r)\n", &hdr->val);

	return NULL;
}


int session_handle_ice_candidate(struct session *sess, const struct odict *od)
{
	const char *cand, *mid;
	struct pl pl_cand;
	char *cand2 = NULL;
	int err;

	if (!sess || !od)
		return EINVAL;

	cand = odict_string(od, "candidate");
	mid  = odict_string(od, "sdpMid");
	if (!cand || !mid) {
		warning("demo: candidate: missing 'candidate' or 'mid'\n");
		return EPROTO;
	}

	err = re_regex(cand, str_len(cand), "candidate:[^]+", &pl_cand);
	if (err)
		return err;

	pl_strdup(&cand2, &pl_cand);

	peerconnection_add_ice_candidate(sess->pc, cand2, mid);

	mem_deref(cand2);

	return 0;
}


void session_close(struct session *sess, int err)
{
	if (!sess)
		return;

	if (err)
		warning("demo: session '%s' closed (%m)\n", sess->id, err);
	else
		info("demo: session '%s' closed\n", sess->id);

	sess->pc = mem_deref(sess->pc);

	if (err) {
		http_ereply(sess->conn_pending, 500, "Session closed");
	}

	mem_deref(sess);
}
