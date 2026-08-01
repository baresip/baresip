/**
 * @file media_transport.c  Session transport adoption tests
 */
#include <errno.h>
#include <re.h>
#include <baresip.h>
#include "core.h"
#include "test.h"


void mock_mnat_register(struct list *mnatl);
void mock_mnat_unregister(void);
void mock_mnat_media_gather_defer(bool defer);
void mock_mnat_media_gather_result(int err);
void mock_mnat_complete_media_gathers(void);
unsigned mock_mnat_media_gather_cancel_count(void);
unsigned mock_mnat_media_gather_callback_count(void);
uint64_t mock_mnat_media_generation(const struct mnat_media *m);
unsigned mock_mnat_media_attempt_start_count(void);


struct fake_transport {
	struct menc_transport_binding binding;
	struct menc_transport_state state;
	struct restart_trace *restart_trace;
	struct udp_sock *sock;
	struct sdp_media *sdpm;
	menc_transport_estab_h *estabh;
	void *arg;
	struct sa peer;
	unsigned sends;
	unsigned prepares;
	unsigned peer_queries;
	unsigned peer_sets;
	bool fail_peer_set;
	bool binding_owned;
};


enum restart_event {
	RESTART_PEER_QUERY = 1,
	RESTART_PREPARE_OLD_INACTIVE,
	RESTART_PREPARE_CANDIDATE_ACTIVE,
	RESTART_PEER_SET_CANDIDATE,
	RESTART_ACTIVATE_CANDIDATE,
	RESTART_ACTIVATE_OLD,
	RESTART_PEER_SET_OLD,
	RESTART_ROLLBACK_OLD,
	RESTART_ROLLBACK_CANDIDATE,
	RESTART_FINALIZE_CANDIDATE,
	RESTART_FINALIZE_OLD,
	RESTART_ABORT_CANDIDATE,
	RESTART_ABORT_OLD,
};


struct restart_trace {
	enum restart_event events[32];
	size_t eventc;
	struct udp_sock *expected_sock;
	struct udp_sock *restart_sock;
	struct sa old_peer;
	struct sa candidate_peer;
	struct fake_restart_media *candidate_media;
	unsigned media_destructors[2];
};


struct fake_restart_media {
	struct restart_trace *trace;
	unsigned generation;
	bool active;
	bool previous_active;
	bool prepared;
	bool target_active;
	unsigned prepares;
	unsigned activates;
	unsigned rollbacks;
	unsigned finalizes;
	unsigned aborts;
};


struct restart_fixture {
	struct restart_trace trace;
	struct fake_restart_media *old_media;
	struct fake_restart_media *candidate_media;
	struct mnat_media *published_media;
	struct fake_transport *transport;
	struct media_transport *active;
	struct media_transport *candidate;
	struct bundle_transport *route;
	struct bundle_group *group;
	struct sdp_session *sdp;
	struct sdp_media *sdpm;
	struct udp_sock *sock;
	struct mnat_sess *mnats;
	struct menc_sess *mencs;
	struct list streams;
	struct list destinations;
};


static void restart_event(struct restart_trace *trace,
			  enum restart_event event)
{
	if (trace && trace->eventc < RE_ARRAY_SIZE(trace->events))
		trace->events[trace->eventc++] = event;
}


static void fake_restart_media_destructor(void *arg)
{
	struct fake_restart_media *media = arg;

	if (media->trace)
		++media->trace->media_destructors[media->generation];
}


static int fake_restart_alloc(struct mnat_media **candidatep,
			      struct mnat_media *active, struct udp_sock *sock,
			      struct sdp_media *sdpm,
			      mnat_connected_h *connh, void *arg)
{
	struct fake_restart_media *old = (struct fake_restart_media *)active;
	struct fake_restart_media *candidate;

	(void)sdpm;
	(void)connh;
	(void)arg;
	if (!candidatep || !old || !old->trace)
		return EINVAL;
	old->trace->restart_sock = sock;
	candidate = mem_zalloc(sizeof(*candidate),
			       fake_restart_media_destructor);
	if (!candidate)
		return ENOMEM;
	candidate->trace = old->trace;
	candidate->generation = 1;
	old->trace->candidate_media = candidate;
	*candidatep = (struct mnat_media *)candidate;
	return 0;
}


static int fake_restart_prepare(struct mnat_media *opaque, bool active)
{
	struct fake_restart_media *media = (struct fake_restart_media *)opaque;

	if (!media || media->prepared)
		return EALREADY;
	media->previous_active = media->active;
	media->target_active = active;
	media->prepared = true;
	++media->prepares;
	restart_event(media->trace,
		media->generation ? RESTART_PREPARE_CANDIDATE_ACTIVE
				  : RESTART_PREPARE_OLD_INACTIVE);
	return 0;
}


static void fake_restart_activate(struct mnat_media *opaque)
{
	struct fake_restart_media *media = (struct fake_restart_media *)opaque;

	media->active = media->target_active;
	++media->activates;
	restart_event(media->trace, media->generation
		? RESTART_ACTIVATE_CANDIDATE : RESTART_ACTIVATE_OLD);
}


static void fake_restart_rollback(struct mnat_media *opaque)
{
	struct fake_restart_media *media = (struct fake_restart_media *)opaque;

	media->active = media->previous_active;
	++media->rollbacks;
	restart_event(media->trace, media->generation
		? RESTART_ROLLBACK_CANDIDATE : RESTART_ROLLBACK_OLD);
}


static void fake_restart_finalize(struct mnat_media *opaque)
{
	struct fake_restart_media *media = (struct fake_restart_media *)opaque;

	media->prepared = false;
	++media->finalizes;
	restart_event(media->trace, media->generation
		? RESTART_FINALIZE_CANDIDATE : RESTART_FINALIZE_OLD);
}


static void fake_restart_abort(struct mnat_media *opaque)
{
	struct fake_restart_media *media = (struct fake_restart_media *)opaque;

	media->active = media->previous_active;
	media->prepared = false;
	++media->aborts;
	restart_event(media->trace, media->generation
		? RESTART_ABORT_CANDIDATE : RESTART_ABORT_OLD);
}


static bool fake_restart_gathered(const struct mnat_media *opaque)
{
	return opaque != NULL;
}


static int fake_restart_attempt(struct mnat_media *opaque,
				mnat_media_attempt_h *attempth, void *arg)
{
	(void)opaque;
	(void)attempth;
	(void)arg;
	return EAGAIN;
}


static void fake_restart_cancel(struct mnat_media *opaque)
{
	(void)opaque;
}


static void fake_mnat_publish(struct mnat_media *media, void *arg)
{
	struct restart_fixture *fixture = arg;

	fixture->published_media = media;
}


struct callback_owner {
	unsigned *destroyed;
	unsigned *callbacks;
	bool *live_arg;
	uint32_t magic;
};


struct bootstrap_state {
	struct pc_transport_session **sessionp;
	const struct pc_transport_generation *last_published;
	unsigned published;
	unsigned errors;
	int last_error;
	bool destroy_session;
};


struct destructive_transport_state {
	struct media_transport **transportp;
	unsigned callbacks;
};


static void destructive_publish(
	const struct pc_transport_generation *generation, void *arg)
{
	struct bootstrap_state *state = arg;

	if (generation)
		++state->published;
	state->last_published = generation;
	if (state->destroy_session)
		*state->sessionp = mem_deref(*state->sessionp);
}


static void coordinator_error(int err, void *arg)
{
	struct bootstrap_state *state = arg;

	++state->errors;
	state->last_error = err;
}


static void coordinator_rtp_handler(const struct rtp_header *hdr,
	struct rtpext *extv, size_t extc, struct mbuf *mb, unsigned lostc,
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


static int coordinator_pt_handler(uint8_t pt, struct mbuf *mb, void *arg)
{
	(void)pt;
	(void)mb;
	(void)arg;
	return 0;
}


static void callback_owner_destructor(void *arg)
{
	struct callback_owner *owner = arg;

	++*owner->destroyed;
}


static void *binding_arg_ref(void *arg)
{
	return mem_ref(arg);
}


static void binding_arg_deref(void *arg)
{
	mem_deref(arg);
}


static void fake_transport_destructor(void *arg)
{
	struct fake_transport *transport = arg;

	if (transport->binding_owned && transport->binding.arg_deref &&
	    transport->binding.arg)
		transport->binding.arg_deref(transport->binding.arg);
	mem_deref(transport->sdpm);
	mem_deref(transport->sock);
}


static int fake_alloc(struct menc_transport **mtp, struct menc_sess *sess,
		      struct udp_sock *sock, const struct sa *remote,
		      struct sdp_media *sdpm, bool offerer,
		      menc_transport_recv_h *recvh,
		      menc_transport_estab_h *estabh,
		      menc_transport_close_h *closeh, void *arg)
{
	struct fake_transport *transport;

	(void)sess;
	(void)remote;
	(void)offerer;
	(void)recvh;
	(void)closeh;
	if (!mtp || !sock || !sdpm)
		return EINVAL;
	transport = mem_zalloc(sizeof(*transport), fake_transport_destructor);
	if (!transport)
		return ENOMEM;
	transport->sock = mem_ref(sock);
	transport->sdpm = mem_ref(sdpm);
	transport->estabh = estabh;
	transport->arg = arg;
	*mtp = (struct menc_transport *)transport;
	return 0;
}


static int fake_commit_identity(struct menc_transport *opaque)
{
	static unsigned generation;
	struct fake_transport *transport = (struct fake_transport *)opaque;

	return transport && transport->sdpm
		? sdp_media_set_lattr(transport->sdpm, true, "tls-id",
				      "fresh-fake-%u", ++generation)
		: EINVAL;
}


static int fake_start(struct menc_transport *opaque, const struct sa *remote)
{
	struct fake_transport *transport = (struct fake_transport *)opaque;

	if (!transport || !remote)
		return EINVAL;
	sa_cpy(&transport->peer, remote);
	transport->state.remote = *remote;
	transport->state.remote_set = true;
	transport->state.started = true;
	transport->state.established = true;
	transport->state.local_role = MENC_DTLS_ROLE_SERVER;
	if (transport->estabh)
		transport->estabh(0, MENC_DTLS_ROLE_SERVER, transport->arg);
	return 0;
}


struct attr_copy {
	char *value;
	size_t size;
};


static bool attr_copy_handler(const char *name, const char *value, void *arg)
{
	struct attr_copy *copy = arg;

	(void)name;
	re_snprintf(copy->value, copy->size, "%s", value);
	return true;
}


static int copy_local_attr(const struct sdp_media *sdpm, const char *name,
			   char *value, size_t size)
{
	struct attr_copy copy = {.value = value, .size = size};

	if (!sdpm || !name || !value || !size)
		return EINVAL;
	value[0] = '\0';
	sdp_media_lattr_apply(sdpm, name, attr_copy_handler, &copy);
	return value[0] ? 0 : ENOENT;
}


static int fake_rebind(struct menc_transport *opaque,
		       const struct menc_transport_binding *binding,
		       void *expected_arg,
		       struct menc_transport_binding *previous,
		       struct menc_transport_state *state)
{
	struct fake_transport *transport = (struct fake_transport *)opaque;
	struct menc_transport_binding old;
	void *new_arg = binding->arg;
	bool new_owned = false;

	if (expected_arg && transport->binding.arg != expected_arg)
		return ESTALE;
	old = transport->binding;
	if (previous) {
		*previous = old;
		if (previous->arg_ref && previous->arg)
			previous->arg = previous->arg_ref(previous->arg);
	}
	if (state)
		*state = transport->state;
	transport->binding = *binding;
	if (binding->arg_ref && binding->arg && binding->arg_deref) {
		new_arg = binding->arg_ref(binding->arg);
		new_owned = true;
	}
	transport->binding.arg = new_arg;
	if (transport->binding_owned && old.arg_deref && old.arg)
		old.arg_deref(old.arg);
	transport->binding_owned = new_owned;
	return 0;
}


static int fake_send(struct menc_transport *opaque, struct mbuf *mb)
{
	struct fake_transport *transport = (struct fake_transport *)opaque;

	(void)mb;
	++transport->sends;
	return 0;
}


static int fake_peer_set(struct menc_transport *opaque,
			 const struct sa *peer, struct sa *old_peer)
{
	struct fake_transport *transport = (struct fake_transport *)opaque;

	if (!peer) {
		if (!old_peer)
			return EINVAL;
		++transport->peer_queries;
		sa_cpy(old_peer, &transport->peer);
		restart_event(transport->restart_trace, RESTART_PEER_QUERY);
		return 0;
	}
	++transport->peer_sets;
	if (transport->fail_peer_set) {
		transport->fail_peer_set = false;
		return EIO;
	}
	if (old_peer)
		sa_cpy(old_peer, &transport->peer);
	restart_event(transport->restart_trace,
		sa_cmp(peer, &transport->restart_trace->candidate_peer, SA_ALL)
			? RESTART_PEER_SET_CANDIDATE : RESTART_PEER_SET_OLD);
	sa_cpy(&transport->peer, peer);
	return 0;
}


static int fake_members_prepare(struct menc_transport *opaque)
{
	struct fake_transport *transport = (struct fake_transport *)opaque;

	++transport->prepares;
	return 0;
}


static void original_recv(struct mbuf *mb, void *arg)
{
	(void)mb;
	(void)arg;
}


static void counted_recv(struct mbuf *mb, void *arg)
{
	unsigned *count = arg;

	(void)mb;
	++*count;
}


static void counted_change(void *arg)
{
	unsigned *count = arg;

	++*count;
}


static void destructive_transport_estab(int err, enum menc_dtls_role role,
					void *arg)
{
	struct destructive_transport_state *state = arg;

	(void)err;
	(void)role;
	++state->callbacks;
	media_transport_release(*state->transportp);
	*state->transportp = mem_deref(*state->transportp);
}


static void destructive_transport_close(int err, void *arg)
{
	struct destructive_transport_state *state = arg;

	(void)err;
	++state->callbacks;
	media_transport_release(*state->transportp);
	*state->transportp = mem_deref(*state->transportp);
}


static void terminal_estab(int err, enum menc_dtls_role role, void *arg)
{
	struct callback_owner *owner = arg;

	(void)role;
	if (owner && owner->magic == 0xcab00d1e && err == ECANCELED)
		*owner->live_arg = true;
	++*owner->callbacks;
}


#ifdef USE_DATACHANNEL
static int test_shared_lower_prepared_abort(void)
{
	struct mnat mnat = {.id = "shared-lower-abort"};
	struct menc menc = {
		.id = "shared-lower-abort",
		.transporth = fake_alloc,
		.transportsendh = fake_send,
		.transportrebindh = fake_rebind,
		.transportmembersprepareh = fake_members_prepare,
	};
	struct stream_param stream_prm = {
		.use_rtp = true,
		.rtcp_mux = true,
		.af = AF_INET,
		.cname = "shared-lower-abort",
	};
	struct media_transport_prm consumer = {0};
	struct config_avt cfg = conf_config()->avt;
	struct data_context *ctx = NULL;
	struct transport_binding *binding = NULL;
	struct media_transport *candidate = NULL;
	struct bundle_transport *route = NULL;
	struct bundle_group *group = NULL;
	struct fake_transport *fake = NULL;
	struct sdp_session *sdp = NULL;
	struct stream *stream = NULL;
	struct menc_sess *mencs = NULL;
	struct udp_sock *sock = NULL;
	struct list streams = LIST_INIT;
	struct sa local;
	struct sa remote;
	uint64_t route_generation = 0;
	unsigned *callbacks = NULL;
	int err;

	cfg.rtcp_mux = true;
	sa_set_str(&local, "127.0.0.1", 0);
	sa_set_str(&remote, "127.0.0.1", 5000);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	err = stream_alloc(&stream, &streams, &stream_prm, &cfg, sdp,
			   MEDIA_AUDIO, NULL, NULL, NULL, NULL, true,
			   coordinator_rtp_handler, NULL,
			   coordinator_pt_handler, NULL);
	TEST_ERR(err);
	err = sdp_format_add(NULL, stream_sdpmedia(stream), false, "0",
			     "PCMU", 8000, 1, NULL, NULL, NULL, false,
			     NULL);
	TEST_ERR(err);
	mencs = mem_zalloc(1, NULL);
	ASSERT_TRUE(mencs != NULL);
	err = data_context_alloc(&ctx, sdp, &mnat, NULL, &menc, mencs,
				 stream, &streams, AF_INET, false, NULL,
				 NULL);
	TEST_ERR(err);
	err = bundle_group_singleton(&group, stream_mid(stream));
	TEST_ERR(err);
	sock = mem_ref(rtp_sock(stream_rtp_sock(stream)));
	err = bundle_transport_alloc(&route, group, &streams,
				     data_context_mid(ctx));
	TEST_ERR(err);
	err = bundle_transport_prepare(route, group, sock,
				       &route_generation);
	TEST_ERR(err);
	err = bundle_transport_set_remote(route, route_generation, &remote);
	TEST_ERR(err);

	callbacks = mem_zalloc(sizeof(*callbacks), NULL);
	fake = mem_zalloc(sizeof(*fake), fake_transport_destructor);
	ASSERT_TRUE(callbacks != NULL);
	ASSERT_TRUE(fake != NULL);
	fake->binding.recvh = counted_recv;
	fake->binding.arg = mem_ref(callbacks);
	fake->binding.arg_ref = binding_arg_ref;
	fake->binding.arg_deref = binding_arg_deref;
	fake->binding_owned = true;
	fake->state.remote = remote;
	fake->state.remote_set = true;
	fake->state.started = true;
	fake->state.established = true;
	fake->state.local_role = MENC_DTLS_ROLE_CLIENT;
	stream_set_menc_transport(stream, (struct menc_transport *)fake);

	consumer.group = group;
	consumer.transport_sdpm = stream_sdpmedia(stream);
	consumer.streaml = &streams;
	consumer.data_mid = data_context_mid(ctx);
	consumer.semantic_key = "shared-lower-abort";
	consumer.mnat = &mnat;
	consumer.menc = &menc;
	consumer.mencs = mencs;
	consumer.af = AF_INET;
	err = data_context_media_binding_alloc(&binding, ctx, &consumer);
	TEST_ERR(err);
	err = media_transport_adopt_pending(
		&candidate, &consumer, sock, NULL,
		(struct menc_transport *)fake, route, route_generation);
	TEST_ERR(err);
	err = data_context_media_binding_attach(binding, candidate);
	TEST_ERR(err);
	ASSERT_TRUE(fake->binding.recvh != counted_recv);

	/* Aborting an unpublished data consumer must detach that consumer without
	 * retiring the established lower's prior callback binding. */
	media_transport_abort(candidate);
	data_context_media_binding_abort(binding);
	binding = NULL;
	candidate = mem_deref(candidate);
	ASSERT_TRUE(fake->binding.recvh == counted_recv);
	fake->binding.recvh(NULL, fake->binding.arg);
	ASSERT_EQ(1, (int)*callbacks);

	/* A committed session-owned binding is terminal, in contrast.  Build a
	 * second generation on the same lower, publish both halves, then break only
	 * the consumer's callback reference as the coordinator does during entry
	 * teardown.  Destroying the owning context must retire the lower callback
	 * binding instead of restoring it. */
	err = bundle_transport_prepare(route, group, sock,
				       &route_generation);
	TEST_ERR(err);
	err = bundle_transport_set_remote(route, route_generation, &remote);
	TEST_ERR(err);
	err = data_context_media_binding_alloc(&binding, ctx, &consumer);
	TEST_ERR(err);
	err = media_transport_adopt_pending(
		&candidate, &consumer, sock, NULL,
		(struct menc_transport *)fake, route, route_generation);
	TEST_ERR(err);
	err = data_context_media_binding_attach(binding, candidate);
	TEST_ERR(err);
	err = data_context_media_binding_prepare(
		binding, MENC_DTLS_ROLE_CLIENT);
	if (err == EAGAIN)
		err = 0;
	TEST_ERR(err);
	err = media_transport_activate(candidate);
	TEST_ERR(err);
	media_transport_finalize(candidate);
	data_context_media_binding_finalize(binding);
	binding = NULL;
	media_transport_detach_consumer(candidate);
	ASSERT_TRUE(fake->binding.recvh != NULL);
	ctx = mem_deref(ctx);
	ASSERT_TRUE(fake->binding.recvh == NULL);
	ASSERT_TRUE(fake->binding.estabh == NULL);
	ASSERT_TRUE(fake->binding.closeh == NULL);

out:
	if (binding)
		data_context_media_binding_abort(binding);
	media_transport_abort(candidate);
	media_transport_release(candidate);
	mem_deref(candidate);
	mem_deref(ctx);
	stream_set_menc_transport(stream, NULL);
	mem_deref(fake);
	mem_deref(callbacks);
	mem_deref(route);
	mem_deref(group);
	mem_deref(sock);
	mem_deref(mencs);
	list_flush(&streams);
	mem_deref(sdp);
	re_fhs_flush();
	return err;
}
#endif


static int planner_role_case(const char *local_setup,
			     const char *remote_setup,
			     enum menc_dtls_role expected,
			     int expected_err)
{
	struct pc_transport_generation *generation = NULL;
	struct pc_transport_generation *ice_restart = NULL;
	struct pc_transport_generation *unresolved = NULL;
	struct pc_transport_data data = {0};
	struct sdp_session *sdp = NULL;
	struct sdp_media *sdpm = NULL;
	struct mbuf *remote = NULL;
	struct list streams = LIST_INIT;
	struct sa local;
	int err;

	sa_set_str(&local, "127.0.0.1", 5000);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	err = sdp_media_add(&sdpm, sdp, "application", 5000,
			    "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_media_set_lattr(sdpm, true, "mid", "data");
	TEST_ERR(err);
	err = sdp_media_set_lattr(sdpm, true, "setup", "%s", local_setup);
	TEST_ERR(err);
	remote = mbuf_alloc(512);
	ASSERT_TRUE(remote != NULL);
	err = mbuf_printf(remote,
		"v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\n"
		"c=IN IP4 127.0.0.1\r\nt=0 0\r\n"
		"m=application 5000 UDP/DTLS/SCTP webrtc-datachannel\r\n"
		"a=mid:data\r\na=setup:%s\r\n", remote_setup);
	TEST_ERR(err);
	remote->pos = 0;
	err = sdp_decode(sdp, remote, true);
	TEST_ERR(err);
	data.mid = "data";
	data.sdpm = sdpm;
	data.socket_identity = sdpm;
	data.accepted = true;
	data.local_sctp_port = 5000;
	data.remote_sctp_port = 5000;
	err = pc_transport_generation_alloc(&generation, sdp, &streams, &data);
	ASSERT_EQ(expected_err, err);
	if (!err) {
		const struct pc_transport_group *active;
		const struct pc_transport_group *restart;

		ASSERT_EQ(1, (int)pc_transport_generation_count(generation));
		ASSERT_EQ(expected,
			  pc_transport_group_role(
				  pc_transport_generation_group(generation, 0)));
		active = pc_transport_generation_group(generation, 0);
		err = sdp_media_set_lattr(sdpm, true, "ice-ufrag",
					   "restart-ufrag");
		err |= sdp_media_set_lattr(sdpm, true, "ice-pwd",
					    "restart-password");
		TEST_ERR(err);
		err = pc_transport_generation_alloc(&ice_restart, sdp, &streams,
						    &data);
		TEST_ERR(err);
		restart = pc_transport_generation_group(ice_restart, 0);
		ASSERT_TRUE(pc_transport_group_reuses(active, restart));
		ASSERT_TRUE(!pc_transport_group_reuses_ice(active, restart));
		ASSERT_TRUE(pc_transport_group_reuses_sctp(active, restart));

		/* A remote actpass re-offer is planned before the local answer has
		 * selected its role.  That provisional UNKNOWN role must reuse the
		 * established DTLS/SCTP association; a resolved conflicting role is
		 * rejected by the coordinator's explicit role check. */
		sdp_media_del_lattr(sdpm, "setup");
		err = pc_transport_generation_alloc(&unresolved, sdp, &streams,
					    &data);
		TEST_ERR(err);
		ASSERT_EQ(MENC_DTLS_ROLE_UNKNOWN,
			  pc_transport_group_role(
				  pc_transport_generation_group(unresolved, 0)));
		ASSERT_TRUE(pc_transport_group_reuses(
			active, pc_transport_generation_group(unresolved, 0)));
		ASSERT_TRUE(pc_transport_group_reuses_sctp(
			active, pc_transport_generation_group(unresolved, 0)));
	}

out:
	mem_deref(unresolved);
	mem_deref(ice_restart);
	mem_deref(generation);
	mem_deref(remote);
	mem_deref(sdp);
	return err == expected_err ? 0 : err;
}


int test_pc_transport_role_resolution(void)
{
	int err;

	err = planner_role_case("passive", "actpass",
				MENC_DTLS_ROLE_SERVER, 0);
	TEST_ERR(err);
	err = planner_role_case("active", "actpass",
				MENC_DTLS_ROLE_CLIENT, 0);
	TEST_ERR(err);
	err = planner_role_case("active", "active",
				MENC_DTLS_ROLE_UNKNOWN, EPROTO);
	TEST_ERR(err);
	err = planner_role_case("passive", "passive",
				MENC_DTLS_ROLE_UNKNOWN, EPROTO);
	TEST_ERR(err);

out:
	return err;
}


int test_media_transport_fresh_gather(void)
{
	static const struct menc menc = {
		.id = "fresh-gather",
		.transporth = fake_alloc,
		.transportcommitidentityh = fake_commit_identity,
		.transportstarth = fake_start,
		.transportsendh = fake_send,
		.transportmembersprepareh = fake_members_prepare,
	};
	struct media_transport_prm prm = {0};
	struct media_transport *success = NULL;
	struct media_transport *failure = NULL;
	struct media_transport *rollback = NULL;
	struct bundle_group *group = NULL;
	struct sdp_session *sdp = NULL;
	struct sdp_media *sdpm = NULL;
	struct mnat_sess *mnats = NULL;
	struct menc_sess *mencs = NULL;
	struct mnat_media *success_mnat = NULL;
	struct mnat_media *failure_mnat = NULL;
	struct udp_sock *success_sock = NULL;
	struct udp_sock *failure_sock = NULL;
	const struct mnat *mnat = NULL;
	struct mbuf *remote = NULL;
	struct list streams = LIST_INIT;
	struct sa local;
	uint16_t stable_port;
	char success_tls_id[64];
	char failure_tls_id[64];
	unsigned callbacks;
	unsigned attempts;
	bool registered = false;
	int err;

	mock_mnat_register(baresip_mnatl());
	registered = true;
	mnat = mnat_find(baresip_mnatl(), "XNAT");
	ASSERT_TRUE(mnat != NULL);
	sa_set_str(&local, "127.0.0.1", 0);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	err = sdp_media_add(&sdpm, sdp, "application", 9,
			    "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_media_set_lattr(sdpm, true, "mid", "data");
	TEST_ERR(err);
	stable_port = sa_port(sdp_media_laddr(sdpm));
	remote = mbuf_alloc(512);
	ASSERT_TRUE(remote != NULL);
	err = mbuf_printf(remote,
		"v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\n"
		"c=IN IP4 127.0.0.1\r\nt=0 0\r\n"
		"m=application 6000 UDP/DTLS/SCTP webrtc-datachannel\r\n"
		"a=mid:data\r\na=setup:actpass\r\n"
		"a=ice-ufrag:remote\r\n"
		"a=ice-pwd:remote-password-0001\r\n");
	TEST_ERR(err);
	remote->pos = 0;
	err = sdp_decode(sdp, remote, true);
	TEST_ERR(err);
	err = bundle_group_singleton(&group, "data");
	TEST_ERR(err);
	err = mnat->sessh(&mnats, mnat, NULL, AF_INET, NULL, NULL, NULL,
			  sdp, false, NULL, NULL);
	TEST_ERR(err);
	mencs = mem_zalloc(1, NULL);
	ASSERT_TRUE(mencs != NULL);
	prm.group = group;
	prm.transport_sdpm = sdpm;
	prm.streaml = &streams;
	prm.data_mid = "data";
	prm.semantic_key = "fresh-before-prepare";
	prm.mnat = mnat;
	prm.mnats = mnats;
	prm.menc = &menc;
	prm.mencs = mencs;
	prm.af = AF_INET;

	/* This is the createAnswer EAGAIN/retry contract at its production
	 * transport boundary: prepare registers exactly one generation-local
	 * waiter, completion makes the same candidate retryable. */
	mock_mnat_media_gather_defer(true);
	err = media_transport_alloc(&success, &prm);
	TEST_ERR(err);
	err = media_transport_prepare(success);
	TEST_ERR(err);
	success_sock = media_transport_socket_ref(success);
	success_mnat = media_transport_mnat_media_ref(success);
	ASSERT_TRUE(success_sock != NULL);
	ASSERT_TRUE(success_mnat != NULL);
	err = copy_local_attr(sdpm, "tls-id", success_tls_id,
			      sizeof(success_tls_id));
	TEST_ERR(err);
	ASSERT_TRUE(!media_transport_gathered(success));
	err = media_transport_gather_start(success);
	ASSERT_EQ(EAGAIN, err);
	err = media_transport_gather_start(success);
	ASSERT_EQ(EAGAIN, err);
	callbacks = mock_mnat_media_gather_callback_count();
	attempts = mock_mnat_media_attempt_start_count();
	mock_mnat_complete_media_gathers();
	ASSERT_EQ(callbacks + 1,
		  mock_mnat_media_gather_callback_count());
	ASSERT_EQ(attempts, mock_mnat_media_attempt_start_count());
	ASSERT_TRUE(media_transport_gathered(success));
	ASSERT_EQ(0, media_transport_error(success));
	/* Publishing the local description/session generation is the explicit
	 * boundary that may emit connectivity checks. */
	err = media_transport_attempt_start(success);
	TEST_ERR(err);
	ASSERT_EQ(attempts + 1, mock_mnat_media_attempt_start_count());
	err = media_transport_rekey(success, "fresh-after-prepare");
	TEST_ERR(err);
	ASSERT_STREQ("fresh-after-prepare", media_transport_key(success));
	media_transport_abort(success);
	ASSERT_EQ(stable_port, sa_port(sdp_media_laddr(sdpm)));

	/* A terminal gather callback is not flattened into perpetual EAGAIN.
	 * Aborting the failed provisional lower restores the stable SDP and leaves
	 * the previously prepared object untouched. */
	err = media_transport_alloc(&failure, &prm);
	TEST_ERR(err);
	err = media_transport_prepare(failure);
	TEST_ERR(err);
	failure_sock = media_transport_socket_ref(failure);
	failure_mnat = media_transport_mnat_media_ref(failure);
	ASSERT_TRUE(failure_sock != NULL);
	ASSERT_TRUE(failure_mnat != NULL);
	err = copy_local_attr(sdpm, "tls-id", failure_tls_id,
			      sizeof(failure_tls_id));
	TEST_ERR(err);
	ASSERT_TRUE(success_sock != failure_sock);
	ASSERT_TRUE(mock_mnat_media_generation(success_mnat) !=
		    mock_mnat_media_generation(failure_mnat));
	ASSERT_TRUE(str_cmp(success_tls_id, failure_tls_id));
	err = media_transport_gather_start(failure);
	ASSERT_EQ(EAGAIN, err);
	mock_mnat_media_gather_result(EHOSTUNREACH);
	mock_mnat_complete_media_gathers();
	ASSERT_EQ(EHOSTUNREACH, media_transport_error(failure));
	ASSERT_STREQ("fresh-after-prepare", media_transport_key(success));
	media_transport_abort(failure);
	ASSERT_EQ(stable_port, sa_port(sdp_media_laddr(sdpm)));

	callbacks = mock_mnat_media_gather_cancel_count();
	mock_mnat_media_gather_result(0);
	err = media_transport_alloc(&rollback, &prm);
	TEST_ERR(err);
	err = media_transport_prepare(rollback);
	TEST_ERR(err);
	err = media_transport_gather_start(rollback);
	ASSERT_EQ(EAGAIN, err);
	media_transport_abort(rollback);
	ASSERT_EQ(callbacks + 1,
		  mock_mnat_media_gather_cancel_count());
	ASSERT_EQ(stable_port, sa_port(sdp_media_laddr(sdpm)));
	err = 0;

out:
	media_transport_abort(rollback);
	media_transport_abort(failure);
	media_transport_abort(success);
	mem_deref(rollback);
	mem_deref(failure);
	mem_deref(success);
	mem_deref(failure_sock);
	mem_deref(success_sock);
	mem_deref(failure_mnat);
	mem_deref(success_mnat);
	mem_deref(mencs);
	mem_deref(mnats);
	mem_deref(group);
	mem_deref(remote);
	mem_deref(sdp);
	if (registered)
		mock_mnat_unregister();
	return err;
}


int test_menc_rebind_destructor_callback(void)
{
	struct menc_transport_binding binding = {0};
	const struct menc *menc = NULL;
	struct menc_transport *transport = NULL;
	struct menc_sess *sess = NULL;
	struct callback_owner *owner = NULL;
	struct sdp_session *sdp = NULL;
	struct sdp_media *sdpm = NULL;
	struct udp_sock *sock = NULL;
	struct sa local;
	unsigned destroyed = 0;
	unsigned callbacks = 0;
	bool live_arg = false;
	bool loaded = false;
	int err;

	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	loaded = true;
	menc = menc_find(baresip_mencl(), "dtls_srtp");
	ASSERT_TRUE(menc != NULL);

	sa_set_str(&local, "127.0.0.1", 0);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	err = sdp_media_add(&sdpm, sdp, "application", 9,
			    "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = udp_listen(&sock, &local, NULL, NULL);
	TEST_ERR(err);
	err = menc->sessh(&sess, sdp, true, NULL, NULL, NULL);
	TEST_ERR(err);
	err = menc->transporth(&transport, sess, sock, NULL, sdpm, true,
			       NULL, NULL, NULL, NULL);
	TEST_ERR(err);

	owner = mem_zalloc(sizeof(*owner), callback_owner_destructor);
	ASSERT_TRUE(owner != NULL);
	owner->destroyed = &destroyed;
	owner->callbacks = &callbacks;
	owner->live_arg = &live_arg;
	owner->magic = 0xcab00d1e;
	binding.estabh = terminal_estab;
	binding.arg = owner;
	binding.arg_ref = binding_arg_ref;
	binding.arg_deref = binding_arg_deref;
	err = menc_transport_rebind(menc, transport, &binding, NULL,
				    NULL, NULL);
	TEST_ERR(err);
	owner = mem_deref(owner);
	ASSERT_EQ(0, (int)destroyed);
	transport = mem_deref(transport);
	ASSERT_EQ(1, (int)callbacks);
	ASSERT_TRUE(live_arg);
	ASSERT_EQ(1, (int)destroyed);

out:
	mem_deref(transport);
	mem_deref(owner);
	mem_deref(sess);
	mem_deref(sock);
	mem_deref(sdp);
	if (loaded)
		module_unload("dtls_srtp");
	return err;
}


static int restart_fixture_init(struct restart_fixture *fixture,
				const struct mnat *mnat,
				const struct menc *menc)
{
	struct media_transport_prm prm = {0};
	uint64_t generation;
	struct sa local;
	int err;

	if (!fixture || !mnat || !menc)
		return EINVAL;
	list_init(&fixture->streams);
	list_init(&fixture->destinations);
	sa_set_str(&local, "127.0.0.1", 0);
	sa_set_str(&fixture->trace.old_peer, "127.0.0.1", 5000);
	sa_set_str(&fixture->trace.candidate_peer, "127.0.0.1", 6000);
	err = sdp_session_alloc(&fixture->sdp, &local);
	if (!err)
		err = sdp_media_add(&fixture->sdpm, fixture->sdp,
				    "application", 9, "UDP/DTLS/SCTP");
	if (!err)
		err = sdp_media_set_lattr(fixture->sdpm, true, "mid", "data");
	if (!err)
		err = bundle_group_singleton(&fixture->group, "data");
	if (!err)
		err = udp_listen(&fixture->sock, &local, NULL, NULL);
	if (!err)
		fixture->trace.expected_sock = fixture->sock;
	if (!err)
		err = bundle_transport_alloc(&fixture->route, fixture->group,
					     &fixture->streams, "data");
	if (!err)
		err = bundle_transport_prepare(fixture->route, fixture->group,
					       fixture->sock, &generation);
	if (!err)
		err = bundle_transport_set_remote(fixture->route, generation,
					  &fixture->trace.old_peer);
	if (!err)
		err = bundle_transport_activate(fixture->route, generation);
	if (!err)
		err = bundle_transport_finalize(fixture->route, generation);
	if (err)
		return err;

	fixture->old_media = mem_zalloc(sizeof(*fixture->old_media),
					 fake_restart_media_destructor);
	if (fixture->old_media)
		fixture->old_media->trace = &fixture->trace;
	fixture->transport = mem_zalloc(sizeof(*fixture->transport),
					fake_transport_destructor);
	fixture->mnats = mem_zalloc(1, NULL);
	fixture->mencs = mem_zalloc(1, NULL);
	if (!fixture->old_media || !fixture->transport || !fixture->mnats ||
	    !fixture->mencs)
		return ENOMEM;
	fixture->old_media->active = true;
	fixture->transport->restart_trace = &fixture->trace;
	sa_cpy(&fixture->transport->peer, &fixture->trace.old_peer);
	fixture->transport->state.remote = fixture->trace.old_peer;
	fixture->transport->state.remote_set = true;
	fixture->transport->state.started = true;
	fixture->transport->state.established = true;
	fixture->transport->state.local_role = MENC_DTLS_ROLE_CLIENT;

	prm.group = fixture->group;
	prm.transport_sdpm = fixture->sdpm;
	prm.streaml = &fixture->streams;
	prm.data_mid = "data";
	prm.semantic_key = "ice-restart-runtime";
	prm.mnat = mnat;
	prm.mnats = fixture->mnats;
	prm.menc = menc;
	prm.mencs = fixture->mencs;
	prm.af = AF_INET;
	prm.mnat_publishh = fake_mnat_publish;
	prm.arg = fixture;
	err = media_transport_adopt(
		&fixture->active, &prm, fixture->sock,
		(struct mnat_media *)fixture->old_media,
		(struct menc_transport *)fixture->transport, fixture->route);
	return err;
}


static int restart_fixture_init_group(
	struct restart_fixture *fixture, const struct mnat *mnat,
	const struct menc *menc, const struct pc_transport_group *planned,
	struct sdp_session *sdp, const struct list *streams, uint16_t old_port,
	uint16_t candidate_port)
{
	struct media_transport_prm prm = {0};
	struct stream *owner;
	uint64_t generation;
	struct sa local;
	int err;

	if (!fixture || !mnat || !menc || !planned || !sdp || !streams)
		return EINVAL;
	owner = pc_transport_group_owner_stream(planned);
	if (!owner)
		return EINVAL;
	fixture->streams = *streams;
	fixture->destinations = *streams;
	fixture->sdp = mem_ref(sdp);
	fixture->sdpm = pc_transport_group_sdpmedia(planned);
	fixture->group = mem_ref((void *)pc_transport_group_bundle(planned));
	fixture->sock = mem_ref(rtp_sock(stream_rtp_sock(owner)));
	sa_set_str(&local, "127.0.0.1", 0);
	sa_set_str(&fixture->trace.old_peer, "127.0.0.1", old_port);
	sa_set_str(&fixture->trace.candidate_peer, "127.0.0.1",
		   candidate_port);
	fixture->trace.expected_sock = fixture->sock;
	err = bundle_transport_alloc(&fixture->route, fixture->group,
				     &fixture->streams, NULL);
	if (!err)
		err = bundle_transport_prepare(fixture->route, fixture->group,
					       fixture->sock, &generation);
	if (!err)
		err = bundle_transport_set_remote(fixture->route, generation,
					  &fixture->trace.old_peer);
	if (!err)
		err = bundle_transport_activate(fixture->route, generation);
	if (!err)
		err = bundle_transport_finalize(fixture->route, generation);
	if (err)
		return err;

	fixture->old_media = mem_zalloc(sizeof(*fixture->old_media),
					 fake_restart_media_destructor);
	fixture->transport = mem_zalloc(sizeof(*fixture->transport),
					fake_transport_destructor);
	fixture->mnats = mem_zalloc(1, NULL);
	fixture->mencs = mem_zalloc(1, NULL);
	if (!fixture->old_media || !fixture->transport || !fixture->mnats ||
	    !fixture->mencs)
		return ENOMEM;
	fixture->old_media->trace = &fixture->trace;
	fixture->old_media->active = true;
	fixture->transport->restart_trace = &fixture->trace;
	sa_cpy(&fixture->transport->peer, &fixture->trace.old_peer);
	fixture->transport->state.remote = fixture->trace.old_peer;
	fixture->transport->state.remote_set = true;
	fixture->transport->state.started = true;
	fixture->transport->state.established = true;
	fixture->transport->state.local_role = MENC_DTLS_ROLE_CLIENT;

	prm.group = fixture->group;
	prm.transport_sdpm = fixture->sdpm;
	prm.streaml = &fixture->streams;
	prm.semantic_key = pc_transport_group_reuse_key(planned);
	prm.mnat = mnat;
	prm.mnats = fixture->mnats;
	prm.menc = menc;
	prm.mencs = fixture->mencs;
	prm.af = AF_INET;
	return media_transport_adopt(
		&fixture->active, &prm, fixture->sock,
		(struct mnat_media *)fixture->old_media,
		(struct menc_transport *)fixture->transport, fixture->route);
}


static int restart_fixture_prepare(struct restart_fixture *fixture)
{
	int err;

	err = media_transport_restart_alloc(
		&fixture->candidate, fixture->active, fixture->group,
		&fixture->streams, &fixture->destinations, "data",
		media_transport_key(fixture->active), NULL);
	if (!err)
		err = media_transport_prepare(fixture->candidate);
	if (!err)
		err = media_transport_set_remote(fixture->candidate,
					 &fixture->trace.candidate_peer);
	fixture->candidate_media = fixture->trace.candidate_media;
	return err;
}


static void restart_fixture_close(struct restart_fixture *fixture)
{
	media_transport_abort(fixture->candidate);
	media_transport_release(fixture->candidate);
	media_transport_release(fixture->active);
	fixture->candidate = mem_deref(fixture->candidate);
	fixture->active = mem_deref(fixture->active);
	fixture->old_media = mem_deref(fixture->old_media);
	fixture->transport = mem_deref(fixture->transport);
	fixture->route = mem_deref(fixture->route);
	fixture->sock = mem_deref(fixture->sock);
	fixture->mnats = mem_deref(fixture->mnats);
	fixture->mencs = mem_deref(fixture->mencs);
	fixture->group = mem_deref(fixture->group);
	fixture->sdp = mem_deref(fixture->sdp);
	re_fhs_flush();
}


static int test_media_transport_ice_restart_runtime(void)
{
	struct mnat mnat = {
		.id = "fake-restart",
		.mediarestartalloch = fake_restart_alloc,
		.mediaprepareh = fake_restart_prepare,
		.mediaactivateh = fake_restart_activate,
		.mediarollbackh = fake_restart_rollback,
		.mediafinalizeh = fake_restart_finalize,
		.mediaaborth = fake_restart_abort,
		.mediaattemptstarth = fake_restart_attempt,
		.mediaattemptcancelh = fake_restart_cancel,
		.mediagatheredh = fake_restart_gathered,
	};
	struct menc menc = {
		.id = "fake-restart",
		.transporth = fake_alloc,
		.transportsendh = fake_send,
		.transportpeerseth = fake_peer_set,
		.transportrebindh = fake_rebind,
		.transportmembersprepareh = fake_members_prepare,
	};
	struct restart_fixture fixture = {0};
	struct mbuf *mb = NULL;
	int err = 0;

	/* Prepare creates a distinct ICE generation on the exact established
	 * socket/MENC/token.  The peer query is observational, and neither
	 * generation changes packet visibility during preparation. */
	err = restart_fixture_init(&fixture, &mnat, &menc);
	TEST_ERR(err);
	err = restart_fixture_prepare(&fixture);
	TEST_ERR(err);
	ASSERT_TRUE(fixture.trace.restart_sock == fixture.trace.expected_sock);
	ASSERT_TRUE(fixture.candidate_media != fixture.old_media);
	ASSERT_EQ(1, fixture.transport->peer_queries);
	ASSERT_EQ(0, fixture.transport->peer_sets);
	ASSERT_TRUE(sa_cmp(&fixture.transport->peer,
			   &fixture.trace.old_peer, SA_ALL));
	ASSERT_TRUE(fixture.old_media->active);
	ASSERT_TRUE(!fixture.candidate_media->active);
	ASSERT_EQ(3, (int)fixture.trace.eventc);
	ASSERT_EQ(RESTART_PEER_QUERY, fixture.trace.events[0]);
	ASSERT_EQ(RESTART_PREPARE_OLD_INACTIVE, fixture.trace.events[1]);
	ASSERT_EQ(RESTART_PREPARE_CANDIDATE_ACTIVE,
		  fixture.trace.events[2]);

	/* Route activation occurs before the peer update.  A peer-update failure
	 * therefore exercises the route rollback path: no MNAT generation or
	 * token is published, and the stable old send remains usable. */
	fixture.transport->fail_peer_set = true;
	err = media_transport_activate(fixture.candidate);
	ASSERT_EQ(EIO, err);
	err = 0;
	ASSERT_EQ(0, fixture.old_media->activates);
	ASSERT_EQ(0, fixture.candidate_media->activates);
	ASSERT_TRUE(fixture.old_media->active);
	ASSERT_TRUE(!fixture.candidate_media->active);
	ASSERT_TRUE(sa_cmp(&fixture.transport->peer,
			   &fixture.trace.old_peer, SA_ALL));
	mb = mbuf_alloc(1);
	ASSERT_TRUE(mb != NULL);
	err = media_transport_send(fixture.active, mb);
	TEST_ERR(err);
	ASSERT_EQ(1, fixture.transport->sends);
	media_transport_abort(fixture.candidate);
	ASSERT_EQ(1, fixture.candidate_media->aborts);
	ASSERT_EQ(1, fixture.old_media->aborts);
	ASSERT_EQ(RESTART_ABORT_CANDIDATE, fixture.trace.events[3]);
	ASSERT_EQ(RESTART_ABORT_OLD, fixture.trace.events[4]);
	restart_fixture_close(&fixture);
	ASSERT_EQ(1, fixture.trace.media_destructors[0]);
	ASSERT_EQ(1, fixture.trace.media_destructors[1]);
	mem_deref(mb);
	mb = NULL;

	/* Successful publication retargets the shared MENC peer, then activates
	 * candidate ICE before making the old checklist inactive.  Rollback must
	 * restore the peer first and reverse the pair in the exact inverse state. */
	memset(&fixture, 0, sizeof(fixture));
	err = restart_fixture_init(&fixture, &mnat, &menc);
	TEST_ERR(err);
	err = restart_fixture_prepare(&fixture);
	TEST_ERR(err);
	err = media_transport_activate(fixture.candidate);
	TEST_ERR(err);
	ASSERT_TRUE(fixture.published_media ==
		    (struct mnat_media *)fixture.candidate_media);
	ASSERT_EQ(RESTART_PEER_SET_CANDIDATE, fixture.trace.events[3]);
	ASSERT_EQ(RESTART_ACTIVATE_CANDIDATE, fixture.trace.events[4]);
	ASSERT_EQ(RESTART_ACTIVATE_OLD, fixture.trace.events[5]);
	ASSERT_TRUE(!fixture.old_media->active);
	ASSERT_TRUE(fixture.candidate_media->active);
	ASSERT_TRUE(sa_cmp(&fixture.transport->peer,
			   &fixture.trace.candidate_peer, SA_ALL));
	mb = mbuf_alloc(1);
	ASSERT_TRUE(mb != NULL);
	err = media_transport_send(fixture.active, mb);
	TEST_ERR(err);
	media_transport_rollback(fixture.candidate);
	ASSERT_TRUE(fixture.published_media ==
		    (struct mnat_media *)fixture.old_media);
	ASSERT_EQ(RESTART_PEER_SET_OLD, fixture.trace.events[6]);
	ASSERT_EQ(RESTART_ROLLBACK_OLD, fixture.trace.events[7]);
	ASSERT_EQ(RESTART_ROLLBACK_CANDIDATE, fixture.trace.events[8]);
	ASSERT_TRUE(fixture.old_media->active);
	ASSERT_TRUE(!fixture.candidate_media->active);
	ASSERT_TRUE(sa_cmp(&fixture.transport->peer,
			   &fixture.trace.old_peer, SA_ALL));
	err = media_transport_send(fixture.active, mb);
	TEST_ERR(err);
	ASSERT_EQ(2, fixture.transport->sends);
	media_transport_abort(fixture.candidate);
	ASSERT_EQ(1, fixture.candidate_media->aborts);
	ASSERT_EQ(1, fixture.old_media->aborts);
	ASSERT_EQ(RESTART_ABORT_CANDIDATE, fixture.trace.events[9]);
	ASSERT_EQ(RESTART_ABORT_OLD, fixture.trace.events[10]);
	restart_fixture_close(&fixture);
	ASSERT_EQ(1, fixture.trace.media_destructors[0]);
	ASSERT_EQ(1, fixture.trace.media_destructors[1]);
	mem_deref(mb);
	mb = NULL;

	/* Finalization retires both prepared ICE generations once and destruction
	 * releases each generation exactly once without a second abort/finalize. */
	memset(&fixture, 0, sizeof(fixture));
	err = restart_fixture_init(&fixture, &mnat, &menc);
	TEST_ERR(err);
	err = restart_fixture_prepare(&fixture);
	TEST_ERR(err);
	err = media_transport_activate(fixture.candidate);
	TEST_ERR(err);
	ASSERT_TRUE(fixture.published_media ==
		    (struct mnat_media *)fixture.candidate_media);
	media_transport_finalize(fixture.candidate);
	ASSERT_TRUE(fixture.published_media ==
		    (struct mnat_media *)fixture.candidate_media);
	ASSERT_EQ(RESTART_FINALIZE_CANDIDATE, fixture.trace.events[6]);
	ASSERT_EQ(RESTART_FINALIZE_OLD, fixture.trace.events[7]);
	ASSERT_EQ(1, fixture.candidate_media->finalizes);
	ASSERT_EQ(1, fixture.old_media->finalizes);
	ASSERT_EQ(0, fixture.candidate_media->aborts);
	ASSERT_EQ(0, fixture.old_media->aborts);
	restart_fixture_close(&fixture);
	ASSERT_EQ(1, fixture.trace.media_destructors[0]);
	ASSERT_EQ(1, fixture.trace.media_destructors[1]);

out:
	mem_deref(mb);
	restart_fixture_close(&fixture);
	return err;
}


static int test_transport_session_atomic_activation(void)
{
	struct mnat mnat = {
		.id = "fake-session-restart",
		.mediarestartalloch = fake_restart_alloc,
		.mediaprepareh = fake_restart_prepare,
		.mediaactivateh = fake_restart_activate,
		.mediarollbackh = fake_restart_rollback,
		.mediafinalizeh = fake_restart_finalize,
		.mediaaborth = fake_restart_abort,
		.mediaattemptstarth = fake_restart_attempt,
		.mediaattemptcancelh = fake_restart_cancel,
		.mediagatheredh = fake_restart_gathered,
	};
	struct menc menc = {
		.id = "fake-session-restart",
		.transporth = fake_alloc,
		.transportsendh = fake_send,
		.transportpeerseth = fake_peer_set,
		.transportrebindh = fake_rebind,
		.transportmembersprepareh = fake_members_prepare,
	};
	struct stream_param stream_prm = {
		.use_rtp = true,
		.rtcp_mux = true,
		.af = AF_INET,
		.cname = "session-atomic-test",
	};
	struct restart_fixture fixture[2] = {0};
	struct pc_transport_session *session = NULL;
	struct pc_transport_generation *topology = NULL;
	const struct pc_transport_generation *active = NULL;
	struct sdp_session *sdp = NULL;
	struct stream *streamv[2] = {0};
	struct mbuf *remote = NULL;
	struct mbuf *packet = NULL;
	struct list streams = LIST_INIT;
	struct config_avt cfg;
	struct bootstrap_state state = {.sessionp = &session};
	struct sa local;
	int err = 0;

	cfg = conf_config()->avt;
	cfg.rtcp_mux = true;
	sa_set_str(&local, "127.0.0.1", 0);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	for (size_t i = 0; i < RE_ARRAY_SIZE(streamv); ++i) {
		err = stream_alloc(&streamv[i], &streams, &stream_prm, &cfg,
				   sdp, MEDIA_AUDIO, NULL, NULL, NULL, NULL,
				   true, coordinator_rtp_handler, NULL,
				   coordinator_pt_handler, NULL);
		TEST_ERR(err);
		err = sdp_format_add(NULL, stream_sdpmedia(streamv[i]), false,
				     "0", "PCMU", 8000, 1, NULL, NULL,
				     NULL, false, NULL);
		TEST_ERR(err);
	}
	remote = mbuf_alloc(1024);
	ASSERT_TRUE(remote != NULL);
	err = mbuf_printf(remote,
		"v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\n"
		"c=IN IP4 127.0.0.1\r\nt=0 0\r\n"
		"m=audio 5000 RTP/AVP 0\r\na=mid:%s\r\n"
		"m=audio 5002 RTP/AVP 0\r\na=mid:%s\r\n",
		stream_mid(streamv[0]), stream_mid(streamv[1]));
	TEST_ERR(err);
	remote->pos = 0;
	err = sdp_decode(sdp, remote, true);
	TEST_ERR(err);
	err = pc_transport_generation_alloc(&topology, sdp, &streams, NULL);
	TEST_ERR(err);
	ASSERT_EQ(2, (int)pc_transport_generation_count(topology));
	{
		struct mbuf *discard = mbuf_alloc(512);
		const struct pc_transport_group *group =
			pc_transport_generation_group(topology, 0);
		struct sa nominated;

		ASSERT_TRUE(discard != NULL);
		err = stream_update(streamv[0]);
		TEST_ERR(err);
		sa_cpy(&nominated, stream_raddr(streamv[0]));
		err = mbuf_printf(discard,
			"v=0\r\no=- 2 2 IN IP4 0.0.0.0\r\ns=-\r\n"
			"c=IN IP4 0.0.0.0\r\nt=0 0\r\n"
			"m=audio 9 RTP/AVP 0\r\na=mid:%s\r\n"
			"m=audio 9 RTP/AVP 0\r\na=mid:%s\r\n",
			stream_mid(streamv[0]), stream_mid(streamv[1]));
		TEST_ERR(err);
		discard->pos = 0;
		err = sdp_decode(sdp, discard, true);
		TEST_ERR(err);
		/* Browser SDP retains a discard address after ICE nomination. */
		ASSERT_TRUE(sa_cmp(pc_transport_group_remote(group),
				   &nominated, SA_ALL));
		ASSERT_TRUE(!sa_cmp(sdp_media_raddr(
			pc_transport_group_sdpmedia(group)), &nominated, SA_ALL));
		remote->pos = 0;
		err = sdp_decode(sdp, remote, true);
		TEST_ERR(err);
		mem_deref(discard);
	}

	for (size_t i = 0; i < RE_ARRAY_SIZE(fixture); ++i) {
		err = restart_fixture_init_group(
			&fixture[i], &mnat, &menc,
			pc_transport_generation_group(topology, i), sdp,
			&streams, (uint16_t)(5000 + 2 * i),
			(uint16_t)(6000 + 2 * i));
		TEST_ERR(err);
	}
	err = pc_transport_session_alloc(&session, destructive_publish,
					 coordinator_error, &state);
	TEST_ERR(err);
	err = pc_transport_session_stage(session, topology);
	TEST_ERR(err);
	for (size_t i = 0; i < RE_ARRAY_SIZE(fixture); ++i) {
		err = pc_transport_session_add(
			session, pc_transport_generation_group(topology, i),
			fixture[i].active, NULL);
		TEST_ERR(err);
	}
	err = pc_transport_session_bootstrap(session);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)state.published);
	ASSERT_TRUE(state.last_published == topology);

	for (size_t i = 0; i < RE_ARRAY_SIZE(fixture); ++i) {
		err = restart_fixture_prepare(&fixture[i]);
		TEST_ERR(err);
	}
	err = pc_transport_session_stage(session, topology);
	TEST_ERR(err);
	for (size_t i = 0; i < RE_ARRAY_SIZE(fixture); ++i) {
		err = pc_transport_session_add(
			session, pc_transport_generation_group(topology, i),
			fixture[i].candidate, NULL);
		TEST_ERR(err);
	}

	/* Group 1 activates first.  Failure while retargeting group 2 must be
	 * returned synchronously, without an async error callback, and roll every
	 * already-published dimension of group 1 back before start() returns. */
	fixture[1].transport->fail_peer_set = true;
	err = pc_transport_session_start(session);
	ASSERT_EQ(EIO, err);
	err = 0;
	ASSERT_EQ(1, (int)state.published);
	ASSERT_EQ(0, (int)state.errors);
	active = pc_transport_session_active_ref(session);
	ASSERT_TRUE(active == topology);
	active = mem_deref((void *)active);
	ASSERT_TRUE(fixture[0].old_media->active);
	ASSERT_TRUE(!fixture[0].candidate_media->active);
	ASSERT_TRUE(sa_cmp(&fixture[0].transport->peer,
			   &fixture[0].trace.old_peer, SA_ALL));
	ASSERT_TRUE(fixture[1].old_media->active);
	ASSERT_TRUE(!fixture[1].candidate_media->active);
	ASSERT_TRUE(sa_cmp(&fixture[1].transport->peer,
			   &fixture[1].trace.old_peer, SA_ALL));
	packet = mbuf_alloc(1);
	ASSERT_TRUE(packet != NULL);
	err = media_transport_send(fixture[0].active, packet);
	TEST_ERR(err);
	err = media_transport_send(fixture[1].active, packet);
	TEST_ERR(err);

	/* The failed candidate is consumed, while the stable generation remains
	 * retryable.  A fresh candidate pair may be staged and atomically publish
	 * on the next exact attempt. */
	for (size_t i = 0; i < RE_ARRAY_SIZE(fixture); ++i) {
		media_transport_release(fixture[i].candidate);
		fixture[i].candidate = mem_deref(fixture[i].candidate);
		fixture[i].candidate_media = NULL;
	}
	fixture[1].transport->fail_peer_set = false;
	for (size_t i = 0; i < RE_ARRAY_SIZE(fixture); ++i) {
		err = restart_fixture_prepare(&fixture[i]);
		TEST_ERR(err);
	}
	err = pc_transport_session_stage(session, topology);
	TEST_ERR(err);
	for (size_t i = 0; i < RE_ARRAY_SIZE(fixture); ++i) {
		err = pc_transport_session_add(
			session, pc_transport_generation_group(topology, i),
			fixture[i].candidate, NULL);
		TEST_ERR(err);
	}
	err = pc_transport_session_start(session);
	TEST_ERR(err);
	ASSERT_EQ(2, (int)state.published);
	ASSERT_EQ(0, (int)state.errors);
	ASSERT_TRUE(state.last_published == topology);
	ASSERT_TRUE(!fixture[0].old_media->active);
	ASSERT_TRUE(fixture[0].candidate_media->active);
	ASSERT_TRUE(!fixture[1].old_media->active);
	ASSERT_TRUE(fixture[1].candidate_media->active);

out:
	mem_deref((void *)active);
	mem_deref(packet);
	mem_deref(session);
	for (size_t i = 0; i < RE_ARRAY_SIZE(fixture); ++i)
		restart_fixture_close(&fixture[i]);
	list_flush(&streams);
	mem_deref(topology);
	mem_deref(remote);
	mem_deref(sdp);
	return err;
}


int test_media_transport_adopt_reconfigure(void)
{
	struct menc menc = {
		.id = "fake",
		.transporth = fake_alloc,
		.transportsendh = fake_send,
		.transportrebindh = fake_rebind,
		.transportmembersprepareh = fake_members_prepare,
	};
	struct fake_transport *fake = NULL;
	struct media_transport_prm prm = {0};
	struct media_transport_prm consumer = {0};
	struct media_transport *active = NULL;
	struct media_transport *candidate = NULL;
	struct media_transport *reused = NULL;
	struct media_transport *removed = NULL;
	struct media_transport *readded = NULL;
	struct pc_transport_session *transport_session = NULL;
	struct pc_transport_generation *topology = NULL;
	struct bundle_transport *route = NULL;
	struct bundle_transport *pending_route = NULL;
	struct bundle_group *group = NULL;
	struct sdp_session *sdp = NULL;
	struct sdp_media *sdpm = NULL;
	struct menc_sess *mencs = NULL;
	struct udp_sock *sock = NULL;
	struct mbuf *mb = NULL;
	struct menc_transport_binding newer = {0};
	struct pc_transport_data transport_data = {0};
	struct pc_transport_data_binding data_binding = {0};
	struct bootstrap_state bootstrap = {0};
	struct callback_owner *owner = NULL;
	struct list streams = LIST_INIT;
	struct list destinations = LIST_INIT;
	struct sa local;
	struct sa remote;
	uint64_t generation;
	uint64_t pending_generation;
	unsigned owner_destroyed = 0;
	unsigned candidate_receives = 0;
	unsigned queued_changes = 0;
	struct destructive_transport_state destructive = {
		.transportp = &active,
	};
	int err;

#ifdef USE_DATACHANNEL
	err = test_shared_lower_prepared_abort();
	TEST_ERR(err);
#endif
	err = test_media_transport_ice_restart_runtime();
	TEST_ERR(err);
	err = test_transport_session_atomic_activation();
	TEST_ERR(err);

	sa_set_str(&local, "127.0.0.1", 0);
	sa_set_str(&remote, "127.0.0.1", 5000);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	err = sdp_media_add(&sdpm, sdp, "application", 9,
			    "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_media_set_lattr(sdpm, true, "mid", "data");
	TEST_ERR(err);
	err = bundle_group_singleton(&group, "data");
	TEST_ERR(err);
	err = udp_listen(&sock, &local, NULL, NULL);
	TEST_ERR(err);
	err = bundle_transport_alloc(&route, group, &streams, "data");
	TEST_ERR(err);
	err = bundle_transport_prepare(route, group, sock, &generation);
	TEST_ERR(err);
	err = bundle_transport_set_remote(route, generation, &remote);
	TEST_ERR(err);
	err = bundle_transport_activate(route, generation);
	TEST_ERR(err);
	err = bundle_transport_finalize(route, generation);
	TEST_ERR(err);

	fake = mem_zalloc(sizeof(*fake), fake_transport_destructor);
	mencs = mem_zalloc(1, NULL);
	owner = mem_zalloc(sizeof(*owner), callback_owner_destructor);
	ASSERT_TRUE(fake != NULL);
	ASSERT_TRUE(mencs != NULL);
	ASSERT_TRUE(owner != NULL);
	owner->destroyed = &owner_destroyed;
	fake->binding.recvh = original_recv;
	fake->binding.arg = mem_ref(owner);
	fake->binding.arg_ref = binding_arg_ref;
	fake->binding.arg_deref = binding_arg_deref;
	fake->binding_owned = true;
	fake->state.remote = remote;
	fake->state.remote_set = true;
	fake->state.started = true;
	fake->state.established = true;
	fake->state.local_role = MENC_DTLS_ROLE_CLIENT;

	prm.group = group;
	prm.transport_sdpm = sdpm;
	prm.streaml = &streams;
	prm.data_mid = "data";
	prm.semantic_key = "adopted-data";
	prm.menc = &menc;
	prm.mencs = mencs;
	prm.af = AF_INET;

	/* Importing an established legacy association into a prepared exact route
	 * must remain invisible until activation and retain full rollback/finalize
	 * behavior. */
	err = bundle_transport_alloc(&pending_route, group, &streams, "data");
	TEST_ERR(err);
	err = bundle_transport_prepare(pending_route, group, sock,
				       &pending_generation);
	TEST_ERR(err);
	err = bundle_transport_set_remote(pending_route, pending_generation,
				  &remote);
	TEST_ERR(err);
	err = media_transport_adopt_pending(
		&candidate, &prm, sock, NULL, (struct menc_transport *)fake,
		pending_route, pending_generation);
	TEST_ERR(err);
	ASSERT_TRUE(media_transport_ready(candidate));
	err = media_transport_activate(candidate);
	TEST_ERR(err);
	media_transport_rollback(candidate);
	ASSERT_TRUE(!media_transport_published(candidate));
	media_transport_release(candidate);
	candidate = mem_deref(candidate);
	pending_route = mem_deref(pending_route);
	ASSERT_TRUE(fake->binding.recvh == original_recv);

	err = bundle_transport_alloc(&pending_route, group, &streams, "data");
	TEST_ERR(err);
	err = bundle_transport_prepare(pending_route, group, sock,
				       &pending_generation);
	TEST_ERR(err);
	err = bundle_transport_set_remote(pending_route, pending_generation,
				  &remote);
	TEST_ERR(err);
	err = media_transport_adopt_pending(
		&candidate, &prm, sock, NULL, (struct menc_transport *)fake,
		pending_route, pending_generation);
	TEST_ERR(err);
	err = media_transport_activate(candidate);
	TEST_ERR(err);
	media_transport_finalize(candidate);
	ASSERT_TRUE(media_transport_published(candidate));
	media_transport_release(candidate);
	candidate = mem_deref(candidate);
	pending_route = mem_deref(pending_route);
	ASSERT_TRUE(fake->binding.recvh == original_recv);
	fake->prepares = 0;

	err = media_transport_adopt(&active, &prm, sock, NULL,
				    (struct menc_transport *)fake, route);
	TEST_ERR(err);
	ASSERT_TRUE(fake->binding.arg != owner);
	/* The original owner may retire immediately after callback takeover.  The
	 * captured binding must keep its callback argument alive until restore. */
	owner = mem_deref(owner);
	ASSERT_EQ(0, (int)owner_destroyed);

	mb = mbuf_alloc(1);
	ASSERT_TRUE(mb != NULL);
	err = media_transport_send(active, mb);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)fake->sends);

	consumer.recvh = counted_recv;
	consumer.arg = &candidate_receives;
	err = media_transport_reconfigure_alloc(&candidate, active, group,
						&streams, &destinations,
						"data", "adopted-data-v2",
						&consumer);
	TEST_ERR(err);
	err = media_transport_prepare(candidate);
	TEST_ERR(err);
	ASSERT_EQ(0, (int)fake->prepares);
	ASSERT_TRUE(media_transport_ready(candidate));
	/* The stable binding handle follows the token's publication point: old
	 * while prepared, candidate after activation, then old again on rollback. */
	err = media_transport_send(active, mb);
	TEST_ERR(err);
	ASSERT_EQ(2, (int)fake->sends);
	err = media_transport_activate(candidate);
	TEST_ERR(err);
	err = media_transport_send(active, mb);
	TEST_ERR(err);
	ASSERT_EQ(3, (int)fake->sends);
	media_transport_rollback(candidate);
	err = media_transport_send(active, mb);
	TEST_ERR(err);
	ASSERT_EQ(4, (int)fake->sends);
	media_transport_abort(candidate);
	candidate = mem_deref(candidate);

	err = media_transport_reconfigure_alloc(&candidate, active, group,
						&streams, &destinations,
						"data", "adopted-data-v2",
						&consumer);
	TEST_ERR(err);
	err = media_transport_prepare(candidate);
	TEST_ERR(err);
	/* DTLS application records select the private pending consumer while the
	 * old runtime remains published.  They become deliverable only after the
	 * consumer explicitly declares readiness. */
	fake->binding.recvh(mb, fake->binding.arg);
	ASSERT_EQ(0, (int)candidate_receives);
	err = media_transport_consumer_ready(candidate);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)candidate_receives);
	/* Adding SCTP to an established BUNDLE group sends its handshake on the
	 * shared DTLS association while the old generation is still published. */
	err = media_transport_send(candidate, mb);
	TEST_ERR(err);
	ASSERT_EQ(5, (int)fake->sends);
	err = media_transport_activate(candidate);
	TEST_ERR(err);
	media_transport_finalize(candidate);
	fake->binding.recvh(mb, fake->binding.arg);
	ASSERT_EQ(2, (int)candidate_receives);
	/* A reused SCTP binding may retain its previous runtime wrapper until
	 * data publication finalizes.  Sends through that wrapper must follow the
	 * shared token to the newly active runtime when the MENC association is
	 * unchanged. */
	err = media_transport_send(active, mb);
	TEST_ERR(err);
	ASSERT_EQ(6, (int)fake->sends);
	active = mem_deref(active);
	err = media_transport_send(candidate, mb);
	TEST_ERR(err);
	ASSERT_EQ(7, (int)fake->sends);

	/* A no-op generation retains the published protocol consumer.  After
	 * token promotion it must deliver immediately without requiring a second
	 * consumer_ready signal that no unchanged SCTP binding will issue. */
	err = media_transport_reconfigure_alloc(
		&reused, candidate, group, &streams, &destinations, "data",
		"adopted-data-v2", NULL);
	TEST_ERR(err);
	err = media_transport_prepare(reused);
	TEST_ERR(err);
	err = media_transport_activate(reused);
	TEST_ERR(err);
	media_transport_finalize(reused);
	fake->binding.recvh(mb, fake->binding.arg);
	ASSERT_EQ(3, (int)candidate_receives);
	media_transport_release(candidate);
	candidate = mem_deref(candidate);

	/* Removing SCTP publishes a wrapper with no application-data callback.
	 * A later re-add may then install a fresh pending consumer on the same
	 * lower DTLS identity; it must not trip the second-association guard. */
	err = media_transport_reconfigure_alloc(
		&removed, reused, group, &streams, &destinations, "data",
		"adopted-no-data-v3", &(struct media_transport_prm){0});
	TEST_ERR(err);
	err = media_transport_prepare(removed);
	TEST_ERR(err);
	err = media_transport_activate(removed);
	TEST_ERR(err);
	media_transport_finalize(removed);
	err = media_transport_reconfigure_alloc(
		&readded, removed, group, &streams, &destinations, "data",
		"adopted-data-v4", &consumer);
	TEST_ERR(err);
	media_transport_abort(readded);
	readded = mem_deref(readded);
	media_transport_release(reused);
	reused = mem_deref(reused);
	media_transport_release(removed);
	removed = mem_deref(removed);

	/* The last runtime restores the binding it took over.  Conditional
	 * restore protects a subsequently rebound owner from stale teardown. */
	ASSERT_TRUE(fake->binding.recvh == original_recv);
	ASSERT_EQ(0, (int)owner_destroyed);

	transport_data.mid = "data";
	transport_data.sdpm = sdpm;
	transport_data.socket_identity = sock;
	transport_data.accepted = true;
	transport_data.local_sctp_port = 5000;
	transport_data.remote_sctp_port = 5000;
	err = pc_transport_generation_alloc(&topology, sdp, &streams,
					    &transport_data);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)pc_transport_generation_count(topology));
	prm.semantic_key = pc_transport_group_reuse_key(
		pc_transport_generation_group(topology, 0));
	err = media_transport_adopt(&active, &prm, sock, NULL,
				    (struct menc_transport *)fake, route);
	TEST_ERR(err);
	bootstrap.sessionp = &transport_session;
	err = pc_transport_session_alloc(&transport_session,
					 destructive_publish, NULL, &bootstrap);
	TEST_ERR(err);
	err = pc_transport_session_stage(transport_session, topology);
	TEST_ERR(err);
	data_binding.object = mencs;
	err = pc_transport_session_add(
		transport_session, pc_transport_generation_group(topology, 0),
		active, &data_binding);
	TEST_ERR(err);
	err = pc_transport_session_bootstrap(transport_session);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)bootstrap.published);
	ASSERT_TRUE(transport_session != NULL);

	/* Exercise the regular generation gate after adopting the established
	 * legacy runtime.  The publish callback drops the caller's last session
	 * reference; activation/finalization must still finish on the internal
	 * callback pin, and the SCTP binding must be inherited by planner key. */
	err = media_transport_reconfigure_alloc(
		&candidate, active, group, &streams, &destinations, "data",
		pc_transport_group_reuse_key(
			pc_transport_generation_group(topology, 0)), NULL);
	TEST_ERR(err);
	err = media_transport_prepare(candidate);
	TEST_ERR(err);
	/* A fresh lower may rotate local ICE/DTLS attributes during prepare.  Its
	 * unpublished planner identity must be refreshable before the candidate
	 * is handed to the atomic session gate. */
	err = media_transport_rekey(candidate, "prepared-rotated-identity");
	TEST_ERR(err);
	ASSERT_STREQ("prepared-rotated-identity",
		     media_transport_key(candidate));
	err = media_transport_rekey(
		candidate,
		pc_transport_group_reuse_key(
			pc_transport_generation_group(topology, 0)));
	TEST_ERR(err);
	err = pc_transport_session_stage(transport_session, topology);
	TEST_ERR(err);
	err = pc_transport_session_add(
		transport_session, pc_transport_generation_group(topology, 0),
		candidate, NULL);
	TEST_ERR(err);
	bootstrap.destroy_session = true;
	err = pc_transport_session_start(transport_session);
	TEST_ERR(err);
	ASSERT_EQ(2, (int)bootstrap.published);
	ASSERT_TRUE(transport_session == NULL);
	candidate = mem_deref(candidate);

	newer.recvh = original_recv;
	newer.arg = fake;
	err = menc_transport_rebind(&menc, (struct menc_transport *)fake,
				    &newer, NULL, NULL, NULL);
	TEST_ERR(err);
	active = mem_deref(active);
	ASSERT_TRUE(fake->binding.arg == fake);
	ASSERT_EQ(1, (int)owner_destroyed);

	/* An unread main-loop notification carries no opaque owning pointer.
	 * Destroying its runtime before the queue drains must cancel the fd and
	 * release every runtime/queue reference without invoking stale state. */
	err = media_transport_adopt(&active, &prm, sock, NULL,
				    (struct menc_transport *)fake, route);
	TEST_ERR(err);
	media_transport_set_observer(active, counted_change, &queued_changes);
	err = media_transport_set_remote(active, &remote);
	TEST_ERR(err);
	media_transport_release(active);
	active = mem_deref(active);
	re_fhs_flush();
	ASSERT_EQ(0, (int)queued_changes);

	/* Lower callbacks may synchronously release their published runtime.  The
	 * callback argument and runtime must be pinned under the publication gate,
	 * then invoked only after the gate is unlocked so teardown can safely
	 * restore the previous MENC binding. */
	prm.estabh = destructive_transport_estab;
	prm.closeh = NULL;
	prm.arg = &destructive;
	err = media_transport_adopt(&active, &prm, sock, NULL,
				    (struct menc_transport *)fake, route);
	TEST_ERR(err);
	{
		struct menc_transport_binding callback = fake->binding;

		callback.estabh(0, MENC_DTLS_ROLE_CLIENT, callback.arg);
	}
	ASSERT_TRUE(active == NULL);
	ASSERT_EQ(1, (int)destructive.callbacks);

	prm.estabh = NULL;
	prm.closeh = destructive_transport_close;
	err = media_transport_adopt(&active, &prm, sock, NULL,
				    (struct menc_transport *)fake, route);
	TEST_ERR(err);
	{
		struct menc_transport_binding callback = fake->binding;

		callback.closeh(ECONNRESET, callback.arg);
	}
	ASSERT_TRUE(active == NULL);
	ASSERT_EQ(2, (int)destructive.callbacks);

out:
	media_transport_abort(readded);
	media_transport_release(readded);
	media_transport_release(removed);
	media_transport_release(candidate);
	media_transport_release(active);
	mem_deref(candidate);
	mem_deref(readded);
	mem_deref(removed);
	mem_deref(active);
	mem_deref(transport_session);
	mem_deref(topology);
	mem_deref(mb);
	mem_deref(fake);
	mem_deref(pending_route);
	mem_deref(route);
	mem_deref(sock);
	mem_deref(mencs);
	mem_deref(owner);
	mem_deref(group);
	mem_deref(sdp);
	/* mqueue destruction retires its fd handler through the event-loop
	 * generation barrier.  Flush that retirement before leak accounting. */
	re_fhs_flush();
	return err;
}
