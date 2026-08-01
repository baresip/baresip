/**
 * @file test/peerconn_transport.c
 *
 * Peer-connection transport and description transaction tests.
 */
#include <errno.h>
#include <stdlib.h>
#include <re.h>
#include <baresip.h>
#include "core.h"
#include "test.h"
#include "peerconn_internal.h"


#ifdef USE_DATACHANNEL
static const uint8_t transport_payload[] = {
	0x00, 0x11, 0x7f, 0x80, 0xfe, 0xff, 0x42
};
#endif

#ifdef USE_DATACHANNEL
void mock_mnat_register(struct list *mnatl);
void mock_mnat_unregister(void);


static bool mbuf_contains(const struct mbuf *mb, const char *text)
{
	size_t len = str_len(text);

	return mb && len <= mb->end &&
		memmem(mb->buf, mb->end, text, len) != NULL;
}
#endif


static int decode_bundle_set(struct bundle_set **setp,
			     const char *const *values, size_t count)
{
	struct sdp_session *offer = NULL;
	struct sdp_session *answer = NULL;
	struct mbuf *encoded = NULL;
	struct sa address;
	int err;

	if (!setp || (!values && count))
		return EINVAL;

	sa_set_str(&address, "127.0.0.1", 5000);
	err = sdp_session_alloc(&offer, &address);
	if (err)
		goto out;
	err = sdp_session_alloc(&answer, &address);
	if (err)
		goto out;

	for (size_t i = 0; i < count; ++i) {
		err = sdp_session_set_lattr(offer, false, "group", "%s",
					    values[i]);
		if (err)
			goto out;
	}

	err = sdp_encode(&encoded, offer, true);
	if (err)
		goto out;
	err = sdp_decode(answer, encoded, true);
	if (err)
		goto out;
	err = bundle_set_decode(setp, answer);

out:
	mem_deref(encoded);
	mem_deref(answer);
	mem_deref(offer);
	return err;
}


static int roundtrip_bundle_set(struct bundle_set **setp,
				const struct bundle_set *source)
{
	struct sdp_session *local = NULL;
	struct sdp_session *remote = NULL;
	struct mbuf *encoded = NULL;
	struct sa address;
	int err;

	if (!setp || !source)
		return EINVAL;

	*setp = NULL;
	sa_set_str(&address, "127.0.0.1", 5000);
	err = sdp_session_alloc(&local, &address);
	if (err)
		goto out;
	err = sdp_session_alloc(&remote, &address);
	if (err)
		goto out;
	err = bundle_set_encode(local, source);
	if (err)
		goto out;
	err = sdp_encode(&encoded, local, true);
	if (err)
		goto out;
	err = sdp_decode(remote, encoded, true);
	if (err)
		goto out;
	err = bundle_set_decode(setp, remote);

out:
	mem_deref(encoded);
	mem_deref(remote);
	mem_deref(local);
	return err;
}


static int test_bundle_set(void)
{
	const char *ordered[] = {"BUNDLE 1 0 2"};
	const char *duplicate_mid[] = {"BUNDLE 1 0 1"};
	const char *multiple_groups[] = {
		"BUNDLE 0 1",
		"BUNDLE 2 3",
	};
	const char *overlapping_groups[] = {
		"BUNDLE 0 1",
		"BUNDLE 2 1",
	};
	const struct bundle_group *group;
	struct bundle_set *set = NULL;
	struct bundle_set *roundtrip = NULL;
	int err;

	err = decode_bundle_set(&set, ordered, RE_ARRAY_SIZE(ordered));
	TEST_ERR(err);
	ASSERT_EQ(1, (int)bundle_set_count(set));
	group = bundle_set_group(set, 0);
	ASSERT_TRUE(group != NULL);
	ASSERT_EQ(3, (int)bundle_group_count(group));
	ASSERT_STREQ("1", bundle_group_tag(group));
	ASSERT_STREQ("1", bundle_group_mid(group, 0));
	ASSERT_STREQ("0", bundle_group_mid(group, 1));
	ASSERT_STREQ("2", bundle_group_mid(group, 2));
	ASSERT_TRUE(bundle_group_mid(group, 3) == NULL);
	ASSERT_TRUE(bundle_group_contains(group, "0"));
	ASSERT_TRUE(bundle_group_contains(group, "2"));
	ASSERT_TRUE(!bundle_group_contains(group, "3"));
	ASSERT_TRUE(bundle_set_find_mid(set, "0") == group);
	ASSERT_TRUE(bundle_set_find_mid(set, "3") == NULL);
	set = mem_deref(set);

	err = decode_bundle_set(&set, duplicate_mid,
				RE_ARRAY_SIZE(duplicate_mid));
	ASSERT_EQ(EPROTO, err);
	ASSERT_TRUE(set == NULL);
	err = 0;

	err = decode_bundle_set(&set, multiple_groups,
				RE_ARRAY_SIZE(multiple_groups));
	TEST_ERR(err);
	ASSERT_EQ(2, (int)bundle_set_count(set));
	ASSERT_STREQ("0", bundle_group_tag(bundle_set_group(set, 0)));
	ASSERT_STREQ("2", bundle_group_tag(bundle_set_group(set, 1)));
	ASSERT_TRUE(bundle_set_find_mid(set, "1") ==
		    bundle_set_group(set, 0));
	ASSERT_TRUE(bundle_set_find_mid(set, "3") ==
		    bundle_set_group(set, 1));

	/* The decoded set owns its exact group topology independently of the
	 * source SDP.  Re-encoding it after that SDP is gone must preserve group
	 * order, tag order, and the identity returned for every member. */
	err = roundtrip_bundle_set(&roundtrip, set);
	TEST_ERR(err);
	ASSERT_EQ(2, (int)bundle_set_count(roundtrip));
	ASSERT_STREQ("0", bundle_group_mid(
		bundle_set_group(roundtrip, 0), 0));
	ASSERT_STREQ("1", bundle_group_mid(
		bundle_set_group(roundtrip, 0), 1));
	ASSERT_STREQ("2", bundle_group_mid(
		bundle_set_group(roundtrip, 1), 0));
	ASSERT_STREQ("3", bundle_group_mid(
		bundle_set_group(roundtrip, 1), 1));
	ASSERT_TRUE(bundle_set_find_mid(roundtrip, "0") ==
		    bundle_set_group(roundtrip, 0));
	ASSERT_TRUE(bundle_set_find_mid(roundtrip, "1") ==
		    bundle_set_group(roundtrip, 0));
	ASSERT_TRUE(bundle_set_find_mid(roundtrip, "2") ==
		    bundle_set_group(roundtrip, 1));
	ASSERT_TRUE(bundle_set_find_mid(roundtrip, "3") ==
		    bundle_set_group(roundtrip, 1));
	roundtrip = mem_deref(roundtrip);
	set = mem_deref(set);

	err = decode_bundle_set(&set, overlapping_groups,
				RE_ARRAY_SIZE(overlapping_groups));
	ASSERT_EQ(EPROTO, err);
	ASSERT_TRUE(set == NULL);
	err = 0;

	err = decode_bundle_set(&set, NULL, 0);
	TEST_ERR(err);
	ASSERT_TRUE(set == NULL);

out:
	mem_deref(roundtrip);
	mem_deref(set);
	return err;
}


static bool bundle_active_group_is(struct bundle_transport *transport,
				   const struct bundle_group *expected)
{
	const struct bundle_group *group =
		bundle_transport_active_group_ref(transport);
	bool equal = group == expected;

	mem_deref((void *)group);
	return equal;
}


static void bundle_route_udp_recv(const struct sa *src, struct mbuf *mb,
				  void *arg)
{
	unsigned *received = arg;

	(void)src;
	(void)mb;
	++*received;
	re_cancel();
}


int test_bundle_route_transaction(void)
{
	const char *group1_value[] = {"BUNDLE 0 1"};
	const char *group2_value[] = {"BUNDLE 1 2"};
	const struct bundle_group *group1;
	const struct bundle_group *group2;
	struct bundle_set *set1 = NULL;
	struct bundle_set *set2 = NULL;
	struct bundle_transport *transport = NULL;
	struct udp_sock *sock1 = NULL;
	struct udp_sock *sock2 = NULL;
	struct list streams = {0};
	struct sa local;
	struct sa remote;
	struct sa sock1_addr;
	struct mbuf *packet = NULL;
	uint64_t route1;
	uint64_t route2;
	unsigned received = 0;
	int err;

	err = decode_bundle_set(&set1, group1_value,
				RE_ARRAY_SIZE(group1_value));
	TEST_ERR(err);
	err = decode_bundle_set(&set2, group2_value,
				RE_ARRAY_SIZE(group2_value));
	TEST_ERR(err);
	group1 = bundle_set_group(set1, 0);
	group2 = bundle_set_group(set2, 0);
	ASSERT_TRUE(group1 != NULL);
	ASSERT_TRUE(group2 != NULL);

	sa_set_str(&local, "127.0.0.1", 0);
	err = udp_listen(&sock1, &local, bundle_route_udp_recv, &received);
	TEST_ERR(err);
	sa_set_str(&local, "127.0.0.1", 0);
	err = udp_listen(&sock2, &local, NULL, NULL);
	TEST_ERR(err);
	sa_set_str(&remote, "127.0.0.1", 5000);

	err = bundle_transport_alloc(&transport, group1, &streams, "2");
	TEST_ERR(err);
	ASSERT_EQ(0, (int)bundle_transport_active_generation(transport));
	ASSERT_TRUE(bundle_active_group_is(transport, NULL));

	/* A prepared exact route is invisible and can be fully aborted. */
	err = bundle_transport_prepare(transport, group1, sock1, &route1);
	TEST_ERR(err);
	ASSERT_TRUE(udp_helper_find(sock1, 40) != NULL);
	ASSERT_EQ(0, (int)bundle_transport_active_generation(transport));
	ASSERT_TRUE(bundle_active_group_is(transport, NULL));
	packet = mbuf_alloc(1);
	ASSERT_TRUE(packet != NULL);
	err = mbuf_write_u8(packet, 0x42);
	TEST_ERR(err);
	packet->pos = 0;
	err = udp_local_get(sock1, &sock1_addr);
	TEST_ERR(err);
	err = udp_send(sock2, &sock1_addr, packet);
	TEST_ERR(err);
	err = re_main_timeout(1000);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)received);
	packet = mem_deref(packet);
	err = bundle_transport_abort(transport, route1);
	TEST_ERR(err);
	ASSERT_TRUE(udp_helper_find(sock1, 40) == NULL);

	/* Activation is reversible back to the explicit legacy route. */
	err = bundle_transport_prepare(transport, group1, sock1, &route1);
	TEST_ERR(err);
	err = bundle_transport_set_remote(transport, route1, &remote);
	TEST_ERR(err);
	mem_threshold_set(0);
	err = bundle_transport_activate(transport, route1);
	mem_threshold_set(-1);
	mem_deref(packet);
	TEST_ERR(err);
	ASSERT_EQ((int)route1,
		  (int)bundle_transport_active_generation(transport));
	ASSERT_TRUE(bundle_active_group_is(transport, group1));
	err = bundle_transport_rollback(transport, route1);
	TEST_ERR(err);
	ASSERT_EQ(0, (int)bundle_transport_active_generation(transport));
	ASSERT_TRUE(bundle_active_group_is(transport, NULL));
	ASSERT_TRUE(udp_helper_find(sock1, 40) == NULL);

	/* Finalize retires only the prior private route helper. */
	err = bundle_transport_prepare(transport, group1, sock1, &route1);
	TEST_ERR(err);
	err = bundle_transport_set_remote(transport, route1, &remote);
	TEST_ERR(err);
	err = bundle_transport_activate(transport, route1);
	TEST_ERR(err);
	err = bundle_transport_finalize(transport, route1);
	TEST_ERR(err);
	ASSERT_TRUE(udp_helper_find(sock1, 40) != NULL);

	/* Route generation comes from the route transaction, not DTLS. */
	err = bundle_transport_prepare(transport, group2, sock2, &route2);
	TEST_ERR(err);
	ASSERT_TRUE(route2 != route1);
	err = bundle_transport_set_remote(transport, route2, &remote);
	TEST_ERR(err);
	err = bundle_transport_activate(transport, route2);
	TEST_ERR(err);
	ASSERT_EQ((int)route2,
		  (int)bundle_transport_active_generation(transport));
	ASSERT_TRUE(bundle_active_group_is(transport, group2));
	err = bundle_transport_rollback(transport, route2);
	TEST_ERR(err);
	ASSERT_EQ((int)route1,
		  (int)bundle_transport_active_generation(transport));
	ASSERT_TRUE(bundle_active_group_is(transport, group1));
	ASSERT_TRUE(udp_helper_find(sock2, 40) == NULL);
	ASSERT_TRUE(udp_helper_find(sock1, 40) != NULL);

	/* Unbundling is another committed route, never a NULL transition. */
	err = bundle_transport_prepare_legacy(transport, &route2);
	TEST_ERR(err);
	err = bundle_transport_activate(transport, route2);
	TEST_ERR(err);
	ASSERT_EQ((int)route2,
		  (int)bundle_transport_active_generation(transport));
	ASSERT_TRUE(bundle_active_group_is(transport, NULL));
	err = bundle_transport_rollback(transport, route2);
	TEST_ERR(err);
	ASSERT_EQ((int)route1,
		  (int)bundle_transport_active_generation(transport));
	ASSERT_TRUE(bundle_active_group_is(transport, group1));

	err = bundle_transport_prepare_legacy(transport, &route2);
	TEST_ERR(err);
	err = bundle_transport_activate(transport, route2);
	TEST_ERR(err);
	err = bundle_transport_finalize(transport, route2);
	TEST_ERR(err);
	ASSERT_EQ((int)route2,
		  (int)bundle_transport_active_generation(transport));
	ASSERT_TRUE(bundle_active_group_is(transport, NULL));
	ASSERT_TRUE(udp_helper_find(sock1, 40) == NULL);

out:
	mem_threshold_set(-1);
	mem_deref(transport);
	mem_deref(sock2);
	mem_deref(sock1);
	mem_deref(set2);
	mem_deref(set1);
	return err;
}


static void bundle_test_rtp_handler(const struct rtp_header *hdr,
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


static int bundle_test_pt_handler(uint8_t pt, struct mbuf *mb, void *arg)
{
	(void)pt;
	(void)mb;
	(void)arg;
	return 0;
}


int test_bundle_disjoint_routes(void)
{
	const char *groups[] = {"BUNDLE 0 1", "BUNDLE 2 3"};
	struct stream_param prm = {
		.use_rtp = true,
		.rtcp_mux = true,
		.af = AF_INET,
		.cname = "bundle-route-test",
	};
	const struct bundle_group *group1;
	const struct bundle_group *group2;
	struct bundle_transport *transport1 = NULL;
	struct bundle_transport *transport2 = NULL;
	struct bundle_set *set = NULL;
	struct sdp_session *sdp = NULL;
	struct stream *streamv[4] = {0};
	struct list streams = LIST_INIT;
	struct config_avt cfg;
	struct sa local;
	struct sa remote;
	uint64_t route1;
	uint64_t route2;
	uint64_t rollback_route;
	int err;

	cfg = conf_config()->avt;
	cfg.rtcp_mux = true;
	sa_set_str(&local, "127.0.0.1", 0);
	err = sdp_session_alloc(&sdp, &local);
	TEST_ERR(err);
	for (size_t i = 0; i < RE_ARRAY_SIZE(streamv); ++i) {
		err = stream_alloc(&streamv[i], &streams, &prm, &cfg, sdp,
				   i & 1 ? MEDIA_VIDEO : MEDIA_AUDIO,
				   NULL, NULL, NULL, NULL, true,
				   bundle_test_rtp_handler, NULL,
				   bundle_test_pt_handler, NULL);
		TEST_ERR(err);
		err = stream_bundle_init(streamv[i], false);
		TEST_ERR(err);
		err = bundle_start_socket(stream_bundle(streamv[i]),
				  rtp_sock(stream_rtp_sock(streamv[i])),
				  &streams);
		TEST_ERR(err);
	}
	err = decode_bundle_set(&set, groups, RE_ARRAY_SIZE(groups));
	TEST_ERR(err);
	group1 = bundle_set_group(set, 0);
	group2 = bundle_set_group(set, 1);
	ASSERT_TRUE(group1 != NULL);
	ASSERT_TRUE(group2 != NULL);

	err = bundle_transport_alloc(&transport1, group1, &streams, NULL);
	TEST_ERR(err);
	err = bundle_transport_alloc(&transport2, group2, &streams, NULL);
	TEST_ERR(err);
	sa_set_str(&remote, "127.0.0.1", 5000);
	err = bundle_transport_prepare(transport1, group1,
		rtp_sock(stream_rtp_sock(streamv[0])), &route1);
	TEST_ERR(err);
	ASSERT_TRUE(!bundle_transport_ready(transport1, route1));
	err = bundle_transport_set_remote(transport1, route1, &remote);
	TEST_ERR(err);
	ASSERT_TRUE(bundle_transport_ready(transport1, route1));
	err = bundle_transport_prepare(transport2, group2,
		rtp_sock(stream_rtp_sock(streamv[2])), &route2);
	TEST_ERR(err);
	ASSERT_TRUE(!bundle_transport_ready(transport2, route2));
	err = bundle_transport_set_remote(transport2, route2, &remote);
	TEST_ERR(err);
	ASSERT_TRUE(bundle_transport_ready(transport2, route2));

	/* Preparation does not attach endpoints or change their packet route. */
	for (size_t i = 0; i < RE_ARRAY_SIZE(streamv); ++i) {
		ASSERT_TRUE(!bundle_transport_attached(transport1,
			stream_bundle(streamv[i])));
		ASSERT_TRUE(!bundle_transport_attached(transport2,
			stream_bundle(streamv[i])));
	}
	err = bundle_transport_activate(transport1, route1);
	TEST_ERR(err);
	ASSERT_TRUE(!bundle_transport_ready(transport1, route1));
	err = bundle_transport_activate(transport2, route2);
	TEST_ERR(err);
	ASSERT_TRUE(!bundle_transport_ready(transport2, route2));

	ASSERT_TRUE(bundle_transport_attached(transport1,
		stream_bundle(streamv[0])));
	ASSERT_TRUE(bundle_transport_attached(transport1,
		stream_bundle(streamv[1])));
	ASSERT_TRUE(!bundle_transport_attached(transport1,
		stream_bundle(streamv[2])));
	ASSERT_TRUE(!bundle_transport_attached(transport1,
		stream_bundle(streamv[3])));
	ASSERT_TRUE(bundle_transport_attached(transport2,
		stream_bundle(streamv[2])));
	ASSERT_TRUE(bundle_transport_attached(transport2,
		stream_bundle(streamv[3])));
	ASSERT_EQ(BUNDLE_BASE, bundle_transport_endpoint_state(
		transport1, stream_bundle(streamv[0])));
	ASSERT_EQ(BUNDLE_MUX, bundle_transport_endpoint_state(
		transport1, stream_bundle(streamv[1])));
	ASSERT_EQ(BUNDLE_BASE, bundle_transport_endpoint_state(
		transport2, stream_bundle(streamv[2])));
	ASSERT_EQ(BUNDLE_MUX, bundle_transport_endpoint_state(
		transport2, stream_bundle(streamv[3])));

	err = bundle_transport_finalize(transport1, route1);
	TEST_ERR(err);
	err = bundle_transport_finalize(transport2, route2);
	TEST_ERR(err);

	/* A speculative cross-group move restores the exact prior owner. */
	err = bundle_transport_prepare(transport1, group2,
		rtp_sock(stream_rtp_sock(streamv[2])), &rollback_route);
	TEST_ERR(err);
	ASSERT_TRUE(!bundle_transport_ready(transport1, rollback_route));
	err = bundle_transport_set_remote(transport1, rollback_route, &remote);
	TEST_ERR(err);
	ASSERT_TRUE(bundle_transport_ready(transport1, rollback_route));
	err = bundle_transport_activate(transport1, rollback_route);
	TEST_ERR(err);
	ASSERT_TRUE(!bundle_transport_ready(transport1, rollback_route));
	ASSERT_TRUE(bundle_transport_attached(transport1,
		stream_bundle(streamv[2])));
	err = bundle_transport_rollback(transport1, rollback_route);
	TEST_ERR(err);
	ASSERT_TRUE(bundle_transport_attached(transport2,
		stream_bundle(streamv[2])));
	ASSERT_TRUE(bundle_transport_attached(transport2,
		stream_bundle(streamv[3])));
	ASSERT_TRUE(bundle_transport_attached(transport1,
		stream_bundle(streamv[0])));
	ASSERT_TRUE(bundle_transport_attached(transport1,
		stream_bundle(streamv[1])));

out:
	mem_deref(transport2);
	mem_deref(transport1);
	list_flush(&streams);
	mem_deref(sdp);
	mem_deref(set);
	return err;
}


#ifdef USE_DATACHANNEL
struct transport_role_fixture {
	struct role_transport *last_transport;
	unsigned attempts;
	unsigned allocations;
	unsigned destructions;
	unsigned detaches;
	unsigned errors;
	int fail_next;
	bool last_offerer;
	bool close_on_detach;
};

static struct transport_role_fixture *role_fixture;


struct role_transport {
	struct transport_role_fixture *fixture;
	menc_transport_close_h *closeh;
	void *arg;
};


static void role_transport_destructor(void *arg)
{
	struct role_transport *transport = arg;

	++transport->fixture->destructions;
}


static int role_transport_alloc(
	struct menc_transport **mtp, struct menc_sess *sess,
	struct udp_sock *sock, const struct sa *raddr,
	struct sdp_media *sdpm, bool offerer,
	menc_transport_recv_h *recvh, menc_transport_estab_h *estabh,
	menc_transport_close_h *closeh, void *arg)
{
	struct role_transport *transport;
	int err;

	(void)sess;
	(void)sock;
	(void)raddr;
	(void)recvh;
	(void)estabh;
	(void)closeh;
	(void)arg;
	if (!mtp || !sdpm || !role_fixture)
		return EINVAL;

	transport = mem_zalloc(sizeof(*transport),
			       role_transport_destructor);
	if (!transport)
		return ENOMEM;
	transport->fixture = role_fixture;
	transport->closeh = closeh;
	transport->arg = arg;

	role_fixture->last_offerer = offerer;
	role_fixture->last_transport = transport;
	++role_fixture->attempts;
	err = sdp_media_set_lattr(sdpm, true, "setup", "%s",
				  offerer ? "actpass" : "active");
	err |= sdp_media_set_lattr(sdpm, true, "fingerprint",
				   "SHA-256 AA:BB");
	err |= sdp_media_set_lattr(sdpm, true, "tls-id",
				   "local-transport-%u",
				   role_fixture->attempts);
	if (!err && role_fixture->fail_next) {
		err = role_fixture->fail_next;
		role_fixture->fail_next = 0;
	}
	if (err)
		mem_deref(transport);
	else {
		++role_fixture->allocations;
		*mtp = (struct menc_transport *)transport;
	}

	return err;
}


static void role_transport_detach(struct menc_transport *mt)
{
	struct role_transport *transport = (struct role_transport *)mt;

	++transport->fixture->detaches;
	if (transport->fixture->close_on_detach && transport->closeh) {
		transport->fixture->close_on_detach = false;
		transport->closeh(EPIPE, transport->arg);
	}
}


static void role_transport_error(int err, void *arg)
{
	struct transport_role_fixture *fixture = arg;

	(void)err;
	++fixture->errors;
}


static int decode_data_description(struct sdp_session *sdp, bool offer,
				   const char *setup, const char *tls_id,
				   const char *ice_generation)
{
	struct mbuf *mb = mbuf_alloc(512);
	int err;

	if (!mb)
		return ENOMEM;

	err = mbuf_printf(
		mb,
		"v=0\r\n"
		"o=- 1 1 IN IP4 127.0.0.1\r\n"
		"s=-\r\n"
		"c=IN IP4 127.0.0.1\r\n"
		"t=0 0\r\n"
		"m=application 5001 UDP/DTLS/SCTP webrtc-datachannel\r\n"
		"a=mid:0\r\n"
		"a=sendrecv\r\n"
		"a=sctp-port:5000\r\n"
		"a=max-message-size:16384\r\n"
		"a=ice-ufrag:ufrag-%s\r\n"
		"a=ice-pwd:password-%s-password\r\n"
		"a=setup:%s\r\n"
		"a=fingerprint:SHA-256 CC:DD\r\n"
		"a=tls-id:%s\r\n",
		ice_generation, ice_generation, setup, tls_id);
	if (!err) {
		mb->pos = 0;
		err = sdp_decode(sdp, mb, offer);
	}

	mem_deref(mb);
	return err;
}


static int test_replacement_uses_current_role(void)
{
	static const struct mnat mnat = {
		.id = "role-test",
	};
	static const struct menc menc = {
		.id = "role-test",
		.transporth = role_transport_alloc,
	};
	struct transport_role_fixture fixture = {0};
	struct data_context *ctx = NULL;
	struct sdp_session *sdp = NULL;
	struct mbuf *local_offer = NULL;
	struct list streaml = LIST_INIT;
	struct sa address;
	int err;

	sa_set_str(&address, "127.0.0.1", 0);
	role_fixture = &fixture;
	err = sdp_session_alloc(&sdp, &address);
	TEST_ERR(err);
	err = data_context_alloc(&ctx, sdp, &mnat, NULL, &menc, NULL,
				 NULL, &streaml, AF_INET, true, NULL, &fixture);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)fixture.allocations);
	ASSERT_TRUE(fixture.last_offerer);
	err = sdp_encode(&local_offer, sdp, true);
	TEST_ERR(err);
	local_offer = mem_deref(local_offer);

	err = decode_data_description(
		sdp, false, "active", "remote-transport-one", "one");
	TEST_ERR(err);
	err = data_context_remote_update(ctx, false);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)fixture.allocations);
	err = decode_data_description(
		sdp, true, "actpass", "remote-transport-two", "one");
	TEST_ERR(err);
	err = data_context_remote_update(ctx, true);
	ASSERT_EQ(EPROTO, err);
	err = 0;
	ASSERT_EQ(1, (int)fixture.allocations);

	err = decode_data_description(
		sdp, true, "actpass", "remote-transport-two", "two");
	TEST_ERR(err);
	err = data_context_remote_update(ctx, true);
	TEST_ERR(err);
	ASSERT_EQ(2, (int)fixture.allocations);
	ASSERT_TRUE(!fixture.last_offerer);

out:
	mem_deref(local_offer);
	mem_deref(ctx);
	mem_deref(sdp);
	role_fixture = NULL;
	return err;
}


static int test_replacement_allocation_rollback(void)
{
	static const struct mnat mnat = {
		.id = "rollback-test",
	};
	static const struct menc menc = {
		.id = "rollback-test",
		.transporth = role_transport_alloc,
	};
	struct transport_role_fixture fixture = {0};
	struct data_context *ctx = NULL;
	struct sdp_session *sdp = NULL;
	struct mbuf *initial_offer = NULL;
	struct mbuf *before = NULL;
	struct mbuf *after = NULL;
	struct list streaml = LIST_INIT;
	struct sa address;
	int err;

	sa_set_str(&address, "127.0.0.1", 0);
	role_fixture = &fixture;
	err = sdp_session_alloc(&sdp, &address);
	TEST_ERR(err);
	err = data_context_alloc(&ctx, sdp, &mnat, NULL, &menc, NULL,
				 NULL, &streaml, AF_INET, true, NULL, &fixture);
	TEST_ERR(err);
	err = sdp_encode(&initial_offer, sdp, true);
	TEST_ERR(err);
	err = decode_data_description(
		sdp, false, "active", "remote-transport-one", "one");
	TEST_ERR(err);
	err = data_context_remote_update(ctx, false);
	TEST_ERR(err);
	err = sdp_encode(&before, sdp, true);
	TEST_ERR(err);

	fixture.fail_next = ENOMEM;
	err = decode_data_description(
		sdp, true, "actpass", "remote-transport-two", "two");
	TEST_ERR(err);
	err = data_context_remote_update(ctx, true);
	ASSERT_EQ(ENOMEM, err);
	err = 0;
	err = sdp_encode(&after, sdp, true);
	TEST_ERR(err);
	{
		static const char media_marker[] = "\r\nm=";
		const uint8_t *before_media = NULL;
		const uint8_t *after_media = NULL;

		for (size_t i = 0;
		     i + sizeof(media_marker) - 1 <= before->end; ++i) {
			if (!memcmp(before->buf + i, media_marker,
				    sizeof(media_marker) - 1)) {
				before_media = before->buf + i;
				break;
			}
		}
		for (size_t i = 0;
		     i + sizeof(media_marker) - 1 <= after->end; ++i) {
			if (!memcmp(after->buf + i, media_marker,
				    sizeof(media_marker) - 1)) {
				after_media = after->buf + i;
				break;
			}
		}
		ASSERT_TRUE(before_media != NULL);
		ASSERT_TRUE(after_media != NULL);
		ASSERT_EQ(before->end - (size_t)(before_media - before->buf),
			  after->end - (size_t)(after_media - after->buf));
		ASSERT_TRUE(!memcmp(
			before_media, after_media,
			before->end -
				(size_t)(before_media - before->buf)));
	}
	ASSERT_EQ(2, (int)fixture.attempts);
	ASSERT_EQ(1, (int)fixture.allocations);
	ASSERT_EQ(1, (int)fixture.destructions);

out:
	mem_deref(after);
	mem_deref(before);
	mem_deref(initial_offer);
	mem_deref(ctx);
	mem_deref(sdp);
	if (!err)
		ASSERT_EQ(2, (int)fixture.destructions);
	role_fixture = NULL;
	return err;
}


static int test_pending_transport_close_preserves_active(void)
{
	static const struct mnat mnat = {
		.id = "pending-close-test",
	};
	static const struct menc menc = {
		.id = "pending-close-test",
		.transporth = role_transport_alloc,
		.transportdetachh = role_transport_detach,
	};
	struct transport_role_fixture fixture = {0};
	struct role_transport *pending;
	struct data_context *ctx = NULL;
	struct sdp_session *sdp = NULL;
	struct mbuf *local_offer = NULL;
	struct list streaml = LIST_INIT;
	struct sa address;
	int err;

	sa_set_str(&address, "127.0.0.1", 0);
	role_fixture = &fixture;
	err = sdp_session_alloc(&sdp, &address);
	TEST_ERR(err);
	err = data_context_alloc(&ctx, sdp, &mnat, NULL, &menc, NULL,
				 NULL, &streaml, AF_INET, true,
				 role_transport_error, &fixture);
	TEST_ERR(err);
	err = sdp_encode(&local_offer, sdp, true);
	TEST_ERR(err);

	err = decode_data_description(
		sdp, false, "active", "remote-transport-one", "one");
	TEST_ERR(err);
	err = data_context_remote_update(ctx, false);
	TEST_ERR(err);
	ASSERT_EQ(1, (int)fixture.allocations);

	err = decode_data_description(
		sdp, true, "actpass", "remote-transport-two", "two");
	TEST_ERR(err);
	err = data_context_remote_update(ctx, true);
	TEST_ERR(err);
	ASSERT_EQ(2, (int)fixture.allocations);
	pending = fixture.last_transport;
	ASSERT_TRUE(pending != NULL);

	fixture.close_on_detach = true;
	pending->closeh(EPIPE, pending->arg);
	ASSERT_EQ(0, (int)fixture.errors);
	ASSERT_EQ(1, (int)fixture.detaches);
	ASSERT_EQ(1, (int)fixture.destructions);

out:
	mem_deref(local_offer);
	mem_deref(ctx);
	if (!err)
		ASSERT_EQ(2, (int)fixture.destructions);
	mem_deref(sdp);
	role_fixture = NULL;
	return err;
}


static int test_dcmap(void)
{
	static const char *invalid[] = {
		"65536",
		"2 ",
		"2 label=msrp",
		"2 label=\"bad%00label\"",
		"2 label=\"one\";label=\"two\"",
		"2 max-retr=1;max-time=2",
		"2 ordered=garbage",
		"2 ordered=FALSE",
		"2 unsupported=true",
	};
	struct dcmap map = {0};
	struct pl attribute;
	const struct pl expected_attribute =
		PL("accept-types:message/cpim text/plain");
	char *encoded = NULL;
	uint16_t id;
	int err;

	err = dcmap_decode(&map,
			   "2 subprotocol=\"msrp\";ordered=false;"
			   "label=\"m%73rp\";max-retr=5;priority=512");
	TEST_ERR(err);
	ASSERT_EQ(2, map.id);
	ASSERT_STREQ("msrp", map.protocol);
	ASSERT_STREQ("msrp", map.label);
	ASSERT_TRUE(!map.ordered);
	ASSERT_EQ(5, map.max_retransmits);
	ASSERT_EQ(-1, map.max_packet_lifetime);
	ASSERT_EQ(512, map.priority);
	err = re_sdprintf(&encoded, "%H", dcmap_print, &map);
	TEST_ERR(err);
	ASSERT_STREQ("2 subprotocol=\"msrp\";label=\"msrp\";"
		     "ordered=false;max-retr=5;priority=512", encoded);
	dcmap_reset(&map);
	encoded = mem_deref(encoded);

	for (size_t i = 0; i < RE_ARRAY_SIZE(invalid); ++i) {
		err = dcmap_decode(&map, invalid[i]);
		ASSERT_EQ(EPROTO, err);
	}

	err = dcsa_decode(&id, &attribute,
			  "2 accept-types:message/cpim text/plain");
	TEST_ERR(err);
	ASSERT_EQ(2, id);
	ASSERT_PLEQ(&expected_attribute, &attribute);
	err = dcsa_decode(&id, &attribute, "2 bad attribute:value");
	ASSERT_EQ(EPROTO, err);
	err = 0;

out:
	dcmap_reset(&map);
	mem_deref(encoded);
	return err;
}


static int validation_transport_alloc(
	struct menc_transport **mtp, struct menc_sess *sess,
	struct udp_sock *sock, const struct sa *raddr, struct sdp_media *sdpm,
	bool offerer, menc_transport_recv_h *recvh,
	menc_transport_estab_h *estabh, menc_transport_close_h *closeh,
	void *arg)
{
	(void)mtp;
	(void)sess;
	(void)sock;
	(void)raddr;
	(void)sdpm;
	(void)offerer;
	(void)recvh;
	(void)estabh;
	(void)closeh;
	(void)arg;
	return 0;
}


static void validation_channel_handler(struct data_channel *dc, void *arg)
{
	unsigned *count = arg;

	++*count;
	(void)datachannel_close(dc);
}


static int test_data_sdp_validation_case(const char *sctp_port,
					 const char *message_size,
					 const char *dcmaps,
					 bool existing_channel,
					 unsigned *callbacks)
{
	const struct mnat mnat = {0};
	const struct menc menc = {
		.transporth = validation_transport_alloc,
	};
	const struct data_channel_config config = {
		.ordered = true,
		.max_retransmits = -1,
		.max_packet_lifetime = -1,
		.protocol = "existing",
		.negotiated = true,
		.id = 2,
	};
	struct data_context *ctx = NULL;
	struct data_channel *dc = NULL;
	struct sdp_session *sdp = NULL;
	struct mbuf *offer = NULL;
	struct list streams = {0};
	struct sa address;
	int err;

	sa_set_str(&address, "127.0.0.1", 5000);
	err = sdp_session_alloc(&sdp, &address);
	TEST_ERR(err);
	err = data_context_alloc(&ctx, sdp, &mnat, NULL, &menc, NULL, NULL,
				 &streams, AF_INET, false, NULL, NULL);
	TEST_ERR(err);
	err = data_context_set_handler(ctx, validation_channel_handler,
				       callbacks);
	TEST_ERR(err);
	if (existing_channel) {
		err = data_context_channel_create(ctx, "existing", &config, &dc);
		TEST_ERR(err);
	}
	err = data_context_description_begin(ctx);
	TEST_ERR(err);

	offer = mbuf_alloc(1024);
	if (!offer) {
		err = ENOMEM;
		goto out;
	}
	err = mbuf_printf(
		offer,
		"v=0\r\n"
		"o=- 1 1 IN IP4 127.0.0.1\r\n"
		"s=-\r\n"
		"c=IN IP4 127.0.0.1\r\n"
		"t=0 0\r\n"
		"m=application 5000 UDP/DTLS/SCTP webrtc-datachannel\r\n"
		"a=mid:0\r\n"
		"a=setup:actpass\r\n"
		"a=fingerprint:SHA-256 AA:BB\r\n"
		"a=tls-id:abcdefghijklmnopqrstuvwx\r\n"
		"a=sctp-port:%s\r\n"
		"a=max-message-size:%s\r\n"
		"%s",
		sctp_port, message_size, dcmaps ? dcmaps : "");
	TEST_ERR(err);
	offer->pos = 0;
	err = sdp_decode(sdp, offer, true);
	TEST_ERR(err);
	err = data_context_remote_update(ctx, true);

out:
	if (ctx) {
		if (err)
			data_context_description_abort(ctx);
		else
			data_context_rollback(ctx);
	}
	mem_deref(dc);
	mem_deref(ctx);
	mem_deref(offer);
	mem_deref(sdp);
	return err;
}


static int test_data_sdp_validation(void)
{
	static const struct {
		const char *port;
		const char *limit;
	} invalid[] = {
		{"+5000", "16384"},
		{" 5000", "16384"},
		{"-001", "16384"},
		{"5000", "+16384"},
		{"5000", " 16384"},
		{"5000", "-0001"},
		{"5000", "12x"},
	};
	unsigned callbacks = 0;
	int err;

	for (size_t i = 0; i < RE_ARRAY_SIZE(invalid); ++i) {
		err = test_data_sdp_validation_case(
			invalid[i].port, invalid[i].limit, NULL, false,
			&callbacks);
		ASSERT_EQ(EPROTO, err);
		ASSERT_EQ(0, callbacks);
	}

	err = test_data_sdp_validation_case(
		"5000", "16384",
		"a=dcmap:4 label=\"new\"\r\n"
		"a=dcmap:2 label=\"conflict\";subprotocol=\"existing\"\r\n",
		true, &callbacks);
	ASSERT_EQ(EPROTO, err);
	ASSERT_EQ(0, callbacks);

	/* A valid provisional dcmap is logical SDP state only: it must not be
	 * exposed to the application and rollback must discard it silently. */
	err = test_data_sdp_validation_case(
		"5000", "16384", "a=dcmap:4 label=\"new\"\r\n",
		false, &callbacks);
	TEST_ERR(err);
	ASSERT_EQ(0, callbacks);
	err = 0;

out:
	return err;
}


void peerconn_validation_gather_handler(void *arg)
{
	struct gather_wait *wait = arg;

	++wait->calls;
	wait->ready = true;
	re_cancel();
}


static int test_invalid_create_preserves_sdp(const struct mnat *mnat,
					     const struct menc *menc)
{
	const struct rtc_configuration rtc = {
		.offerer = true,
	};
	const struct data_channel_config invalid = {
		.ordered = true,
		.max_retransmits = 0,
		.max_packet_lifetime = 0,
	};
	struct gather_wait wait = {0};
	struct peer_connection *pc = NULL;
	struct data_channel *dc = NULL;
	struct mbuf *offer = NULL;
	bool g711_loaded = false;
	int err;

	err = module_load(".", "g711");
	TEST_ERR(err);
	g711_loaded = true;
	err = peerconnection_new(&pc, &rtc, mnat, menc,
			 peerconn_validation_gather_handler, NULL, NULL, &wait);
	TEST_ERR(err);
	err = peerconnection_add_audio_track(pc, conf_config(),
					     baresip_aucodecl(),
					     SDP_SENDRECV);
	TEST_ERR(err);
	if (!wait.ready) {
		err = re_main_timeout(10000);
		TEST_ERR(err);
	}
	ASSERT_TRUE(wait.ready);
	err = peerconnection_create_datachannel(
		pc, "invalid", &invalid, &dc);
	ASSERT_EQ(EINVAL, err);
	ASSERT_TRUE(dc == NULL);
	err = peerconnection_create_offer(pc, &offer);
	TEST_ERR(err);
	ASSERT_TRUE(!mbuf_contains(offer, "m=application "));

out:
	mem_deref(offer);
	mem_deref(dc);
	mem_deref(pc);
	if (g711_loaded)
		module_unload("g711");
	return err;
}


static int test_local_offer_rollback_restores_state(const struct mnat *mnat,
						    const struct menc *menc)
{
	static const char malformed_remote[] =
		"v=0\r\n"
		"o=- 3 3 IN IP4 203.0.113.1\r\n"
		"s=-\r\n"
		"t=0 0\r\n"
		"a=x-session:partial\r\n"
		"m=application 7000 UDP/DTLS/SCTP webrtc-datachannel\r\n"
		"a=mid:0\r\n"
		"a=sctp-port:5000\r\n"
		"a=max-message-size:16384\r\n"
		"malformed-line\r\n";
	const struct rtc_configuration rtc = {
		.offerer = true,
	};
	const struct data_channel_config config = {
		.ordered = true,
		.max_retransmits = -1,
		.max_packet_lifetime = -1,
	};
	const struct session_description rollback = {
		.type = SDP_ROLLBACK,
	};
	struct gather_wait wait = {0};
	struct peer_connection *pc = NULL;
	struct data_channel *dc = NULL;
	struct mbuf *first = NULL;
	struct mbuf *second = NULL;
	struct mbuf *malformed = NULL;
	int err;

	err = peerconnection_new(&pc, &rtc, mnat, menc,
			 peerconn_validation_gather_handler, NULL, NULL, &wait);
	TEST_ERR(err);
	err = peerconnection_create_datachannel(pc, "rollback", &config, &dc);
	TEST_ERR(err);
	if (!wait.ready) {
		err = re_main_timeout(10000);
		TEST_ERR(err);
	}
	ASSERT_TRUE(wait.ready);

	err = peerconnection_create_offer(pc, &first);
	TEST_ERR(err);
	ASSERT_EQ(SS_HAVE_LOCAL_OFFER, peerconnection_signaling(pc));
	err = peerconnection_set_remote_descr(pc, &rollback);
	TEST_ERR(err);
	ASSERT_EQ(SS_STABLE, peerconnection_signaling(pc));

	malformed = mbuf_alloc(sizeof(malformed_remote));
	ASSERT_TRUE(malformed != NULL);
	err = mbuf_write_str(malformed, malformed_remote);
	TEST_ERR(err);
	malformed->pos = 0;
	{
		const struct session_description description = {
			.type = SDP_OFFER,
			.sdp = malformed,
		};

		err = peerconnection_set_remote_descr(pc, &description);
		ASSERT_TRUE(err != 0);
		err = 0;
	}
	ASSERT_EQ(SS_STABLE, peerconnection_signaling(pc));

	err = peerconnection_create_offer(pc, &second);
	TEST_ERR(err);
	ASSERT_EQ(first->end, second->end);
	ASSERT_TRUE(!memcmp(first->buf, second->buf, first->end));

	err = peerconnection_set_remote_descr(pc, &rollback);
	TEST_ERR(err);
	ASSERT_EQ(SS_STABLE, peerconnection_signaling(pc));

out:
	mem_deref(malformed);
	mem_deref(second);
	mem_deref(first);
	mem_deref(dc);
	mem_deref(pc);
	return err;
}


static int test_answer_allocation_retry(const struct mnat *mnat,
					const struct menc *menc)
{
	const struct rtc_configuration offer_config = {.offerer = true};
	const struct rtc_configuration answer_config = {.offerer = false};
	const struct data_channel_config config = {
		.ordered = true,
		.max_retransmits = -1,
		.max_packet_lifetime = -1,
		.negotiated = true,
		.id = 2,
	};
	struct session_description description;
	struct gather_wait offer_wait = {0};
	struct gather_wait answer_wait = {0};
	struct peer_connection *offerer = NULL;
	struct peer_connection *answerer = NULL;
	struct data_channel *offer_dc = NULL;
	struct data_channel *answer_dc = NULL;
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	int err;

	err = peerconnection_new(&offerer, &offer_config, mnat, menc,
		peerconn_validation_gather_handler, NULL, NULL, &offer_wait);
	TEST_ERR(err);
	err = peerconnection_create_datachannel(
		offerer, "answer-retry", &config, &offer_dc);
	TEST_ERR(err);
	err = peerconnection_new(&answerer, &answer_config, mnat, menc,
		peerconn_validation_gather_handler, NULL, NULL, &answer_wait);
	TEST_ERR(err);
	err = peerconnection_create_datachannel(
		answerer, "answer-retry", &config, &answer_dc);
	TEST_ERR(err);
	if (!offer_wait.ready || !answer_wait.ready) {
		err = re_main_timeout(10000);
		TEST_ERR(err);
	}

	err = peerconnection_create_offer(offerer, &offer);
	TEST_ERR(err);
	description.type = SDP_OFFER;
	description.sdp = offer;
	err = peerconnection_set_remote_descr(answerer, &description);
	TEST_ERR(err);

#ifndef NDEBUG
	mem_threshold_set(0);
	err = peerconnection_create_answer(answerer, &answer);
	mem_threshold_set(-1);
	ASSERT_EQ(ENOMEM, err);
	err = 0;
	ASSERT_TRUE(answer == NULL);
	ASSERT_EQ(SS_HAVE_REMOTE_OFFER,
		  peerconnection_signaling(answerer));
#endif
	err = peerconnection_create_answer(answerer, &answer);
	TEST_ERR(err);

	description.type = SDP_ANSWER;
	description.sdp = answer;
#ifndef NDEBUG
	mem_threshold_set(0);
	err = peerconnection_set_remote_descr(offerer, &description);
	mem_threshold_set(-1);
	ASSERT_EQ(ENOMEM, err);
	err = 0;
	ASSERT_EQ(SS_HAVE_LOCAL_OFFER, peerconnection_signaling(offerer));
#endif
	err = peerconnection_set_remote_descr(offerer, &description);
	TEST_ERR(err);
	ASSERT_EQ(SS_STABLE, peerconnection_signaling(offerer));

out:
	mem_threshold_set(-1);
	mem_deref(answer);
	mem_deref(offer);
	mem_deref(answer_dc);
	mem_deref(offer_dc);
	mem_deref(answerer);
	mem_deref(offerer);
	return err;
}


struct transport_fixture;

struct transport_endpoint {
	struct transport_fixture *fixture;
	struct sdp_session *sdp;
	struct sdp_media *media;
	struct menc_sess *sess;
	struct menc_transport *transport;
	struct udp_sock *udp;
	struct sa address;
	enum menc_dtls_role role;
	unsigned secure_events;
	bool destroy_on_error;
	bool error_destroyed;
};

struct transport_fixture {
	const struct menc *menc;
	struct transport_endpoint client;
	struct transport_endpoint server;
	unsigned established;
	unsigned received;
	unsigned secure;
	int err;
};


static void transport_fail(struct transport_fixture *fixture, int err)
{
	if (!fixture->err)
		fixture->err = err ? err : EPROTO;
	re_cancel();
}


static void transport_media_event_handler(enum menc_event event,
					  const char *prm,
					  struct stream *stream, void *arg)
{
	struct transport_endpoint *endpoint = arg;
	(void)prm;
	(void)stream;

	if (event == MENC_EVENT_SECURE)
		++endpoint->secure_events;
}


static void transport_recv_handler(struct mbuf *mb, void *arg)
{
	struct transport_endpoint *endpoint = arg;
	struct transport_fixture *fixture = endpoint->fixture;
	int err = 0;

	++fixture->received;
	if (mbuf_get_left(mb) != sizeof(transport_payload) ||
	    memcmp(mbuf_buf(mb), transport_payload,
		   sizeof(transport_payload))) {
		transport_fail(fixture, EBADMSG);
		return;
	}

	if (endpoint->role == MENC_DTLS_ROLE_SERVER) {
		err = menc_transport_send(fixture->menc,
					  endpoint->transport, mb);
		if (err)
			transport_fail(fixture, err);
	}
	else {
		re_cancel();
	}
}


static int transport_send_payload(struct transport_endpoint *endpoint)
{
	struct mbuf mb = {
		.buf = (uint8_t *)transport_payload,
		.pos = 0,
		.end = sizeof(transport_payload),
		.size = sizeof(transport_payload),
	};

	return menc_transport_send(endpoint->fixture->menc,
				   endpoint->transport, &mb);
}


static void transport_estab_handler(int err, enum menc_dtls_role role,
				    void *arg)
{
	struct transport_endpoint *endpoint = arg;
	struct transport_fixture *fixture = endpoint->fixture;

	if (err) {
		if (endpoint->destroy_on_error) {
			endpoint->error_destroyed = true;
			endpoint->transport = mem_deref(endpoint->transport);
			endpoint->sess = mem_deref(endpoint->sess);
			re_cancel();
			return;
		}
		transport_fail(fixture, err);
		return;
	}

	endpoint->role = role;
	++fixture->established;

	if (role == MENC_DTLS_ROLE_CLIENT) {
		err = transport_send_payload(endpoint);
		if (err)
			transport_fail(fixture, err);
	}
}


static void transport_endpoint_reset(struct transport_endpoint *endpoint)
{
	endpoint->transport = mem_deref(endpoint->transport);
	endpoint->sess = mem_deref(endpoint->sess);
	endpoint->udp = mem_deref(endpoint->udp);
	endpoint->sdp = mem_deref(endpoint->sdp);
}


static int transport_endpoint_alloc(struct transport_endpoint *endpoint,
				    struct transport_fixture *fixture,
				    bool offerer);


static void transport_wait_handler(void *arg)
{
	bool *expired = arg;

	*expired = true;
	re_cancel();
}


static int test_menc_transport_peer_retarget(struct transport_fixture *fixture)
{
	struct sa old_peer;
	struct sa replaced_peer;
	struct sa unreachable;
	struct tmr wait;
	unsigned received = fixture->received;
	bool expired = false;
	int err;

	sa_cpy(&unreachable, &fixture->client.address);
	sa_set_port(&unreachable, sa_port(&unreachable) == UINT16_MAX
				     ? 1
				     : sa_port(&unreachable) + 1);

	err = menc_transport_peer_set(fixture->menc,
				      fixture->server.transport,
				      &unreachable, &old_peer);
	TEST_ERR(err);
	ASSERT_TRUE(sa_cmp(&old_peer, &fixture->client.address, SA_ALL));

	/* Retargeting must rehash the established DTLS connection.  A record
	 * from the former peer is no longer dispatched to that connection. */
	err = transport_send_payload(&fixture->client);
	TEST_ERR(err);
	tmr_init(&wait);
	tmr_start(&wait, 50, transport_wait_handler, &expired);
	re_main(NULL);
	tmr_cancel(&wait);
	ASSERT_TRUE(expired);
	ASSERT_EQ(received, fixture->received);

	/* The same allocation-free operation is the rollback primitive.  It
	 * returns the replaced address and restores hash lookup and traffic. */
	err = menc_transport_peer_set(fixture->menc,
				      fixture->server.transport,
				      &old_peer, &replaced_peer);
	TEST_ERR(err);
	ASSERT_TRUE(sa_cmp(&replaced_peer, &unreachable, SA_ALL));
	err = transport_send_payload(&fixture->client);
	TEST_ERR(err);
	err = re_main_timeout(1000);
	TEST_ERR(err);
	ASSERT_EQ(received + 2, fixture->received);

	/* Prepare can query the installed peer without changing hash routing. */
	err = menc_transport_peer_set(fixture->menc,
				      fixture->server.transport,
				      NULL, &replaced_peer);
	TEST_ERR(err);
	ASSERT_TRUE(sa_cmp(&replaced_peer, &fixture->client.address, SA_ALL));

	/* A no-op update also snapshots the installed peer for callers. */
	err = menc_transport_peer_set(fixture->menc,
				      fixture->server.transport,
				      &fixture->client.address,
				      &replaced_peer);
	TEST_ERR(err);
	ASSERT_TRUE(sa_cmp(&replaced_peer, &fixture->client.address, SA_ALL));

out:
	return err;
}


static int transport_copy_local_identity(struct sdp_media *dst,
					 const struct sdp_media *src)
{
	static const char *const names[] = {
		"setup", "fingerprint", "tls-id",
	};
	size_t i;
	int err;

	for (i = 0; i < RE_ARRAY_SIZE(names); ++i) {
		const char *value = sdp_media_lattr_apply(src, names[i],
							 NULL, NULL);

		if (!value)
			return EPROTO;
		err = sdp_media_set_lattr(dst, true, names[i], "%s", value);
		if (err)
			return err;
	}

	return 0;
}


static int transport_replacement_alloc(
	struct transport_fixture *replacement,
	const struct transport_fixture *identity_source)
{
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	const struct menc *menc = identity_source->menc;
	int err;

	replacement->menc = menc;
	err = transport_endpoint_alloc(&replacement->server, replacement, true);
	TEST_ERR(err);
	err = transport_endpoint_alloc(&replacement->client, replacement, false);
	TEST_ERR(err);
	err = menc->transporth(&replacement->server.transport,
			       replacement->server.sess, replacement->server.udp,
			       NULL, replacement->server.media, true,
			       transport_recv_handler, transport_estab_handler,
			       NULL, &replacement->server);
	TEST_ERR(err);
	err = transport_copy_local_identity(replacement->server.media,
					    identity_source->server.media);
	TEST_ERR(err);
	err = sdp_encode(&offer, replacement->server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(replacement->client.sdp, offer, true);
	TEST_ERR(err);
	err = menc->transporth(&replacement->client.transport,
			       replacement->client.sess, replacement->client.udp,
			       NULL, replacement->client.media, false,
			       transport_recv_handler, transport_estab_handler,
			       NULL, &replacement->client);
	TEST_ERR(err);
	err = transport_copy_local_identity(replacement->client.media,
					    identity_source->client.media);
	TEST_ERR(err);
	err = sdp_encode(&answer, replacement->client.sdp, false);
	TEST_ERR(err);
	err = sdp_decode(replacement->server.sdp, answer, false);
	TEST_ERR(err);
	err = menc_transport_commit_identity(menc,
					     replacement->client.transport);
	TEST_ERR(err);
	err = menc_transport_commit_identity(menc,
					     replacement->server.transport);
	TEST_ERR(err);
	err = menc_transport_start(menc, replacement->server.transport,
				   &replacement->client.address);
	TEST_ERR(err);
	err = menc_transport_start(menc, replacement->client.transport,
				   &replacement->server.address);
	TEST_ERR(err);
	err = re_main_timeout(3000);
	TEST_ERR(err);
	err = replacement->err;
	TEST_ERR(err);
	ASSERT_EQ(2, replacement->established);
	ASSERT_EQ(2, replacement->received);

out:
	mem_deref(answer);
	mem_deref(offer);
	return err;
}


static int test_menc_member_transaction(struct transport_fixture *fixture)
{
	struct transport_fixture replacement = {0};
	struct transport_endpoint unstarted = {
		.fixture = fixture,
	};
	struct menc_transport *candidate;
	struct menc_media *member = NULL;
	struct menc_media *unstarted_member = NULL;
	struct sdp_media *media = NULL;
	struct sdp_media *peer_media = NULL;
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	size_t active_refs;
	size_t candidate_refs;
	int err;

	if (!fixture || !fixture->menc ||
	    !fixture->menc->transportmembersprepareh)
		return ENOTSUP;

	/* Add an ordinary negotiated RTP member to the established group. */
	err = sdp_media_add(&media, fixture->server.sdp, "audio",
			    sa_port(&fixture->server.address),
			    "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = sdp_media_add(&peer_media, fixture->client.sdp, "audio",
			    sa_port(&fixture->client.address),
			    "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = sdp_format_add(NULL, media, false, "0", "PCMU", 8000, 1,
			     NULL, NULL, NULL, false, NULL);
	err |= sdp_format_add(NULL, peer_media, false, "0", "PCMU", 8000, 1,
			      NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	/*
	 * RFC 8843 requires bundled m-lines to carry matching transport
	 * attributes.  Copy each endpoint's committed tag attributes so this
	 * fixture exercises member transactions rather than the conflict path.
	 */
	err = transport_copy_local_identity(media, fixture->server.media);
	err |= transport_copy_local_identity(peer_media, fixture->client.media);
	TEST_ERR(err);
	err = sdp_encode(&offer, fixture->server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture->client.sdp, offer, true);
	TEST_ERR(err);
	err = sdp_encode(&answer, fixture->client.sdp, false);
	TEST_ERR(err);
	err = sdp_decode(fixture->server.sdp, answer, false);
	TEST_ERR(err);
	ASSERT_TRUE(sdp_media_has_media(media));
	err = transport_replacement_alloc(&replacement, fixture);
	TEST_ERR(err);
	candidate = replacement.server.transport;

	active_refs = mem_nrefs(fixture->server.transport);
	candidate_refs = mem_nrefs(candidate);
	err = fixture->menc->mediah(
		&member, fixture->server.sess, fixture->server.transport,
		NULL, fixture->server.udp, NULL, NULL, NULL, media, NULL);
	TEST_ERR(err);
	ASSERT_EQ(active_refs + 1,
		  mem_nrefs(fixture->server.transport));

	/* Structural preparation must not disturb or retain the candidate. */
	err = fixture->menc->mediah(
		&member, fixture->server.sess, candidate,
		NULL, fixture->server.udp, NULL, NULL, NULL, media, NULL);
	TEST_ERR(err);
	ASSERT_EQ(active_refs + 1,
		  mem_nrefs(fixture->server.transport));
	ASSERT_EQ(candidate_refs,
		  mem_nrefs(candidate));
	err = menc_transport_members_prepare(
		fixture->menc, candidate);
	TEST_ERR(err);

	menc_transport_members_activate(
		fixture->menc, candidate);
	ASSERT_EQ(active_refs + 1,
		  mem_nrefs(fixture->server.transport));
	ASSERT_EQ(candidate_refs + 1,
		  mem_nrefs(candidate));
	menc_transport_members_rollback(
		fixture->menc, candidate);
	ASSERT_EQ(active_refs + 1,
		  mem_nrefs(fixture->server.transport));
	ASSERT_EQ(candidate_refs,
		  mem_nrefs(candidate));

	/* A second transaction can be finalized after a completed rollback. */
	err = fixture->menc->mediah(
		&member, fixture->server.sess, candidate,
		NULL, fixture->server.udp, NULL, NULL, NULL, media, NULL);
	TEST_ERR(err);
	err = menc_transport_members_prepare(
		fixture->menc, candidate);
	TEST_ERR(err);
	menc_transport_members_activate(
		fixture->menc, candidate);
	menc_transport_members_finalize(
		fixture->menc, candidate);
	ASSERT_EQ(active_refs, mem_nrefs(fixture->server.transport));
	ASSERT_EQ(candidate_refs + 1,
		  mem_nrefs(candidate));

	/* Aborting a reverse preparation leaves the finalized member intact. */
	err = fixture->menc->mediah(
		&member, fixture->server.sess, fixture->server.transport,
		NULL, fixture->server.udp, fixture->client.udp,
		NULL, NULL, media, NULL);
	ASSERT_EQ(EPROTO, err);
	err = 0;
	err = fixture->menc->mediah(
		&member, fixture->server.sess, fixture->server.transport,
		NULL, fixture->server.udp, NULL, NULL, NULL, media, NULL);
	TEST_ERR(err);
	menc_transport_members_abort(
		fixture->menc, fixture->server.transport);
	ASSERT_EQ(active_refs, mem_nrefs(fixture->server.transport));
	ASSERT_EQ(candidate_refs + 1,
		  mem_nrefs(candidate));

	/* Removing a member is the same reversible structural transaction:
	 * activation detaches it without releasing the rollback owner, rollback
	 * restores it, and only finalize retires the old shared transport. */
	err = menc_transport_member_remove(
		fixture->menc, fixture->server.transport, member);
	TEST_ERR(err);
	err = menc_transport_members_prepare(
		fixture->menc, fixture->server.transport);
	TEST_ERR(err);
	menc_transport_members_activate(
		fixture->menc, fixture->server.transport);
	ASSERT_EQ(candidate_refs + 1,
		  mem_nrefs(candidate));
	menc_transport_members_rollback(
		fixture->menc, fixture->server.transport);
	ASSERT_EQ(candidate_refs + 1,
		  mem_nrefs(candidate));

	err = menc_transport_member_remove(
		fixture->menc, fixture->server.transport, member);
	TEST_ERR(err);
	err = menc_transport_members_prepare(
		fixture->menc, fixture->server.transport);
	TEST_ERR(err);
	menc_transport_members_activate(
		fixture->menc, fixture->server.transport);
	menc_transport_members_finalize(
		fixture->menc, fixture->server.transport);
	ASSERT_EQ(candidate_refs,
		  mem_nrefs(fixture->client.transport));

	/* A detached/null member is staged too: prepare must not attach it,
	 * rollback returns to NULL, and finalize is the first irreversible
	 * attachment. */
	err = menc_transport_member_add(
		fixture->menc, fixture->server.transport, member, true);
	TEST_ERR(err);
	err = menc_transport_members_prepare(
		fixture->menc, fixture->server.transport);
	TEST_ERR(err);
	ASSERT_EQ(active_refs, mem_nrefs(fixture->server.transport));
	menc_transport_members_activate(
		fixture->menc, fixture->server.transport);
	ASSERT_EQ(active_refs + 1,
		  mem_nrefs(fixture->server.transport));
	menc_transport_members_rollback(
		fixture->menc, fixture->server.transport);
	ASSERT_EQ(active_refs, mem_nrefs(fixture->server.transport));
	err = menc_transport_member_add(
		fixture->menc, fixture->server.transport, member, true);
	TEST_ERR(err);
	err = menc_transport_members_prepare(
		fixture->menc, fixture->server.transport);
	TEST_ERR(err);
	menc_transport_members_activate(
		fixture->menc, fixture->server.transport);
	menc_transport_members_finalize(
		fixture->menc, fixture->server.transport);
	ASSERT_EQ(active_refs + 1,
		  mem_nrefs(fixture->server.transport));

	/*
	 * An unestablished old group must acquire the candidate role and emit
	 * its first secure event only after the candidate transaction finalizes.
	 */
	err = transport_endpoint_alloc(&unstarted, fixture, false);
	TEST_ERR(err);
	offer = mem_deref(offer);
	err = sdp_encode(&offer, fixture->server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(unstarted.sdp, offer, true);
	TEST_ERR(err);
	err = fixture->menc->transporth(
		&unstarted.transport, unstarted.sess, unstarted.udp, NULL,
		unstarted.media, false, NULL, NULL, NULL, &unstarted);
	TEST_ERR(err);
	err = transport_copy_local_identity(unstarted.media,
					    fixture->client.media);
	TEST_ERR(err);
	err = menc_transport_commit_identity(fixture->menc,
					     unstarted.transport);
	TEST_ERR(err);
	active_refs = mem_nrefs(unstarted.transport);
	err = fixture->menc->mediah(
		&unstarted_member, unstarted.sess, unstarted.transport,
		NULL, unstarted.udp, NULL, NULL, NULL,
		unstarted.media, NULL);
	TEST_ERR(err);
	ASSERT_EQ(0, unstarted.secure_events);
	ASSERT_EQ(active_refs + 1, mem_nrefs(unstarted.transport));
	err = fixture->menc->mediah(
		&unstarted_member, unstarted.sess, fixture->client.transport,
		NULL, unstarted.udp, NULL, NULL, NULL,
		unstarted.media, NULL);
	TEST_ERR(err);
	err = menc_transport_members_prepare(
		fixture->menc, fixture->client.transport);
	TEST_ERR(err);
	menc_transport_members_activate(
		fixture->menc, fixture->client.transport);
	ASSERT_EQ(0, unstarted.secure_events);
	menc_transport_members_finalize(
		fixture->menc, fixture->client.transport);
	ASSERT_EQ(1, unstarted.secure_events);
	ASSERT_EQ(active_refs, mem_nrefs(unstarted.transport));

out:
	mem_deref(answer);
	mem_deref(offer);
	mem_deref(unstarted_member);
	mem_deref(member);
	transport_endpoint_reset(&unstarted);
	transport_endpoint_reset(&replacement.client);
	transport_endpoint_reset(&replacement.server);
	return err;
}


static int transport_endpoint_alloc(struct transport_endpoint *endpoint,
				    struct transport_fixture *fixture,
				    bool offerer)
{
	int err;

	endpoint->fixture = fixture;
	sa_set_str(&endpoint->address, "127.0.0.1", 0);

	err = udp_listen(&endpoint->udp, &endpoint->address, NULL, NULL);
	TEST_ERR(err);
	err = udp_local_get(endpoint->udp, &endpoint->address);
	TEST_ERR(err);
	err = sdp_session_alloc(&endpoint->sdp, &endpoint->address);
	TEST_ERR(err);
	err = sdp_media_add(&endpoint->media, endpoint->sdp, "application",
			    sa_port(&endpoint->address), "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_format_add(NULL, endpoint->media, false,
			     "webrtc-datachannel", NULL, 0, 0,
			     NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	err = fixture->menc->sessh(&endpoint->sess, endpoint->sdp, offerer,
				   transport_media_event_handler,
				   NULL, endpoint);
	TEST_ERR(err);

out:
	return err;
}


static int test_menc_transport(const struct menc *menc, bool test_members)
{
	struct transport_fixture fixture = {
		.menc = menc,
	};
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	int err;

	if (!menc->transporth || !menc->transportsendh)
		return ENOTSUP;

	err = transport_endpoint_alloc(&fixture.server, &fixture, true);
	TEST_ERR(err);
	err = transport_endpoint_alloc(&fixture.client, &fixture, false);
	TEST_ERR(err);
	err = menc->transporth(&fixture.server.transport,
			       fixture.server.sess, fixture.server.udp, NULL,
			       fixture.server.media, true,
			       transport_recv_handler, transport_estab_handler,
			       NULL, &fixture.server);
	TEST_ERR(err);

	err = sdp_encode(&offer, fixture.server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture.client.sdp, offer, true);
	TEST_ERR(err);

	err = menc->transporth(&fixture.client.transport,
			       fixture.client.sess, fixture.client.udp,
			       &fixture.server.address, fixture.client.media,
			       false, transport_recv_handler,
			       transport_estab_handler, NULL, &fixture.client);
	TEST_ERR(err);

	err = sdp_encode(&answer, fixture.client.sdp, false);
	TEST_ERR(err);
	err = sdp_decode(fixture.server.sdp, answer, false);
	TEST_ERR(err);

	err = menc_transport_commit_identity(menc, fixture.server.transport);
	TEST_ERR(err);
	err = menc_transport_start(menc, fixture.server.transport,
				   &fixture.client.address);
	TEST_ERR(err);

	err = re_main_timeout(3000);
	TEST_ERR(err);
	err = fixture.err;
	TEST_ERR(err);
	ASSERT_EQ(2, fixture.established);
	ASSERT_EQ(2, fixture.received);
	ASSERT_EQ(MENC_DTLS_ROLE_CLIENT, fixture.client.role);
	ASSERT_EQ(MENC_DTLS_ROLE_SERVER, fixture.server.role);
	err = test_menc_transport_peer_retarget(&fixture);
	TEST_ERR(err);
	if (test_members) {
		err = test_menc_member_transaction(&fixture);
		TEST_ERR(err);
	}

out:
	mem_deref(answer);
	mem_deref(offer);
	transport_endpoint_reset(&fixture.client);
	transport_endpoint_reset(&fixture.server);
	return err;
}


static int test_menc_transport_preserves_session_attrs(const struct menc *menc)
{
	struct transport_fixture fixture = {
		.menc = menc,
	};
	struct sdp_media *rtp_media = NULL;
	int err;

	err = transport_endpoint_alloc(&fixture.client, &fixture, false);
	TEST_ERR(err);
	err = sdp_media_add(&rtp_media, fixture.client.sdp, "audio",
			    sa_port(&fixture.client.address),
			    "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = menc->transporth(&fixture.client.transport,
			       fixture.client.sess, fixture.client.udp, NULL,
			       fixture.client.media, false, NULL, NULL, NULL,
			       &fixture.client);
	TEST_ERR(err);

	ASSERT_TRUE(sdp_media_lattr_apply(
			    rtp_media, "setup", NULL, NULL) != NULL);
	ASSERT_TRUE(sdp_media_lattr_apply(
			    rtp_media, "fingerprint", NULL, NULL) != NULL);
	ASSERT_TRUE(sdp_session_lattr_apply(
			    fixture.client.sdp, "setup", NULL, NULL) == NULL);
	ASSERT_TRUE(sdp_session_lattr_apply(
			    fixture.client.sdp, "fingerprint",
			    NULL, NULL) == NULL);

out:
	transport_endpoint_reset(&fixture.client);
	return err;
}


static int test_menc_transport_unstarted_promotion(const struct menc *menc)
{
	struct sdp_session *sdp = NULL;
	struct sdp_media *media = NULL;
	struct menc_sess *sess = NULL;
	struct menc_media *menc_media = NULL;
	struct menc_transport *transport = NULL;
	struct udp_sock *udp = NULL;
	struct sa address;
	int err;

	sa_set_str(&address, "127.0.0.1", 0);
	err = udp_listen(&udp, &address, NULL, NULL);
	TEST_ERR(err);
	err = udp_local_get(udp, &address);
	TEST_ERR(err);
	err = sdp_session_alloc(&sdp, &address);
	TEST_ERR(err);
	err = sdp_media_add(&media, sdp, "audio", sa_port(&address),
			    "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = menc->sessh(&sess, sdp, false, NULL, NULL, NULL);
	TEST_ERR(err);
	err = menc->mediah(&menc_media, sess, NULL, NULL, udp, udp,
			   NULL, NULL, media, NULL);
	TEST_ERR(err);

	err = menc->transportpromoteh(&transport, menc_media, NULL, NULL,
				      NULL, NULL);
	ASSERT_EQ(ENOENT, err);
	ASSERT_TRUE(transport == NULL);
	err = 0;

out:
	mem_deref(transport);
	mem_deref(menc_media);
	mem_deref(sess);
	mem_deref(sdp);
	mem_deref(udp);
	return err;
}


static int test_menc_transport_inprogress_promotion(const struct menc *menc)
{
	struct sdp_session *offer_sdp = NULL;
	struct sdp_session *answer_sdp = NULL;
	struct sdp_media *offer_media = NULL;
	struct sdp_media *answer_media = NULL;
	struct menc_sess *offer_sess = NULL;
	struct menc_sess *answer_sess = NULL;
	struct menc_media *offer_menc = NULL;
	struct menc_media *answer_menc = NULL;
	struct menc_transport *transport = NULL;
	struct udp_sock *offer_udp = NULL;
	struct udp_sock *answer_udp = NULL;
	struct mbuf *offer = NULL;
	struct sa offer_addr;
	struct sa answer_addr;
	int err;

	sa_set_str(&offer_addr, "127.0.0.1", 0);
	sa_set_str(&answer_addr, "127.0.0.1", 0);
	err = udp_listen(&offer_udp, &offer_addr, NULL, NULL);
	TEST_ERR(err);
	err = udp_listen(&answer_udp, &answer_addr, NULL, NULL);
	TEST_ERR(err);
	err = udp_local_get(offer_udp, &offer_addr);
	TEST_ERR(err);
	err = udp_local_get(answer_udp, &answer_addr);
	TEST_ERR(err);
	err = sdp_session_alloc(&offer_sdp, &offer_addr);
	TEST_ERR(err);
	err = sdp_session_alloc(&answer_sdp, &answer_addr);
	TEST_ERR(err);
	err = sdp_media_add(&offer_media, offer_sdp, "audio",
			    sa_port(&offer_addr), "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = sdp_media_add(&answer_media, answer_sdp, "audio",
			    sa_port(&answer_addr), "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = sdp_format_add(NULL, offer_media, false, "0", "PCMU",
			     8000, 1, NULL, NULL, NULL, false, NULL);
	err |= sdp_format_add(NULL, answer_media, false, "0", "PCMU",
			      8000, 1, NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	err = menc->sessh(&offer_sess, offer_sdp, true,
			   NULL, NULL, NULL);
	TEST_ERR(err);
	err = menc->sessh(&answer_sess, answer_sdp, false,
			   NULL, NULL, NULL);
	TEST_ERR(err);
	err = menc->mediah(&offer_menc, offer_sess, NULL, NULL,
			   offer_udp, offer_udp, NULL, NULL,
			   offer_media, NULL);
	TEST_ERR(err);
	err = menc->mediah(&answer_menc, answer_sess, NULL, NULL,
			   answer_udp, answer_udp, NULL, NULL,
			   answer_media, NULL);
	TEST_ERR(err);
	err = sdp_encode(&offer, offer_sdp, true);
	TEST_ERR(err);
	err = sdp_decode(answer_sdp, offer, true);
	TEST_ERR(err);
	err = menc->mediah(&answer_menc, answer_sess, NULL, NULL,
			   answer_udp, answer_udp, &offer_addr, &offer_addr,
			   answer_media, NULL);
	TEST_ERR(err);

	err = menc->transportpromoteh(&transport, answer_menc, NULL, NULL,
				      NULL, NULL);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_TRUE(transport == NULL);
	err = 0;

out:
	mem_deref(transport);
	mem_deref(answer_menc);
	mem_deref(offer_menc);
	mem_deref(answer_sess);
	mem_deref(offer_sess);
	mem_deref(offer);
	mem_deref(answer_sdp);
	mem_deref(offer_sdp);
	mem_deref(answer_udp);
	mem_deref(offer_udp);
	return err;
}


struct promotion_identity_fixture {
	unsigned secure;
	int err;
};


static void promotion_identity_event_handler(enum menc_event event,
					     const char *prm,
					     struct stream *stream, void *arg)
{
	struct promotion_identity_fixture *fixture = arg;

	(void)prm;
	(void)stream;
	if (event == MENC_EVENT_SECURE && ++fixture->secure == 2)
		re_cancel();
}


static void promotion_identity_error_handler(int err, void *arg)
{
	struct promotion_identity_fixture *fixture = arg;

	fixture->err = err ? err : EPROTO;
	re_cancel();
}


static int test_menc_transport_promotion_tls_id(const struct menc *menc)
{
	static const char offer_tls_id[] = "abcdefghijklmnopqrstuvwx";
	static const char changed_tls_id[] = "ABCDEFGHIJKLMNOPQRSTUVWX";
	static const char answer_tls_id[] = "zyxwvutsrqponmlkjihgfedc";
	struct promotion_identity_fixture fixture = {0};
	struct sdp_session *offer_sdp = NULL;
	struct sdp_session *answer_sdp = NULL;
	struct sdp_media *offer_media = NULL;
	struct sdp_media *answer_media = NULL;
	struct menc_sess *offer_sess = NULL;
	struct menc_sess *answer_sess = NULL;
	struct menc_media *offer_menc = NULL;
	struct menc_media *answer_menc = NULL;
	struct menc_transport *promoted = NULL;
	struct udp_sock *offer_udp = NULL;
	struct udp_sock *answer_udp = NULL;
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	struct sa offer_addr;
	struct sa answer_addr;
	int err;

	sa_set_str(&offer_addr, "127.0.0.1", 0);
	sa_set_str(&answer_addr, "127.0.0.1", 0);
	err = udp_listen(&offer_udp, &offer_addr, NULL, NULL);
	TEST_ERR(err);
	err = udp_listen(&answer_udp, &answer_addr, NULL, NULL);
	TEST_ERR(err);
	err = udp_local_get(offer_udp, &offer_addr);
	TEST_ERR(err);
	err = udp_local_get(answer_udp, &answer_addr);
	TEST_ERR(err);
	err = sdp_session_alloc(&offer_sdp, &offer_addr);
	TEST_ERR(err);
	err = sdp_session_alloc(&answer_sdp, &answer_addr);
	TEST_ERR(err);
	err = sdp_media_add(&offer_media, offer_sdp, "audio",
			    sa_port(&offer_addr), "UDP/TLS/RTP/SAVPF");
	err |= sdp_media_add(&answer_media, answer_sdp, "audio",
			     sa_port(&answer_addr), "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = sdp_format_add(NULL, offer_media, false, "0", "PCMU",
			     8000, 1, NULL, NULL, NULL, false, NULL);
	err |= sdp_format_add(NULL, answer_media, false, "0", "PCMU",
			      8000, 1, NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	err = menc->sessh(&offer_sess, offer_sdp, true,
			  promotion_identity_event_handler,
			  promotion_identity_error_handler, &fixture);
	err |= menc->sessh(&answer_sess, answer_sdp, false,
			   promotion_identity_event_handler,
			   promotion_identity_error_handler, &fixture);
	TEST_ERR(err);
	err = sdp_session_set_lattr(offer_sdp, true, "tls-id", "%s",
				    offer_tls_id);
	err |= sdp_session_set_lattr(answer_sdp, true, "tls-id", "%s",
				     answer_tls_id);
	TEST_ERR(err);
	err = menc->mediah(&offer_menc, offer_sess, NULL, NULL,
			   offer_udp, offer_udp, NULL, NULL, offer_media, NULL);
	err |= menc->mediah(&answer_menc, answer_sess, NULL, NULL,
			    answer_udp, answer_udp, NULL, NULL, answer_media, NULL);
	TEST_ERR(err);

	err = sdp_encode(&offer, offer_sdp, true);
	TEST_ERR(err);
	err = sdp_decode(answer_sdp, offer, true);
	TEST_ERR(err);
	err = menc->mediah(&answer_menc, answer_sess, NULL, NULL,
			   answer_udp, answer_udp, &offer_addr, &offer_addr,
			   answer_media, NULL);
	TEST_ERR(err);
	err = sdp_encode(&answer, answer_sdp, false);
	TEST_ERR(err);
	err = sdp_decode(offer_sdp, answer, false);
	TEST_ERR(err);
	err = menc->mediah(&offer_menc, offer_sess, NULL, NULL,
			   offer_udp, offer_udp, &answer_addr, &answer_addr,
			   offer_media, NULL);
	TEST_ERR(err);
	err = re_main_timeout(3000);
	TEST_ERR(err);
	TEST_ERR(fixture.err);
	ASSERT_EQ(2, fixture.secure);

	/* The established association belongs to offer_tls_id.  Mutating the
	 * live SDP graph must not let promotion relabel it as another RFC 8842
	 * association merely because the certificate and setup role are
	 * unchanged. */
	err = sdp_session_set_lattr(offer_sdp, true, "tls-id", "%s",
				    changed_tls_id);
	TEST_ERR(err);
	offer = mem_deref(offer);
	err = sdp_encode(&offer, offer_sdp, true);
	TEST_ERR(err);
	err = sdp_decode(answer_sdp, offer, true);
	TEST_ERR(err);
	err = menc->transportpromoteh(&promoted, answer_menc, NULL, NULL,
				      NULL, NULL);
	ASSERT_EQ(EPROTO, err);
	ASSERT_TRUE(promoted == NULL);
	err = 0;

	/* Restoring the exact handshake-time identity preserves normal promotion. */
	err = sdp_session_set_lattr(offer_sdp, true, "tls-id", "%s",
				    offer_tls_id);
	TEST_ERR(err);
	offer = mem_deref(offer);
	err = sdp_encode(&offer, offer_sdp, true);
	TEST_ERR(err);
	err = sdp_decode(answer_sdp, offer, true);
	TEST_ERR(err);
	err = menc->transportpromoteh(&promoted, answer_menc, NULL, NULL,
				      NULL, NULL);
	TEST_ERR(err);
	ASSERT_TRUE(promoted != NULL);

out:
	mem_deref(promoted);
	mem_deref(answer);
	mem_deref(offer);
	mem_deref(answer_menc);
	mem_deref(offer_menc);
	mem_deref(answer_sess);
	mem_deref(offer_sess);
	mem_deref(answer_sdp);
	mem_deref(offer_sdp);
	mem_deref(answer_udp);
	mem_deref(offer_udp);
	return err;
}


static int test_menc_transport_fingerprint_destruction(
	const struct menc *menc)
{
	struct transport_fixture fixture = {
		.menc = menc,
	};
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	int err;

	err = transport_endpoint_alloc(&fixture.server, &fixture, true);
	TEST_ERR(err);
	err = transport_endpoint_alloc(&fixture.client, &fixture, false);
	TEST_ERR(err);
	err = sdp_media_set_lattr(
		fixture.server.media, true, "fingerprint",
		"SHA-256 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
		"00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00");
	TEST_ERR(err);

	err = sdp_encode(&offer, fixture.server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture.client.sdp, offer, true);
	TEST_ERR(err);
	fixture.client.destroy_on_error = true;
	err = menc->transporth(&fixture.client.transport,
			       fixture.client.sess, fixture.client.udp,
			       &fixture.server.address, fixture.client.media,
			       false, NULL, transport_estab_handler, NULL,
			       &fixture.client);
	TEST_ERR(err);

	err = sdp_encode(&answer, fixture.client.sdp, false);
	TEST_ERR(err);
	err = sdp_decode(fixture.server.sdp, answer, false);
	TEST_ERR(err);
	err = menc->transporth(&fixture.server.transport,
			       fixture.server.sess, fixture.server.udp,
			       &fixture.client.address, fixture.server.media,
			       true, NULL, transport_estab_handler, NULL,
			       &fixture.server);
	TEST_ERR(err);

	err = re_main_timeout(3000);
	TEST_ERR(err);
	ASSERT_TRUE(fixture.client.error_destroyed);

out:
	mem_deref(answer);
	mem_deref(offer);
	transport_endpoint_reset(&fixture.client);
	transport_endpoint_reset(&fixture.server);
	return err;
}


static int test_menc_transport_committed_identity(const struct menc *menc)
{
	struct transport_fixture fixture = {
		.menc = menc,
	};
	struct mbuf *offer = NULL;
	struct mbuf *answer = NULL;
	char *fingerprint = NULL;
	const char *separator;
	int err;

	err = transport_endpoint_alloc(&fixture.server, &fixture, true);
	TEST_ERR(err);
	err = transport_endpoint_alloc(&fixture.client, &fixture, false);
	TEST_ERR(err);

	/* An unsupported choice preceding equivalent SHA-256 spellings proves
	 * that commit selects, decodes, sorts and deduplicates the supported set. */
	err = str_dup(&fingerprint, sdp_session_lattr_apply(
		fixture.server.sdp, "fingerprint", NULL, NULL));
	TEST_ERR(err);
	separator = strchr(fingerprint, ' ');
	ASSERT_TRUE(separator != NULL);
	err = sdp_session_set_lattr(
		fixture.server.sdp, true, "fingerprint",
		"SHA-1 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
		"00:00:00:00");
	err |= sdp_session_set_lattr(fixture.server.sdp, false,
				     "fingerprint", "%s", fingerprint);
	err |= sdp_session_set_lattr(fixture.server.sdp, false,
				     "fingerprint", "sha-256%s", separator);
	TEST_ERR(err);

	err = sdp_encode(&offer, fixture.server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture.client.sdp, offer, true);
	TEST_ERR(err);
	err = menc->transporth(&fixture.client.transport,
			       fixture.client.sess, fixture.client.udp, NULL,
			       fixture.client.media, false,
			       transport_recv_handler, transport_estab_handler,
			       NULL, &fixture.client);
	TEST_ERR(err);
	err = menc_transport_commit_identity(menc, fixture.client.transport);
	TEST_ERR(err);

	err = sdp_encode(&answer, fixture.client.sdp, false);
	TEST_ERR(err);
	err = sdp_decode(fixture.server.sdp, answer, false);
	TEST_ERR(err);
	err = menc->transporth(&fixture.server.transport,
			       fixture.server.sess, fixture.server.udp, NULL,
			       fixture.server.media, true,
			       transport_recv_handler, transport_estab_handler,
			       NULL, &fixture.server);
	TEST_ERR(err);
	err = menc_transport_commit_identity(menc, fixture.server.transport);
	TEST_ERR(err);

	/* A later provisional description mutates the live remote SDP before ICE
	 * nominates an address.  DTLS must still use the committed identity. */
	err = sdp_media_set_lattr(
		fixture.server.media, true, "fingerprint",
		"SHA-256 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
		"00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00");
	TEST_ERR(err);
	offer = mem_deref(offer);
	err = sdp_encode(&offer, fixture.server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture.client.sdp, offer, true);
	TEST_ERR(err);

	err = menc_transport_start(menc, fixture.server.transport,
				   &fixture.client.address);
	TEST_ERR(err);
	err = menc_transport_start(menc, fixture.client.transport,
				   &fixture.server.address);
	TEST_ERR(err);
	err = re_main_timeout(3000);
	TEST_ERR(err);
	err = fixture.err;
	TEST_ERR(err);
	ASSERT_EQ(2, fixture.established);
	ASSERT_EQ(2, fixture.received);

out:
	mem_deref(fingerprint);
	mem_deref(answer);
	mem_deref(offer);
	transport_endpoint_reset(&fixture.client);
	transport_endpoint_reset(&fixture.server);
	return err;
}


static int test_menc_transport_identity_oom(const struct menc *menc)
{
	struct transport_fixture fixture = {
		.menc = menc,
	};
	struct mbuf *offer = NULL;
	int err;

	err = transport_endpoint_alloc(&fixture.server, &fixture, true);
	TEST_ERR(err);
	err = transport_endpoint_alloc(&fixture.client, &fixture, false);
	TEST_ERR(err);
	err = sdp_encode(&offer, fixture.server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture.client.sdp, offer, true);
	TEST_ERR(err);
	err = menc->transporth(&fixture.client.transport,
			       fixture.client.sess, fixture.client.udp, NULL,
			       fixture.client.media, false,
			       NULL, NULL, NULL, NULL);
	TEST_ERR(err);

	mem_threshold_set(0);
	err = menc_transport_commit_identity(menc, fixture.client.transport);
	mem_threshold_set(-1);
	ASSERT_EQ(ENOMEM, err);
	mem_threshold_set(1);
	err = menc_transport_commit_identity(menc, fixture.client.transport);
	mem_threshold_set(-1);
	ASSERT_EQ(ENOMEM, err);
	err = menc_transport_commit_identity(menc, fixture.client.transport);
	TEST_ERR(err);

out:
	mem_threshold_set(-1);
	mem_deref(offer);
	transport_endpoint_reset(&fixture.client);
	transport_endpoint_reset(&fixture.server);
	return err;
}


static int test_menc_transport_identity_role_pair(const struct menc *menc,
					  const char *setup)
{
	struct transport_fixture fixture = {
		.menc = menc,
	};
	struct mbuf *offer = NULL;
	const char *valid_local;
	int err;

	err = transport_endpoint_alloc(&fixture.server, &fixture, true);
	TEST_ERR(err);
	err = transport_endpoint_alloc(&fixture.client, &fixture, false);
	TEST_ERR(err);
	err = sdp_session_set_lattr(fixture.server.sdp, true, "setup", "%s",
				    setup);
	TEST_ERR(err);
	err = sdp_encode(&offer, fixture.server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture.client.sdp, offer, true);
	TEST_ERR(err);
	err = menc->transporth(&fixture.client.transport,
			       fixture.client.sess, fixture.client.udp, NULL,
			       fixture.client.media, false,
			       NULL, NULL, NULL, NULL);
	TEST_ERR(err);

	/* Fixed roles with the same value are invalid and must not be repaired
	 * by rewriting the local description during commit. */
	err = sdp_media_set_lattr(fixture.client.media, true, "setup", "%s",
				  setup);
	TEST_ERR(err);
	err = menc_transport_commit_identity(menc, fixture.client.transport);
	ASSERT_EQ(EPROTO, err);
	ASSERT_STREQ(setup, sdp_media_lattr_apply(
		fixture.client.media, "setup", NULL, NULL));

	valid_local = !str_casecmp(setup, "active") ? "passive" : "active";
	err = sdp_media_set_lattr(fixture.client.media, true, "setup", "%s",
				  valid_local);
	TEST_ERR(err);
	err = menc_transport_commit_identity(menc, fixture.client.transport);
	TEST_ERR(err);

out:
	mem_deref(offer);
	transport_endpoint_reset(&fixture.client);
	transport_endpoint_reset(&fixture.server);
	return err;
}


static int test_menc_transport_member_identity(const struct menc *menc)
{
	struct transport_fixture fixture = {
		.menc = menc,
	};
	struct sdp_media *server_member = NULL;
	struct sdp_media *client_member = NULL;
	struct menc_media *member = NULL;
	struct mbuf *offer = NULL;
	char *fingerprint = NULL;
	const char *separator;
	int err;

	err = transport_endpoint_alloc(&fixture.server, &fixture, true);
	TEST_ERR(err);
	err = transport_endpoint_alloc(&fixture.client, &fixture, false);
	TEST_ERR(err);
	err = sdp_media_add(&server_member, fixture.server.sdp, "audio",
			    sa_port(&fixture.server.address),
			    "UDP/TLS/RTP/SAVPF");
	err |= sdp_media_add(&client_member, fixture.client.sdp, "audio",
			     sa_port(&fixture.client.address),
			     "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = sdp_format_add(NULL, server_member, false, "0", "PCMU",
			     8000, 1, NULL, NULL, NULL, false, NULL);
	err |= sdp_format_add(NULL, client_member, false, "0", "PCMU",
			      8000, 1, NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	err = str_dup(&fingerprint, sdp_session_lattr_apply(
		fixture.server.sdp, "fingerprint", NULL, NULL));
	TEST_ERR(err);
	separator = strchr(fingerprint, ' ');
	ASSERT_TRUE(separator != NULL);
	err = sdp_media_set_lattr(server_member, true, "setup", "actpass");
	err |= sdp_media_set_lattr(server_member, true, "fingerprint",
				   "sha-256%s", separator);
	err |= sdp_media_set_lattr(server_member, false, "fingerprint",
				   "%s", fingerprint);
	TEST_ERR(err);

	err = sdp_encode(&offer, fixture.server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture.client.sdp, offer, true);
	TEST_ERR(err);
	err = menc->transporth(&fixture.client.transport,
			       fixture.client.sess, fixture.client.udp, NULL,
			       fixture.client.media, false,
			       NULL, NULL, NULL, NULL);
	TEST_ERR(err);
	err = menc_transport_commit_identity(menc, fixture.client.transport);
	TEST_ERR(err);
	err = sdp_media_set_lattr(client_member, true, "setup", "active");
	TEST_ERR(err);

	/* Equivalent order/case/duplicates normalize to the committed tag. */
	err = menc->mediah(&member, fixture.client.sess,
			   fixture.client.transport, NULL, fixture.client.udp,
			   NULL, NULL, NULL, client_member, NULL);
	TEST_ERR(err);

	/* A later provisional mutation of the member cannot redefine the cached
	 * group baseline or be attached to it. */
	err = sdp_media_set_lattr(
		server_member, true, "fingerprint",
		"SHA-256 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
		"00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00");
	TEST_ERR(err);
	offer = mem_deref(offer);
	err = sdp_encode(&offer, fixture.server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture.client.sdp, offer, true);
	TEST_ERR(err);
	err = menc->mediah(&member, fixture.client.sess,
			   fixture.client.transport, NULL, fixture.client.udp,
			   NULL, NULL, NULL, client_member, NULL);
	ASSERT_EQ(EPROTO, err);
	err = 0;

	/* A tag-level override does not make a conflicting session default
	 * disappear from another member's effective transport identity. */
	err = sdp_media_set_lattr(fixture.server.media, true, "setup",
				  "actpass");
	err |= sdp_media_set_lattr(fixture.server.media, true, "fingerprint",
				   "%s", fingerprint);
	sdp_media_del_lattr(server_member, "setup");
	sdp_media_del_lattr(server_member, "fingerprint");
	err |= sdp_session_set_lattr(
		fixture.server.sdp, true, "fingerprint",
		"SHA-256 11:11:11:11:11:11:11:11:11:11:11:11:11:11:11:11:"
		"11:11:11:11:11:11:11:11:11:11:11:11:11:11:11:11");
	TEST_ERR(err);
	offer = mem_deref(offer);
	err = sdp_encode(&offer, fixture.server.sdp, true);
	TEST_ERR(err);
	err = sdp_decode(fixture.client.sdp, offer, true);
	TEST_ERR(err);
	err = menc->mediah(&member, fixture.client.sess,
			   fixture.client.transport, NULL, fixture.client.udp,
			   NULL, NULL, NULL, client_member, NULL);
	ASSERT_EQ(EPROTO, err);
	err = 0;

out:
	mem_deref(member);
	mem_deref(fingerprint);
	mem_deref(offer);
	transport_endpoint_reset(&fixture.client);
	transport_endpoint_reset(&fixture.server);
	return err;
}


static int test_menc_bundle_conflict(const struct menc *menc,
				     const char *name,
				     const char *member_value, bool append)
{
	struct sdp_session *offer_sdp = NULL;
	struct sdp_session *answer_sdp = NULL;
	struct sdp_media *offer_tag = NULL;
	struct sdp_media *offer_member = NULL;
	struct sdp_media *answer_tag = NULL;
	struct sdp_media *answer_member = NULL;
	struct menc_sess *sess = NULL;
	struct menc_transport *transport = NULL;
	struct menc_media *media = NULL;
	struct udp_sock *udp = NULL;
	struct mbuf *offer = NULL;
	struct sa address;
	int err;

	sa_set_str(&address, "127.0.0.1", 0);
	err = udp_listen(&udp, &address, NULL, NULL);
	TEST_ERR(err);
	err = udp_local_get(udp, &address);
	TEST_ERR(err);
	err = sdp_session_alloc(&offer_sdp, &address);
	TEST_ERR(err);
	err = sdp_session_alloc(&answer_sdp, &address);
	TEST_ERR(err);
	err = sdp_media_add(&offer_tag, offer_sdp, "application",
			    sa_port(&address), "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_media_add(&offer_member, offer_sdp, "application",
			    sa_port(&address), "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_media_add(&answer_tag, answer_sdp, "application",
			    sa_port(&address), "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_media_add(&answer_member, answer_sdp, "application",
			    sa_port(&address), "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_format_add(NULL, offer_tag, false, "webrtc-datachannel",
			     NULL, 0, 0, NULL, NULL, NULL, false, NULL);
	err |= sdp_format_add(NULL, offer_member, false,
			      "webrtc-datachannel", NULL, 0, 0,
			      NULL, NULL, NULL, false, NULL);
	err |= sdp_format_add(NULL, answer_tag, false, "webrtc-datachannel",
			      NULL, 0, 0, NULL, NULL, NULL, false, NULL);
	err |= sdp_format_add(NULL, answer_member, false,
			      "webrtc-datachannel", NULL, 0, 0,
			      NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);

	err = sdp_media_set_lattr(offer_tag, true, "setup", "actpass");
	err |= sdp_media_set_lattr(offer_tag, true, "fingerprint",
				   "SHA-256 AA:BB");
	err |= sdp_media_set_lattr(offer_tag, true, "tls-id",
				   "abcdefghijklmnopqrstuvwx");
	err |= sdp_media_set_lattr(offer_member, true, "setup", "actpass");
	err |= sdp_media_set_lattr(offer_member, true, "fingerprint",
				   "SHA-256 AA:BB");
	err |= sdp_media_set_lattr(offer_member, true, "tls-id",
				   "abcdefghijklmnopqrstuvwx");
	TEST_ERR(err);
	err = sdp_media_set_lattr(offer_member, !append, name, "%s",
				  member_value);
	TEST_ERR(err);

	err = sdp_encode(&offer, offer_sdp, true);
	TEST_ERR(err);
	err = sdp_decode(answer_sdp, offer, true);
	TEST_ERR(err);
	if (!append)
		ASSERT_STREQ(member_value,
			     sdp_media_rattr(answer_member, name));
	err = menc->sessh(&sess, answer_sdp, false, NULL, NULL, NULL);
	TEST_ERR(err);
	err = menc->transporth(&transport, sess, udp, NULL, answer_tag, false,
			       NULL, NULL, NULL, NULL);
	TEST_ERR(err);

	err = menc->mediah(&media, sess, transport, NULL, udp, udp,
			   NULL, NULL, answer_member, NULL);
	ASSERT_EQ(EPROTO, err);
	err = 0;

out:
	mem_deref(media);
	mem_deref(transport);
	mem_deref(sess);
	mem_deref(offer);
	mem_deref(answer_sdp);
	mem_deref(offer_sdp);
	mem_deref(udp);
	return err;
}


static int test_menc_bundle_conflicts(const struct menc *menc)
{
	int err;

	err = test_menc_bundle_conflict(menc, "setup", "passive", false);
	TEST_ERR(err);
	err = test_menc_bundle_conflict(menc, "fingerprint",
					"SHA-256 CC:DD", false);
	TEST_ERR(err);
	err = test_menc_bundle_conflict(menc, "tls-id",
					"zyxwvutsrqponmlkjihgfedc", false);
	TEST_ERR(err);
	err = test_menc_bundle_conflict(menc, "fingerprint",
					"SHA-256 CC:DD", true);
	TEST_ERR(err);

out:
	return err;
}


static int test_menc_rejected_bundle_member(const struct menc *menc)
{
	struct sdp_session *offer_sdp = NULL;
	struct sdp_session *answer_sdp = NULL;
	struct sdp_media *offer_tag = NULL;
	struct sdp_media *offer_member = NULL;
	struct sdp_media *answer_tag = NULL;
	struct sdp_media *answer_member = NULL;
	struct menc_sess *sess = NULL;
	struct menc_transport *transport = NULL;
	struct menc_media *media = NULL;
	struct udp_sock *rtp = NULL;
	struct udp_sock *rtcp = NULL;
	struct mbuf *offer = NULL;
	struct sa address;
	int err;

	sa_set_str(&address, "127.0.0.1", 0);
	err = udp_listen(&rtp, &address, NULL, NULL);
	TEST_ERR(err);
	err = udp_local_get(rtp, &address);
	TEST_ERR(err);
	sa_set_port(&address, 0);
	err = udp_listen(&rtcp, &address, NULL, NULL);
	TEST_ERR(err);
	err = sdp_session_alloc(&offer_sdp, &address);
	TEST_ERR(err);
	err = sdp_session_alloc(&answer_sdp, &address);
	TEST_ERR(err);
	err = sdp_media_add(&offer_tag, offer_sdp, "application",
			    sa_port(&address), "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_format_add(NULL, offer_tag, false, "webrtc-datachannel",
			     NULL, 0, 0, NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	err = sdp_media_add(&offer_member, offer_sdp, "video", 0,
			    "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = sdp_format_add(NULL, offer_member, false, "120", "VP8",
			     90000, 1, NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	err = sdp_media_add(&answer_tag, answer_sdp, "application",
			    sa_port(&address), "UDP/DTLS/SCTP");
	TEST_ERR(err);
	err = sdp_format_add(NULL, answer_tag, false, "webrtc-datachannel",
			     NULL, 0, 0, NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	err = sdp_media_add(&answer_member, answer_sdp, "video",
			    sa_port(&address), "UDP/TLS/RTP/SAVPF");
	TEST_ERR(err);
	err = sdp_format_add(NULL, answer_member, false, "120", "VP8",
			     90000, 1, NULL, NULL, NULL, false, NULL);
	TEST_ERR(err);
	err = sdp_media_set_lattr(offer_tag, true, "setup", "actpass");
	err |= sdp_media_set_lattr(offer_tag, true, "fingerprint",
				   "SHA-256 AA:BB");
	TEST_ERR(err);

	err = sdp_encode(&offer, offer_sdp, true);
	TEST_ERR(err);
	err = sdp_decode(answer_sdp, offer, true);
	TEST_ERR(err);
	ASSERT_TRUE(!sdp_media_has_media(answer_member));
	err = menc->sessh(&sess, answer_sdp, false, NULL, NULL, NULL);
	TEST_ERR(err);
	err = menc->transporth(&transport, sess, rtp, NULL, answer_tag, false,
			       NULL, NULL, NULL, NULL);
	TEST_ERR(err);

	/*
	 * A rejected BUNDLE member has no transport to negotiate.  In
	 * particular, the absence of rtcp-mux on its port-zero section must
	 * not reject the established BUNDLE-tag transport.
	 */
	err = menc->mediah(&media, sess, transport, NULL, rtp, rtcp,
			   NULL, NULL, answer_member, NULL);
	TEST_ERR(err);

out:
	mem_deref(media);
	mem_deref(transport);
	mem_deref(sess);
	mem_deref(offer);
	mem_deref(answer_sdp);
	mem_deref(offer_sdp);
	mem_deref(rtcp);
	mem_deref(rtp);
	return err;
}


#endif


int peerconn_test_menc_transport(const struct menc *menc, bool members)
{
#ifdef USE_DATACHANNEL
	return test_menc_transport(menc, members);
#else
	(void)menc;
	(void)members;
	return 0;
#endif
}


int peerconn_test_transport_identity(const struct menc *menc)
{
#ifdef USE_DATACHANNEL
	int err;

	err = test_menc_transport(menc, false);
	TEST_ERR(err);
	err = test_menc_transport_committed_identity(menc);
	TEST_ERR(err);
	err = test_menc_transport_identity_oom(menc);
	TEST_ERR(err);
	err = test_menc_transport_identity_role_pair(menc, "active");
	TEST_ERR(err);
	err = test_menc_transport_identity_role_pair(menc, "passive");
	TEST_ERR(err);
	err = test_menc_transport_member_identity(menc);
	TEST_ERR(err);
	err = test_menc_transport_promotion_tls_id(menc);
	TEST_ERR(err);

out:
	return err;
#else
	(void)menc;
	return 0;
#endif
}


int peerconn_test_transport_suite(const struct menc *menc)
{
	int err;

	err = test_bundle_set();
	TEST_ERR(err);
#ifdef USE_DATACHANNEL
	err = test_menc_bundle_conflicts(menc);
	TEST_ERR(err);
	err = test_menc_rejected_bundle_member(menc);
	TEST_ERR(err);
	err = test_menc_transport_preserves_session_attrs(menc);
	TEST_ERR(err);
	err = test_menc_transport_unstarted_promotion(menc);
	TEST_ERR(err);
	err = test_menc_transport_inprogress_promotion(menc);
	TEST_ERR(err);
	err = test_menc_transport_fingerprint_destruction(menc);
	TEST_ERR(err);
	err = peerconn_test_transport_identity(menc);
	TEST_ERR(err);
#else
	(void)menc;
#endif

out:
	return err;
}


int peerconn_test_data_contracts(const struct mnat *mnat,
				 const struct menc *menc)
{
#ifdef USE_DATACHANNEL
	int err;

	err = test_dcmap();
	TEST_ERR(err);
	err = test_data_sdp_validation();
	TEST_ERR(err);
	err = test_replacement_uses_current_role();
	TEST_ERR(err);
	err = test_replacement_allocation_rollback();
	TEST_ERR(err);
	err = test_pending_transport_close_preserves_active();
	TEST_ERR(err);
	err = test_invalid_create_preserves_sdp(mnat, menc);
	TEST_ERR(err);
	err = test_local_offer_rollback_restores_state(mnat, menc);
	TEST_ERR(err);

out:
	return err;
#else
	(void)mnat;
	(void)menc;
	return 0;
#endif
}


int peerconn_test_answer_retry(const struct mnat *mnat,
			       const struct menc *menc)
{
#ifdef USE_DATACHANNEL
	return test_answer_allocation_retry(mnat, menc);
#else
	(void)mnat;
	(void)menc;
	return 0;
#endif
}
