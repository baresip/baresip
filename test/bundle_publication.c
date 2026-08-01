/**
 * @file bundle_publication.c  Atomic BUNDLE route publication tests
 *
 * Copyright (C) 2026 Alfred E. Heggestad
 */
#include <stdatomic.h>
#include <re.h>
#include <baresip.h>
#include "core.h"
#include "test.h"


struct publication_fixture {
	struct bundle_publication *publication;
	struct bundle_transport *transportv[2];
	uint64_t generationv[2];
	atomic_uint command;
	atomic_uint stage;
	atomic_uint received[2];
	atomic_uint received_total;
	atomic_int worker_err;
};


struct publication_receiver {
	struct publication_fixture *fixture;
	unsigned index;
};


struct teardown_fixture {
	atomic_bool callback_entered;
	atomic_bool callback_release;
	atomic_uint callback_calls;
	atomic_bool teardown_started;
	atomic_bool teardown_done;
	struct stream **stream;
};


struct teardown_sender {
	struct udp_sock *sock;
	struct sa destination;
	atomic_int err;
};


struct wire_teardown_worker {
	struct udp_sock *sock;
	struct sa source;
	atomic_bool started;
	atomic_bool done;
};


static bool teardown_blocking_send(int *err, struct sa *dst,
				   struct mbuf *mb, void *arg)
{
	struct teardown_fixture *fixture = arg;

	(void)dst;
	(void)mb;
	atomic_fetch_add(&fixture->callback_calls, 1);
	atomic_store(&fixture->callback_entered, true);
	while (!atomic_load(&fixture->callback_release))
		thrd_yield();
	*err = 0;
	return true;
}


static bool teardown_self_stop_send(int *err, struct sa *dst,
				    struct mbuf *mb, void *arg)
{
	struct teardown_fixture *fixture = arg;
	struct stream *stream = *fixture->stream;

	(void)dst;
	(void)mb;
	atomic_fetch_add(&fixture->callback_calls, 1);
	atomic_store(&fixture->callback_entered, true);
	stream_stop(stream);
	*fixture->stream = mem_deref(stream);
	atomic_store(&fixture->teardown_done, true);
	*err = 0;
	return true;
}


static int teardown_send_worker(void *arg)
{
	struct teardown_sender *sender = arg;
	struct mbuf *mb = mbuf_alloc(1);
	int err = mb ? 0 : ENOMEM;

	if (!err)
		err = mbuf_write_u8(mb, 0x42);
	if (!err) {
		mb->pos = 0;
		err = udp_send(sender->sock, &sender->destination, mb);
	}
	atomic_store(&sender->err, err);
	mem_deref(mb);
	return err;
}


static int teardown_stream_worker(void *arg)
{
	struct teardown_fixture *fixture = arg;
	struct stream *stream = *fixture->stream;

	atomic_store(&fixture->teardown_started, true);
	stream_stop(stream);
	*fixture->stream = mem_deref(stream);
	atomic_store(&fixture->teardown_done, true);
	return 0;
}


static int wire_teardown_recv_worker(void *arg)
{
	static const uint8_t rtp[] = {
		0x80, 0x60, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x01,
	};
	struct wire_teardown_worker *worker = arg;
	struct mbuf *mb = mbuf_alloc(sizeof(rtp));
	int err = mb ? 0 : ENOMEM;

	if (!err)
		err = mbuf_write_mem(mb, rtp, sizeof(rtp));
	if (!err) {
		mb->pos = 0;
		atomic_store(&worker->started, true);
		udp_recv_helper(worker->sock, &worker->source, mb, NULL);
	}
	atomic_store(&worker->done, true);
	mem_deref(mb);
	return err;
}


static int wait_atomic_bool(const atomic_bool *value, bool wanted,
			    uint64_t timeout_ms)
{
	const uint64_t deadline = tmr_jiffies() + timeout_ms;

	while (atomic_load(value) != wanted) {
		if (tmr_jiffies() >= deadline)
			return ETIMEDOUT;
		thrd_yield();
	}
	return 0;
}


static void teardown_rtp_handler(const struct rtp_header *hdr,
				 struct rtpext *extv, size_t extc,
				 struct mbuf *mb, unsigned lostc,
				 bool new_source, void *arg)
{
	(void)hdr;
	(void)extv;
	(void)extc;
	(void)mb;
	(void)lostc;
	(void)new_source;
	(void)arg;
}


static int teardown_pt_handler(uint8_t pt, struct mbuf *mb, void *arg)
{
	(void)pt;
	(void)mb;
	(void)arg;
	return 0;
}


static void publication_udp_recv(const struct sa *src, struct mbuf *mb,
				 void *arg)
{
	struct publication_receiver *receiver = arg;
	struct publication_fixture *fixture = receiver->fixture;

	(void)src;
	(void)mb;
	atomic_fetch_add(&fixture->received[receiver->index], 1);
	if (atomic_fetch_add(&fixture->received_total, 1) + 1 == 2)
		re_cancel();
}


static void wait_command(const struct publication_fixture *fixture,
			 unsigned wanted)
{
	while (atomic_load(&fixture->command) < wanted)
		thrd_yield();
}


static int publication_worker(void *arg)
{
	struct publication_fixture *fixture = arg;
	int err;

	bundle_publication_lock(fixture->publication);
	err = bundle_transport_activate(fixture->transportv[0],
					fixture->generationv[0]);
	atomic_store(&fixture->worker_err, err);
	atomic_store(&fixture->stage, 1);
	wait_command(fixture, 1);
	/* Keep an RX callback waiting between the two route swaps. */
	sys_msleep(30);
	if (!err)
		err = bundle_transport_activate(fixture->transportv[1],
						fixture->generationv[1]);
	atomic_store(&fixture->worker_err, err);
	bundle_publication_unlock(fixture->publication);
	atomic_store(&fixture->stage, 2);

	wait_command(fixture, 2);
	bundle_publication_lock(fixture->publication);
	if (!err)
		err = bundle_transport_rollback(fixture->transportv[0],
						fixture->generationv[0]);
	atomic_store(&fixture->worker_err, err);
	atomic_store(&fixture->stage, 3);
	wait_command(fixture, 3);
	/* Rollback must have the same visibility boundary as activation. */
	sys_msleep(30);
	if (!err)
		err = bundle_transport_rollback(fixture->transportv[1],
						fixture->generationv[1]);
	atomic_store(&fixture->worker_err, err);
	bundle_publication_unlock(fixture->publication);
	atomic_store(&fixture->stage, 4);
	return err;
}


static int start_worker(thrd_t *thread, struct publication_fixture *f)
{
	return thrd_create(thread, publication_worker, f) != thrd_success;
}


static int wait_stage(const struct publication_fixture *fixture,
		      unsigned wanted)
{
	uint64_t deadline = tmr_jiffies() + 1000;

	while (atomic_load(&fixture->stage) < wanted) {
		if (tmr_jiffies() >= deadline)
			return ETIMEDOUT;
		thrd_yield();
	}
	return atomic_load(&fixture->worker_err);
}


static int send_probe(struct udp_sock *sender, const struct sa *destination)
{
	static const uint8_t rtp[] = {
		0x80, 0x60, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x01,
	};
	struct mbuf *mb = mbuf_alloc(sizeof(rtp));
	int err;

	if (!mb)
		return ENOMEM;
	err = mbuf_write_mem(mb, rtp, sizeof(rtp));
	mb->pos = 0;
	if (!err)
		err = udp_send(sender, destination, mb);
	mem_deref(mb);
	return err;
}


static void publication_cancel(void *arg)
{
	(void)arg;
	re_cancel();
}


int test_bundle_stream_teardown(void)
{
	enum { BUNDLE_LAYER = 40, TEST_LOWER_LAYER = 30 };
	struct stream_param prm = {
		.use_rtp = true,
		.rtcp_mux = true,
		.af = AF_INET,
		.cname = "bundle-teardown-test",
	};
	struct teardown_fixture active = {0};
	struct teardown_fixture control = {0};
	struct teardown_sender sender = {0};
	struct udp_helper *blocker = NULL;
	struct sdp_session *sdp = NULL;
	struct stream *streamv[3] = {0};
	struct list streams = LIST_INIT;
	struct config_avt cfg = conf_config()->avt;
	struct sa local;
	thrd_t sender_thread;
	thrd_t control_thread;
	thrd_t active_thread;
	bool sender_started = false;
	bool control_started = false;
	bool active_started = false;
	int sender_result = 0;
	int control_result = 0;
	int active_result = 0;
	int err;

	cfg.rtcp_mux = true;
	sa_set_str(&local, "127.0.0.1", 0);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	for (size_t i = 0; i < RE_ARRAY_SIZE(streamv); ++i) {
		err = stream_alloc(&streamv[i], &streams, &prm, &cfg, sdp,
				   i ? MEDIA_VIDEO : MEDIA_AUDIO,
				   NULL, NULL, NULL, NULL, true,
				   teardown_rtp_handler, NULL,
				   teardown_pt_handler, NULL);
		TEST_ERR(err);
		err = stream_bundle_init(streamv[i], false);
		TEST_ERR(err);
		bundle_set_state(stream_bundle(streamv[i]),
				 i ? BUNDLE_MUX : BUNDLE_BASE);
		err = bundle_start_socket(stream_bundle(streamv[i]),
				rtp_sock(stream_rtp_sock(streamv[i])),
				&streams);
		TEST_ERR(err);
		ASSERT_TRUE(udp_helper_find(
			rtp_sock(stream_rtp_sock(streamv[i])),
			BUNDLE_LAYER) != NULL);
	}

	/* The lower helper pauses inside the mux stream's legacy BUNDLE send
	 * callback, after that callback has read the shared stream list and found
	 * the base endpoint. */
	err = udp_register_helper(
		&blocker, rtp_sock(stream_rtp_sock(streamv[0])),
		TEST_LOWER_LAYER, teardown_blocking_send, NULL, &active);
	TEST_ERR(err);
	active.stream = &streamv[1];
	control.stream = &streamv[2];
	sender.sock = rtp_sock(stream_rtp_sock(streamv[1]));
	sa_set_str(&sender.destination, "127.0.0.1", 9);
	err = thrd_create(&sender_thread, teardown_send_worker, &sender) ==
		thrd_success ? 0 : ENOMEM;
	TEST_ERR(err);
	sender_started = true;
	err = wait_atomic_bool(&active.callback_entered, true, 1000);
	TEST_ERR(err);

	/* Negative control: a different stream owns a different helper, so its
	 * stop/destruction must not wait for the in-flight callback. */
	err = thrd_create(&control_thread, teardown_stream_worker, &control) ==
		thrd_success ? 0 : ENOMEM;
	TEST_ERR(err);
	control_started = true;
	err = wait_atomic_bool(&control.teardown_done, true, 1000);
	TEST_ERR(err);
	(void)thrd_join(control_thread, &control_result);
	control_started = false;
	TEST_ERR(control_result);
	ASSERT_TRUE(streamv[2] == NULL);

	/* Destroying the callback's stream cannot finish until the helper has
	 * quiesced.  This is the lifetime edge ASan must be able to exercise. */
	err = thrd_create(&active_thread, teardown_stream_worker, &active) ==
		thrd_success ? 0 : ENOMEM;
	TEST_ERR(err);
	active_started = true;
	err = wait_atomic_bool(&active.teardown_started, true, 1000);
	TEST_ERR(err);
	for (unsigned i = 0; i < 1000; ++i)
		thrd_yield();
	ASSERT_TRUE(!atomic_load(&active.teardown_done));

	atomic_store(&active.callback_release, true);
	(void)thrd_join(sender_thread, &sender_result);
	sender_started = false;
	TEST_ERR(sender_result);
	(void)thrd_join(active_thread, &active_result);
	active_started = false;
	TEST_ERR(active_result);
	ASSERT_TRUE(streamv[1] == NULL);

out:
	atomic_store(&active.callback_release, true);
	if (sender_started)
		(void)thrd_join(sender_thread, &sender_result);
	if (control_started)
		(void)thrd_join(control_thread, &control_result);
	if (active_started)
		(void)thrd_join(active_thread, &active_result);
	mem_deref(blocker);
	for (size_t i = 0; i < RE_ARRAY_SIZE(streamv); ++i) {
		stream_stop(streamv[i]);
		streamv[i] = mem_deref(streamv[i]);
	}
	mem_deref(sdp);
	return err;
}


int test_bundle_self_callback_teardown(void)
{
	enum { BUNDLE_LAYER = 40, TEST_LOWER_LAYER = 30 };
	struct stream_param prm = {
		.use_rtp = true,
		.rtcp_mux = true,
		.af = AF_INET,
		.cname = "bundle-self-teardown-test",
	};
	struct teardown_fixture fixture = {0};
	struct udp_helper *blocker = NULL;
	struct udp_sock *mux_sock = NULL;
	struct mbuf *packet = NULL;
	struct sdp_session *sdp = NULL;
	struct stream *streamv[2] = {0};
	struct list streams = LIST_INIT;
	struct config_avt cfg = conf_config()->avt;
	struct sa local;
	struct sa destination;
	int err;

	cfg.rtcp_mux = true;
	sa_set_str(&local, "127.0.0.1", 0);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	for (size_t i = 0; i < RE_ARRAY_SIZE(streamv); ++i) {
		err = stream_alloc(&streamv[i], &streams, &prm, &cfg, sdp,
				   i ? MEDIA_VIDEO : MEDIA_AUDIO,
				   NULL, NULL, NULL, NULL, true,
				   teardown_rtp_handler, NULL,
				   teardown_pt_handler, NULL);
		TEST_ERR(err);
		err = stream_bundle_init(streamv[i], false);
		TEST_ERR(err);
		bundle_set_state(stream_bundle(streamv[i]),
				 i ? BUNDLE_MUX : BUNDLE_BASE);
		err = bundle_start_socket(stream_bundle(streamv[i]),
				rtp_sock(stream_rtp_sock(streamv[i])),
				&streams);
		TEST_ERR(err);
	}

	fixture.stream = &streamv[1];
	err = udp_register_helper(
		&blocker, rtp_sock(stream_rtp_sock(streamv[0])),
		TEST_LOWER_LAYER, teardown_self_stop_send, NULL, &fixture);
	TEST_ERR(err);
	mux_sock = mem_ref(rtp_sock(stream_rtp_sock(streamv[1])));
	ASSERT_TRUE(udp_helper_find(mux_sock, BUNDLE_LAYER) != NULL);
	sa_set_str(&destination, "127.0.0.1", 9);
	packet = mbuf_alloc(1);
	ASSERT_TRUE(packet != NULL);
	err = mbuf_write_u8(packet, 0x42);
	TEST_ERR(err);
	packet->pos = 0;

	/* The downstream helper tears down the mux stream while its own BUNDLE
	 * helper is still on this thread's traversal stack.  Quiesce reports
	 * EDEADLK, but traversal's helper/socket pins and the callback's bundle pin
	 * make direct final release safe.  No deferred allocation is permitted. */
	mem_threshold_set(0);
	err = udp_send(mux_sock, &destination, packet);
	mem_threshold_set(-1);
	TEST_ERR(err);
	ASSERT_TRUE(atomic_load(&fixture.callback_entered));
	ASSERT_TRUE(atomic_load(&fixture.teardown_done));
	ASSERT_TRUE(streamv[1] == NULL);
	ASSERT_EQ(1, (int)atomic_load(&fixture.callback_calls));
	ASSERT_TRUE(udp_helper_find(mux_sock, BUNDLE_LAYER) == NULL);

	/* The terminal unlink is complete before traversal returns.  A later send
	 * cannot reach the withdrawn stream/list endpoints. */
	err = send_probe(mux_sock, &destination);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)atomic_load(&fixture.callback_calls));

out:
	mem_threshold_set(-1);
	mem_deref(packet);
	mem_deref(blocker);
	mem_deref(mux_sock);
	for (size_t i = 0; i < RE_ARRAY_SIZE(streamv); ++i) {
		stream_stop(streamv[i]);
		streamv[i] = mem_deref(streamv[i]);
	}
	mem_deref(sdp);
	return err;
}


int test_bundle_wire_callback_teardown(void)
{
	enum { BUNDLE_LAYER = 40 };
	struct wire_teardown_worker worker = {0};
	struct bundle_publication *publication = NULL;
	struct bundle_transport *transport = NULL;
	struct bundle_group *group = NULL;
	struct udp_sock *sock = NULL;
	struct list streams = LIST_INIT;
	struct sa local;
	struct sa remote;
	thrd_t thread;
	uint64_t generation;
	bool publication_locked = false;
	bool thread_started = false;
	int thread_result = 0;
	int err;

	err = bundle_group_singleton(&group, "0");
	TEST_ERR(err);
	err = bundle_publication_alloc(&publication);
	TEST_ERR(err);
	sa_set_str(&local, "127.0.0.1", 0);
	err = udp_listen(&sock, &local, NULL, NULL);
	TEST_ERR(err);
	err = bundle_transport_alloc(&transport, group, &streams, NULL);
	TEST_ERR(err);
	err = bundle_transport_bind_publication(transport, publication);
	TEST_ERR(err);
	err = bundle_transport_prepare(transport, group, sock, &generation);
	TEST_ERR(err);
	sa_set_str(&remote, "127.0.0.1", 9);
	err = bundle_transport_set_remote(transport, generation, &remote);
	TEST_ERR(err);
	err = bundle_transport_activate(transport, generation);
	TEST_ERR(err);
	err = bundle_transport_finalize(transport, generation);
	TEST_ERR(err);
	ASSERT_TRUE(udp_helper_find(sock, BUNDLE_LAYER) != NULL);

	/* Hold the publication gate so the exact-wire callback pins wire and
	 * transport, then blocks before observing route state. */
	bundle_publication_lock(publication);
	publication_locked = true;
	worker.sock = sock;
	sa_set_str(&worker.source, "127.0.0.1", 5000);
	err = thrd_create(&thread, wire_teardown_recv_worker, &worker) ==
		thrd_success ? 0 : ENOMEM;
	TEST_ERR(err);
	thread_started = true;
	err = wait_atomic_bool(&worker.started, true, 1000);
	TEST_ERR(err);
	{
		const uint64_t deadline = tmr_jiffies() + 1000;

		while (mem_nrefs(transport) < 2 && tmr_jiffies() < deadline)
			thrd_yield();
		ASSERT_TRUE(mem_nrefs(transport) >= 2);
	}

	/* Remove the last external owner.  Direct callback_deref must safely run
	 * both transport and wire destructors as the callback unwinds; traversal's
	 * helper/socket refs remain valid until it returns. */
	transport = mem_deref(transport);
	bundle_publication_unlock(publication);
	publication_locked = false;
	(void)thrd_join(thread, &thread_result);
	thread_started = false;
	TEST_ERR(thread_result);
	ASSERT_TRUE(atomic_load(&worker.done));
	ASSERT_TRUE(udp_helper_find(sock, BUNDLE_LAYER) == NULL);

out:
	if (publication_locked)
		bundle_publication_unlock(publication);
	if (thread_started)
		(void)thrd_join(thread, &thread_result);
	mem_deref(transport);
	mem_deref(sock);
	mem_deref(publication);
	mem_deref(group);
	return err;
}


int test_bundle_atomic_publication(void)
{
	struct publication_fixture fixture = {0};
	struct publication_receiver receiverv[2];
	struct bundle_group *groupv[2] = {0};
	struct udp_sock *sockv[2] = {0};
	struct udp_sock *sender = NULL;
	struct list streams = LIST_INIT;
	struct sa local;
	struct sa destinationv[2];
	struct tmr cancel_tmr = {0};
	thrd_t worker;
	bool worker_started = false;
	int worker_result = 0;
	int err;

	err = bundle_group_singleton(&groupv[0], "0");
	TEST_ERR(err);
	err = bundle_group_singleton(&groupv[1], "1");
	TEST_ERR(err);
	err = bundle_publication_alloc(&fixture.publication);
	TEST_ERR(err);

	sa_set_str(&local, "127.0.0.1", 0);
	for (size_t i = 0; i < RE_ARRAY_SIZE(sockv); ++i) {
		receiverv[i].fixture = &fixture;
		receiverv[i].index = (unsigned)i;
		err = udp_listen(&sockv[i], &local, publication_udp_recv,
				 &receiverv[i]);
		TEST_ERR(err);
		err = udp_local_get(sockv[i], &destinationv[i]);
		TEST_ERR(err);
		err = bundle_transport_alloc(&fixture.transportv[i], groupv[i],
					     &streams, NULL);
		TEST_ERR(err);
		err = bundle_transport_bind_publication(fixture.transportv[i],
						fixture.publication);
		TEST_ERR(err);
		err = bundle_transport_prepare(fixture.transportv[i],
					       groupv[i], sockv[i],
					       &fixture.generationv[i]);
		TEST_ERR(err);
		err = bundle_transport_set_remote(fixture.transportv[i],
					  fixture.generationv[i],
					  &destinationv[i]);
		TEST_ERR(err);
	}
	err = udp_listen(&sender, &local, NULL, NULL);
	TEST_ERR(err);

	err = start_worker(&worker, &fixture);
	if (err)
		err = ENOMEM;
	TEST_ERR(err);
	worker_started = true;
	err = wait_stage(&fixture, 1);
	TEST_ERR(err);
	err = send_probe(sender, &destinationv[0]);
	TEST_ERR(err);
	err = send_probe(sender, &destinationv[1]);
	TEST_ERR(err);
	atomic_store(&fixture.command, 1);
	tmr_start(&cancel_tmr, 100, publication_cancel, NULL);
	err = re_main_timeout(250);
	tmr_cancel(&cancel_tmr);
	TEST_ERR(err);
	err = wait_stage(&fixture, 2);
	TEST_ERR(err);
	ASSERT_EQ(0, (int)atomic_load(&fixture.received[0]));
	ASSERT_EQ(0, (int)atomic_load(&fixture.received[1]));
	ASSERT_EQ((int)fixture.generationv[0],
		  (int)bundle_transport_active_generation(
			  fixture.transportv[0]));
	ASSERT_EQ((int)fixture.generationv[1],
		  (int)bundle_transport_active_generation(
			  fixture.transportv[1]));

	atomic_store(&fixture.command, 2);
	err = wait_stage(&fixture, 3);
	TEST_ERR(err);
	err = send_probe(sender, &destinationv[0]);
	TEST_ERR(err);
	err = send_probe(sender, &destinationv[1]);
	TEST_ERR(err);
	atomic_store(&fixture.command, 3);
	err = re_main_timeout(1000);
	TEST_ERR(err);
	err = wait_stage(&fixture, 4);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)atomic_load(&fixture.received[0]));
	ASSERT_EQ(1, (int)atomic_load(&fixture.received[1]));
	ASSERT_EQ(0, (int)bundle_transport_active_generation(
		fixture.transportv[0]));
	ASSERT_EQ(0, (int)bundle_transport_active_generation(
		fixture.transportv[1]));

out:
	tmr_cancel(&cancel_tmr);
	atomic_store(&fixture.command, 3);
	if (worker_started) {
		(void)thrd_join(worker, &worker_result);
		if (!err && worker_result)
			err = worker_result;
	}
	mem_deref(sender);
	for (size_t i = 0; i < RE_ARRAY_SIZE(sockv); ++i) {
		mem_deref(fixture.transportv[i]);
		mem_deref(sockv[i]);
		mem_deref(groupv[i]);
	}
	mem_deref(fixture.publication);
	return err;
}
