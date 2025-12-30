/**
 * @file test/peerconn.c  Tests for peer connection
 *
 * Copyright (C) 2025 Alfred E. Heggestad
 */
#include <re.h>
#include <baresip.h>
#include "test.h"


struct agent {
	struct agent *peer;          /* pointer */
	struct media_track *media;   /* pointer */
	struct peer_connection *pc;
	struct mqueue *mq;
	const char *name;
	bool got_sdp;
	bool got_estab;
	bool got_audio;
	int err;
};


static bool agents_are_complete(const struct agent *ag)
{
	const struct agent *peer = ag->peer;

	bool got_audio = ag->got_audio || peer->got_audio;

	return ag->got_estab && peer->got_estab && got_audio;
}


static int agent_handle_sdp(struct agent *ag, enum sdp_type type,
			    struct mbuf *sdp)
{
	struct session_description sd = {
		.type = type,
		.sdp = sdp
	};

	int err = peerconnection_set_remote_descr(ag->pc, &sd);
	TEST_ERR(err);

	ag->got_sdp = true;

	if (ag->peer->got_sdp) {

		err = peerconnection_start_ice(ag->pc);
		TEST_ERR(err);

		err = peerconnection_start_ice(ag->peer->pc);
		TEST_ERR(err);
	}

 out:
	return err;
}


static void peerconnection_gather_handler(void *arg)
{
	struct agent *ag = arg;
	struct mbuf *mb = NULL;
	enum sdp_type type = SDP_NONE;
	int err;

	if (ag->err || ag->peer->err)
		return;

	switch (peerconnection_signaling(ag->pc)) {

	case SS_STABLE:
		type = SDP_OFFER;
		break;

	case SS_HAVE_LOCAL_OFFER:
		warning("gather: illegal state HAVE_LOCAL_OFFER\n");
		type = SDP_OFFER;
		break;

	case SS_HAVE_REMOTE_OFFER:
		type = SDP_ANSWER;
		break;
	}

	if (type == SDP_OFFER) {
		err = peerconnection_create_offer(ag->pc, &mb);
		TEST_ERR(err);
	}
	else {
		err = peerconnection_create_answer(ag->pc, &mb);
		TEST_ERR(err);
	}

	err = agent_handle_sdp(ag->peer, type, mb);
	TEST_ERR(err);

 out:
	mem_deref(mb);

	if (err) {
		ag->err = err;
		re_cancel();
	}
}


static void peerconnection_estab_handler(struct media_track *media, void *arg)
{
	struct agent *ag = arg;

	ag->got_estab = true;

	ag->media = media;

	int err = mediatrack_start_audio(media, baresip_ausrcl(),
					 baresip_aufiltl());
	if (err) {
		warning("estab: could not start audio (%m)\n", err);
	}

	if (err) {
		ag->err = err;
		re_cancel();
	}

	if (agents_are_complete(ag)) {
		re_cancel();
	}
}


static void peerconnection_close_handler(int err, void *arg)
{
	struct agent *ag = arg;

	info("[ %s ] peer connection closed\n", ag->name);

	ag->err = err;
	re_cancel();
}


/* called in the context of the main thread */
static void mqueue_handler(int id, void *data, void *arg)
{
	(void)id;
	(void)data;
	(void)arg;

	re_cancel();
}


static int agent_init(struct agent *ag, const struct mnat *mnat,
		      const struct menc *menc, bool offerer)
{
	struct rtc_configuration config = {
		.offerer = offerer
	};

	int err = mqueue_alloc(&ag->mq, mqueue_handler, ag);
	TEST_ERR(err);

	err = peerconnection_new(&ag->pc, &config, mnat, menc,
				 peerconnection_gather_handler,
				 peerconnection_estab_handler,
				 peerconnection_close_handler, ag);
	TEST_ERR(err);

	err = peerconnection_add_audio_track(ag->pc, conf_config(),
					     baresip_aucodecl(), SDP_SENDRECV);
	TEST_ERR(err);

 out:
	return err;
}


static void agent_reset(struct agent *ag)
{
	ag->pc = mem_deref(ag->pc);
	ag->pc = mem_deref(ag->mq);

	ag->media = NULL;
}


static void auframe_handler(struct auframe *af, const char *dev, void *arg)
{
	struct agent *ag = arg;
	(void)af;
	(void)dev;

	struct audio *au = media_get_audio(ag->media);

	/* Does auframe come from the decoder ? */
	if (!audio_rxaubuf_started(au)) {
		debug("test: [ %s ] no audio received from decoder yet\n",
		      ag->name);
		return;
	}

	ag->got_audio = true;

	if (agents_are_complete(ag)) {
		mqueue_push(ag->mq, 0, NULL);
	}
}


int test_peerconn(void)
{
	struct agent a = { .name = "A" };
	struct agent b = { .name = "B" };
	struct auplay *auplay = NULL;
	int err;

	a.peer = &b;
	b.peer = &a;

	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	err = module_load(".", "ice");
	TEST_ERR(err);
	err = module_load(".", "g711");
	TEST_ERR(err);
	err = module_load(".", "ausine");
	TEST_ERR(err);

	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   auframe_handler, &b);
	TEST_ERR(err);

	const struct mnat *mnat = mnat_find(baresip_mnatl(), "ice");
	ASSERT_TRUE(mnat != NULL);

	const struct menc *menc = menc_find(baresip_mencl(), "dtls_srtp");
	ASSERT_TRUE(menc != NULL);

	err = agent_init(&a, mnat, menc, true);
	TEST_ERR(err);
	err = agent_init(&b, mnat, menc, false);
	TEST_ERR(err);

	err = re_main_timeout(10000);
	TEST_ERR(err);

	TEST_ERR(a.err);
	TEST_ERR(b.err);

	ASSERT_TRUE(a.got_sdp);
	ASSERT_TRUE(b.got_sdp);
	ASSERT_TRUE(a.got_estab);
	ASSERT_TRUE(b.got_estab);

 out:
	agent_reset(&b);
	agent_reset(&a);

	mem_deref(auplay);

	module_unload("ausine");
	module_unload("g711");
	module_unload("ice");
	module_unload("dtls_srtp");

	return err;
}
