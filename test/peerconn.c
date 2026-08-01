/**
 * @file test/peerconn.c  Tests for peer connection
 *
 * Copyright (C) 2025 Alfred E. Heggestad
 */
#include <errno.h>
#include <stdlib.h>
#include <re.h>
#include <baresip.h>
#include "core.h"
#include "test.h"
#include "peerconn_internal.h"


struct fixture {
	struct agent *a;
	struct agent *b;
	struct mqueue *mq;
	bool terminated;
	bool bundle_only_data;
	bool application_first_bundle;
	bool application_first_answer;
#ifdef USE_DATACHANNEL
	uint16_t next_continuity_id;
#endif
};


struct agent {
	struct fixture *fix;         /* pointer */
	struct media_track *media;   /* pointer */
	struct peer_connection *pc;
	const char *name;
	uint32_t magic;
	bool use_audio;
	bool use_video;
#ifdef USE_DATACHANNEL
	struct data_channel *dc;
	bool use_data;
	bool negotiated_data;
	bool got_data;
	bool data_sent;
	bool destroy_on_channel;
	bool destroy_on_data_close;
	bool data_close_stable;
	bool data_close_closed;
	bool destroy_on_pc_close;
	bool destroy_on_media_estab;
#endif
	bool got_sdp;
	bool got_estab_audio;
	bool got_estab_video;
	unsigned estab_audio_count;
	unsigned estab_video_count;
	bool got_audio;
	bool got_video;
	int err;
};


#ifdef USE_DATACHANNEL
struct deferred_receive_check {
	struct agent *agent;
	unsigned messages;
	unsigned closed;
	unsigned incoming;
	int close_err;
	bool destroy_on_first_message;
	bool destroy_on_closed;
};
#endif


static const uint32_t MAGIC_AGENT = 0x0ee0c001;
#ifdef USE_DATACHANNEL
static const uint8_t transport_payload[] = {
	0x00, 0x11, 0x7f, 0x80, 0xfe, 0xff, 0x42
};
#endif

#ifdef USE_DATACHANNEL
static bool mbuf_contains(const struct mbuf *mb, const char *text);
void mock_mnat_register(struct list *mnatl);
void mock_mnat_unregister(void);
void mock_mnat_media_gather_defer(bool defer);
void mock_mnat_complete_media_gathers(void);
unsigned mock_mnat_media_gather_callback_count(void);
#endif


static struct agent *agent_peer(const struct agent *ag)
{
	const struct fixture *fix = ag->fix;

	if (!fix)
		return NULL;

	if (ag == fix->a)
		return fix->b;
	else if (ag == fix->b)
		return fix->a;
	else
		return NULL;
}


static void agent_close(struct agent *ag, int err)
{
	ag->media = NULL;
	ag->err = err;

	if (ag->fix)
		ag->fix->terminated = true;

	re_cancel();
}


static bool agents_are_complete(const struct agent *ag)
{
	const struct agent *peer = agent_peer(ag);

	if (ag->use_audio) {

		if (!ag->got_estab_audio)
			return false;

		bool got_audio = (ag->got_audio || peer->got_audio);
		if (!got_audio)
			return false;
	}

	if (ag->use_video) {

		if (!ag->got_estab_video)
			return false;

		bool got_video = (ag->got_video || peer->got_video);
		if (!got_video)
			return false;
	}

#ifdef USE_DATACHANNEL
	if (ag->use_data && !(ag->got_data || peer->got_data))
		return false;
#endif

	return true;
}


#ifdef USE_DATACHANNEL
static const uint8_t data_binary[] = {0x00, 0x80, 0xff, 0x42};
static const uint8_t data_text[] = "data-channel-ok";

static bool mbuf_contains(const struct mbuf *mb, const char *text)
{
	size_t len = str_len(text);

	return mb && len <= mb->end &&
		memmem(mb->buf, mb->end, text, len) != NULL;
}


static int sdp_attr_value(const struct mbuf *mb, const char *name,
			  char *value, size_t size)
{
	char prefix[64];
	const uint8_t *start;
	const uint8_t *end;
	size_t len;
	int written;

	if (!mb || !name || !value || !size)
		return EINVAL;
	written = re_snprintf(prefix, sizeof(prefix), "a=%s:", name);
	if (written < 0 || (size_t)written >= sizeof(prefix))
		return EOVERFLOW;
	start = memmem(mb->buf, mb->end, prefix, (size_t)written);
	if (!start)
		return ENOENT;
	start += written;
	end = memchr(start, '\r', mb->end - (size_t)(start - mb->buf));
	if (!end)
		return EPROTO;
	len = (size_t)(end - start);
	if (len >= size)
		return EOVERFLOW;
	memcpy(value, start, len);
	value[len] = '\0';
	return 0;
}


static int sdp_application_port(const struct mbuf *mb, uint16_t *port)
{
	static const char prefix[] = "m=application ";
	const uint8_t *start;
	char *end;
	unsigned long parsed;

	if (!mb || !port)
		return EINVAL;
	start = memmem(mb->buf, mb->end, prefix, sizeof(prefix) - 1);
	if (!start)
		return ENOENT;
	start += sizeof(prefix) - 1;
	errno = 0;
	parsed = strtoul((const char *)start, &end, 10);
	if (errno || end == (char *)start || parsed > UINT16_MAX)
		return EPROTO;
	*port = (uint16_t)parsed;
	return 0;
}


static int sdp_application_attr_value(const struct mbuf *mb, const char *name,
				      char *value, size_t size)
{
	static const char media_prefix[] = "m=application ";
	char attr_prefix[64];
	const uint8_t *media;
	const uint8_t *next;
	const uint8_t *start;
	const uint8_t *end;
	size_t section_len;
	size_t len;
	int written;

	if (!mb || !name || !value || !size)
		return EINVAL;
	media = memmem(mb->buf, mb->end, media_prefix,
			 sizeof(media_prefix) - 1);
	if (!media)
		return ENOENT;
	next = memmem(media + sizeof(media_prefix) - 1,
		      mb->end - (size_t)(media + sizeof(media_prefix) - 1 -
					mb->buf), "\r\nm=", 4);
	section_len = next ? (size_t)(next - media)
			   : mb->end - (size_t)(media - mb->buf);
	written = re_snprintf(attr_prefix, sizeof(attr_prefix), "a=%s:", name);
	if (written < 0 || (size_t)written >= sizeof(attr_prefix))
		return EOVERFLOW;
	start = memmem(media, section_len, attr_prefix, (size_t)written);
	if (!start)
		return sdp_attr_value(mb, name, value, size);
	start += written;
	end = memchr(start, '\r', section_len - (size_t)(start - media));
	if (!end)
		return EPROTO;
	len = (size_t)(end - start);
	if (len >= size)
		return EOVERFLOW;
	memcpy(value, start, len);
	value[len] = '\0';
	return 0;
}


static int sdp_video_attr_value(const struct mbuf *mb, const char *name,
				char *value, size_t size)
{
	static const char media_prefix[] = "m=video ";
	char attr_prefix[64];
	const uint8_t *media;
	const uint8_t *next;
	const uint8_t *start;
	const uint8_t *end;
	size_t section_len;
	size_t len;
	int written;

	if (!mb || !name || !value || !size)
		return EINVAL;
	media = memmem(mb->buf, mb->end, media_prefix,
			 sizeof(media_prefix) - 1);
	if (!media)
		return ENOENT;
	next = memmem(media + sizeof(media_prefix) - 1,
		      mb->end - (size_t)(media + sizeof(media_prefix) - 1 -
					mb->buf), "\r\nm=", 4);
	section_len = next ? (size_t)(next - media)
			   : mb->end - (size_t)(media - mb->buf);
	written = re_snprintf(attr_prefix, sizeof(attr_prefix), "a=%s:", name);
	if (written < 0 || (size_t)written >= sizeof(attr_prefix))
		return EOVERFLOW;
	start = memmem(media, section_len, attr_prefix, (size_t)written);
	if (!start)
		return sdp_attr_value(mb, name, value, size);
	start += written;
	end = memchr(start, '\r', section_len - (size_t)(start - media));
	if (!end)
		return EPROTO;
	len = (size_t)(end - start);
	if (len >= size)
		return EOVERFLOW;
	memcpy(value, start, len);
	value[len] = '\0';
	return 0;
}


static int sdp_audio_attr_value(const struct mbuf *mb, const char *name,
				char *value, size_t size)
{
	static const char media_prefix[] = "m=audio ";
	char attr_prefix[64];
	const uint8_t *media;
	const uint8_t *next;
	const uint8_t *start;
	const uint8_t *end;
	size_t section_len;
	size_t len;
	int written;

	if (!mb || !name || !value || !size)
		return EINVAL;
	media = memmem(mb->buf, mb->end, media_prefix,
			 sizeof(media_prefix) - 1);
	if (!media)
		return ENOENT;
	next = memmem(media + sizeof(media_prefix) - 1,
		      mb->end - (size_t)(media + sizeof(media_prefix) - 1 -
					mb->buf), "\r\nm=", 4);
	section_len = next ? (size_t)(next - media)
			   : mb->end - (size_t)(media - mb->buf);
	written = re_snprintf(attr_prefix, sizeof(attr_prefix), "a=%s:", name);
	if (written < 0 || (size_t)written >= sizeof(attr_prefix))
		return EOVERFLOW;
	start = memmem(media, section_len, attr_prefix, (size_t)written);
	if (!start)
		return sdp_attr_value(mb, name, value, size);
	start += written;
	end = memchr(start, '\r', section_len - (size_t)(start - media));
	if (!end)
		return EPROTO;
	len = (size_t)(end - start);
	if (len >= size)
		return EOVERFLOW;
	memcpy(value, start, len);
	value[len] = '\0';
	return 0;
}


struct restart_shadow_receive {
	unsigned messages;
	int err;
};


struct restart_shadow_open_wait {
	struct data_channel *a;
	struct data_channel *b;
	int err;
};


static void restart_shadow_state_handler(struct data_channel *dc,
					 enum data_channel_state state,
					 int err, void *arg)
{
	struct restart_shadow_open_wait *wait = arg;
	(void)dc;
	(void)state;

	if (err)
		wait->err = err;
	if (wait->err ||
	    (datachannel_state(wait->a) == DATACHANNEL_OPEN &&
	     datachannel_state(wait->b) == DATACHANNEL_OPEN))
		re_cancel();
}


static void restart_shadow_message_handler(
	struct data_channel *dc, enum data_channel_message_type type,
	const uint8_t *buf, size_t len, void *arg)
{
	struct restart_shadow_receive *receive = arg;
	(void)dc;

	if (type != DATACHANNEL_MESSAGE_BINARY ||
	    len != sizeof(transport_payload) ||
	    memcmp(buf, transport_payload, len))
		receive->err = EBADMSG;
	else
		++receive->messages;
	re_cancel();
}


static int restart_shadow_send_and_wait(
	struct data_channel *dc, struct restart_shadow_receive *receive)
{
	unsigned previous = receive->messages;
	int err;

	err = datachannel_send(dc, DATACHANNEL_MESSAGE_BINARY,
			       transport_payload, sizeof(transport_payload));
	if (err)
		return err;
	err = re_main_timeout(1000);
	if (err)
		return err;
	if (receive->err)
		return receive->err;
	return receive->messages == previous + 1 ? 0 : EPROTO;
}


static int test_restart_shadow_sdp_rollback(const struct menc *menc)
{
	const struct rtc_configuration rtc = {
		.offerer = true,
	};
	const struct rtc_configuration answer_rtc = {
		.offerer = false,
	};
	const struct data_channel_config config = {
		.ordered = true,
		.max_retransmits = -1,
		.max_packet_lifetime = -1,
		.negotiated = true,
		.id = 1,
	};
	const struct session_description rollback = {
		.type = SDP_ROLLBACK,
	};
	struct gather_wait wait = {0};
	struct gather_wait answer_wait = {0};
	struct peer_connection *pc = NULL;
	struct peer_connection *answerer = NULL;
	struct data_channel *dc = NULL;
	struct data_channel *answer_dc = NULL;
	const struct mnat *mnat;
	struct session_description description;
	struct mbuf *initial_offer = NULL;
	struct mbuf *initial_answer = NULL;
	struct mbuf *stable = NULL;
	struct mbuf *restart = NULL;
	struct mbuf *restored = NULL;
	struct mbuf *retry_answer = NULL;
	struct restart_shadow_receive receive = {0};
	struct restart_shadow_open_wait open_wait = {0};
	char stable_ufrag[64];
	char initial_remote_ufrag[64];
	char restart_ufrag[64];
	char retry_ufrag[64];
	char answer_ufrag[64];
	char candidate[256];
	char stable_tls_id[256];
	char restart_tls_id[256];
	uint16_t stable_port;
	uint16_t restart_port;
	unsigned gather_callbacks;
	unsigned candidate_attrs;
	bool mock_registered = false;
	int channel_id;
	int err;

	mock_mnat_register(baresip_mnatl());
	mock_registered = true;
	mnat = mnat_find(baresip_mnatl(), "XNAT");
	ASSERT_TRUE(mnat != NULL);
	err = peerconnection_new(&pc, &rtc, mnat, menc,
			 peerconn_validation_gather_handler, NULL, NULL, &wait);
	TEST_ERR(err);
	err = peerconnection_create_datachannel(pc, "restart-shadow", &config,
						&dc);
	TEST_ERR(err);
	if (!wait.ready) {
		err = re_main_timeout(10000);
		TEST_ERR(err);
	}
	ASSERT_TRUE(wait.ready);
	err = peerconnection_new(&answerer, &answer_rtc, mnat, menc,
			 peerconn_validation_gather_handler, NULL, NULL,
				 &answer_wait);
	TEST_ERR(err);
	err = peerconnection_set_datachannel_handler(answerer, NULL, NULL);
	TEST_ERR(err);
	err = peerconnection_create_datachannel(answerer, "restart-shadow",
						&config, &answer_dc);
	TEST_ERR(err);
	open_wait.a = dc;
	open_wait.b = answer_dc;
	err = datachannel_set_handlers(dc, NULL, restart_shadow_state_handler,
				       NULL, &open_wait);
	err |= datachannel_set_handlers(answer_dc, NULL,
					restart_shadow_state_handler, NULL,
					&open_wait);
	TEST_ERR(err);
	if (!answer_wait.ready) {
		err = re_main_timeout(10000);
		TEST_ERR(err);
	}
	ASSERT_TRUE(answer_wait.ready);
	err = peerconnection_create_offer(pc, &initial_offer);
	TEST_ERR(err);
	description.type = SDP_OFFER;
	description.sdp = initial_offer;
	err = peerconnection_set_remote_descr(answerer, &description);
	TEST_ERR(err);
	err = peerconnection_create_answer(answerer, &initial_answer);
	TEST_ERR(err);
	description.type = SDP_ANSWER;
	description.sdp = initial_answer;
	err = peerconnection_set_remote_descr(pc, &description);
	TEST_ERR(err);
	ASSERT_EQ(SS_STABLE, peerconnection_signaling(pc));
	err = peerconnection_start_ice(answerer);
	TEST_ERR(err);
	err = peerconnection_start_ice(pc);
	TEST_ERR(err);
	err = re_main_timeout(1000);
	TEST_ERR(err);
	TEST_ERR(open_wait.err);
	channel_id = datachannel_id(dc);
	ASSERT_TRUE(channel_id >= 0);
	ASSERT_EQ(DATACHANNEL_OPEN, datachannel_state(dc));
	ASSERT_EQ(DATACHANNEL_OPEN, datachannel_state(answer_dc));
	err = datachannel_set_handlers(answer_dc, restart_shadow_message_handler,
				       NULL, NULL, &receive);
	TEST_ERR(err);
	err = restart_shadow_send_and_wait(dc, &receive);
	TEST_ERR(err);
	err = sdp_application_attr_value(initial_answer, "ice-ufrag",
					 initial_remote_ufrag,
					 sizeof(initial_remote_ufrag));
	TEST_ERR(err);
	candidate_attrs = mock_mnat_candidate_attr_count();
	peerconnection_add_ice_candidate(
		pc,
		"candidate:7 1 UDP 1 192.0.2.16 40012 typ host ufrag stale",
		"0");
	ASSERT_EQ(candidate_attrs, mock_mnat_candidate_attr_count());
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:8 1 UDP 1 192.0.2.17 40014 typ host ufrag %s",
		    initial_remote_ufrag);
	peerconnection_add_ice_candidate(pc, candidate, "0");
	ASSERT_EQ(candidate_attrs + 1, mock_mnat_candidate_attr_count());
	ASSERT_EQ(1, mock_mnat_last_candidate_generation());

	err = peerconnection_create_offer(pc, &stable);
	TEST_ERR(err);
	err = sdp_application_attr_value(stable, "ice-ufrag", stable_ufrag,
					 sizeof(stable_ufrag));
	TEST_ERR(err);
	err = sdp_application_attr_value(stable, "tls-id", stable_tls_id,
					 sizeof(stable_tls_id));
	if (err == ENOENT) {
		stable_tls_id[0] = '\0';
		err = 0;
	}
	TEST_ERR(err);
	err = sdp_application_port(stable, &stable_port);
	TEST_ERR(err);
	err = peerconnection_set_remote_descr(pc, &rollback);
	TEST_ERR(err);
	ASSERT_EQ(SS_STABLE, peerconnection_signaling(pc));

	/* The first retry must retain the one prepared generation while its
	 * private shadow SDP gathers.  Neither EAGAIN may publish provisional
	 * credentials into the active session. */
	mock_mnat_media_gather_defer(true);
	err = peerconnection_restart_ice(pc);
	TEST_ERR(err);
	err = peerconnection_create_offer(pc, &restart);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_TRUE(restart == NULL);
	err = 0;
	err = peerconnection_create_offer(pc, &restart);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_TRUE(restart == NULL);
	err = 0;
	ASSERT_EQ(SS_STABLE, peerconnection_signaling(pc));
	gather_callbacks = mock_mnat_media_gather_callback_count();
	mock_mnat_complete_media_gathers();
	ASSERT_EQ(gather_callbacks + 1,
		  mock_mnat_media_gather_callback_count());

	err = peerconnection_create_offer(pc, &restart);
	TEST_ERR(err);
	err = sdp_application_attr_value(restart, "ice-ufrag", restart_ufrag,
					 sizeof(restart_ufrag));
	TEST_ERR(err);
	ASSERT_STREQ("mock-2", restart_ufrag);
	ASSERT_TRUE(str_cmp(stable_ufrag, restart_ufrag));
	err = sdp_application_attr_value(restart, "tls-id", restart_tls_id,
					 sizeof(restart_tls_id));
	if (err == ENOENT) {
		restart_tls_id[0] = '\0';
		err = 0;
	}
	TEST_ERR(err);
	ASSERT_STREQ(stable_tls_id, restart_tls_id);
	err = sdp_application_port(restart, &restart_port);
	TEST_ERR(err);
	ASSERT_EQ(stable_port, restart_port);
	ASSERT_EQ(channel_id, datachannel_id(dc));
	candidate_attrs = mock_mnat_candidate_attr_count();
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:0 1 UDP 1 192.0.2.9 39998 typ host ufrag %s",
		    restart_ufrag);
	peerconnection_add_ice_candidate(pc, candidate, "0");
	ASSERT_EQ(candidate_attrs, mock_mnat_candidate_attr_count());

	err = peerconnection_set_remote_descr(pc, &rollback);
	TEST_ERR(err);
	ASSERT_EQ(candidate_attrs, mock_mnat_candidate_attr_count());
	ASSERT_EQ(SS_STABLE, peerconnection_signaling(pc));
	ASSERT_EQ(channel_id, datachannel_id(dc));
	err = restart_shadow_send_and_wait(dc, &receive);
	TEST_ERR(err);
	mock_mnat_media_gather_defer(false);
	err = peerconnection_create_offer(pc, &restored);
	TEST_ERR(err);
	err = sdp_application_attr_value(restored, "ice-ufrag", retry_ufrag,
					 sizeof(retry_ufrag));
	TEST_ERR(err);
	ASSERT_STREQ("mock-3", retry_ufrag);
	ASSERT_TRUE(str_cmp(stable_ufrag, retry_ufrag));
	ASSERT_TRUE(str_cmp(restart_ufrag, retry_ufrag));
	err = sdp_application_attr_value(restored, "tls-id", restart_tls_id,
					 sizeof(restart_tls_id));
	if (err == ENOENT) {
		restart_tls_id[0] = '\0';
		err = 0;
	}
	TEST_ERR(err);
	ASSERT_STREQ(stable_tls_id, restart_tls_id);
	err = sdp_application_port(restored, &restart_port);
	TEST_ERR(err);
	ASSERT_EQ(stable_port, restart_port);
	ASSERT_EQ(channel_id, datachannel_id(dc));
	ASSERT_EQ(DATACHANNEL_OPEN, datachannel_state(dc));
	ASSERT_EQ(DATACHANNEL_OPEN, datachannel_state(answer_dc));

	/* Trickle received with the provisional remote offer is queued until the
	 * answer transaction has prepared the exact replacement checklist.  A
	 * BUNDLE member mid must still route to the group's replacement MNAT. */
	description.type = SDP_OFFER;
	description.sdp = restored;
	err = peerconnection_set_remote_descr(answerer, &description);
	TEST_ERR(err);
	candidate_attrs = mock_mnat_candidate_attr_count();
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:1 1 UDP 1 192.0.2.10 40000 typ host ufrag %s",
		    retry_ufrag);
	peerconnection_add_ice_candidate(answerer, candidate, "0");
	ASSERT_EQ(candidate_attrs, mock_mnat_candidate_attr_count());
	err = peerconnection_create_answer(answerer, &retry_answer);
	TEST_ERR(err);
	ASSERT_EQ(candidate_attrs + 1, mock_mnat_candidate_attr_count());
	ASSERT_EQ(2, mock_mnat_last_candidate_generation());
	err = sdp_application_attr_value(retry_answer, "ice-ufrag",
					 answer_ufrag, sizeof(answer_ufrag));
	TEST_ERR(err);

	/* The answer candidate follows the local-offer queue and reaches the
	 * retried generation (generation 3), never the still-active checklist. */
	candidate_attrs = mock_mnat_candidate_attr_count();
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:2 1 UDP 1 192.0.2.11 40002 typ host ufrag %s",
		    answer_ufrag);
	peerconnection_add_ice_candidate(pc, candidate, "0");
	ASSERT_EQ(candidate_attrs, mock_mnat_candidate_attr_count());
	description.type = SDP_ANSWER;
	description.sdp = retry_answer;
	err = peerconnection_set_remote_descr(pc, &description);
	TEST_ERR(err);
	ASSERT_EQ(candidate_attrs + 1, mock_mnat_candidate_attr_count());
	ASSERT_EQ(3, mock_mnat_last_candidate_generation());

	/* An explicitly generation-scoped late candidate from the rolled-back
	 * credentials is consumed as stale while a candidate transport remains
	 * unpublished.  A current candidate continues to reach generation 3. */
	candidate_attrs = mock_mnat_candidate_attr_count();
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:3 1 UDP 1 192.0.2.12 40004 typ host ufrag %s",
		    stable_ufrag);
	peerconnection_add_ice_candidate(pc, candidate, "0");
	ASSERT_EQ(candidate_attrs, mock_mnat_candidate_attr_count());
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:4 1 UDP 1 192.0.2.13 40006 typ host ufrag %s",
		    answer_ufrag);
	peerconnection_add_ice_candidate(pc, candidate, "0");
	ASSERT_EQ(candidate_attrs + 1, mock_mnat_candidate_attr_count());
	ASSERT_EQ(3, mock_mnat_last_candidate_generation());
	err = restart_shadow_send_and_wait(dc, &receive);
	TEST_ERR(err);
	mock_mnat_complete_media_attempts();
	err = re_main_timeout(10);
	if (err == ETIMEDOUT)
		err = 0;
	TEST_ERR(err);
	/* This data-only application m-line owns its transport group.  Once the
	 * replacement publishes, future trickle must reach generation 3 through
	 * the coordinator runtime rather than data_context's stable old MNAT. */
	candidate_attrs = mock_mnat_candidate_attr_count();
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:5 1 UDP 1 192.0.2.14 40008 typ host ufrag %s",
		    stable_ufrag);
	peerconnection_add_ice_candidate(pc, candidate, "0");
	ASSERT_EQ(candidate_attrs, mock_mnat_candidate_attr_count());
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:6 1 UDP 1 192.0.2.15 40010 typ host ufrag %s",
		    answer_ufrag);
	peerconnection_add_ice_candidate(pc, candidate, "0");
	ASSERT_EQ(candidate_attrs + 1, mock_mnat_candidate_attr_count());
	ASSERT_EQ(3, mock_mnat_last_candidate_generation());

out:
	mem_deref(initial_answer);
	mem_deref(initial_offer);
	mem_deref(retry_answer);
	mem_deref(restored);
	mem_deref(restart);
	mem_deref(stable);
	mem_deref(dc);
	mem_deref(answer_dc);
	mem_deref(answerer);
	mem_deref(pc);
	if (mock_registered)
		mock_mnat_unregister();
	return err;
}


static int sdp_reject_application(struct mbuf *mb)
{
	static const char prefix[] = "m=application ";
	uint8_t *port;
	uint8_t *end;

	if (!mb)
		return EINVAL;
	port = memmem(mb->buf, mb->end, prefix, sizeof(prefix) - 1);
	if (!port)
		return ENOENT;
	port += sizeof(prefix) - 1;
	end = memchr(port, ' ', mb->end - (size_t)(port - mb->buf));
	if (!end || end == port)
		return EPROTO;
	for (uint8_t *digit = port; digit < end; ++digit) {
		if (*digit < '0' || *digit > '9')
			return EPROTO;
		*digit = '0';
	}
	return 0;
}


static int sdp_replace_tls_id(struct mbuf *mb, const char *replacement)
{
	static const char prefix[] = "a=tls-id:";
	uint8_t *cursor;
	bool replaced = false;

	if (!mb || !replacement)
		return EINVAL;
	cursor = mb->buf;
	while ((size_t)(cursor - mb->buf) < mb->end) {
		uint8_t *start = memmem(
			cursor, mb->end - (size_t)(cursor - mb->buf),
			prefix, sizeof(prefix) - 1);
		uint8_t *end;
		size_t len;

		if (!start)
			break;
		start += sizeof(prefix) - 1;
		end = memchr(start, '\r',
			     mb->end - (size_t)(start - mb->buf));
		if (!end)
			return EPROTO;
		len = (size_t)(end - start);
		if (str_len(replacement) != len)
			return EINVAL;
		memcpy(start, replacement, len);
		replaced = true;
		cursor = end;
	}

	return replaced ? 0 : ENOENT;
}


static int sdp_replace_setup(struct mbuf **mbp, const char *replacement)
{
	static const char prefix[] = "a=setup:";
	struct mbuf *source;
	struct mbuf *result;
	size_t pos = 0;
	bool replaced = false;
	int err = 0;

	if (!mbp || !*mbp || !replacement)
		return EINVAL;
	source = *mbp;
	result = mbuf_alloc(source->end + str_len(replacement) * 4 + 1);
	if (!result)
		return ENOMEM;
	while (!err && pos < source->end) {
		const uint8_t *line = source->buf + pos;
		const uint8_t *newline =
			memchr(line, '\n', source->end - pos);
		size_t len = newline ? (size_t)(newline + 1 - line)
				     : source->end - pos;

		if (len >= sizeof(prefix) - 1 &&
		    !memcmp(line, prefix, sizeof(prefix) - 1)) {
			err = mbuf_printf(result, "%s%s\r\n", prefix,
					  replacement);
			replaced = true;
		}
		else
			err = mbuf_write_mem(result, line, len);
		pos += len;
	}
	if (err || !replaced) {
		mem_deref(result);
		return err ? err : ENOENT;
	}
	result->pos = 0;
	mem_deref(source);
	*mbp = result;
	return 0;
}


static int sdp_add_fingerprint(struct mbuf **mbp)
{
	static const char fingerprint[] =
		"a=fingerprint:SHA-256 "
		"00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
		"00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00\r\n";
	struct mbuf *source;
	struct mbuf *result;
	size_t pos = 0;
	int err = 0;

	if (!mbp || !*mbp)
		return EINVAL;
	source = *mbp;
	result = mbuf_alloc(source->end + sizeof(fingerprint) * 3);
	if (!result)
		return ENOMEM;

	while (!err && pos < source->end) {
		const uint8_t *line = source->buf + pos;
		const uint8_t *newline =
			memchr(line, '\n', source->end - pos);
		size_t len = newline ? (size_t)(newline + 1 - line)
				     : source->end - pos;

		err = mbuf_write_mem(result, line, len);
		if (!err && len >= 2 && !memcmp(line, "m=", 2))
			err = mbuf_write_str(result, fingerprint);
		pos += len;
	}
	if (err)
		mem_deref(result);
	else {
		result->pos = 0;
		mem_deref(source);
		*mbp = result;
	}
	return err;
}


static int sdp_bundle_only_media(struct mbuf **mbp, const char *type)
{
	char prefix[32];
	struct mbuf *source;
	struct mbuf *result;
	const uint8_t *line;
	const uint8_t *port;
	const uint8_t *space;
	const uint8_t *line_end;
	size_t offset;
	int err = 0;

	if (!mbp || !*mbp || !str_isset(type))
		return EINVAL;
	if (re_snprintf(prefix, sizeof(prefix), "m=%s ", type) < 0)
		return EINVAL;
	source = *mbp;
	line = memmem(source->buf, source->end, prefix,
		      str_len(prefix));
	if (!line)
		return ENOENT;
	port = line + str_len(prefix);
	space = memchr(port, ' ', source->end -
		      (size_t)(port - source->buf));
	line_end = memchr(port, '\n', source->end -
			 (size_t)(port - source->buf));
	if (!space || !line_end || space > line_end)
		return EPROTO;

	result = mbuf_alloc(source->end + 32);
	if (!result)
		return ENOMEM;
	offset = (size_t)(port - source->buf);
	err = mbuf_write_mem(result, source->buf, offset);
	for (const uint8_t *digit = port; !err && digit < space; ++digit)
		err = mbuf_write_u8(result, '0');
	if (!err)
		err = mbuf_write_mem(result, space,
				     (size_t)(line_end + 1 - space));
	if (!err)
		err = mbuf_write_str(result, "a=bundle-only\r\n");
	if (!err)
		err = mbuf_write_mem(result, line_end + 1,
				     source->end -
				     (size_t)(line_end + 1 - source->buf));
	if (err) {
		mem_deref(result);
		return err;
	}
	result->pos = 0;
	mem_deref(source);
	*mbp = result;
	return 0;
}


static int sdp_application_mid(const struct mbuf *mb, char *mid, size_t size)
{
	static const char media_prefix[] = "m=application ";
	static const char mid_prefix[] = "a=mid:";
	const uint8_t *media;
	const uint8_t *section_end;
	const uint8_t *start;
	const uint8_t *end;
	size_t len;

	if (!mb || !mid || !size)
		return EINVAL;
	media = memmem(mb->buf, mb->end, media_prefix,
		       sizeof(media_prefix) - 1);
	if (!media)
		return ENOENT;
	section_end = memmem(media + 1,
			     mb->end - (size_t)(media + 1 - mb->buf),
			     "\nm=", 3);
	if (!section_end)
		section_end = mb->buf + mb->end;
	start = memmem(media, (size_t)(section_end - media), mid_prefix,
		       sizeof(mid_prefix) - 1);
	if (!start)
		return ENOENT;
	start += sizeof(mid_prefix) - 1;
	end = memchr(start, '\r', (size_t)(section_end - start));
	if (!end)
		end = memchr(start, '\n', (size_t)(section_end - start));
	if (!end)
		end = section_end;
	len = (size_t)(end - start);
	if (!len || len >= size)
		return EOVERFLOW;
	memcpy(mid, start, len);
	mid[len] = '\0';
	return 0;
}


static int sdp_bundle_application_first(const struct mbuf *mb, bool *first)
{
	static const char prefix[] = "a=group:BUNDLE ";
	const uint8_t *group;
	const uint8_t *end;
	char mid[16];
	size_t len;
	int err;

	if (!first)
		return EINVAL;
	err = sdp_application_mid(mb, mid, sizeof(mid));
	if (err)
		return err;
	group = memmem(mb->buf, mb->end, prefix, sizeof(prefix) - 1);
	if (!group)
		return ENOENT;
	group += sizeof(prefix) - 1;
	end = group;
	while ((size_t)(end - mb->buf) < mb->end &&
	       *end != ' ' && *end != '\r' && *end != '\n')
		++end;
	len = (size_t)(end - group);
	*first = len == str_len(mid) && !memcmp(group, mid, len);
	return 0;
}


static int sdp_move_application_first(struct mbuf **mbp)
{
	static const char prefix[] = "a=group:BUNDLE ";
	struct mbuf *source;
	struct mbuf *result;
	const uint8_t *group;
	const uint8_t *line_end;
	const uint8_t *cursor;
	char mid[16];
	int err;

	if (!mbp || !*mbp)
		return EINVAL;
	source = *mbp;
	err = sdp_application_mid(source, mid, sizeof(mid));
	if (err)
		return err;
	group = memmem(source->buf, source->end, prefix, sizeof(prefix) - 1);
	if (!group)
		return ENOENT;
	group += sizeof(prefix) - 1;
	line_end = memchr(group, '\r',
			  source->end - (size_t)(group - source->buf));
	if (!line_end)
		return EPROTO;

	result = mbuf_alloc(source->end + 1);
	if (!result)
		return ENOMEM;
	err = mbuf_write_mem(result, source->buf,
			     (size_t)(group - source->buf));
	if (!err)
		err = mbuf_write_str(result, mid);
	cursor = group;
	while (!err && cursor < line_end) {
		const uint8_t *start;
		size_t len;

		while (cursor < line_end && *cursor == ' ')
			++cursor;
		start = cursor;
		while (cursor < line_end && *cursor != ' ')
			++cursor;
		len = (size_t)(cursor - start);
		if (!len || (len == str_len(mid) && !memcmp(start, mid, len)))
			continue;
		err = mbuf_write_u8(result, ' ');
		if (!err)
			err = mbuf_write_mem(result, start, len);
	}
	if (!err)
		err = mbuf_write_mem(result, line_end,
				     source->end -
				     (size_t)(line_end - source->buf));
	if (err) {
		mem_deref(result);
		return err;
	}
	result->pos = 0;
	mem_deref(source);
	*mbp = result;
	return 0;
}


static bool is_transport_attr_line(const uint8_t *line, size_t len)
{
	static const char *prefixes[] = {
		"a=setup:",
		"a=fingerprint:",
		"a=tls-id:",
		"a=ice-ufrag:",
		"a=ice-pwd:",
		"a=candidate:",
		"a=end-of-candidates",
	};

	for (size_t i = 0; i < RE_ARRAY_SIZE(prefixes); ++i) {
		size_t prefix_len = str_len(prefixes[i]);

		if (len >= prefix_len &&
		    !memcmp(line, prefixes[i], prefix_len))
			return true;
	}
	return false;
}


static int sdp_application_transport_only(struct mbuf **mbp)
{
	struct mbuf *source;
	struct mbuf *result;
	size_t pos = 0;
	bool in_media = false;
	bool application = false;
	int err = 0;

	if (!mbp || !*mbp)
		return EINVAL;
	source = *mbp;
	result = mbuf_alloc(source->end + 1);
	if (!result)
		return ENOMEM;

	while (!err && pos < source->end) {
		const uint8_t *line = source->buf + pos;
		const uint8_t *newline =
			memchr(line, '\n', source->end - pos);
		size_t len = newline ? (size_t)(newline + 1 - line)
				     : source->end - pos;

		if (len >= 2 && !memcmp(line, "m=", 2)) {
			in_media = true;
			application = len >= 14 &&
				!memcmp(line, "m=application ", 14);
		}
		if (!(in_media && !application &&
		      is_transport_attr_line(line, len)))
			err = mbuf_write_mem(result, line, len);
		pos += len;
	}

	if (err)
		mem_deref(result);
	else {
		result->pos = 0;
		mem_deref(source);
		*mbp = result;
	}
	return err;
}


enum sdp_transport_media {
	SDP_TRANSPORT_AUDIO       = 1U << 0,
	SDP_TRANSPORT_VIDEO       = 1U << 1,
	SDP_TRANSPORT_APPLICATION = 1U << 2,
};


static int sdp_set_bundle_group(struct mbuf **mbp, const char *value)
{
	static const char prefix[] = "a=group:";
	struct mbuf *source;
	struct mbuf *result;
	const uint8_t *line;
	const uint8_t *line_end;
	size_t result_size;
	int err;

	if (!mbp || !*mbp || !str_isset(value))
		return EINVAL;
	source = *mbp;
	line = memmem(source->buf, source->end, prefix,
		      sizeof(prefix) - 1);
	if (!line)
		return ENOENT;
	line_end = memchr(line, '\n',
			  source->end - (size_t)(line - source->buf));
	if (!line_end)
		return EPROTO;
	++line_end;

	result_size = source->end - (size_t)(line_end - line) +
		sizeof(prefix) - 1 + str_len(value) + 2;
	result = mbuf_alloc(result_size);
	if (!result)
		return ENOMEM;
	err = mbuf_write_mem(result, source->buf,
			     (size_t)(line - source->buf));
	if (!err)
		err = mbuf_printf(result, "%s%s\r\n", prefix, value);
	if (!err)
		err = mbuf_write_mem(result, line_end,
				     source->end -
				     (size_t)(line_end - source->buf));
	if (err) {
		mem_deref(result);
		return err;
	}

	result->pos = 0;
	mem_deref(source);
	*mbp = result;
	return 0;
}


static int sdp_video_require_rtcp_mux(struct mbuf **mbp)
{
	static const char prefix[] = "m=video ";
	struct mbuf *source;
	struct mbuf *result;
	const uint8_t *media;
	const uint8_t *line_end;
	const uint8_t *section_end;

	if (!mbp || !*mbp)
		return EINVAL;
	source = *mbp;
	media = memmem(source->buf, source->end, prefix,
		       sizeof(prefix) - 1);
	if (!media)
		return ENOENT;
	line_end = memchr(media, '\n',
			  source->end - (size_t)(media - source->buf));
	if (!line_end)
		return EPROTO;
	++line_end;
	section_end = memmem(line_end,
		source->end - (size_t)(line_end - source->buf), "\r\nm=", 4);
	if (!section_end)
		section_end = source->buf + source->end;
	if (memmem(line_end, (size_t)(section_end - line_end),
		   "a=rtcp-mux", 10))
		return 0;
	result = mbuf_alloc(source->end + 14);
	if (!result)
		return ENOMEM;
	if (mbuf_write_mem(result, source->buf,
			   (size_t)(line_end - source->buf)) ||
	    mbuf_write_str(result, "a=rtcp-mux\r\n") ||
	    mbuf_write_mem(result, line_end,
			   source->end - (size_t)(line_end - source->buf))) {
		mem_deref(result);
		return ENOMEM;
	}
	result->pos = 0;
	mem_deref(source);
	*mbp = result;
	return 0;
}


static int sdp_video_isolate_transport(struct mbuf **mbp)
{
	struct mbuf *source;
	struct mbuf *result;
	size_t pos = 0;
	uint16_t video_port = 0;
	bool video = false;
	int err = 0;

	if (!mbp || !*mbp)
		return EINVAL;
	source = *mbp;
	result = mbuf_alloc(source->end + 128);
	if (!result)
		return ENOMEM;
	while (!err && pos < source->end) {
		const uint8_t *line = source->buf + pos;
		const uint8_t *newline = memchr(line, '\n', source->end - pos);
		size_t len = newline ? (size_t)(newline + 1 - line)
				     : source->end - pos;
		bool media_rewritten = false;

		if (len >= 8 && !memcmp(line, "m=video ", 8)) {
			char *end = NULL;
			unsigned long port = strtoul((const char *)line + 8,
						     &end, 10);

			if (!end || end == (const char *)line + 8 ||
			    port > UINT16_MAX) {
				err = EPROTO;
				break;
			}
			video_port = 40000;
			video = true;
			err = mbuf_write_mem(result, line, 8);
			if (!err)
				err = mbuf_printf(result, "%u", video_port);
			if (!err)
				err = mbuf_write_mem(
					result, (const uint8_t *)end,
					len - (size_t)((const uint8_t *)end - line));
			media_rewritten = true;
		}
		else if (len >= 2 && !memcmp(line, "m=", 2))
			video = false;

		if (media_rewritten) {
			/* The rewritten media line was emitted above. */
		}
		else if (video && len >= 12 &&
			 !memcmp(line, "a=ice-ufrag:", 12))
			err = mbuf_write_str(result,
				"a=ice-ufrag:video-split\r\n");
		else if (video && len >= 10 &&
			 !memcmp(line, "a=ice-pwd:", 10))
			err = mbuf_write_str(result,
				"a=ice-pwd:video-split-password-0001\r\n");
		else if (video && len >= 9 && !memcmp(line, "a=tls-id:", 9))
			err = mbuf_write_str(result,
				"a=tls-id:VideoSplitTransport000001\r\n");
		else if (video && len >= 12 &&
			 !memcmp(line, "a=candidate:", 12)) {
			const uint8_t *port = line;
			const uint8_t *space;

			for (unsigned field = 0; field < 5; ++field) {
				space = memchr(port, ' ', len - (size_t)(port - line));
				if (!space) {
					err = EPROTO;
					break;
				}
				port = space + 1;
			}
			space = !err ? memchr(port, ' ',
				len - (size_t)(port - line)) : NULL;
			if (!err && !space)
				err = EPROTO;
			if (!err)
				err = mbuf_write_mem(result, line,
					(size_t)(port - line));
			if (!err)
				err = mbuf_printf(result, "%u", video_port);
			if (!err)
				err = mbuf_write_mem(result, space,
					len - (size_t)(space - line));
		}
		else
			err = mbuf_write_mem(result, line, len);
		pos += len;
	}
	if (err || !video_port) {
		mem_deref(result);
		return err ? err : ENOENT;
	}
	result->pos = 0;
	mem_deref(source);
	*mbp = result;
	return 0;
}


static unsigned sdp_transport_media_mask(const uint8_t *line, size_t len)
{
	if (len >= 8 && !memcmp(line, "m=audio ", 8))
		return SDP_TRANSPORT_AUDIO;
	if (len >= 8 && !memcmp(line, "m=video ", 8))
		return SDP_TRANSPORT_VIDEO;
	if (len >= 14 && !memcmp(line, "m=application ", 14))
		return SDP_TRANSPORT_APPLICATION;

	return 0;
}


static int sdp_share_bundle_transport_attrs(struct mbuf **mbp, unsigned tag,
					    unsigned members)
{
	struct mbuf *source;
	struct mbuf *result;
	struct mbuf *attrs;
	size_t pos = 0;
	unsigned media = 0;
	int err = 0;

	if (!mbp || !*mbp || !tag || (tag & (tag - 1)) || !(members & tag))
		return EINVAL;
	source = *mbp;
	attrs = mbuf_alloc(256);
	if (!attrs)
		return ENOMEM;

	while (!err && pos < source->end) {
		const uint8_t *line = source->buf + pos;
		const uint8_t *newline =
			memchr(line, '\n', source->end - pos);
		size_t len = newline ? (size_t)(newline + 1 - line)
				     : source->end - pos;

		if (len >= 2 && !memcmp(line, "m=", 2))
			media = sdp_transport_media_mask(line, len);
		else if (media == tag && is_transport_attr_line(line, len))
			err = mbuf_write_mem(attrs, line, len);
		pos += len;
	}
	if (err || !attrs->end) {
		mem_deref(attrs);
		return err ? err : EPROTO;
	}

	result = mbuf_alloc(source->end + attrs->end * 2 + 1);
	if (!result)
		goto nomem;

	pos = 0;
	media = 0;
	while (!err && pos < source->end) {
		const uint8_t *line = source->buf + pos;
		const uint8_t *newline =
			memchr(line, '\n', source->end - pos);
		size_t len = newline ? (size_t)(newline + 1 - line)
				     : source->end - pos;

		if (len >= 2 && !memcmp(line, "m=", 2)) {
			media = sdp_transport_media_mask(line, len);
			err = mbuf_write_mem(result, line, len);
			if (!err && (members & media) && media != tag)
				err = mbuf_write_mem(result, attrs->buf,
						     attrs->end);
		}
		else if (!((members & media) && media != tag &&
			   is_transport_attr_line(line, len)))
			err = mbuf_write_mem(result, line, len);
		pos += len;
	}

	mem_deref(attrs);
	if (err)
		mem_deref(result);
	else {
		result->pos = 0;
		mem_deref(source);
		*mbp = result;
	}
	return err;

nomem:
	mem_deref(attrs);
	return ENOMEM;
}


static int sdp_media_status(const struct mbuf *mb, const char *type,
			    uint16_t *portp, bool *bundle_onlyp)
{
	char prefix[32];
	const uint8_t *media;
	const uint8_t *section_end;
	const uint8_t *port;
	char *end;
	unsigned long parsed;
	int written;

	if (!mb || !str_isset(type) || !portp || !bundle_onlyp)
		return EINVAL;
	written = re_snprintf(prefix, sizeof(prefix), "m=%s ", type);
	if (written < 0 || (size_t)written >= sizeof(prefix))
		return EOVERFLOW;
	media = memmem(mb->buf, mb->end, prefix, (size_t)written);
	if (!media)
		return ENOENT;
	port = media + written;
	errno = 0;
	parsed = strtoul((const char *)port, &end, 10);
	if (errno || end == (char *)port || parsed > UINT16_MAX)
		return EPROTO;
	section_end = memmem(media + 1,
			     mb->end - (size_t)(media + 1 - mb->buf),
			     "\nm=", 3);
	if (!section_end)
		section_end = mb->buf + mb->end;

	*portp = (uint16_t)parsed;
	*bundle_onlyp =
		memmem(media, (size_t)(section_end - media),
		       "a=bundle-only\r\n", 15) != NULL;
	return 0;
}


struct bundle_layout_case {
	const char *group;
	unsigned tag_media;
	unsigned bundle_media;
	const char *independent_media;
	size_t transport_groups;
};


static int add_bundle_layout_media(struct peer_connection *pc,
				   bool create_channel,
				   struct data_channel **dcp)
{
	int err;

	err = peerconnection_add_audio_track(pc, conf_config(),
					     baresip_aucodecl(),
					     SDP_SENDRECV);
	TEST_ERR(err);
	err = peerconnection_add_video_track(pc, conf_config(),
					     baresip_vidcodecl(),
					     SDP_SENDRECV);
	TEST_ERR(err);
	if (create_channel)
		err = peerconnection_create_datachannel(
			pc, "bundle-layout", NULL, dcp);
	else
		err = peerconnection_set_datachannel_handler(pc, NULL, NULL);
	TEST_ERR(err);

out:
	return err;
}


static int wait_for_gather(struct gather_wait *wait)
{
	int err = 0;

	if (!wait->ready)
		err = re_main_timeout(10000);
	TEST_ERR(err);
	ASSERT_TRUE(wait->ready);

out:
	return err;
}


static int test_bundle_layout_case(const struct mnat *mnat,
				   const struct menc *menc,
				   const struct bundle_layout_case *layout)
{
	const struct rtc_configuration offer_config = {
		.offerer = true,
	};
	const struct rtc_configuration answer_config = {
		.offerer = false,
	};
	struct session_description description;
	struct gather_wait offer_wait = {0};
	struct gather_wait answer_wait = {0};
	struct peer_connection *offerer = NULL;
	struct peer_connection *answerer = NULL;
	struct data_channel *dc = NULL;
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	char group[64];
	const char *types[] = {"audio", "video", "application"};
	uint16_t port;
	bool bundle_only;
	int err;

	if (!mnat || !menc || !layout)
		return EINVAL;

	err = peerconnection_new(&offerer, &offer_config, mnat, menc,
			 peerconn_validation_gather_handler, NULL, NULL,
				 &offer_wait);
	TEST_ERR(err);
	err = add_bundle_layout_media(offerer, true, &dc);
	TEST_ERR(err);
	err = wait_for_gather(&offer_wait);
	TEST_ERR(err);

	err = peerconnection_new(&answerer, &answer_config, mnat, menc,
				 peerconn_validation_gather_handler, NULL, NULL,
				 &answer_wait);
	TEST_ERR(err);
	err = add_bundle_layout_media(answerer, false, NULL);
	TEST_ERR(err);
	err = wait_for_gather(&answer_wait);
	TEST_ERR(err);

	err = peerconnection_create_offer(offerer, &offer);
	TEST_ERR(err);
	err = sdp_attr_value(offer, "group", group, sizeof(group));
	TEST_ERR(err);
	ASSERT_STREQ("BUNDLE 0 1 2", group);
	err = sdp_set_bundle_group(&offer, layout->group);
	TEST_ERR(err);
	err = sdp_share_bundle_transport_attrs(&offer, layout->tag_media,
					       layout->bundle_media);
	TEST_ERR(err);
	err = sdp_attr_value(offer, "group", group, sizeof(group));
	TEST_ERR(err);
	ASSERT_STREQ(layout->group, group);

	description.type = SDP_OFFER;
	description.sdp = offer;
	err = peerconnection_set_remote_descr(answerer, &description);
	TEST_ERR(err);
	err = peerconnection_create_answer(answerer, &answer);
	TEST_ERR(err);
	err = sdp_attr_value(answer, "group", group, sizeof(group));
	TEST_ERR(err);
	ASSERT_STREQ(layout->group, group);

	for (size_t i = 0; i < RE_ARRAY_SIZE(types); ++i) {
		err = sdp_media_status(answer, types[i], &port, &bundle_only);
		TEST_ERR(err);
		ASSERT_TRUE(port || bundle_only);
		if (!str_cmp(types[i], layout->independent_media))
			ASSERT_TRUE(port != 0 && !bundle_only);
	}

out:
	mem_deref(answer);
	mem_deref(offer);
	mem_deref(dc);
	mem_deref(answerer);
	mem_deref(offerer);
	return err;
}


static int test_bundle_layout_negotiation(const struct mnat *mnat,
					  const struct menc *menc)
{
	static const struct bundle_layout_case cases[] = {
		{
			.group = "BUNDLE 1 0 2",
			.tag_media = SDP_TRANSPORT_VIDEO,
			.bundle_media = SDP_TRANSPORT_AUDIO |
					SDP_TRANSPORT_VIDEO |
					SDP_TRANSPORT_APPLICATION,
			.transport_groups = 1,
		},
		{
			.group = "BUNDLE 1 2",
			.tag_media = SDP_TRANSPORT_VIDEO,
			.bundle_media = SDP_TRANSPORT_VIDEO |
					SDP_TRANSPORT_APPLICATION,
			.independent_media = "audio",
			.transport_groups = 2,
		},
		{
			.group = "BUNDLE 0 1",
			.tag_media = SDP_TRANSPORT_AUDIO,
			.bundle_media = SDP_TRANSPORT_AUDIO |
					SDP_TRANSPORT_VIDEO,
			.independent_media = "application",
			.transport_groups = 2,
		},
	};
	bool g711_loaded = false;
	bool video_registered = false;
	int err;

	err = module_load(".", "g711");
	TEST_ERR(err);
	g711_loaded = true;
	mock_vidcodec_register();
	video_registered = true;

	for (size_t i = 0; i < RE_ARRAY_SIZE(cases); ++i) {
		err = test_bundle_layout_case(mnat, menc, &cases[i]);
		TEST_ERR(err);
	}

out:
	if (video_registered)
		mock_vidcodec_unregister();
	if (g711_loaded)
		module_unload("g711");
	return err;
}


static void data_message_handler(struct data_channel *dc,
				 enum data_channel_message_type type,
				 const uint8_t *buf, size_t len, void *arg)
{
	struct agent *ag = arg;
	int err = 0;

	if (ag == ag->fix->b) {
		if (type != DATACHANNEL_MESSAGE_BINARY ||
		    len != sizeof(data_binary) ||
		    memcmp(buf, data_binary, len)) {
			agent_close(ag, EBADMSG);
			return;
		}

		err = datachannel_send(dc, DATACHANNEL_MESSAGE_TEXT,
				       data_text, sizeof(data_text) - 1);
	}
	else {
		if (type != DATACHANNEL_MESSAGE_TEXT ||
		    len != sizeof(data_text) - 1 ||
		    memcmp(buf, data_text, len)) {
			agent_close(ag, EBADMSG);
			return;
		}
		ag->got_data = true;
	}

	if (err || agents_are_complete(ag))
		agent_close(ag, err);
}


static void data_state_handler(struct data_channel *dc,
			       enum data_channel_state state,
			       int err, void *arg)
{
	struct agent *ag = arg;

	if (state == DATACHANNEL_CLOSED && ag->destroy_on_data_close) {
		ag->data_close_closed = true;
		ag->data_close_stable =
			peerconnection_signaling(ag->pc) == SS_STABLE;
		ag->pc = mem_deref(ag->pc);
		return;
	}
	if (err) {
		agent_close(ag, err);
		return;
	}

	if (state == DATACHANNEL_OPEN && ag == ag->fix->a &&
	    !ag->data_sent) {
		ag->data_sent = true;
		err = datachannel_send(dc, DATACHANNEL_MESSAGE_BINARY,
				       data_binary, sizeof(data_binary));
		if (err)
			agent_close(ag, err);
	}
}


static void deferred_receive_message_handler(
	struct data_channel *dc, enum data_channel_message_type type,
	const uint8_t *buf, size_t len, void *arg)
{
	struct deferred_receive_check *check = arg;

	(void)dc;
	(void)buf;
	if (type == DATACHANNEL_MESSAGE_BINARY && len == 16384) {
		++check->messages;
		if (check->destroy_on_first_message &&
		    check->messages == 1) {
			check->agent->pc = mem_deref(check->agent->pc);
			check->agent->dc = NULL;
		}
	}
}


static void deferred_receive_state_handler(
	struct data_channel *dc, enum data_channel_state state, int err,
	void *arg)
{
	struct deferred_receive_check *check = arg;

	(void)dc;
	if (state == DATACHANNEL_CLOSED) {
		++check->closed;
		check->close_err = err;
		if (check->destroy_on_closed) {
			check->agent->pc = mem_deref(check->agent->pc);
			check->agent->dc = NULL;
		}
	}
}


static void deferred_receive_incoming_handler(struct data_channel *dc,
					      void *arg)
{
	struct deferred_receive_check *check = arg;

	++check->incoming;
	(void)datachannel_close(dc);
}


struct close_wait {
	bool closed;
	int err;
};

struct release_on_closed {
	struct data_channel **dcp;
	bool closed;
};


static void release_on_closed_handler(struct data_channel *dc,
				      enum data_channel_state state,
				      int err, void *arg)
{
	struct release_on_closed *release = arg;

	(void)dc;
	(void)err;
	if (state == DATACHANNEL_CLOSED) {
		release->closed = true;
		*release->dcp = mem_deref(*release->dcp);
		re_cancel();
	}
}


static void close_state_handler(struct data_channel *dc,
				enum data_channel_state state,
				int err, void *arg)
{
	struct close_wait *wait = arg;

	(void)dc;
	if (err)
		wait->err = err;
	if (err || state == DATACHANNEL_CLOSED) {
		wait->closed = state == DATACHANNEL_CLOSED;
		re_cancel();
	}
}


static int wait_for_channel_close(struct close_wait *wait, uint64_t timeout_ms)
{
	const uint64_t deadline = tmr_jiffies() + timeout_ms;

	while (!wait->closed && !wait->err) {
		const uint64_t now = tmr_jiffies();
		int err;

		if (now >= deadline)
			return ETIMEDOUT;
		err = re_main_timeout((uint32_t)(deadline - now));
		if (err)
			return err;
	}

	return wait->err;
}


static int wait_for_channel_release(struct release_on_closed *release,
				    uint64_t timeout_ms)
{
	const uint64_t deadline = tmr_jiffies() + timeout_ms;

	while (!release->closed) {
		const uint64_t now = tmr_jiffies();
		int err;

		if (now >= deadline)
			return ETIMEDOUT;
		err = re_main_timeout((uint32_t)(deadline - now));
		if (err)
			return err;
	}

	return 0;
}


static void incoming_datachannel_handler(struct data_channel *dc, void *arg)
{
	struct agent *ag = arg;
	int err;

	if (ag->destroy_on_channel) {
		ag->pc = mem_deref(ag->pc);
		ag->dc = NULL;
		re_cancel();
		return;
	}

	ag->dc = dc;
	err = datachannel_set_handlers(dc, data_message_handler,
				       data_state_handler, NULL, ag);
	if (err)
		agent_close(ag, err);
}
#endif


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

	struct agent *peer = agent_peer(ag);

	if (peer && peer->got_sdp) {

		err = peerconnection_start_ice(ag->pc);
		TEST_ERR(err);

		err = peerconnection_start_ice(peer->pc);
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

	ASSERT_EQ(MAGIC_AGENT, ag->magic);

	if (ag->err)
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

#ifdef USE_DATACHANNEL
	if (type == SDP_OFFER && ag->fix->bundle_only_data) {
		err = sdp_bundle_only_media(&mb, "application");
		TEST_ERR(err);
	}
	if (type == SDP_OFFER && ag->fix->application_first_bundle) {
		err = sdp_move_application_first(&mb);
		TEST_ERR(err);
		err = sdp_application_transport_only(&mb);
		TEST_ERR(err);
	}
	if (type == SDP_ANSWER && ag->fix->application_first_bundle) {
		bool first = false;

		err = sdp_bundle_application_first(mb, &first);
		TEST_ERR(err);
		ASSERT_TRUE(first);
		ag->fix->application_first_answer = true;
	}
#endif
	err = agent_handle_sdp(agent_peer(ag), type, mb);
	TEST_ERR(err);

 out:
	mem_deref(mb);

	if (err) {
		agent_close(ag, err);
	}
}


static void peerconnection_estab_handler(struct media_track *media, void *arg)
{
	struct agent *ag = arg;
	struct audio *au;
	int err = 0;

	ASSERT_EQ(MAGIC_AGENT, ag->magic);

#ifdef USE_DATACHANNEL
	if (ag->destroy_on_media_estab) {
		ag->pc = mem_deref(ag->pc);
		re_cancel();
		return;
	}
#endif

	switch (mediatrack_kind(media)) {

	case MEDIA_KIND_AUDIO:
		ag->got_estab_audio = true;
		++ag->estab_audio_count;
		ag->media = media;

		au = media_get_audio(media);

		err = audio_set_devicename(au, "440", "default");
		TEST_ERR(err);

		err = audio_set_source(au, "ausine", "440");
		TEST_ERR(err);

		err = audio_set_player(au, "mock-auplay", "default");
		TEST_ERR(err);

		err = audio_set_bitrate(au, 64000);
		TEST_ERR(err);

		err = mediatrack_start_audio(media, baresip_ausrcl(),
					     baresip_aufiltl());
		TEST_ERR(err);
		break;

	case MEDIA_KIND_VIDEO:
		ag->got_estab_video = true;
		++ag->estab_video_count;

		err = mediatrack_start_video(media);
		TEST_ERR(err);
		break;

	default:
		break;
	}

 out:
	if (err || agents_are_complete(ag)) {
		agent_close(ag, err);
	}
}


static void peerconnection_close_handler(int err, void *arg)
{
	struct agent *ag = arg;

	ASSERT_EQ(MAGIC_AGENT, ag->magic);
#ifdef USE_DATACHANNEL
	if (ag->destroy_on_pc_close) {
		ag->pc = mem_deref(ag->pc);
		ag->dc = NULL;
		return;
	}
#endif

	if (err) {
		warning("[ %s ] peer connection closed (%m)\n",
			ag->name, err);
	}
	else {
		info("[ %s ] peer connection closed\n", ag->name);
	}

 out:
	agent_close(ag, err);
}


/* called in the context of the main thread */
static void mqueue_handler(int id, void *data, void *arg)
{
	struct fixture *fix = arg;

	(void)id;
	(void)data;
	(void)fix;

	re_cancel();
}


static void destructor(void *arg)
{
	struct agent *ag = arg;

	mem_deref(ag->pc);
}


#ifndef USE_DATACHANNEL
static int test_disabled_datachannel_api(void)
{
	struct peer_connection *pc = (struct peer_connection *)(uintptr_t)1;
	struct data_channel *dc = (struct data_channel *)(uintptr_t)1;
	struct data_channel_config config = {0};
	struct data_channel *created = NULL;
	const uint8_t payload = 0;
	int err = 0;

	ASSERT_EQ(EINVAL,
		  peerconnection_set_datachannel_handler(NULL, NULL, NULL));
	ASSERT_EQ(ENOTSUP,
		  peerconnection_set_datachannel_handler(pc, NULL, NULL));
	ASSERT_EQ(EINVAL, peerconnection_create_datachannel(
				  NULL, "disabled", &config, &created));
	ASSERT_EQ(ENOTSUP, peerconnection_create_datachannel(
				   pc, "disabled", &config, &created));
	ASSERT_EQ(EINVAL,
		  datachannel_set_handlers(NULL, NULL, NULL, NULL, NULL));
	ASSERT_EQ(ENOTSUP,
		  datachannel_set_handlers(dc, NULL, NULL, NULL, NULL));
	ASSERT_EQ(EINVAL, datachannel_send(
				  NULL, DATACHANNEL_MESSAGE_BINARY,
				  &payload, sizeof(payload)));
	ASSERT_EQ(ENOTSUP, datachannel_send(
				   dc, DATACHANNEL_MESSAGE_BINARY,
				   &payload, sizeof(payload)));
	ASSERT_EQ(EINVAL, datachannel_close(NULL));
	ASSERT_EQ(ENOTSUP, datachannel_close(dc));
	ASSERT_TRUE(datachannel_label(dc) == NULL);
	ASSERT_TRUE(datachannel_protocol(dc) == NULL);
	ASSERT_EQ(-1, datachannel_id(dc));
	ASSERT_EQ(DATACHANNEL_CLOSED, datachannel_state(dc));
	ASSERT_EQ(0, (int)datachannel_buffered_amount(dc));

out:
	return err;
}
#endif


#ifdef USE_DATACHANNEL
enum data_setup_order {
	DATA_AFTER_MEDIA,
	DATA_BEFORE_MEDIA,
	DATA_BETWEEN_MEDIA,
};


static int agent_add_data(struct agent *ag, bool offerer)
{
	const struct data_channel_config dc_config = {
		.ordered = true,
		.max_retransmits = -1,
		.max_packet_lifetime = -1,
		.protocol = ag->negotiated_data ? "sdp-test" : NULL,
		.negotiated = ag->negotiated_data,
		.id = 1,
	};
	int err;

	err = peerconnection_set_datachannel_handler(
		ag->pc, incoming_datachannel_handler, ag);
	if (err || !offerer)
		return err;

	err = peerconnection_create_datachannel(
		ag->pc, "loopback",
		ag->negotiated_data ? &dc_config : NULL, &ag->dc);
	if (err)
		return err;
	err = datachannel_set_handlers(
		ag->dc, data_message_handler, data_state_handler, NULL, ag);
	if (err)
		return err;
	if (ag->negotiated_data) {
		struct data_channel *duplicate = NULL;

		err = peerconnection_create_datachannel(
			ag->pc, "duplicate", &dc_config, &duplicate);
		if (err != EADDRINUSE) {
			mem_deref(duplicate);
			return err ? err : EPROTO;
		}
	}
	if (datachannel_id(ag->dc) !=
	    (ag->negotiated_data ? 1 : -1))
		return EPROTO;
	if (datachannel_state(ag->dc) != DATACHANNEL_CONNECTING)
		return EPROTO;

	return 0;
}


static int verify_continuity_id(struct fixture *fix, uint16_t id)
{
	struct data_channel_config config = {
		.ordered = true,
		.max_retransmits = -1,
		.max_packet_lifetime = -1,
		.protocol = "replacement",
		.negotiated = true,
		.id = id,
	};
	struct data_channel *a = NULL;
	struct data_channel *b = NULL;
	int err;

	fix->terminated = false;
	fix->a->got_data = false;
	fix->b->got_data = false;
	fix->a->data_sent = false;
	fix->a->got_audio = false;
	fix->b->got_audio = false;
	fix->a->got_video = false;
	fix->b->got_video = false;

	err = peerconnection_create_datachannel(
		fix->a->pc, "replacement", &config, &a);
	TEST_ERR(err);
	err = peerconnection_create_datachannel(
		fix->b->pc, "replacement", &config, &b);
	TEST_ERR(err);
	err = datachannel_set_handlers(
		a, data_message_handler, data_state_handler, NULL, fix->a);
	TEST_ERR(err);
	err = datachannel_set_handlers(
		b, data_message_handler, data_state_handler, NULL, fix->b);
	TEST_ERR(err);
	if (datachannel_state(a) == DATACHANNEL_OPEN &&
	    !fix->a->data_sent) {
		fix->a->data_sent = true;
		err = datachannel_send(a, DATACHANNEL_MESSAGE_BINARY,
				       data_binary, sizeof(data_binary));
		TEST_ERR(err);
	}

	if (!agents_are_complete(fix->a)) {
		uint64_t deadline = tmr_jiffies() + 10000;

		do {
			err = re_main_timeout(100);
			if (err == ETIMEDOUT)
				err = 0;
		} while (!err && !fix->terminated &&
			 !agents_are_complete(fix->a) &&
			 tmr_jiffies() < deadline);
		if (!err && !agents_are_complete(fix->a))
			err = ETIMEDOUT;
		if (err) {
			warning("replacement continuity:"
				" data=%d/%d audio=%d/%d video=%d/%d"
				" states=%d/%d errors=%m/%m\n",
				fix->a->got_data, fix->b->got_data,
				fix->a->got_audio, fix->b->got_audio,
				fix->a->got_video, fix->b->got_video,
				datachannel_state(a), datachannel_state(b),
				fix->a->err, fix->b->err);
		}
		TEST_ERR(err);
	}
	TEST_ERR(fix->a->err);
	TEST_ERR(fix->b->err);
	ASSERT_TRUE(fix->a->got_data);
	if (fix->a->use_audio)
		ASSERT_TRUE(fix->a->got_audio || fix->b->got_audio);
	if (fix->a->use_video)
		ASSERT_TRUE(fix->a->got_video || fix->b->got_video);

out:
	mem_deref(b);
	mem_deref(a);
	return err;
}


static int verify_replacement_continuity(struct fixture *fix)
{
	uint16_t id;

	if (!fix->next_continuity_id)
		/* Keep continuity probes away from fixture-owned DCEP/negotiated SIDs. */
		fix->next_continuity_id = 100;
	id = fix->next_continuity_id++;
	return verify_continuity_id(fix, id);
}


static int renegotiate_bundle_membership(struct fixture *fix,
					 const char *group,
					 bool rollback,
					 uint16_t channel_id)
{
	struct session_description description;
	struct stream *established = fix->b->media
		? media_get_stream(fix->b->media) : NULL;
	struct sa active_remote;
	enum bundle_state active_bundle = BUNDLE_NONE;
	char active_mid[64] = {0};
	int active_pt = -1;
	bool active_secure = false;
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	int err;

	if (established) {
		sa_cpy(&active_remote, stream_raddr(established));
		active_bundle = bundle_state(stream_bundle(established));
		active_pt = stream_pt_enc(established);
		active_secure = stream_is_secure(established);
		re_snprintf(active_mid, sizeof(active_mid), "%s",
			    stream_mid(established));
	}

	err = peerconnection_create_offer(fix->a->pc, &offer);
	TEST_ERR(err);
	if (!str_cmp(group, "BUNDLE 0 2")) {
		err = sdp_share_bundle_transport_attrs(
			&offer, SDP_TRANSPORT_AUDIO,
			SDP_TRANSPORT_AUDIO | SDP_TRANSPORT_VIDEO);
		TEST_ERR(err);
	}
	err = sdp_set_bundle_group(&offer, group);
	TEST_ERR(err);
	if (!str_cmp(group, "BUNDLE 0 2")) {
		err = sdp_video_require_rtcp_mux(&offer);
		TEST_ERR(err);
		err = sdp_video_isolate_transport(&offer);
		TEST_ERR(err);
	}
	description.type = SDP_OFFER;
	description.sdp = offer;
	err = peerconnection_set_remote_descr(fix->b->pc, &description);
	TEST_ERR(err);
	/* A provisional remote offer must not publish any established RTP,
	 * crypto, MID, or BUNDLE runtime state. */
	if (established) {
		ASSERT_TRUE(sa_cmp(&active_remote, stream_raddr(established),
				   SA_ALL));
		ASSERT_EQ(active_bundle,
			  bundle_state(stream_bundle(established)));
		ASSERT_EQ(active_pt, stream_pt_enc(established));
		ASSERT_EQ(active_secure, stream_is_secure(established));
		ASSERT_STREQ(active_mid, stream_mid(established));
	}

	if (rollback) {
		description.type = SDP_ROLLBACK;
		description.sdp = NULL;
		err = peerconnection_set_remote_descr(fix->b->pc,
						    &description);
		TEST_ERR(err);
		err = peerconnection_set_remote_descr(fix->a->pc,
						    &description);
		TEST_ERR(err);
		if (established) {
			ASSERT_TRUE(sa_cmp(&active_remote,
					   stream_raddr(established), SA_ALL));
			ASSERT_EQ(active_bundle,
				  bundle_state(stream_bundle(established)));
			ASSERT_EQ(active_pt, stream_pt_enc(established));
			ASSERT_EQ(active_secure, stream_is_secure(established));
			ASSERT_STREQ(active_mid, stream_mid(established));
		}
	}
	else {
		err = peerconnection_create_answer(fix->b->pc, &answer);
		TEST_ERR(err);
		if (!str_cmp(group, "BUNDLE 0 2")) {
			uint16_t active_port;
			uint16_t fresh_port;

			err = sdp_media_status(answer, "audio", &active_port,
					       &(bool){false});
			TEST_ERR(err);
			err = sdp_media_status(answer, "video", &fresh_port,
					       &(bool){false});
			TEST_ERR(err);
			ASSERT_TRUE(fresh_port != 0);
			ASSERT_TRUE(fresh_port != active_port);
			description.type = SDP_ROLLBACK;
			description.sdp = NULL;
			err = peerconnection_set_remote_descr(fix->a->pc,
							    &description);
			TEST_ERR(err);
		}
		else {
			description.type = SDP_ANSWER;
			description.sdp = answer;
			err = peerconnection_set_remote_descr(fix->a->pc,
							    &description);
			TEST_ERR(err);
		}
	}

	err = verify_continuity_id(fix, channel_id);

out:
	mem_deref(answer);
	mem_deref(offer);
	return err;
}


struct default_candidate_check {
	const struct sa *default_addr;
	int err;
	bool found;
};


static bool default_candidate_handler(const char *name, const char *value,
				      void *arg)
{
	struct default_candidate_check *check = arg;
	struct ice_cand_attr candidate;
	(void)name;

	check->err = ice_cand_attr_decode(&candidate, value);
	if (check->err)
		return true;
	if (candidate.compid == 1 &&
	    sa_cmp(&candidate.addr, check->default_addr, SA_ALL)) {
		check->found = true;
		return true;
	}

	return false;
}


static int verify_application_default_candidate(struct mbuf *description)
{
	struct default_candidate_check check = {0};
	struct sdp_session *sdp = NULL;
	struct mbuf *copy = NULL;
	struct sdp_media *application = NULL;
	struct sa local;
	struct le *le;
	int err;

	if (!description)
		return EINVAL;

	sa_set_str(&local, "127.0.0.1", 0);
	err = sdp_session_alloc(&sdp, &local);
	if (err)
		goto out;
	copy = mbuf_dup(description);
	if (!copy) {
		err = ENOMEM;
		goto out;
	}
	copy->pos = 0;
	err = sdp_decode(sdp, copy, true);
	if (err)
		goto out;

	for (le = list_head(sdp_session_medial(sdp, false));
	     le; le = le->next) {
		struct sdp_media *media = le->data;

		if (!str_cmp(sdp_media_name(media), "application")) {
			application = media;
			break;
		}
	}
	if (!application) {
		err = ENOENT;
		goto out;
	}

	check.default_addr = sdp_media_raddr(application);
	(void)sdp_media_rattr_apply(application, "candidate",
				    default_candidate_handler, &check);
	err = check.err ? check.err : check.found ? 0 : EPROTO;

out:
	mem_deref(copy);
	mem_deref(sdp);
	return err;
}
#endif


static int agent_alloc(struct agent **agp, struct fixture *fix,
		       const char *name,
		       const struct mnat *mnat, const struct menc *menc,
		       bool use_audio, bool use_video,
#ifdef USE_DATACHANNEL
		       bool use_data,
		       bool negotiated_data,
		       enum data_setup_order data_order,
#endif
		       bool offerer)
{
	struct rtc_configuration config = {
		.offerer = offerer
	};

	struct agent *ag = mem_zalloc(sizeof(*ag), destructor);
	if (!ag)
		return ENOMEM;

	ag->magic = MAGIC_AGENT;
	ag->fix = fix;
	ag->name = name;
	ag->use_audio = use_audio;
	ag->use_video = use_video;
#ifdef USE_DATACHANNEL
	ag->use_data = use_data;
	ag->negotiated_data = negotiated_data;
#endif

	int err = peerconnection_new(&ag->pc, &config, mnat, menc,
				     peerconnection_gather_handler,
				     peerconnection_estab_handler,
				     peerconnection_close_handler, ag);
	TEST_ERR(err);

#ifdef USE_DATACHANNEL
	if (use_data && data_order == DATA_BEFORE_MEDIA) {
		err = agent_add_data(ag, offerer);
		TEST_ERR(err);
	}
#endif
	if (use_audio) {
		err = peerconnection_add_audio_track(ag->pc, conf_config(),
						     baresip_aucodecl(),
						     SDP_SENDRECV);
		TEST_ERR(err);
	}

#ifdef USE_DATACHANNEL
	if (use_data && data_order == DATA_BETWEEN_MEDIA) {
		err = agent_add_data(ag, offerer);
		TEST_ERR(err);
	}
#endif
	if (use_video) {
		err = peerconnection_add_video_track(ag->pc, conf_config(),
					     baresip_vidcodecl(),
					     SDP_SENDRECV);
		TEST_ERR(err);
	}
#ifdef USE_DATACHANNEL
	if (use_data && data_order == DATA_AFTER_MEDIA) {
		err = agent_add_data(ag, offerer);
		TEST_ERR(err);
	}
#endif

 out:
	if (err)
		mem_deref(ag);
	else
		*agp = ag;

	return err;
}


#ifdef USE_DATACHANNEL
struct data_expectation {
	enum data_channel_message_type type;
	const uint8_t *data;
	size_t len;
};

struct data_transcript {
	const struct data_expectation *items;
	size_t count;
	size_t index;
	int err;
};

struct captured_channel {
	struct data_channel *dc;
	int err;
};


static int wait_until(bool (*ready)(void *arg), void *arg,
		      uint64_t timeout_ms)
{
	const uint64_t deadline = tmr_jiffies() + timeout_ms;

	while (!ready(arg)) {
		const uint64_t now = tmr_jiffies();
		int err;

		if (now >= deadline)
			return ETIMEDOUT;
		err = re_main_timeout((uint32_t)(deadline - now));
		if (err && err != ETIMEDOUT)
			return err;
	}

	return 0;
}


static bool transcript_complete(void *arg)
{
	const struct data_transcript *transcript = arg;

	return transcript->err || transcript->index == transcript->count;
}


static void transcript_handler(struct data_channel *dc,
			       enum data_channel_message_type type,
			       const uint8_t *buf, size_t len, void *arg)
{
	struct data_transcript *transcript = arg;
	const struct data_expectation *expected;

	(void)dc;
	if (transcript->index >= transcript->count) {
		transcript->err = EOVERFLOW;
		return;
	}
	expected = &transcript->items[transcript->index];
	if (type != expected->type || len != expected->len ||
	    (len && memcmp(buf, expected->data, len))) {
		transcript->err = EBADMSG;
		re_cancel();
		return;
	}
	++transcript->index;
	re_cancel();
}


static void captured_state_handler(struct data_channel *dc,
				   enum data_channel_state state,
				   int err, void *arg)
{
	struct captured_channel *captured = arg;

	(void)dc;
	(void)state;
	if (err && !captured->err)
		captured->err = err;
	re_cancel();
}


static void capture_channel_handler(struct data_channel *dc, void *arg)
{
	struct captured_channel *captured = arg;

	if (captured->dc) {
		captured->err = EALREADY;
		(void)datachannel_close(dc);
		return;
	}
	captured->dc = mem_ref(dc);
	captured->err = datachannel_set_handlers(
		dc, NULL, captured_state_handler, NULL, captured);
	re_cancel();
}


struct channel_pair_wait {
	struct captured_channel *local;
	struct captured_channel *remote;
};


static bool channel_pair_open(void *arg)
{
	const struct channel_pair_wait *wait = arg;

	if (wait->local->err || wait->remote->err)
		return true;
	return wait->local->dc && wait->remote->dc &&
		datachannel_state(wait->local->dc) == DATACHANNEL_OPEN &&
		datachannel_state(wait->remote->dc) == DATACHANNEL_OPEN;
}


static bool channel_pair_closed(void *arg)
{
	const struct channel_pair_wait *wait = arg;

	if (wait->local->err || wait->remote->err)
		return true;
	return wait->local->dc && wait->remote->dc &&
		datachannel_state(wait->local->dc) == DATACHANNEL_CLOSED &&
		datachannel_state(wait->remote->dc) == DATACHANNEL_CLOSED;
}


static int runtime_fixture_alloc(struct fixture *fix, const char *prefix,
				 const struct mnat *mnat,
				 const struct menc *menc)
{
	int err;

	err = mqueue_alloc(&fix->mq, mqueue_handler, fix);
	if (err)
		return err;
	err = agent_alloc(&fix->a, fix, prefix, mnat, menc,
			  false, false, true, false, DATA_AFTER_MEDIA, true);
	if (err)
		return err;
	err = agent_alloc(&fix->b, fix, prefix, mnat, menc,
			  false, false, true, false, DATA_AFTER_MEDIA, false);
	return err;
}


static void runtime_fixture_close(struct fixture *fix)
{
	fix->terminated = true;
	fix->b = mem_deref(fix->b);
	fix->a = mem_deref(fix->a);
	fix->mq = mem_deref(fix->mq);
}


static bool runtime_fixtures_ready(void *arg)
{
	struct fixture *fixtures = arg;

	return fixtures[0].terminated && fixtures[1].terminated;
}


static int send_transcript(struct data_channel *sender,
			   struct data_channel *receiver,
			   const struct data_expectation *items,
			   size_t count)
{
	struct data_transcript transcript = {
		.items = items,
		.count = count,
	};
	int err;

	err = datachannel_set_handlers(
		receiver, transcript_handler, NULL, NULL, &transcript);
	if (err)
		return err;
	for (size_t i = 0; i < count; ++i) {
		err = datachannel_send(sender, items[i].type,
				       items[i].data, items[i].len);
		if (err)
			return err;
	}
	err = wait_until(transcript_complete, &transcript, 5000);
	if (err)
		return err;
	return transcript.err;
}


static int open_channel_pair(struct peer_connection *owner,
			     struct peer_connection *peer,
			     const char *label,
			     const struct data_channel_config *config,
			     struct captured_channel *local,
			     struct captured_channel *remote)
{
	struct channel_pair_wait wait = {
		.local = local,
		.remote = remote,
	};
	int err;

	err = peerconnection_set_datachannel_handler(
		peer, capture_channel_handler, remote);
	if (err)
		return err;
	err = peerconnection_create_datachannel(
		owner, label, config, &local->dc);
	if (err)
		return err;
	err = datachannel_set_handlers(
		local->dc, NULL, captured_state_handler, NULL, local);
	if (err)
		return err;
	err = wait_until(channel_pair_open, &wait, 5000);
	if (err)
		return err;
	return local->err ? local->err : remote->err;
}


static int close_channel_pair(struct captured_channel *local,
			      struct captured_channel *remote)
{
	struct channel_pair_wait wait = {
		.local = local,
		.remote = remote,
	};
	int err;

	err = datachannel_set_handlers(
		local->dc, NULL, captured_state_handler, NULL, local);
	if (err)
		return err;
	err = datachannel_set_handlers(
		remote->dc, NULL, captured_state_handler, NULL, remote);
	if (err)
		return err;
	err = datachannel_close(local->dc);
	if (err)
		return err;
	err = wait_until(channel_pair_closed, &wait, 5000);
	if (err)
		return err;
	if (local->err || remote->err)
		return local->err ? local->err : remote->err;
	local->dc = mem_deref(local->dc);
	remote->dc = mem_deref(remote->dc);
	return 0;
}


static int test_public_data_transcript(struct fixture *fix)
{
	static const size_t sizes[] = {
		0, 1, 256, 1199, 1200, 1201, 4095, 4096, 4097,
		8191, 8192, 8193, 16383, 16384,
	};
	uint8_t *payload = NULL;
	int err = 0;

	payload = mem_alloc(16385, NULL);
	ASSERT_TRUE(payload != NULL);
	for (size_t i = 0; i < 16385; ++i)
		payload[i] = (uint8_t)('a' + i % 23);

	for (size_t i = 0; i < RE_ARRAY_SIZE(sizes); ++i) {
		const struct data_expectation expected = {
			.type = i & 1 ? DATACHANNEL_MESSAGE_TEXT
				      : DATACHANNEL_MESSAGE_BINARY,
			.data = payload,
			.len = sizes[i],
		};
		struct data_channel *sender = i & 1
			? fix->b->dc : fix->a->dc;
		struct data_channel *receiver = i & 1
			? fix->a->dc : fix->b->dc;

		err = send_transcript(sender, receiver, &expected, 1);
		TEST_ERR(err);
	}

	{
		uint8_t sequence[12][2];
		struct data_expectation expected[12];

		for (size_t i = 0; i < RE_ARRAY_SIZE(expected); ++i) {
			sequence[i][0] = (uint8_t)('A' + i);
			sequence[i][1] = (uint8_t)('a' + i);
			expected[i].type = i & 1
				? DATACHANNEL_MESSAGE_TEXT
				: DATACHANNEL_MESSAGE_BINARY;
			expected[i].data = sequence[i];
			expected[i].len = sizeof(sequence[i]);
		}
		err = send_transcript(fix->a->dc, fix->b->dc,
				      expected, RE_ARRAY_SIZE(expected));
		TEST_ERR(err);
	}

	err = datachannel_send(fix->a->dc, DATACHANNEL_MESSAGE_BINARY,
			       payload, 16385);
	ASSERT_EQ(EMSGSIZE, err);
	err = 0;

out:
	mem_deref(payload);
	return err;
}


static int test_public_data_configs(struct fixture *fix)
{
	static const struct data_channel_config configs[] = {
		{
			.ordered = false,
			.max_retransmits = -1,
			.max_packet_lifetime = -1,
			.protocol = "unordered",
		},
		{
			.ordered = false,
			.max_retransmits = 2,
			.max_packet_lifetime = -1,
			.protocol = "retransmit",
		},
		{
			.ordered = false,
			.max_retransmits = -1,
			.max_packet_lifetime = 100,
			.protocol = "lifetime",
		},
	};
	const struct data_channel_config invalid_both = {
		.ordered = true,
		.max_retransmits = 1,
		.max_packet_lifetime = 1,
	};
	const struct data_channel_config invalid_negative = {
		.ordered = true,
		.max_retransmits = -2,
		.max_packet_lifetime = -1,
	};
	struct data_channel *invalid = NULL;
	int err = 0;

	ASSERT_EQ(EINVAL, peerconnection_create_datachannel(
		fix->a->pc, "invalid-both", &invalid_both, &invalid));
	ASSERT_EQ(EINVAL, peerconnection_create_datachannel(
		fix->a->pc, "invalid-negative", &invalid_negative, &invalid));

	for (size_t i = 0; i < RE_ARRAY_SIZE(configs); ++i) {
		struct captured_channel local = {0};
		struct captured_channel remote = {0};
		uint8_t payload[] = {0x40, (uint8_t)i, 0x80};
		const struct data_expectation expected = {
			.type = DATACHANNEL_MESSAGE_BINARY,
			.data = payload,
			.len = sizeof(payload),
		};
		char label[32];

		re_snprintf(label, sizeof(label), "policy-%zu", i);
		err = open_channel_pair(fix->a->pc, fix->b->pc, label,
					&configs[i], &local, &remote);
		TEST_ERR(err);
		ASSERT_STREQ(configs[i].protocol,
			     datachannel_protocol(remote.dc));
		err = send_transcript(local.dc, remote.dc, &expected, 1);
		TEST_ERR(err);
		err = close_channel_pair(&local, &remote);
		TEST_ERR(err);
	}

out:
	mem_deref(invalid);
	return err;
}


static int test_simultaneous_data_open(struct fixture *fix)
{
	struct captured_channel a_local = {0};
	struct captured_channel a_remote = {0};
	struct captured_channel b_local = {0};
	struct captured_channel b_remote = {0};
	struct channel_pair_wait a_wait = {
		.local = &a_local,
		.remote = &b_remote,
	};
	struct channel_pair_wait b_wait = {
		.local = &b_local,
		.remote = &a_remote,
	};
	const uint8_t from_a[] = {0xa0, 0x01};
	const uint8_t from_b[] = {0xb0, 0x02};
	const struct data_expectation expect_a = {
		DATACHANNEL_MESSAGE_BINARY, from_a, sizeof(from_a),
	};
	const struct data_expectation expect_b = {
		DATACHANNEL_MESSAGE_BINARY, from_b, sizeof(from_b),
	};
	int err;

	err = peerconnection_set_datachannel_handler(
		fix->a->pc, capture_channel_handler, &a_remote);
	TEST_ERR(err);
	err = peerconnection_set_datachannel_handler(
		fix->b->pc, capture_channel_handler, &b_remote);
	TEST_ERR(err);
	err = peerconnection_create_datachannel(
		fix->a->pc, "simultaneous-a", NULL, &a_local.dc);
	TEST_ERR(err);
	err = datachannel_set_handlers(
		a_local.dc, NULL, captured_state_handler, NULL, &a_local);
	TEST_ERR(err);
	err = peerconnection_create_datachannel(
		fix->b->pc, "simultaneous-b", NULL, &b_local.dc);
	TEST_ERR(err);
	err = datachannel_set_handlers(
		b_local.dc, NULL, captured_state_handler, NULL, &b_local);
	TEST_ERR(err);
	err = wait_until(channel_pair_open, &a_wait, 5000);
	TEST_ERR(err);
	err = wait_until(channel_pair_open, &b_wait, 5000);
	TEST_ERR(err);
	TEST_ERR(a_local.err || a_remote.err || b_local.err || b_remote.err);
	ASSERT_TRUE((datachannel_id(a_local.dc) & 1) !=
		    (datachannel_id(b_local.dc) & 1));
	ASSERT_EQ(datachannel_id(a_local.dc), datachannel_id(b_remote.dc));
	ASSERT_EQ(datachannel_id(b_local.dc), datachannel_id(a_remote.dc));
	err = send_transcript(a_local.dc, b_remote.dc, &expect_a, 1);
	TEST_ERR(err);
	err = send_transcript(b_local.dc, a_remote.dc, &expect_b, 1);
	TEST_ERR(err);
	err = close_channel_pair(&a_local, &b_remote);
	TEST_ERR(err);
	err = close_channel_pair(&b_local, &a_remote);
	TEST_ERR(err);

out:
	mem_deref(a_local.dc);
	mem_deref(a_remote.dc);
	mem_deref(b_local.dc);
	mem_deref(b_remote.dc);
	return err;
}


static int test_data_lifecycle(struct fixture *fix)
{
	int reusable_id = -1;
	int err = 0;

	for (unsigned cycle = 0; cycle < 8; ++cycle) {
		struct peer_connection *owner = cycle & 1
			? fix->b->pc : fix->a->pc;
		struct peer_connection *peer = cycle & 1
			? fix->a->pc : fix->b->pc;
		struct captured_channel local = {0};
		struct captured_channel remote = {0};
		uint8_t payload[] = {0xc0, (uint8_t)cycle};
		const struct data_expectation expected = {
			DATACHANNEL_MESSAGE_BINARY, payload, sizeof(payload),
		};
		char label[32];

		re_snprintf(label, sizeof(label), "lifecycle-%u", cycle);
		err = open_channel_pair(owner, peer, label, NULL,
					&local, &remote);
		TEST_ERR(err);
		ASSERT_TRUE(datachannel_id(local.dc) >= 0);
		ASSERT_EQ(datachannel_id(local.dc), datachannel_id(remote.dc));
		if (!cycle)
			reusable_id = datachannel_id(local.dc);
		err = send_transcript(local.dc, remote.dc, &expected, 1);
		TEST_ERR(err);
		err = close_channel_pair(&local, &remote);
		TEST_ERR(err);
	}

	/* Automatic allocation may prefer fresh SIDs while capacity remains.
	 * Prove that bilateral reset nevertheless releases the old identifier
	 * by explicitly reopening the first SID as a negotiated channel. */
	{
		struct data_channel_config config = {
			.ordered = true,
			.max_retransmits = -1,
			.max_packet_lifetime = -1,
			.protocol = "reused",
			.negotiated = true,
			.id = (uint16_t)reusable_id,
		};
		struct captured_channel a = {0};
		struct captured_channel b = {0};
		struct channel_pair_wait wait = {
			.local = &a,
			.remote = &b,
		};
		const uint8_t payload[] = {0xee, 0x01};
		const struct data_expectation expected = {
			DATACHANNEL_MESSAGE_BINARY, payload, sizeof(payload),
		};

		err = peerconnection_create_datachannel(
			fix->a->pc, "reused", &config, &a.dc);
		TEST_ERR(err);
		err = datachannel_set_handlers(
			a.dc, NULL, captured_state_handler, NULL, &a);
		TEST_ERR(err);
		err = peerconnection_create_datachannel(
			fix->b->pc, "reused", &config, &b.dc);
		TEST_ERR(err);
		err = datachannel_set_handlers(
			b.dc, NULL, captured_state_handler, NULL, &b);
		TEST_ERR(err);
		err = wait_until(channel_pair_open, &wait, 5000);
		TEST_ERR(err);
		err = send_transcript(a.dc, b.dc, &expected, 1);
		TEST_ERR(err);
		err = close_channel_pair(&a, &b);
		TEST_ERR(err);
	}

out:
	return err;
}


static int test_peerconn_data_runtime_impl(const struct mnat *mnat,
					   const struct menc *menc)
{
	struct fixture fixtures[2] = {0};
	const uint8_t recovery_payload[] = {0xde, 0xad, 0xbe, 0xef};
	const struct data_expectation recovery = {
		DATACHANNEL_MESSAGE_BINARY,
		recovery_payload,
		sizeof(recovery_payload),
	};
	int err;

	err = runtime_fixture_alloc(&fixtures[0], "runtime-0", mnat, menc);
	TEST_ERR(err);
	err = runtime_fixture_alloc(&fixtures[1], "runtime-1", mnat, menc);
	TEST_ERR(err);
	err = wait_until(runtime_fixtures_ready, fixtures, 15000);
	TEST_ERR(err);
	TEST_ERR(fixtures[0].a->err);
	TEST_ERR(fixtures[0].b->err);
	TEST_ERR(fixtures[1].a->err);
	TEST_ERR(fixtures[1].b->err);
	ASSERT_EQ(DATACHANNEL_OPEN, datachannel_state(fixtures[0].a->dc));
	ASSERT_EQ(DATACHANNEL_OPEN, datachannel_state(fixtures[1].a->dc));

	err = test_public_data_transcript(&fixtures[0]);
	TEST_ERR(err);
	err = test_data_lifecycle(&fixtures[0]);
	TEST_ERR(err);
	err = test_public_data_configs(&fixtures[0]);
	TEST_ERR(err);
	err = test_simultaneous_data_open(&fixtures[0]);
	TEST_ERR(err);

	/* Keep a second process-global usrsctp association active while the
	 * first peer pair is destroyed.  Its transcript must remain isolated
	 * and usable. */
	err = send_transcript(fixtures[1].a->dc, fixtures[1].b->dc,
			      &recovery, 1);
	TEST_ERR(err);
	runtime_fixture_close(&fixtures[0]);
	err = send_transcript(fixtures[1].b->dc, fixtures[1].a->dc,
			      &recovery, 1);
	TEST_ERR(err);

out:
	runtime_fixture_close(&fixtures[1]);
	runtime_fixture_close(&fixtures[0]);
	return err;
}
#endif


#ifdef USE_DATACHANNEL
static int test_data_callback_destruction(const struct mnat *mnat,
					  const struct menc *menc,
					  bool negotiated)
{
	struct fixture fix = {0};
	int err;

	err = mqueue_alloc(&fix.mq, mqueue_handler, &fix);
	TEST_ERR(err);
	err = agent_alloc(&fix.a, &fix, "destroy-A", mnat, menc,
			  false, false, true, negotiated,
			  DATA_AFTER_MEDIA, true);
	TEST_ERR(err);
	err = agent_alloc(&fix.b, &fix, "destroy-B", mnat, menc,
			  false, false, true, negotiated,
			  DATA_AFTER_MEDIA, false);
	TEST_ERR(err);
	fix.b->destroy_on_channel = true;

	err = re_main_timeout(10000);
	TEST_ERR(err);
	ASSERT_TRUE(fix.b->pc == NULL);

out:
	fix.terminated = true;
	fix.b = mem_deref(fix.b);
	fix.a = mem_deref(fix.a);
	mem_deref(fix.mq);
	return err;
}


enum deferred_receive_mode {
	DEFERRED_RECEIVE_BUDGET,
	DEFERRED_RECEIVE_ROLLBACK,
	DEFERRED_RECEIVE_DESTRUCTION,
	DEFERRED_RECEIVE_MESSAGE_DESTRUCTION,
	DEFERRED_RECEIVE_CLOSED_DESTRUCTION,
	DEFERRED_RECEIVE_ERROR_DESTRUCTION,
};


static int send_deferred_payload(struct data_channel *dc,
				 const uint8_t *payload, size_t len)
{
	uint64_t deadline = tmr_jiffies() + 2000;
	int err;

	do {
		err = datachannel_send(dc, DATACHANNEL_MESSAGE_BINARY,
				       payload, len);
		if (err != EAGAIN)
			return err;
		err = re_main_timeout(10);
		if (err == ETIMEDOUT)
			err = 0;
	} while (!err && tmr_jiffies() < deadline);

	return err ? err : ETIMEDOUT;
}


static int test_deferred_receive_queue(const struct mnat *mnat,
				       const struct menc *menc,
				       enum deferred_receive_mode mode)
{
	static const uint8_t payload[16384];
	struct deferred_receive_check check = {0};
	struct session_description description;
	struct fixture fix = {0};
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	struct data_channel *staged = NULL;
	unsigned count = mode == DEFERRED_RECEIVE_MESSAGE_DESTRUCTION ? 2 : 15;
	int err;

	err = mqueue_alloc(&fix.mq, mqueue_handler, &fix);
	TEST_ERR(err);
	err = agent_alloc(&fix.a, &fix, "deferred-A", mnat, menc,
			  false, false, true, false, DATA_AFTER_MEDIA, true);
	TEST_ERR(err);
	err = agent_alloc(&fix.b, &fix, "deferred-B", mnat, menc,
			  false, false, true, false, DATA_AFTER_MEDIA, false);
	TEST_ERR(err);
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.a->err);
	TEST_ERR(fix.b->err);
	ASSERT_TRUE(fix.a->dc != NULL);
	ASSERT_TRUE(fix.b->dc != NULL);
	ASSERT_EQ(DATACHANNEL_OPEN, datachannel_state(fix.a->dc));
	ASSERT_EQ(DATACHANNEL_OPEN, datachannel_state(fix.b->dc));

	fix.terminated = false;
	check.agent = fix.a;
	check.destroy_on_first_message =
		mode == DEFERRED_RECEIVE_MESSAGE_DESTRUCTION;
	check.destroy_on_closed =
		mode == DEFERRED_RECEIVE_CLOSED_DESTRUCTION;
	fix.a->destroy_on_pc_close =
		mode == DEFERRED_RECEIVE_ERROR_DESTRUCTION;
	err = datachannel_set_handlers(
		fix.a->dc, deferred_receive_message_handler,
		deferred_receive_state_handler, NULL, &check);
	TEST_ERR(err);
	if (mode == DEFERRED_RECEIVE_CLOSED_DESTRUCTION ||
	    mode == DEFERRED_RECEIVE_ERROR_DESTRUCTION) {
		const struct data_channel_config config = {
			.ordered = true,
			.max_retransmits = -1,
			.max_packet_lifetime = -1,
			.protocol = "staged",
			.negotiated = true,
			.id = 7,
		};

		err = peerconnection_set_datachannel_handler(
			fix.a->pc, deferred_receive_incoming_handler, &check);
		TEST_ERR(err);
		err = peerconnection_create_datachannel(
			fix.b->pc, "staged", &config, &staged);
		TEST_ERR(err);
	}
	err = peerconnection_create_offer(fix.b->pc, &offer);
	TEST_ERR(err);
	if (mode == DEFERRED_RECEIVE_CLOSED_DESTRUCTION ||
	    mode == DEFERRED_RECEIVE_ERROR_DESTRUCTION)
		ASSERT_TRUE(mbuf_contains(offer, "a=dcmap:7 "));
	description.type = SDP_OFFER;
	description.sdp = offer;
	err = peerconnection_set_remote_descr(fix.a->pc, &description);
	TEST_ERR(err);
	ASSERT_EQ(SS_HAVE_REMOTE_OFFER,
		  peerconnection_signaling(fix.a->pc));

	for (unsigned i = 0; i < count; ++i) {
		err = send_deferred_payload(fix.b->dc, payload,
					    sizeof(payload));
		TEST_ERR(err);
		ASSERT_EQ(0, (int)check.messages);
		ASSERT_EQ(0, (int)check.closed);
	}
	err = re_main_timeout(1000);
	if (err == ETIMEDOUT)
		err = 0;
	TEST_ERR(err);
	ASSERT_EQ(0, (int)check.messages);
	ASSERT_EQ(0, (int)check.closed);
	if (mode == DEFERRED_RECEIVE_ROLLBACK ||
	    mode == DEFERRED_RECEIVE_MESSAGE_DESTRUCTION) {
		description.type = SDP_ROLLBACK;
		description.sdp = NULL;
		err = peerconnection_set_remote_descr(fix.a->pc, &description);
		TEST_ERR(err);
		if (fix.b->pc) {
			err = peerconnection_set_remote_descr(
				fix.b->pc, &description);
			TEST_ERR(err);
		}
		if (mode == DEFERRED_RECEIVE_MESSAGE_DESTRUCTION) {
			ASSERT_EQ(1, (int)check.messages);
			ASSERT_TRUE(fix.a->pc == NULL);
		}
		else {
			ASSERT_EQ(SS_STABLE,
				  peerconnection_signaling(fix.a->pc));
			ASSERT_EQ(15, (int)check.messages);
		}
		ASSERT_EQ(0, (int)check.closed);
		goto out;
	}

	if (mode == DEFERRED_RECEIVE_DESTRUCTION) {
		fix.a->pc = mem_deref(fix.a->pc);
		fix.a->dc = NULL;
		ASSERT_EQ(0, (int)check.messages);
		ASSERT_EQ(0, (int)check.closed);
		goto out;
	}

	if (mode == DEFERRED_RECEIVE_BUDGET ||
	    mode == DEFERRED_RECEIVE_CLOSED_DESTRUCTION ||
	    mode == DEFERRED_RECEIVE_ERROR_DESTRUCTION) {
		/* Fifteen charged 16 KiB messages fit.  The sixteenth exceeds
		 * the fixed 256 KiB aggregate payload-plus-metadata budget. */
		err = send_deferred_payload(fix.b->dc, payload,
					    sizeof(payload));
		TEST_ERR(err);
		err = re_main_timeout(50);
		if (err == ETIMEDOUT)
			err = 0;
		TEST_ERR(err);
		ASSERT_EQ(0, (int)check.messages);
		ASSERT_EQ(0, (int)check.closed);
	}

	err = peerconnection_create_answer(fix.a->pc, &answer);
	TEST_ERR(err);
	if (fix.a->pc)
		ASSERT_EQ(SS_STABLE, peerconnection_signaling(fix.a->pc));
	ASSERT_EQ(0, (int)check.messages);
	ASSERT_EQ(1, (int)check.closed);
	ASSERT_EQ(ENOBUFS, check.close_err);
	ASSERT_EQ(0, (int)check.incoming);
	if (mode == DEFERRED_RECEIVE_CLOSED_DESTRUCTION ||
	    mode == DEFERRED_RECEIVE_ERROR_DESTRUCTION)
		ASSERT_TRUE(fix.a->pc == NULL);

	description.type = SDP_ANSWER;
	description.sdp = answer;
	err = peerconnection_set_remote_descr(fix.b->pc, &description);
	TEST_ERR(err);

out:
	mem_threshold_set(-1);
	fix.terminated = true;
	mem_deref(answer);
	mem_deref(offer);
	mem_deref(staged);
	fix.b = mem_deref(fix.b);
	fix.a = mem_deref(fix.a);
	mem_deref(fix.mq);
	return err;
}


#endif


/* NOTE: called from main-thread or worker-threads */
static void auframe_handler(struct auframe *af, const char *dev, void *arg)
{
	struct fixture *fix = arg;
	(void)af;
	(void)dev;

	if (fix->terminated)
		return;

	struct agent *ag = fix->b;

	if (!ag)
		return;

	struct audio *au = media_get_audio(ag->media);

	/* Does auframe come from the decoder ? */
	if (!audio_rxaubuf_started(au)) {
		debug("test: [ %s ] no audio received from decoder yet\n",
		      ag->name);
		return;
	}

	ag->got_audio = true;

	if (agents_are_complete(ag)) {
		mqueue_push(fix->mq, 0, NULL);
	}
}


static void mock_vidisp_handler(const struct vidframe *frame,
				uint64_t timestamp, const char *title,
				void *arg)
{
	struct fixture *fix = arg;
	struct agent *ag = fix->b;
	(void)frame;
	(void)timestamp;
	(void)title;

	ag->got_video = true;

	if (agents_are_complete(ag)) {
		mqueue_push(fix->mq, 0, NULL);
	}
}


static int test_peerconn_param(bool use_audio, bool use_aufilt, bool use_video
#ifdef USE_DATACHANNEL
			       , bool use_data, bool negotiated_data,
			       bool bundle_only_data,
			       bool destroy_on_media_estab,
			       bool application_first_bundle,
			       bool restart_media_only,
			       enum data_setup_order data_order
#endif
			       )
{
	const char *aufilters[] = {
		"auconv",
		"auresamp",
		"mixausrc",
	};
	struct fixture fix = {0};
	struct auplay *auplay = NULL;
	struct vidisp *vidisp = NULL;
	struct mbuf *reoffer = NULL;
	struct mbuf *reanswer = NULL;
	struct mbuf *reuse_offer = NULL;
	struct mbuf *reuse_answer = NULL;
	struct mbuf *fingerprint_offer = NULL;
	struct mbuf *fingerprint_answer = NULL;
	struct mbuf *replacement_offer = NULL;
	struct mbuf *replacement_answer = NULL;
	struct mbuf *role_offer = NULL;
	struct mbuf *role_answer = NULL;
	struct mbuf *topology_offer = NULL;
	struct mbuf *topology_answer = NULL;
	struct mbuf *late_offer = NULL;
	struct mbuf *late_answer = NULL;
	int err = 0;

#ifdef USE_DATACHANNEL
	fix.bundle_only_data = bundle_only_data;
	fix.application_first_bundle = application_first_bundle;
#endif
	if (use_audio) {
		err = mock_auplay_register(&auplay, baresip_auplayl(),
					   auframe_handler, &fix);
		TEST_ERR(err);

		err = module_load(".", "g711");
		TEST_ERR(err);
		err = module_load(".", "ausine");
		TEST_ERR(err);

		if (use_aufilt) {
			for (size_t i=0; i<RE_ARRAY_SIZE(aufilters); i++) {
				const char *name = aufilters[i];

				err = module_load(".", name);
				TEST_ERR(err);

				aufilt_enable(baresip_aufiltl(), name, true);
			}
		}
	}

	if (use_video) {
		/* NOTE: must be loaded before 'fakevideo' */
		err = mock_vidisp_register(&vidisp, mock_vidisp_handler, &fix);
		TEST_ERR(err);

		mock_vidcodec_register();

		err = module_load(".", "fakevideo");
		TEST_ERR(err);
	}

	const struct mnat *mnat = mnat_find(baresip_mnatl(), "ice");
	ASSERT_TRUE(mnat != NULL);

	const struct menc *menc = menc_find(baresip_mencl(), "dtls_srtp");
	ASSERT_TRUE(menc != NULL);
#ifdef USE_DATACHANNEL
	ASSERT_TRUE(menc->transportpromoteh != NULL);
#endif

	err = peerconn_test_menc_transport(menc, false);
	TEST_ERR(err);

	err = mqueue_alloc(&fix.mq, mqueue_handler, &fix);
	TEST_ERR(err);

	err = agent_alloc(&fix.a, &fix, "A", mnat, menc,
			  use_audio, use_video,
#ifdef USE_DATACHANNEL
			  use_data,
			  negotiated_data,
			  data_order,
#endif
			  true);
	TEST_ERR(err);

	err = agent_alloc(&fix.b, &fix, "B", mnat, menc,
			  use_audio, use_video,
#ifdef USE_DATACHANNEL
			  use_data,
			  negotiated_data,
			  data_order,
#endif
			  false);
	TEST_ERR(err);
#ifdef USE_DATACHANNEL
	fix.b->destroy_on_media_estab = destroy_on_media_estab;
#endif

	err = re_main_timeout(10000);
	TEST_ERR(err);

	fix.terminated = true;

#ifdef USE_DATACHANNEL
	if (destroy_on_media_estab) {
		ASSERT_TRUE(fix.b->pc == NULL);
		goto out;
	}
#endif

	err = fix.a->err;
	TEST_ERR(err);
	err = fix.b->err;
	TEST_ERR(err);

	ASSERT_TRUE(fix.a->got_sdp);
	ASSERT_TRUE(fix.b->got_sdp);

	if (use_audio) {
		ASSERT_TRUE(fix.a->got_estab_audio);
		ASSERT_TRUE(fix.b->got_estab_audio);
		ASSERT_EQ(1, (int)fix.a->estab_audio_count);
		ASSERT_EQ(1, (int)fix.b->estab_audio_count);
		ASSERT_TRUE(fix.a->got_audio || fix.b->got_audio);
	}
	if (use_video) {
		ASSERT_TRUE(fix.a->got_estab_video);
		ASSERT_TRUE(fix.b->got_estab_video);
		ASSERT_EQ(1, (int)fix.a->estab_video_count);
		ASSERT_EQ(1, (int)fix.b->estab_video_count);
		ASSERT_TRUE(fix.a->got_video || fix.b->got_video);
	}
#ifdef USE_DATACHANNEL
	if (restart_media_only) {
		struct session_description description;
		char stable_ufrag[256];
		char restart_ufrag[256];

		ASSERT_TRUE(!use_data && (use_audio || use_video));
		err = peerconnection_create_offer(fix.a->pc, &reoffer);
		TEST_ERR(err);
		err = use_video
			? sdp_video_attr_value(reoffer, "ice-ufrag", stable_ufrag,
					       sizeof(stable_ufrag))
			: sdp_audio_attr_value(reoffer, "ice-ufrag", stable_ufrag,
					       sizeof(stable_ufrag));
		TEST_ERR(err);
		description.type = SDP_ROLLBACK;
		description.sdp = NULL;
		err = peerconnection_set_remote_descr(fix.a->pc, &description);
		TEST_ERR(err);
		reoffer = mem_deref(reoffer);

		err = peerconnection_restart_ice(fix.a->pc);
		TEST_ERR(err);
		err = peerconnection_create_offer(fix.a->pc, &reoffer);
		TEST_ERR(err);
		err = use_video
			? sdp_video_attr_value(reoffer, "ice-ufrag", restart_ufrag,
					       sizeof(restart_ufrag))
			: sdp_audio_attr_value(reoffer, "ice-ufrag", restart_ufrag,
					       sizeof(restart_ufrag));
		TEST_ERR(err);
		ASSERT_TRUE(str_cmp(stable_ufrag, restart_ufrag));

		description.type = SDP_OFFER;
		description.sdp = reoffer;
		err = peerconnection_set_remote_descr(fix.b->pc, &description);
		TEST_ERR(err);
		err = peerconnection_create_answer(fix.b->pc, &reanswer);
		TEST_ERR(err);
		description.type = SDP_ANSWER;
		description.sdp = reanswer;
		err = peerconnection_set_remote_descr(fix.a->pc, &description);
		TEST_ERR(err);

		for (unsigned i = 0; i < 2; ++i) {
			err = re_main_timeout(10);
			if (err == ETIMEDOUT)
				err = 0;
			TEST_ERR(err);
		}
		ASSERT_EQ(SS_STABLE, peerconnection_signaling(fix.a->pc));
		ASSERT_EQ(SS_STABLE, peerconnection_signaling(fix.b->pc));
		err = fix.a->err;
		TEST_ERR(err);
		err = fix.b->err;
		TEST_ERR(err);
		reanswer = mem_deref(reanswer);
		reoffer = mem_deref(reoffer);
	}
	if (use_data) {
		ASSERT_TRUE(fix.a->got_data);
		ASSERT_TRUE(datachannel_id(fix.a->dc) >= 0);
	}
	if (use_data && !negotiated_data && !use_audio && !use_video) {
		struct session_description description;

		fix.a->destroy_on_data_close = true;
		err = peerconnection_create_offer(fix.a->pc, &reoffer);
		TEST_ERR(err);
		description.type = SDP_OFFER;
		description.sdp = reoffer;
		err = peerconnection_set_remote_descr(fix.b->pc, &description);
		TEST_ERR(err);
		err = peerconnection_create_answer(fix.b->pc, &reanswer);
		TEST_ERR(err);
		err = sdp_reject_application(reanswer);
		TEST_ERR(err);
		description.type = SDP_ANSWER;
		description.sdp = reanswer;
		err = peerconnection_set_remote_descr(fix.a->pc, &description);
		TEST_ERR(err);
		ASSERT_TRUE(fix.a->data_close_closed);
		ASSERT_TRUE(fix.a->data_close_stable);
		ASSERT_TRUE(fix.a->pc == NULL);
		err = 0;
	}
	if (application_first_bundle) {
		ASSERT_TRUE(fix.application_first_answer);
	}
	if (use_audio && !use_video && !use_data && !use_aufilt &&
	    !restart_media_only) {
		struct session_description description;
		struct close_wait close_wait = {0};
		struct data_channel *late = NULL;
		int late_id;

		err = peerconnection_set_datachannel_handler(
			fix.a->pc, incoming_datachannel_handler, fix.a);
		TEST_ERR(err);
		err = peerconnection_set_datachannel_handler(
			fix.b->pc, incoming_datachannel_handler, fix.b);
		TEST_ERR(err);
		fix.terminated = false;
		fix.a->use_data = true;
		fix.b->use_data = true;
		fix.a->got_data = false;
		fix.b->got_data = false;
		fix.a->data_sent = false;
		fix.a->got_audio = false;
		fix.b->got_audio = false;
		err = peerconnection_create_datachannel(
			fix.a->pc, "late", NULL, &late);
		TEST_ERR(err);
		err = datachannel_set_handlers(
			late, data_message_handler, data_state_handler, NULL,
			fix.a);
		TEST_ERR(err);
		err = peerconnection_create_offer(fix.a->pc, &late_offer);
		TEST_ERR(err);
		ASSERT_TRUE(mbuf_contains(late_offer, "m=application "));
		ASSERT_TRUE(mbuf_contains(late_offer, "a=group:BUNDLE 0 1"));
		description.type = SDP_OFFER;
		description.sdp = late_offer;
		err = peerconnection_set_remote_descr(fix.b->pc, &description);
		TEST_ERR(err);
		err = peerconnection_create_answer(fix.b->pc, &late_answer);
		TEST_ERR(err);
		description.type = SDP_ANSWER;
		description.sdp = late_answer;
		err = peerconnection_set_remote_descr(fix.a->pc, &description);
		TEST_ERR(err);
		/* The shared DTLS lower may deliver the peer's INIT synchronously
		 * while the new SCTP consumer is still unpublished.  Both sides must
		 * remain behind the session gate until that queued input is drained
		 * and the associations independently report COMM_UP. */
		err = re_main_timeout(10000);
		TEST_ERR(err);
		err = fix.a->err;
		TEST_ERR(err);
		err = fix.b->err;
		TEST_ERR(err);
		ASSERT_TRUE(fix.a->got_data);
		ASSERT_TRUE(fix.a->got_audio || fix.b->got_audio);
		ASSERT_EQ(1, fix.a->estab_audio_count);
		ASSERT_EQ(1, fix.b->estab_audio_count);
		ASSERT_EQ(DATACHANNEL_OPEN, datachannel_state(late));
		late_id = datachannel_id(late);
		ASSERT_TRUE(late_id >= 0);
		err = datachannel_set_handlers(
			late, NULL, close_state_handler, NULL, &close_wait);
		TEST_ERR(err);
		err = datachannel_close(late);
		TEST_ERR(err);
		err = wait_for_channel_close(&close_wait, 10000);
		TEST_ERR(err);
		ASSERT_TRUE(close_wait.closed);
		ASSERT_EQ(DATACHANNEL_CLOSED, datachannel_state(late));
		ASSERT_EQ(late_id, datachannel_id(late));
		late_answer = mem_deref(late_answer);
		late_offer = mem_deref(late_offer);
	}
	if (negotiated_data) {
		struct session_description description;
		struct release_on_closed release = {
			.dcp = &fix.b->dc,
		};
		char reused_tls_id[256];
		char replacement_tls_id[256];
		char reused_ice_ufrag[256];
		char replacement_ice_ufrag[256];
		uint16_t reused_port;
		uint16_t replacement_port;

		ASSERT_EQ(DATACHANNEL_OPEN,
			  datachannel_state(fix.a->dc));
		ASSERT_EQ(DATACHANNEL_OPEN,
			  datachannel_state(fix.b->dc));
		err = datachannel_close(fix.a->dc);
		TEST_ERR(err);
		err = datachannel_set_handlers(
			fix.b->dc, NULL, release_on_closed_handler, NULL,
			&release);
		TEST_ERR(err);
		err = peerconnection_create_offer(fix.a->pc, &reoffer);
		TEST_ERR(err);
		ASSERT_TRUE(!mbuf_contains(reoffer, "a=dcmap:"));

		description.type = SDP_OFFER;
		description.sdp = reoffer;
		err = peerconnection_set_remote_descr(fix.b->pc, &description);
		TEST_ERR(err);
		ASSERT_TRUE(!release.closed);
		ASSERT_TRUE(fix.b->dc != NULL);
		/* Process the peer's stream reset while its CLOSED callback remains
		 * behind the provisional-description publication barrier. */
		err = re_main_timeout(1000);
		if (err == ETIMEDOUT)
			err = 0;
		TEST_ERR(err);
		ASSERT_TRUE(!release.closed);
		err = peerconnection_create_answer(fix.b->pc, &reanswer);
		TEST_ERR(err);
		err = wait_for_channel_release(&release, 10000);
		TEST_ERR(err);
		ASSERT_TRUE(release.closed);
		ASSERT_TRUE(fix.b->dc == NULL);
		ASSERT_TRUE(!mbuf_contains(reanswer, "a=dcmap:"));

		description.type = SDP_ANSWER;
		description.sdp = reanswer;
		err = peerconnection_set_remote_descr(fix.a->pc, &description);
		TEST_ERR(err);
		ASSERT_EQ(DATACHANNEL_CLOSED,
			  datachannel_state(fix.a->dc));
		{
			const struct data_channel_config reused_config = {
				.ordered = true,
				.max_retransmits = -1,
				.max_packet_lifetime = -1,
				.protocol = "sdp-test",
				.negotiated = true,
				.id = 1,
			};
			struct data_channel *reused = NULL;

			err = peerconnection_create_datachannel(
				fix.a->pc, "loopback", &reused_config, &reused);
			TEST_ERR(err);
			err = datachannel_close(reused);
			TEST_ERR(err);
		}

		err = peerconnection_create_offer(fix.a->pc, &reuse_offer);
		TEST_ERR(err);
		description.type = SDP_OFFER;
		description.sdp = reuse_offer;
		err = peerconnection_set_remote_descr(fix.b->pc, &description);
		TEST_ERR(err);
		err = peerconnection_create_answer(fix.b->pc, &reuse_answer);
		TEST_ERR(err);
		err = sdp_application_attr_value(
			reanswer, "tls-id", reused_tls_id,
			sizeof(reused_tls_id));
		if (err == ENOENT) {
			reused_tls_id[0] = '\0';
			err = 0;
		}
		TEST_ERR(err);
		err = sdp_application_attr_value(
			reuse_answer, "tls-id", replacement_tls_id,
			sizeof(replacement_tls_id));
		if (err == ENOENT) {
			replacement_tls_id[0] = '\0';
			err = 0;
		}
		TEST_ERR(err);
		ASSERT_STREQ(reused_tls_id, replacement_tls_id);
		err = sdp_application_port(reanswer, &reused_port);
		TEST_ERR(err);
		err = sdp_application_attr_value(
			reanswer, "ice-ufrag", reused_ice_ufrag,
			sizeof(reused_ice_ufrag));
		TEST_ERR(err);
		err = sdp_application_port(reuse_answer, &replacement_port);
		TEST_ERR(err);
		ASSERT_EQ(reused_port, replacement_port);
			description.type = SDP_ANSWER;
			description.sdp = reuse_answer;
			err = peerconnection_set_remote_descr(fix.a->pc, &description);
			TEST_ERR(err);
			err = verify_replacement_continuity(&fix);
			TEST_ERR(err);

			err = peerconnection_create_offer(fix.a->pc,
							   &fingerprint_offer);
			TEST_ERR(err);
			err = sdp_add_fingerprint(&fingerprint_offer);
			TEST_ERR(err);
			description.type = SDP_OFFER;
			description.sdp = fingerprint_offer;
			err = peerconnection_set_remote_descr(fix.b->pc, &description);
			ASSERT_EQ(EPROTO, err);
			err = 0;
			ASSERT_EQ(SS_STABLE,
				  peerconnection_signaling(fix.b->pc));
			description.type = SDP_ROLLBACK;
			description.sdp = NULL;
			err = peerconnection_set_remote_descr(fix.a->pc,
							       &description);
			TEST_ERR(err);
			err = verify_replacement_continuity(&fix);
			TEST_ERR(err);

			err = peerconnection_create_offer(fix.a->pc,
							   &replacement_offer);
		TEST_ERR(err);
		err = sdp_replace_tls_id(
			replacement_offer, "ABCDEFGHIJKLMNOPQRSTUVWX");
		TEST_ERR(err);
		description.type = SDP_OFFER;
		description.sdp = replacement_offer;
		err = peerconnection_set_remote_descr(fix.b->pc, &description);
		ASSERT_EQ(EPROTO, err);
		err = 0;
		ASSERT_EQ(SS_STABLE, peerconnection_signaling(fix.b->pc));
		description.type = SDP_ROLLBACK;
		description.sdp = NULL;
		err = peerconnection_set_remote_descr(fix.a->pc, &description);
		TEST_ERR(err);
		err = verify_replacement_continuity(&fix);
		TEST_ERR(err);

			err = peerconnection_create_offer(fix.b->pc, &role_offer);
			TEST_ERR(err);
			err = sdp_replace_setup(&role_offer, "passive");
			TEST_ERR(err);
			description.type = SDP_OFFER;
			description.sdp = role_offer;
			err = peerconnection_set_remote_descr(fix.a->pc,
							       &description);
			ASSERT_EQ(EPROTO, err);
			err = 0;
			ASSERT_EQ(SS_STABLE,
				  peerconnection_signaling(fix.a->pc));
			description.type = SDP_ROLLBACK;
			description.sdp = NULL;
			err = peerconnection_set_remote_descr(fix.b->pc,
							       &description);
			TEST_ERR(err);
			err = verify_replacement_continuity(&fix);
			TEST_ERR(err);

			err = peerconnection_restart_ice(fix.a->pc);
			TEST_ERR(err);
			err = peerconnection_create_offer(fix.a->pc,
							   &fingerprint_answer);
			TEST_ERR(err);
			description.type = SDP_OFFER;
			description.sdp = fingerprint_answer;
			err = peerconnection_set_remote_descr(fix.b->pc,
							       &description);
			TEST_ERR(err);
			err = peerconnection_create_answer(fix.b->pc,
							    &replacement_answer);
			TEST_ERR(err);
			err = verify_application_default_candidate(
				replacement_answer);
			TEST_ERR(err);
			err = sdp_application_attr_value(
				replacement_answer, "tls-id", replacement_tls_id,
				sizeof(replacement_tls_id));
			if (err == ENOENT) {
				replacement_tls_id[0] = '\0';
				err = 0;
			}
			TEST_ERR(err);
			ASSERT_STREQ(reused_tls_id, replacement_tls_id);
			err = sdp_application_attr_value(
				replacement_answer, "ice-ufrag",
				replacement_ice_ufrag,
				sizeof(replacement_ice_ufrag));
			TEST_ERR(err);
			ASSERT_TRUE(str_cmp(reused_ice_ufrag,
					   replacement_ice_ufrag));
			err = sdp_application_port(replacement_answer,
						   &replacement_port);
			TEST_ERR(err);
			ASSERT_EQ(reused_port, replacement_port);
			description.type = SDP_ANSWER;
			description.sdp = replacement_answer;
			err = peerconnection_set_remote_descr(fix.a->pc,
							       &description);
			TEST_ERR(err);
			err = verify_replacement_continuity(&fix);
			TEST_ERR(err);

			if (use_audio && use_video) {
				err = peerconnection_create_offer(
					fix.a->pc, &topology_offer);
				TEST_ERR(err);
				err = sdp_set_bundle_group(
					&topology_offer, "BUNDLE 0 2 1");
				TEST_ERR(err);
				description.type = SDP_OFFER;
				description.sdp = topology_offer;
				err = peerconnection_set_remote_descr(
					fix.b->pc, &description);
				TEST_ERR(err);
				err = peerconnection_create_answer(
					fix.b->pc, &topology_answer);
				TEST_ERR(err);
				description.type = SDP_ANSWER;
				description.sdp = topology_answer;
				err = peerconnection_set_remote_descr(
					fix.a->pc, &description);
				TEST_ERR(err);
				err = verify_replacement_continuity(&fix);
				TEST_ERR(err);

				/* A provisional tag change rolls back without disturbing
				 * the active generation; a committed topology then removes
				 * video from the data transport's exact member set. */
				err = renegotiate_bundle_membership(
					&fix, "BUNDLE 2 0 1", true, 4);
				TEST_ERR(err);
				err = renegotiate_bundle_membership(
					&fix, "BUNDLE 0 2", true, 5);
				TEST_ERR(err);
			}
		}
#endif

		out:
	mem_deref(late_answer);
	mem_deref(late_offer);
	mem_deref(topology_answer);
	mem_deref(topology_offer);
	mem_deref(role_answer);
	mem_deref(role_offer);
	mem_deref(replacement_answer);
	mem_deref(replacement_offer);
	mem_deref(fingerprint_answer);
	mem_deref(fingerprint_offer);
	mem_deref(reuse_answer);
	mem_deref(reuse_offer);
	mem_deref(reanswer);
	mem_deref(reoffer);
	fix.b = mem_deref(fix.b);
	fix.a = mem_deref(fix.a);
	mem_deref(fix.mq);

	if (use_audio) {
		if (use_aufilt) {
			for (size_t i=0; i<RE_ARRAY_SIZE(aufilters); i++) {
				const char *name = aufilters[i];

				module_unload(name);
			}
		}

		module_unload("ausine");
		module_unload("g711");
		mem_deref(auplay);
	}
	if (use_video) {
		module_unload("fakevideo");
		mock_vidcodec_unregister();
		mem_deref(vidisp);
	}

	return err;
}


static bool if_handler(const char *ifname, const struct sa *sa, void *arg)
{
	bool *have_real_ip = arg;
	(void)ifname;

	if (sa_is_loopback(sa) || sa_is_linklocal(sa))
		return false;

	*have_real_ip = true;

	return true;
}


#ifdef USE_DATACHANNEL
static int test_remote_split_preanswer_trickle(const struct menc *menc)
{
	const struct rtc_configuration offer_config = {.offerer = true};
	const struct rtc_configuration answer_config = {.offerer = false};
	struct session_description description;
	struct gather_wait offer_wait = {0};
	struct gather_wait answer_wait = {0};
	struct peer_connection *offerer = NULL;
	struct peer_connection *answerer = NULL;
	struct data_channel *dc = NULL;
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	char active_ufrag[64];
	char ufrag[64];
	char candidate[256];
	const struct mnat *mnat;
	unsigned attrs;
	unsigned active_attrs;
	unsigned gather_callbacks;
	unsigned app_gather_calls;
	uint64_t active_generation;
	uint64_t fresh_generation;
	bool codecs_registered = false;
	bool mock_registered = false;
	int err;

	mock_aucodec_register();
	mock_vidcodec_register();
	codecs_registered = true;
	mock_mnat_register(baresip_mnatl());
	mock_registered = true;
	mnat = mnat_find(baresip_mnatl(), "XNAT");
	ASSERT_TRUE(mnat != NULL);
	err = peerconnection_new(&offerer, &offer_config, mnat, menc,
			 peerconn_validation_gather_handler, NULL, NULL, &offer_wait);
	TEST_ERR(err);
	err = peerconnection_add_audio_track(offerer, conf_config(),
					     baresip_aucodecl(), SDP_SENDRECV);
	TEST_ERR(err);
	err = peerconnection_add_video_track(offerer, conf_config(),
						     baresip_vidcodecl(), SDP_SENDRECV);
	TEST_ERR(err);
	err = peerconnection_create_datachannel(
		offerer, "remote-split", NULL, &dc);
	TEST_ERR(err);
	err = peerconnection_new(&answerer, &answer_config, mnat, menc,
			 peerconn_validation_gather_handler, NULL, NULL, &answer_wait);
	TEST_ERR(err);
	err = peerconnection_add_audio_track(answerer, conf_config(),
					     baresip_aucodecl(), SDP_SENDRECV);
	TEST_ERR(err);
	err = peerconnection_add_video_track(answerer, conf_config(),
						     baresip_vidcodecl(), SDP_SENDRECV);
	TEST_ERR(err);
	err = peerconnection_set_datachannel_handler(answerer, NULL, NULL);
	TEST_ERR(err);
	err = wait_for_gather(&offer_wait);
	TEST_ERR(err);
	err = wait_for_gather(&answer_wait);
	TEST_ERR(err);
	err = peerconnection_create_offer(offerer, &offer);
	TEST_ERR(err);
	err = sdp_video_attr_value(offer, "ice-ufrag", active_ufrag,
				   sizeof(active_ufrag));
	TEST_ERR(err);
	description.type = SDP_OFFER;
	description.sdp = offer;
	err = peerconnection_set_remote_descr(answerer, &description);
	TEST_ERR(err);
	err = peerconnection_create_answer(answerer, &answer);
	TEST_ERR(err);
	description.type = SDP_ANSWER;
	description.sdp = answer;
	err = peerconnection_set_remote_descr(offerer, &description);
	TEST_ERR(err);
	err = peerconnection_start_ice(answerer);
	TEST_ERR(err);
	err = peerconnection_start_ice(offerer);
	TEST_ERR(err);
	for (unsigned i = 0; i < 2; ++i) {
		err = re_main_timeout(10);
		if (err == ETIMEDOUT)
			err = 0;
		TEST_ERR(err);
	}
	/* Establish an exact oracle for the still-active answerer checklist. */
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:89 1 UDP 1 192.0.2.89 40898 typ host ufrag %s",
		    active_ufrag);
	peerconnection_add_ice_candidate(answerer, candidate, "1");
	active_generation = mock_mnat_last_candidate_generation();
	ASSERT_TRUE(active_generation != 0);
	active_attrs = mock_mnat_candidate_attr_count_generation(
		active_generation);

	offer = mem_deref(offer);
	answer = mem_deref(answer);
	err = peerconnection_create_offer(offerer, &offer);
	TEST_ERR(err);
	err = sdp_set_bundle_group(&offer, "BUNDLE 0 2");
	TEST_ERR(err);
	err = sdp_video_require_rtcp_mux(&offer);
	TEST_ERR(err);
	err = sdp_video_isolate_transport(&offer);
	TEST_ERR(err);
	err = sdp_video_attr_value(offer, "ice-ufrag", ufrag, sizeof(ufrag));
	TEST_ERR(err);
	description.type = SDP_OFFER;
	description.sdp = offer;
	mock_mnat_media_gather_defer(true);
	err = peerconnection_set_remote_descr(answerer, &description);
	TEST_ERR(err);

	attrs = mock_mnat_candidate_attr_count();
	re_snprintf(candidate, sizeof(candidate),
		    "candidate:90 1 UDP 1 192.0.2.90 40900 typ host ufrag %s",
		    ufrag);
	peerconnection_add_ice_candidate(answerer, candidate, "1");
	/* Trickle received against a pending remote description is quarantined
	 * until the local answer commits that generation.  It must not mutate
	 * either the active or provisional checklist before publication. */
	ASSERT_EQ(attrs, mock_mnat_candidate_attr_count());
	ASSERT_EQ(active_attrs,
		  mock_mnat_candidate_attr_count_generation(active_generation));

	attrs = mock_mnat_candidate_attr_count();
	peerconnection_add_ice_candidate(
		answerer,
		"candidate:91 1 UDP 1 192.0.2.91 40902 typ host ufrag stale",
		"1");
	ASSERT_EQ(attrs, mock_mnat_candidate_attr_count());

	err = peerconnection_create_answer(answerer, &answer);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_TRUE(answer == NULL);
	err = 0;
	err = peerconnection_create_answer(answerer, &answer);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_TRUE(answer == NULL);
	err = 0;
	gather_callbacks = mock_mnat_media_gather_callback_count();
	app_gather_calls = answer_wait.calls;
	mock_mnat_complete_media_gathers();
	ASSERT_EQ(gather_callbacks + 1,
			  mock_mnat_media_gather_callback_count());
	for (unsigned i = 0; i < 100 &&
	     answer_wait.calls == app_gather_calls; ++i) {
		err = re_main_timeout(10);
		if (err == ETIMEDOUT)
			err = 0;
		TEST_ERR(err);
	}
	ASSERT_EQ(app_gather_calls + 1, answer_wait.calls);
	err = peerconnection_create_answer(answerer, &answer);
	TEST_ERR(err);
	ASSERT_TRUE(answer != NULL);
	ASSERT_EQ(attrs + 1, mock_mnat_candidate_attr_count());
	fresh_generation = mock_mnat_last_candidate_generation();
	ASSERT_TRUE(fresh_generation != 0);
	ASSERT_TRUE(fresh_generation != active_generation);
	/* Publication retires the old all-members group, so its per-generation
	 * mock counter may disappear here.  The last recipient and surviving
	 * counter prove the queued candidate was applied to the fresh singleton. */
	ASSERT_EQ(1, mock_mnat_candidate_attr_count_generation(
			fresh_generation));

out:
	mem_deref(answer);
	mem_deref(offer);
	mem_deref(dc);
	mem_deref(answerer);
	mem_deref(offerer);
	if (mock_registered)
		mock_mnat_unregister();
	if (codecs_registered) {
		mock_vidcodec_unregister();
		mock_aucodec_unregister();
	}
	return err;
}
#endif


int test_peerconn_remote_split(void)
{
#ifdef USE_DATACHANNEL
	const struct menc *menc;
	int err;

	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	menc = menc_find(baresip_mencl(), "dtls_srtp");
	ASSERT_TRUE(menc != NULL);
	err = test_remote_split_preanswer_trickle(menc);
	TEST_ERR(err);

out:
	module_unload("dtls_srtp");
	return err;
#else
	return 0;
#endif
}

int test_bundle_legacy_forwarding(void)
{
	int err;

	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	err = module_load(".", "ice");
	TEST_ERR(err);
	err = test_peerconn_param(1, 0, 1
#ifdef USE_DATACHANNEL
				 , 0, 0, 0, 0, 0, 0, DATA_AFTER_MEDIA
#endif
				 );
	TEST_ERR(err);

out:
	module_unload("ice");
	module_unload("dtls_srtp");
	return err;
}


int test_peerconn_late_data(void)
{
#ifdef USE_DATACHANNEL
	bool have_real_ip = false;
	int err;

	net_laddr_apply(baresip_network(), if_handler, &have_real_ip);
	if (!have_real_ip)
		return 0;
	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	err = module_load(".", "ice");
	TEST_ERR(err);
	err = test_peerconn_param(1, 0, 0, 0, 0, 0, 0, 0, 0,
				  DATA_AFTER_MEDIA);
	TEST_ERR(err);

out:
	module_unload("ice");
	module_unload("dtls_srtp");
	return err;
#else
	return 0;
#endif
}


int test_menc_transport_identity(void)
{
	const struct menc *menc;
	int err;

	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	menc = menc_find(baresip_mencl(), "dtls_srtp");
	ASSERT_TRUE(menc != NULL);
	err = peerconn_test_transport_identity(menc);
	TEST_ERR(err);
out:
	module_unload("dtls_srtp");
	return err;
}


int test_peerconn_restart_shadow(void)
{
#ifdef USE_DATACHANNEL
	const struct menc *menc;
	int err;

	if (conf_config()->avt.rxmode == RECEIVE_MODE_THREAD)
		return 0;
	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	menc = menc_find(baresip_mencl(), "dtls_srtp");
	ASSERT_TRUE(menc != NULL);
	err = test_restart_shadow_sdp_rollback(menc);
	TEST_ERR(err);

out:
	module_unload("dtls_srtp");
	return err;
#else
	return 0;
#endif
}


int test_peerconn_rtp_restart(void)
{
#ifdef USE_DATACHANNEL
	bool have_real_ip = false;
	int err;

	net_laddr_apply(baresip_network(), if_handler, &have_real_ip);
	if (!have_real_ip ||
	    conf_config()->avt.rxmode == RECEIVE_MODE_THREAD)
		return 0;
	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	err = module_load(".", "ice");
	TEST_ERR(err);
	err = test_peerconn_param(1, 0, 0, 0, 0, 0, 0, 0, 1,
				  DATA_AFTER_MEDIA);
	TEST_ERR(err);
	err = test_peerconn_param(0, 0, 1, 0, 0, 0, 0, 0, 1,
				  DATA_AFTER_MEDIA);
	TEST_ERR(err);

out:
	module_unload("ice");
	module_unload("dtls_srtp");
	return err;
#else
	return 0;
#endif
}


int test_peerconn(void)
{
	const struct menc *menc;
	bool have_real_ip = false;
	int err;

#ifndef USE_DATACHANNEL
	err = test_disabled_datachannel_api();
	TEST_ERR(err);
#endif

	/* skip the test if no other interface than loopback is available */
	net_laddr_apply(baresip_network(), if_handler, &have_real_ip);
	if (!have_real_ip)
		return 0;

	if (conf_config()->avt.rxmode == RECEIVE_MODE_THREAD)
		return 0;

	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	err = module_load(".", "ice");
	TEST_ERR(err);

	menc = menc_find(baresip_mencl(), "dtls_srtp");
	ASSERT_TRUE(menc != NULL);
	err = peerconn_test_transport_suite(menc);
	TEST_ERR(err);

	err = test_peerconn_param(1, 0, 0
#ifdef USE_DATACHANNEL
				  , 0, 0, 0, 0, 0, 0, DATA_AFTER_MEDIA
#endif
				  );
	TEST_ERR(err);
	err = test_peerconn_param(0, 0, 1
#ifdef USE_DATACHANNEL
				  , 0, 0, 0, 0, 0, 0, DATA_AFTER_MEDIA
#endif
				  );
	TEST_ERR(err);

	/*
	 * Exercise member replacement once, then prove that its teardown leaves
	 * a fresh audio/video BUNDLE session able to establish and receive both
	 * media kinds.  Running the destructive fixture mutation before every
	 * scenario obscured this regression behind accumulated test state.
	 */
	err = peerconn_test_menc_transport(menc, true);
	TEST_ERR(err);
	err = test_peerconn_param(1, 0, 1
#ifdef USE_DATACHANNEL
				  , 0, 0, 0, 0, 0, 0, DATA_AFTER_MEDIA
#endif
				  );
	TEST_ERR(err);

	err = test_peerconn_param(1, 1, 0
#ifdef USE_DATACHANNEL
				  , 0, 0, 0, 0, 0, 0, DATA_AFTER_MEDIA
#endif
				  );
	TEST_ERR(err);

#ifdef USE_DATACHANNEL
	err = peerconn_test_data_contracts(
		mnat_find(baresip_mnatl(), "ice"), menc);
	TEST_ERR(err);
	err = test_restart_shadow_sdp_rollback(menc);
	TEST_ERR(err);
	err = test_bundle_layout_negotiation(
		mnat_find(baresip_mnatl(), "ice"), menc);
	TEST_ERR(err);
	err = test_peerconn_param(
		0, 0, 0, 1, 0, 0, 0, 0, 0, DATA_AFTER_MEDIA);
	TEST_ERR(err);
	err = test_peerconn_param(
		1, 0, 1, 1, 0, 0, 1, 0, 0, DATA_AFTER_MEDIA);
	TEST_ERR(err);
	err = test_peerconn_param(
		1, 0, 1, 1, 0, 0, 0, 0, 0, DATA_AFTER_MEDIA);
	TEST_ERR(err);
	err = test_peerconn_param(
		1, 0, 1, 1, 0, 1, 0, 0, 0, DATA_AFTER_MEDIA);
	TEST_ERR(err);
	err = test_peerconn_param(
		0, 0, 0, 1, 1, 0, 0, 0, 0, DATA_AFTER_MEDIA);
	TEST_ERR(err);
	err = test_peerconn_param(
		1, 0, 1, 1, 1, 0, 0, 0, 0, DATA_AFTER_MEDIA);
	TEST_ERR(err);
	err = test_peerconn_param(
		1, 0, 1, 1, 0, 0, 0, 1, 0, DATA_AFTER_MEDIA);
	TEST_ERR(err);
	err = test_peerconn_param(
		1, 0, 0, 1, 0, 0, 0, 0, 0, DATA_BEFORE_MEDIA);
	TEST_ERR(err);
	err = test_peerconn_param(
		1, 0, 1, 1, 0, 0, 0, 0, 0, DATA_BETWEEN_MEDIA);
	TEST_ERR(err);
	err = test_data_callback_destruction(
		mnat_find(baresip_mnatl(), "ice"),
		menc_find(baresip_mencl(), "dtls_srtp"), false);
	TEST_ERR(err);
	err = test_data_callback_destruction(
		mnat_find(baresip_mnatl(), "ice"),
		menc_find(baresip_mencl(), "dtls_srtp"), true);
	TEST_ERR(err);
	err = test_deferred_receive_queue(
		mnat_find(baresip_mnatl(), "ice"), menc,
		DEFERRED_RECEIVE_ROLLBACK);
	TEST_ERR(err);
	err = test_deferred_receive_queue(
		mnat_find(baresip_mnatl(), "ice"), menc,
		DEFERRED_RECEIVE_DESTRUCTION);
	TEST_ERR(err);
	err = test_deferred_receive_queue(
		mnat_find(baresip_mnatl(), "ice"), menc,
		DEFERRED_RECEIVE_BUDGET);
	TEST_ERR(err);
	err = test_deferred_receive_queue(
		mnat_find(baresip_mnatl(), "ice"), menc,
		DEFERRED_RECEIVE_MESSAGE_DESTRUCTION);
	TEST_ERR(err);
	err = test_deferred_receive_queue(
		mnat_find(baresip_mnatl(), "ice"), menc,
		DEFERRED_RECEIVE_CLOSED_DESTRUCTION);
	TEST_ERR(err);
	err = test_deferred_receive_queue(
		mnat_find(baresip_mnatl(), "ice"), menc,
		DEFERRED_RECEIVE_ERROR_DESTRUCTION);
	TEST_ERR(err);
#endif

 out:
	module_unload("ice");
	module_unload("dtls_srtp");

	return err;
}


int test_peerconn_data_runtime(void)
{
#ifdef USE_DATACHANNEL
	const struct menc *menc;
	const struct mnat *mnat;
	bool have_real_ip = false;
	int err;

	if (conf_config()->avt.rxmode == RECEIVE_MODE_THREAD)
		return 0;
	net_laddr_apply(baresip_network(), if_handler, &have_real_ip);
	if (!have_real_ip)
		return 0;

	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	err = module_load(".", "ice");
	TEST_ERR(err);
	menc = menc_find(baresip_mencl(), "dtls_srtp");
	mnat = mnat_find(baresip_mnatl(), "ice");
	ASSERT_TRUE(menc != NULL);
	ASSERT_TRUE(mnat != NULL);
	err = test_peerconn_data_runtime_impl(mnat, menc);
	TEST_ERR(err);

out:
	module_unload("ice");
	module_unload("dtls_srtp");
	return err;
#else
	return 0;
#endif
}


int test_peerconn_answer_retry(void)
{
#ifdef USE_DATACHANNEL
	const struct menc *menc;
	const struct mnat *mnat;
	int err;

	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	err = module_load(".", "ice");
	TEST_ERR(err);
	menc = menc_find(baresip_mencl(), "dtls_srtp");
	mnat = mnat_find(baresip_mnatl(), "ice");
	ASSERT_TRUE(menc != NULL);
	ASSERT_TRUE(mnat != NULL);
	err = peerconn_test_answer_retry(mnat, menc);
	TEST_ERR(err);

out:
	module_unload("ice");
	module_unload("dtls_srtp");
	return err;
#else
	return 0;
#endif
}
