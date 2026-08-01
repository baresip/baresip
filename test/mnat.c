/**
 * @file mnat.c Media-NAT transaction tests
 *
 * Copyright (C) 2026 The baresip contributors
 */

#include <re.h>
#include <baresip.h>
#include "test.h"


struct attempt_result {
	unsigned calls;
	int err;
	struct sa remote;
};


struct gather_result {
	unsigned calls;
	int err;
};


void mock_mnat_media_gather_defer(bool defer);
void mock_mnat_media_gather_result(int err);
void mock_mnat_complete_media_gathers(void);
unsigned mock_mnat_media_gather_cancel_count(void);
unsigned mock_mnat_media_gather_callback_count(void);


static void gather_handler(int err, void *arg)
{
	struct gather_result *result = arg;

	++result->calls;
	result->err = err;
}


static void active_estab_handler(int err, uint16_t scode, const char *reason,
				 void *arg)
{
	unsigned *calls = arg;

	(void)err;
	(void)scode;
	(void)reason;
	++*calls;
}


static void attempt_handler(int err, const struct sa *raddr1,
			    const struct sa *raddr2, void *arg)
{
	struct attempt_result *result = arg;

	(void)raddr2;
	++result->calls;
	result->err = err;
	if (raddr1)
		sa_cpy(&result->remote, raddr1);
}


int test_mnat_media_attempt(void)
{
	struct attempt_result result = {0};
	struct gather_result gather = {0};
	const struct mnat *mnat;
	struct mnat_sess *sess = NULL;
	struct mnat_media *media = NULL;
	struct mnat_media *failure_media = NULL;
	struct sdp_session *local_sdp = NULL;
	struct sdp_session *remote_sdp = NULL;
	struct sdp_media *local_media = NULL;
	struct sdp_media *remote_media = NULL;
	struct udp_sock *sock = NULL;
	struct mbuf *offer = NULL;
	struct sa local;
	struct sa remote;
	unsigned active_calls = 0;
	int err = 0;

	mock_mnat_register(baresip_mnatl());
	mnat = mnat_find(baresip_mnatl(), "XNAT");
	ASSERT_TRUE(mnat != NULL);
	ASSERT_TRUE(mnat->mediaattemptstarth != NULL);
	ASSERT_TRUE(mnat->mediaattemptcancelh != NULL);
	ASSERT_TRUE(mnat->mediagatheredh != NULL);
	ASSERT_TRUE(mnat->mediagatherwaith != NULL);
	ASSERT_TRUE(mnat->mediagathercancelh != NULL);

	sa_set_str(&local, "127.0.0.1", 0);
	sa_set_str(&remote, "127.0.0.1", 6000);
	err = udp_listen(&sock, &local, NULL, NULL);
	TEST_ERR(err);
	err = udp_local_get(sock, &local);
	TEST_ERR(err);
	err = sdp_session_alloc(&local_sdp, &local);
	TEST_ERR(err);
	err = sdp_session_alloc(&remote_sdp, &remote);
	TEST_ERR(err);
	err = sdp_media_add(&local_media, local_sdp, "audio",
			    sa_port(&local), "RTP/AVP");
	TEST_ERR(err);
	err = sdp_media_add(&remote_media, remote_sdp, "audio",
			    sa_port(&remote), "RTP/AVP");
	TEST_ERR(err);
	err = sdp_format_add(NULL, local_media, false, "0", "PCMU", 8000, 1,
			     NULL, NULL, NULL, false, NULL);
	err |= sdp_format_add(NULL, remote_media, false, "0", "PCMU", 8000, 1,
			      NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	err = sdp_encode(&offer, remote_sdp, true);
	TEST_ERR(err);
	err = sdp_decode(local_sdp, offer, true);
	TEST_ERR(err);

	err = mnat->sessh(&sess, mnat, NULL, AF_INET, NULL, NULL, NULL,
			  local_sdp, true, active_estab_handler, &active_calls);
	TEST_ERR(err);
	mock_mnat_media_gather_defer(true);
	err = mnat->mediah(&media, sess, sock, NULL, local_media, NULL, NULL);
	TEST_ERR(err);
	ASSERT_TRUE(!mnat->mediagatheredh(media));
	err = mnat->mediaprepareh(media, true);
	TEST_ERR(err);

	/* A cancelled provisional gather can never escape into either its
	 * generation callback or the active session establishment callback. */
	err = mnat->mediagatherwaith(media, gather_handler, &gather);
	ASSERT_EQ(EAGAIN, err);
	mnat->mediagathercancelh(media);
	mock_mnat_complete_media_gathers();
	ASSERT_EQ(1, mock_mnat_media_gather_cancel_count());
	ASSERT_EQ(0, gather.calls);
	ASSERT_EQ(0, active_calls);

	/* Deferred success is delivered exactly once and remains local. */
	err = mnat->mediagatherwaith(media, gather_handler, &gather);
	ASSERT_EQ(EAGAIN, err);
	mock_mnat_complete_media_gathers();
	ASSERT_EQ(1, gather.calls);
	TEST_ERR(gather.err);
	ASSERT_TRUE(mnat->mediagatheredh(media));
	ASSERT_EQ(0, active_calls);

	/* A separately prepared generation reports an asynchronous terminal
	 * failure instead of remaining indistinguishable from EAGAIN forever. */
	err = mnat->mediah(&failure_media, sess, sock, NULL, local_media,
			   NULL, NULL);
	TEST_ERR(err);
	err = mnat->mediaprepareh(failure_media, true);
	TEST_ERR(err);
	err = mnat->mediagatherwaith(failure_media, gather_handler, &gather);
	ASSERT_EQ(EAGAIN, err);
	mock_mnat_media_gather_result(EHOSTUNREACH);
	mock_mnat_complete_media_gathers();
	ASSERT_EQ(2, gather.calls);
	ASSERT_EQ(EHOSTUNREACH, gather.err);
	ASSERT_EQ(2, mock_mnat_media_gather_callback_count());
	ASSERT_EQ(0, active_calls);
	err = mnat->mediagatherwaith(failure_media, gather_handler, &gather);
	ASSERT_EQ(EHOSTUNREACH, err);
	ASSERT_EQ(2, gather.calls);
	mock_mnat_media_gather_result(0);

	mock_mnat_media_attempt_defer(true);
	err = mnat->mediaattemptstarth(media, attempt_handler, &result);
	TEST_ERR(err);
	ASSERT_EQ(1, mock_mnat_media_attempt_start_count());
	mnat->mediaattemptcancelh(media);
	mock_mnat_complete_media_attempts();
	ASSERT_EQ(1, mock_mnat_media_attempt_cancel_count());
	ASSERT_EQ(0, result.calls);

	err = mnat->mediaattemptstarth(media, attempt_handler, &result);
	TEST_ERR(err);
	mock_mnat_complete_media_attempts();
	ASSERT_EQ(1, result.calls);
	TEST_ERR(result.err);
	ASSERT_TRUE(sa_cmp(&remote, &result.remote, SA_ALL));

	mock_mnat_media_attempt_result(EHOSTUNREACH);
	err = mnat->mediaattemptstarth(media, attempt_handler, &result);
	TEST_ERR(err);
	mock_mnat_complete_media_attempts();
	ASSERT_EQ(2, result.calls);
	ASSERT_EQ(EHOSTUNREACH, result.err);
	ASSERT_EQ(2, mock_mnat_media_attempt_callback_count());

out:
	mem_deref(failure_media);
	mem_deref(media);
	mem_deref(sess);
	mem_deref(offer);
	mem_deref(remote_sdp);
	mem_deref(local_sdp);
	mem_deref(sock);
	mock_mnat_unregister();
	return err;
}


int test_mnat_media_restart(void)
{
	struct gather_result stale = {0};
	const struct mnat *mnat;
	struct mnat unsupported = {0};
	struct mnat_sess *sess = NULL;
	struct mnat_media *active1 = NULL, *active2 = NULL;
	struct mnat_media *candidate1 = NULL, *candidate2 = NULL;
	struct sdp_session *sdp = NULL;
	struct sdp_media *media1 = NULL, *media2 = NULL;
	struct sdp_media *restart1 = NULL, *restart2 = NULL;
	struct udp_sock *sock1 = NULL, *sock2 = NULL;
	struct udp_sock *candidate_sock1 = NULL, *candidate_sock2 = NULL;
	struct sa local;
	const char *active_ufrag, *candidate_ufrag1, *candidate_ufrag2;
	unsigned cancels;
	unsigned active_calls = 0;
	int err = 0;

	mock_mnat_register(baresip_mnatl());
	mnat = mnat_find(baresip_mnatl(), "XNAT");
	ASSERT_TRUE(mnat != NULL);
	ASSERT_TRUE(mnat->mediarestartalloch != NULL);

	sa_set_str(&local, "127.0.0.1", 0);
	err = udp_listen(&sock1, &local, NULL, NULL);
	err |= udp_listen(&sock2, &local, NULL, NULL);
	err |= udp_listen(&candidate_sock1, &local, NULL, NULL);
	err |= udp_listen(&candidate_sock2, &local, NULL, NULL);
	TEST_ERR(err);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	err = sdp_media_add(&media1, sdp, "audio", 10000, "RTP/AVP");
	err |= sdp_media_add(&media2, sdp, "video", 10002, "RTP/AVP");
	err |= sdp_media_add(&restart1, sdp, "audio", 10004, "RTP/AVP");
	err |= sdp_media_add(&restart2, sdp, "video", 10006, "RTP/AVP");
	TEST_ERR(err);
	err = mnat->sessh(&sess, mnat, NULL, AF_INET, NULL, NULL, NULL,
			  sdp, true, active_estab_handler, &active_calls);
	TEST_ERR(err);
	err = mnat->mediah(&active1, sess, sock1, NULL, media1, NULL, NULL);
	err |= mnat->mediah(&active2, sess, sock2, NULL, media2, NULL, NULL);
	TEST_ERR(err);
	err = mnat_media_restart_alloc(&unsupported, &candidate1, active1,
				       candidate_sock1, restart1, NULL, NULL);
	ASSERT_EQ(ENOTSUP, err);

	/* Each restart is a new media generation under the same session.  It
	 * owns its socket/SDP identity and does not displace active media while
	 * gathering. */
	mock_mnat_media_gather_defer(true);
	err = mnat_media_restart_alloc(mnat, &candidate1, active1,
				       candidate_sock1, restart1, NULL, NULL);
	err |= mnat_media_restart_alloc(mnat, &candidate2, active2,
					candidate_sock2, restart2, NULL, NULL);
	TEST_ERR(err);
	ASSERT_TRUE(mock_mnat_media_generation(active1) !=
		    mock_mnat_media_generation(candidate1));
	ASSERT_TRUE(mock_mnat_media_generation(candidate1) !=
		    mock_mnat_media_generation(candidate2));
	ASSERT_TRUE(mock_mnat_media_is_active(active1));
	ASSERT_TRUE(mock_mnat_media_is_active(active2));
	ASSERT_TRUE(!mock_mnat_media_is_active(candidate1));
	ASSERT_TRUE(!mock_mnat_media_is_active(candidate2));
	active_ufrag = sdp_media_lattr_apply(media1, "ice-ufrag", NULL, NULL);
	candidate_ufrag1 = sdp_media_lattr_apply(restart1, "ice-ufrag",
						 NULL, NULL);
	candidate_ufrag2 = sdp_media_lattr_apply(restart2, "ice-ufrag",
						 NULL, NULL);
	ASSERT_TRUE(str_isset(active_ufrag));
	ASSERT_TRUE(str_isset(candidate_ufrag1));
	ASSERT_TRUE(str_isset(candidate_ufrag2));
	ASSERT_TRUE(str_cmp(active_ufrag, candidate_ufrag1));
	ASSERT_TRUE(str_cmp(candidate_ufrag1, candidate_ufrag2));

	/* The activation protocol is independently reversible for each media
	 * group.  Rolling group one back cannot alter group two. */
	err = mnat->mediaprepareh(active1, false);
	err |= mnat->mediaprepareh(candidate1, true);
	err |= mnat->mediaprepareh(active2, false);
	err |= mnat->mediaprepareh(candidate2, true);
	TEST_ERR(err);
	mnat->mediaactivateh(active1);
	mnat->mediaactivateh(candidate1);
	mnat->mediaactivateh(active2);
	mnat->mediaactivateh(candidate2);
	ASSERT_TRUE(!mock_mnat_media_is_active(active1));
	ASSERT_TRUE(mock_mnat_media_is_active(candidate1));
	ASSERT_TRUE(!mock_mnat_media_is_active(active2));
	ASSERT_TRUE(mock_mnat_media_is_active(candidate2));
	mnat->mediarollbackh(candidate1);
	mnat->mediarollbackh(active1);
	ASSERT_TRUE(mock_mnat_media_is_active(active1));
	ASSERT_TRUE(!mock_mnat_media_is_active(candidate1));
	ASSERT_TRUE(!mock_mnat_media_is_active(active2));
	ASSERT_TRUE(mock_mnat_media_is_active(candidate2));
	mnat->mediarollbackh(candidate2);
	mnat->mediarollbackh(active2);
	ASSERT_TRUE(mock_mnat_media_is_active(active2));
	ASSERT_TRUE(!mock_mnat_media_is_active(candidate2));

	/* Aborting a provisional generation synchronously cancels its gather;
	 * a later completion cannot call the stale owner or the active session. */
	err = mnat->mediaprepareh(candidate1, true);
	TEST_ERR(err);
	err = mnat->mediagatherwaith(candidate1, gather_handler, &stale);
	ASSERT_EQ(EAGAIN, err);
	err = 0;
	cancels = mock_mnat_media_gather_cancel_count();
	mnat->mediaaborth(candidate1);
	ASSERT_EQ(cancels + 1, mock_mnat_media_gather_cancel_count());
	mock_mnat_complete_media_gathers();
	ASSERT_EQ(0, stale.calls);
	ASSERT_EQ(0, active_calls);

out:
	mem_deref(candidate2);
	mem_deref(candidate1);
	mem_deref(active2);
	mem_deref(active1);
	mem_deref(sess);
	mem_deref(sdp);
	mem_deref(candidate_sock2);
	mem_deref(candidate_sock1);
	mem_deref(sock2);
	mem_deref(sock1);
	mock_mnat_unregister();
	return err;
}
