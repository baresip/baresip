/**
 * @file media_transport.c Exact session transport-group runtime
 *
 * Copyright (C) 2026 The baresip contributors
 */

#include <re.h>
#include <baresip.h>
#include <string.h>
#include "core.h"


enum {
	TRANSPORT_PACKET_LIMIT = 256,
	TRANSPORT_BYTE_LIMIT = 1024 * 1024
};


struct media_transport_member {
	struct le le;
	struct le route_le;
	struct stream *stream;
	struct menc_transport *previous;
	bool target;
	enum media_transport_transition transition;
};


struct media_transport_packet {
	struct le le;
	struct mbuf *mb;
};


struct media_transport_token {
	mtx_t lock;
	struct media_transport *active;
	struct media_transport *pending_consumer;
	struct bundle_publication *publication;
};


struct media_transport {
	struct bundle_group *group;
	struct sdp_media *transport_sdpm;
	struct sdp_session *restart_sdp;
	struct sdp_media *restart_sdpm;
	struct udp_sock *sock;
	struct mnat_media *mnat_media;
	struct menc_transport *menc_transport;
	struct bundle_transport *route;
	struct mqueue *changeq;
	struct media_transport_token *token;
	struct stream *tag_owner;
	struct menc_transport_binding previous_binding;
	struct media_transport *previous_runtime;
	struct sdp_media_lattr_state *saved_attrs;
	struct mnat_sess *mnats;
	struct menc_sess *mencs;
	const struct mnat *mnat;
	const struct menc *menc;
	struct list members;
	struct list route_streams;
	struct list packets;
	char *key;
	char *data_mid;
	struct sa remote;
	struct sa old_peer;
	struct sa saved_laddr;
	uint64_t route_generation;
	size_t queued_bytes;
	size_t queued_packets;
	int af;
	int terminal_err;
	enum menc_dtls_role role;
	bool offerer;
	bool prepared;
	bool route_prepared;
	bool mnat_prepared;
	bool menc_prepared;
	bool menc_ready;
	bool menc_started;
	bool activated;
	bool adopted;
	bool remote_set;
	bool connected;
	bool established;
	bool finalized;
	bool sdp_staged;
	bool owns_menc_transport;
	bool restore_binding;
	bool previous_binding_retained;
	bool change_pending;
	bool attempt_running;
	bool gather_waiting;
	bool consumer_ready;
	bool reconfigure;
	bool ice_restart;
	bool previous_mnat_prepared;
	bool peer_prepared;
	bool peer_activated;
	bool remote_ice_loaded;
	mnat_connected_h *connectedh;
	menc_transport_recv_h *recvh;
	menc_transport_estab_h *estabh;
	menc_transport_close_h *closeh;
	void (*mnat_publishh)(struct mnat_media *mnat_media, void *arg);
	void (*changeh)(void *arg);
	void (*observerh)(void *arg);
	void *(*arg_ref)(void *arg);
	void (*arg_deref)(void *arg);
	void *observer_arg;
	void *arg;
};


static int prepare_members(struct media_transport *mt);
static void transport_recv_handler(struct mbuf *mb, void *arg);
static void transport_estab_handler(int err, enum menc_dtls_role role,
				    void *arg);
static void transport_close_handler(int err, void *arg);
static void change_queue_handler(int id, void *data, void *arg);
static void mnat_connected_handler(const struct sa *raddr1,
				   const struct sa *raddr2, void *arg);


static void *token_ref(void *arg)
{
	return mem_ref(arg);
}


static void token_deref(void *arg)
{
	mem_deref(arg);
}


static void token_destructor(void *arg)
{
	struct media_transport_token *token = arg;

	mem_deref(token->pending_consumer);
	mem_deref(token->active);
	mem_deref(token->publication);
	mtx_destroy(&token->lock);
}


static int token_alloc(struct media_transport_token **tokenp)
{
	struct media_transport_token *token;

	token = mem_zalloc(sizeof(*token), token_destructor);
	if (!token)
		return ENOMEM;
	if (mtx_init(&token->lock, mtx_plain) != thrd_success) {
		/* The destructor must only observe an initialized mutex. */
		mem_destructor(token, NULL);
		mem_deref(token);
		return ENOMEM;
	}
	*tokenp = token;
	return 0;
}


static struct media_transport *token_active_ref(
	struct media_transport_token *token)
{
	struct media_transport *active;

	if (!token)
		return NULL;
	mtx_lock(&token->lock);
	active = mem_ref(token->active);
	mtx_unlock(&token->lock);
	return active;
}


static struct media_transport *token_receiver_ref(
	struct media_transport_token *token)
{
	struct media_transport *receiver;

	if (!token)
		return NULL;
	mtx_lock(&token->lock);
	receiver = mem_ref(token->pending_consumer
				   ? token->pending_consumer : token->active);
	mtx_unlock(&token->lock);
	return receiver;
}


static bool token_is_active(struct media_transport_token *token,
			    const struct media_transport *expected)
{
	bool equal;

	if (!token)
		return false;
	mtx_lock(&token->lock);
	equal = token->active == expected;
	mtx_unlock(&token->lock);
	return equal;
}


static void token_set_active(struct media_transport_token *token,
			     struct media_transport *active)
{
	struct media_transport *old;

	if (!token)
		return;
	mtx_lock(&token->lock);
	old = token->active;
	token->active = mem_ref(active);
	mtx_unlock(&token->lock);
	mem_deref(old);
}


static void token_set_pending_consumer(struct media_transport_token *token,
				       struct media_transport *consumer)
{
	struct media_transport *old;

	if (!token)
		return;
	mtx_lock(&token->lock);
	old = token->pending_consumer;
	token->pending_consumer = mem_ref(consumer);
	mtx_unlock(&token->lock);
	mem_deref(old);
}


static void token_clear_pending_consumer(
	struct media_transport_token *token,
	const struct media_transport *expected)
{
	struct media_transport *old = NULL;

	if (!token)
		return;
	mtx_lock(&token->lock);
	if (token->pending_consumer == expected) {
		old = token->pending_consumer;
		token->pending_consumer = NULL;
	}
	mtx_unlock(&token->lock);
	mem_deref(old);
}


static void token_promote(struct media_transport_token *token,
			  struct media_transport *candidate)
{
	struct media_transport *old = NULL;

	if (!token)
		return;
	mtx_lock(&token->lock);
	old = token->active;
	if (token->pending_consumer == candidate) {
		token->active = token->pending_consumer;
		token->pending_consumer = NULL;
	}
	else {
		token->active = mem_ref(candidate);
	}
	mtx_unlock(&token->lock);
	mem_deref(old);
}


static void token_clear_active(struct media_transport_token *token,
			       const struct media_transport *expected)
{
	struct media_transport *old = NULL;

	if (!token)
		return;
	mtx_lock(&token->lock);
	if (token->active == expected) {
		old = token->active;
		token->active = NULL;
	}
	mtx_unlock(&token->lock);
	mem_deref(old);
}


static void member_destructor(void *arg)
{
	struct media_transport_member *member = arg;

	list_unlink(&member->le);
	list_unlink(&member->route_le);
	mem_deref(member->previous);
	mem_deref(member->stream);
}


static void packet_destructor(void *arg)
{
	struct media_transport_packet *packet = arg;

	list_unlink(&packet->le);
	mem_deref(packet->mb);
}


static void destructor(void *arg)
{
	struct media_transport *mt = arg;

	if (mt->attempt_running && mt->mnat &&
	    mt->mnat->mediaattemptcancelh) {
		mt->mnat->mediaattemptcancelh(mt->mnat_media);
		mt->attempt_running = false;
	}
	if (mt->gather_waiting && mt->mnat &&
	    mt->mnat->mediagathercancelh) {
		mt->mnat->mediagathercancelh(mt->mnat_media);
		mt->gather_waiting = false;
	}
	if (mt->prepared || mt->activated)
		media_transport_abort(mt);
	token_clear_pending_consumer(mt->token, mt);
	token_clear_active(mt->token, mt);
	list_flush(&mt->packets);
	list_flush(&mt->members);
	if (mt->owns_menc_transport && mt->menc_transport) {
		if (mt->restore_binding && mt->token) {
			(void)menc_transport_rebind(mt->menc,
				mt->menc_transport, &mt->previous_binding,
				mt->token, NULL, NULL);
		}
		else {
			struct menc_transport_binding detached = {0};

			(void)menc_transport_rebind(mt->menc,
				mt->menc_transport, &detached, mt->token,
				NULL, NULL);
		}
	}
	if (mt->previous_binding_retained && mt->previous_binding.arg_deref &&
	    mt->previous_binding.arg) {
		mt->previous_binding.arg_deref(mt->previous_binding.arg);
		mt->previous_binding.arg = NULL;
		mt->previous_binding_retained = false;
	}
	mem_deref(mt->route);
	mem_deref(mt->changeq);
	mem_deref(mt->previous_runtime);
	mem_deref(mt->menc_transport);
	mem_deref(mt->token);
	mem_deref(mt->mnat_media);
	mem_deref(mt->tag_owner);
	mem_deref(mt->sock);
	mem_deref(mt->mencs);
	mem_deref(mt->mnats);
	mem_deref(mt->transport_sdpm);
	mem_deref(mt->restart_sdpm);
	mem_deref(mt->restart_sdp);
	mem_deref(mt->saved_attrs);
	mem_deref(mt->group);
	mem_deref(mt->data_mid);
	mem_deref(mt->key);
	if (mt->arg_ref && mt->arg_deref && mt->arg)
		mt->arg_deref(mt->arg);
}


static bool exact_member(const struct media_transport *mt,
			 const struct stream *stream)
{
	const char *mid = sdp_media_rattr(stream_sdpmedia(stream), "mid");

	if (!str_isset(mid))
		mid = stream_mid(stream);

	return str_isset(mid) && bundle_group_contains(mt->group, mid);
}


static int capture_members(struct media_transport *mt, struct list *streaml)
{
	struct le *le;

	for (le = list_head(streaml); le; le = le->next) {
		struct stream *stream = le->data;
		struct media_transport_member *member;

		if (!exact_member(mt, stream))
			continue;
		member = mem_zalloc(sizeof(*member), member_destructor);
		if (!member)
			return ENOMEM;
		member->stream = mem_ref(stream);
		member->target = true;
		list_append(&mt->members, &member->le, member);
		list_append(&mt->route_streams, &member->route_le, stream);
	}
	return 0;
}


static bool has_member(const struct media_transport *mt,
		       const struct stream *stream)
{
	const struct le *le;

	for (le = list_head(&mt->members); le; le = le->next) {
		const struct media_transport_member *member = le->data;

		if (member->stream == stream)
			return true;
	}
	return false;
}


static struct stream *find_tag_owner(const struct bundle_group *group,
				     const struct list *streaml)
{
	const char *tag;
	const struct le *le;

	if (!group || !streaml)
		return NULL;
	tag = bundle_group_tag(group);
	if (!str_isset(tag))
		return NULL;
	for (le = list_head(streaml); le; le = le->next) {
		struct stream *stream = le->data;
		const char *mid;

		mid = sdp_media_rattr(stream_sdpmedia(stream), "mid");
		if (!str_isset(mid))
			mid = stream_mid(stream);
		if (!str_cmp(mid, tag))
			return stream;
	}
	return NULL;
}


static bool stream_list_contains(const struct list *streaml,
			 const struct stream *stream)
{
	const struct le *le;

	for (le = list_head(streaml); le; le = le->next) {
		if (le->data == stream)
			return true;
	}
	return false;
}


static int restart_shadow_alloc(struct media_transport *mt)
{
	struct sdp_media_lattr_state *attrs = NULL;
	struct sdp_media *media = NULL;
	const struct sa *laddr;
	int err;

	if (!mt || !mt->transport_sdpm)
		return EINVAL;
	laddr = sdp_media_laddr(mt->transport_sdpm);
	err = sdp_session_alloc(&mt->restart_sdp, laddr);
	if (!err)
		err = sdp_media_add(&media, mt->restart_sdp,
			sdp_media_name(mt->transport_sdpm), sa_port(laddr),
			sdp_media_proto(mt->transport_sdpm));
	if (!err)
		err = sdp_media_save_lattrs(&attrs, mt->transport_sdpm);
	if (!err)
		err = sdp_media_apply_lattrs(media, attrs);
	if (!err)
		mt->restart_sdpm = mem_ref(media);
	mem_deref(attrs);
	return err;
}


static int alloc_common(struct media_transport **mtp,
			const struct media_transport_prm *prm)
{
	struct media_transport *mt;
	int err;

	if (!mtp || !prm || !prm->group || !prm->transport_sdpm ||
	    !prm->streaml || !str_isset(prm->semantic_key) || !prm->menc ||
	    !prm->mencs || !prm->menc->transporth)
		return EINVAL;

	mt = mem_zalloc(sizeof(*mt), destructor);
	if (!mt)
		return ENOMEM;
	err = mqueue_alloc(&mt->changeq, change_queue_handler, mt);
	if (!err)
		err = bundle_group_clone(&mt->group, prm->group);
	if (!err)
		err = str_dup(&mt->key, prm->semantic_key);
	if (!err && prm->data_mid)
		err = str_dup(&mt->data_mid, prm->data_mid);
	if (err) {
		mem_deref(mt);
		return err;
	}
	mt->transport_sdpm = mem_ref(prm->transport_sdpm);
	mt->mnats = mem_ref(prm->mnats);
	mt->mencs = mem_ref(prm->mencs);
	mt->mnat = prm->mnat;
	mt->menc = prm->menc;
	mt->af = prm->af;
	mt->offerer = prm->offerer;
	mt->tag_owner = mem_ref(find_tag_owner(prm->group, prm->streaml));
	mt->connectedh = prm->connectedh;
	mt->recvh = prm->recvh;
	mt->estabh = prm->estabh;
	mt->closeh = prm->closeh;
	mt->mnat_publishh = prm->mnat_publishh;
	mt->changeh = prm->changeh;
	mt->arg_ref = prm->arg_ref;
	mt->arg_deref = prm->arg_deref;
	mt->arg = prm->arg_ref && prm->arg && prm->arg_deref
			  ? prm->arg_ref(prm->arg) : prm->arg;
	err = capture_members(mt, prm->streaml);
	if (err) {
		mem_deref(mt);
		return err;
	}
	*mtp = mt;
	return 0;
}


static void change_handler(int err, void *arg)
{
	struct media_transport *mt = arg;

	(void)err;
	mt->change_pending = false;
	if (mt->changeh)
		mt->changeh(mt->arg);
	if (mt->observerh)
		mt->observerh(mt->observer_arg);
	mem_deref(mt);
}


static void change_queue_handler(int id, void *data, void *arg)
{
	struct media_transport *mt = arg;

	(void)id;
	(void)data;
	change_handler(0, mem_ref(mt));
}


static void notify_change(struct media_transport *mt)
{
	if (!mt || (!mt->changeh && !mt->observerh) || mt->change_pending)
		return;
	mt->change_pending = true;
	if (mqueue_push(mt->changeq, 0, NULL))
		mt->change_pending = false;
}


int media_transport_alloc(struct media_transport **mtp,
			  const struct media_transport_prm *prm)
{
	return alloc_common(mtp, prm);
}


int media_transport_adopt(struct media_transport **mtp,
			  const struct media_transport_prm *prm,
			  struct udp_sock *sock, struct mnat_media *mnat_media,
			  struct menc_transport *menc_transport,
			  struct bundle_transport *route)
{
	struct media_transport *mt;
	struct menc_transport_binding binding;
	struct menc_transport_state state;
	int err;

	if (!sock || !menc_transport || !route)
		return EINVAL;
	err = alloc_common(&mt, prm);
	if (err)
		return err;
	mt->sock = mem_ref(sock);
	mt->mnat_media = mem_ref(mnat_media);
	mt->menc_transport = mem_ref(menc_transport);
	mt->route = mem_ref(route);
	err = token_alloc(&mt->token);
	if (err) {
		mem_deref(mt);
		return err;
	}
	token_set_active(mt->token, mt);
	memset(&binding, 0, sizeof(binding));
	binding.recvh = transport_recv_handler;
	binding.estabh = transport_estab_handler;
	binding.closeh = transport_close_handler;
	binding.arg = mt->token;
	binding.arg_ref = token_ref;
	binding.arg_deref = token_deref;
	memset(&state, 0, sizeof(state));
	err = menc_transport_rebind(mt->menc, mt->menc_transport, &binding,
				    NULL, &mt->previous_binding, &state);
	if (err) {
		token_clear_active(mt->token, mt);
		mem_deref(mt);
		return err;
	}
	mt->owns_menc_transport = true;
	/* Raw legacy callback arguments have no lifetime contract.  Reinstalling
	 * one after its owner retired would create a dangling callback, so only a
	 * retainable captured binding is eligible for restoration. */
	mt->restore_binding = mt->previous_binding.arg_ref &&
		mt->previous_binding.arg_deref;
	mt->previous_binding_retained = mt->restore_binding &&
		mt->previous_binding.arg;
	mt->adopted = true;
	mt->activated = true;
	mt->finalized = true;
	mt->menc_started = state.started;
	mt->established = state.established;
	mt->role = state.local_role;
	mt->remote_set = state.remote_set;
	mt->connected = state.remote_set;
	if (state.remote_set)
		sa_cpy(&mt->remote, &state.remote);
	*mtp = mt;
	return 0;
}


int media_transport_adopt_pending(struct media_transport **mtp,
			  const struct media_transport_prm *prm,
			  struct udp_sock *sock, struct mnat_media *mnat_media,
			  struct menc_transport *menc_transport,
			  struct bundle_transport *route,
			  uint64_t route_generation)
{
	struct media_transport *mt;
	struct menc_transport_binding binding;
	struct menc_transport_state state;
	int err;

	if (!mtp || !sock || !menc_transport || !route || !route_generation)
		return EINVAL;
	err = alloc_common(&mt, prm);
	if (err)
		return err;
	mt->sock = mem_ref(sock);
	mt->mnat_media = mem_ref(mnat_media);
	mt->menc_transport = mem_ref(menc_transport);
	mt->route = mem_ref(route);
	mt->route_generation = route_generation;
	mt->route_prepared = true;
	err = token_alloc(&mt->token);
	if (err)
		goto out;
	token_set_active(mt->token, mt);
	memset(&binding, 0, sizeof(binding));
	binding.recvh = transport_recv_handler;
	binding.estabh = transport_estab_handler;
	binding.closeh = transport_close_handler;
	binding.arg = mt->token;
	binding.arg_ref = token_ref;
	binding.arg_deref = token_deref;
	memset(&state, 0, sizeof(state));
	err = menc_transport_rebind(mt->menc, mt->menc_transport, &binding,
				    NULL, &mt->previous_binding, &state);
	if (err)
		goto out;
	mt->owns_menc_transport = true;
	mt->restore_binding = mt->previous_binding.arg_ref &&
		mt->previous_binding.arg_deref;
	mt->previous_binding_retained = mt->restore_binding &&
		mt->previous_binding.arg;
	mt->menc_started = state.started;
	mt->established = state.established;
	mt->role = state.local_role;
	mt->remote_set = state.remote_set;
	mt->connected = state.remote_set;
	if (state.remote_set)
		sa_cpy(&mt->remote, &state.remote);
	if (!mt->established || !mt->remote_set) {
		err = EAGAIN;
		goto out;
	}

	/* The legacy DTLS association is already established.  Only its exact
	 * member and route transactions remain pending, so the session-wide gate
	 * can publish every group in one activation pass. */
	mt->menc_prepared = true;
	err = prepare_members(mt);
	if (err)
		goto out;
	mt->menc_ready = true;
	mt->prepared = true;
	*mtp = mt;
	return 0;

out:
	media_transport_abort(mt);
	mem_deref(mt);
	return err;
}


int media_transport_import_pending(
	struct media_transport **mtp, const struct media_transport_prm *prm,
	struct udp_sock *sock, struct mnat_media *mnat_media,
	struct menc_transport *menc_transport)
{
	struct media_transport *mt;
	struct menc_transport_binding binding;
	struct menc_transport_state state;
	int err;

	if (!mtp || !prm || !sock || !menc_transport)
		return EINVAL;
	err = alloc_common(&mt, prm);
	if (err)
		return err;
	mt->sock = mem_ref(sock);
	mt->mnat_media = mem_ref(mnat_media);
	mt->menc_transport = mem_ref(menc_transport);
	err = bundle_transport_alloc(&mt->route, mt->group,
				     &mt->route_streams, mt->data_mid);
	if (!err)
		err = bundle_transport_prepare(mt->route, mt->group, mt->sock,
					       &mt->route_generation);
	if (!err)
		mt->route_prepared = true;
	if (!err && mt->mnat && mt->mnat->mediah) {
		if (!mt->mnat->mediaprepareh || !mt->mnat->mediaactivateh ||
		    !mt->mnat->mediarollbackh || !mt->mnat->mediafinalizeh ||
		    !mt->mnat->mediaaborth)
			err = ENOTSUP;
		else if (!prm->mnat_prepared)
			err = mt->mnat->mediaprepareh(mt->mnat_media, true);
		if (!err)
			mt->mnat_prepared = true;
	}
	if (!err)
		err = token_alloc(&mt->token);
	if (!err) {
		token_set_active(mt->token, mt);
		memset(&binding, 0, sizeof(binding));
		binding.recvh = transport_recv_handler;
		binding.estabh = transport_estab_handler;
		binding.closeh = transport_close_handler;
		binding.arg = mt->token;
		binding.arg_ref = token_ref;
		binding.arg_deref = token_deref;
		memset(&state, 0, sizeof(state));
		err = menc_transport_rebind(mt->menc, mt->menc_transport,
					    &binding, NULL,
					    &mt->previous_binding, &state);
	}
	if (!err) {
		mt->owns_menc_transport = true;
		/* This is an independent replacement association.  On abort it is
		 * detached; reinstalling the superseded pending data binding would leave
		 * callbacks targeting an object the coordinator is about to destroy. */
		mt->restore_binding = false;
		mt->previous_binding_retained =
			mt->previous_binding.arg_ref &&
			mt->previous_binding.arg_deref &&
			mt->previous_binding.arg;
		mt->menc_started = state.started;
		mt->established = state.established;
		mt->role = state.local_role;
		mt->remote_set = state.remote_set;
		mt->connected = state.remote_set;
		if (state.remote_set)
			sa_cpy(&mt->remote, &state.remote);
		mt->menc_prepared = true;
		err = prepare_members(mt);
		if (err == EAGAIN)
			err = 0;
		else if (!err)
			mt->menc_ready = true;
	}
	if (!err)
		mt->prepared = true;
	if (err) {
		media_transport_abort(mt);
		mem_deref(mt);
		return err;
	}
	*mtp = mt;
	return 0;
}


static int reconfigure_alloc(
	struct media_transport **candidatep, struct media_transport *active,
	const struct bundle_group *group, struct list *streaml,
	const struct list *destination_streaml, const char *data_mid,
	const char *semantic_key,
	const struct media_transport_prm *consumer, bool ice_restart)
{
	struct media_transport_prm prm;
	struct media_transport *candidate;
	struct le *le;
	int err;

	if (!candidatep || !active || !group || !streaml ||
	    !str_isset(semantic_key))
		return EINVAL;
	/* A second association cannot safely steal DTLS application records from
	 * an already-published SCTP consumer on the same DTLS connection.  Such
	 * generations must reuse the existing association instead. */
	if (consumer && consumer->recvh && active->recvh)
		return EALREADY;
	if (!active->activated || !active->finalized || !active->token ||
	    !token_is_active(active->token, active) || !active->menc_transport)
		return ENOTSUP;

	memset(&prm, 0, sizeof(prm));
	prm.group = group;
	prm.transport_sdpm = active->transport_sdpm;
	prm.streaml = streaml;
	prm.data_mid = data_mid;
	prm.semantic_key = semantic_key;
	prm.mnat = active->mnat;
	prm.mnats = active->mnats;
	prm.menc = active->menc;
	prm.mencs = active->mencs;
	prm.af = active->af;
	prm.offerer = active->offerer;
	prm.connectedh = active->connectedh;
	prm.recvh = active->recvh;
	prm.estabh = active->estabh;
	prm.closeh = active->closeh;
	prm.mnat_publishh = active->mnat_publishh;
	prm.changeh = active->changeh;
	prm.arg_ref = active->arg_ref;
	prm.arg_deref = active->arg_deref;
	prm.arg = active->arg;
	if (consumer) {
		prm.recvh = consumer->recvh;
		prm.estabh = consumer->estabh;
		prm.closeh = consumer->closeh;
		prm.mnat_publishh = consumer->mnat_publishh;
		prm.changeh = consumer->changeh;
		prm.arg_ref = consumer->arg_ref;
		prm.arg_deref = consumer->arg_deref;
		prm.arg = consumer->arg;
	}
	err = alloc_common(&candidate, &prm);
	if (err)
		return err;
	for (le = list_head(&candidate->members); le; le = le->next) {
		struct media_transport_member *member = le->data;

		member->transition = has_member(active, member->stream)
			? MEDIA_TRANSPORT_RETAIN : MEDIA_TRANSPORT_ADD;
	}

	for (le = list_head(&active->members); le; le = le->next) {
		const struct media_transport_member *old = le->data;
		struct media_transport_member *member;

		if (has_member(candidate, old->stream))
			continue;
		member = mem_zalloc(sizeof(*member), member_destructor);
		if (!member) {
			mem_deref(candidate);
			return ENOMEM;
		}
		member->stream = mem_ref(old->stream);
		member->target = false;
		member->transition = stream_list_contains(
			destination_streaml, old->stream)
			? MEDIA_TRANSPORT_MOVE_OUT : MEDIA_TRANSPORT_REMOVE;
		list_append(&candidate->members, &member->le, member);
	}

	candidate->sock = mem_ref(active->sock);
	if (ice_restart) {
		if (!active->mnat || !active->mnat_media ||
		    !active->mnat->mediarestartalloch ||
		    !active->mnat->mediaprepareh ||
		    !active->mnat->mediaactivateh ||
		    !active->mnat->mediarollbackh ||
		    !active->mnat->mediafinalizeh ||
		    !active->mnat->mediaaborth ||
		    !active->mnat->mediaattemptstarth ||
		    !active->mnat->mediaattemptcancelh ||
		    !active->mnat->mediagatheredh) {
			mem_deref(candidate);
			return ENOTSUP;
		}
		err = restart_shadow_alloc(candidate);
		if (err) {
			mem_deref(candidate);
			return err;
		}
		err = mnat_media_restart_alloc(
				active->mnat, &candidate->mnat_media,
				active->mnat_media, candidate->sock,
				candidate->restart_sdpm,
				mnat_connected_handler, candidate);
		if (err) {
			mem_deref(candidate);
			return err;
		}
	}
	else
		candidate->mnat_media = mem_ref(active->mnat_media);
	candidate->menc_transport = mem_ref(active->menc_transport);
	candidate->token = mem_ref(active->token);
	candidate->previous_binding = active->previous_binding;
	candidate->restore_binding = active->restore_binding;
	candidate->previous_runtime = mem_ref(active);
	candidate->reconfigure = true;
	candidate->ice_restart = ice_restart;
	candidate->connected = active->connected;
	candidate->established = active->established;
	/* A reconfiguration without a replacement consumer retains the already
	 * published callback contract.  Once this candidate becomes token-active,
	 * records must remain immediately deliverable instead of being stranded in
	 * its private pre-publication queue.  A genuinely new consumer still starts
	 * gated until media_transport_consumer_ready() is called. */
	if (!consumer)
		candidate->consumer_ready = active->consumer_ready;
	candidate->menc_started = active->menc_started;
	candidate->remote_set = ice_restart ? false : active->remote_set;
	candidate->terminal_err = active->terminal_err;
	candidate->role = active->role;
	sa_cpy(&candidate->remote, &active->remote);
	if (consumer && consumer->recvh)
		token_set_pending_consumer(candidate->token, candidate);
	*candidatep = candidate;
	return 0;
}


int media_transport_reconfigure_alloc(
	struct media_transport **candidatep, struct media_transport *active,
	const struct bundle_group *group, struct list *streaml,
	const struct list *destination_streaml, const char *data_mid,
	const char *semantic_key,
	const struct media_transport_prm *consumer)
{
	return reconfigure_alloc(candidatep, active, group, streaml,
		destination_streaml, data_mid, semantic_key, consumer, false);
}


int media_transport_restart_alloc(
	struct media_transport **candidatep, struct media_transport *active,
	const struct bundle_group *group, struct list *streaml,
	const struct list *destination_streaml, const char *data_mid,
	const char *semantic_key,
	const struct media_transport_prm *consumer)
{
	return reconfigure_alloc(candidatep, active, group, streaml,
		destination_streaml, data_mid, semantic_key, consumer, true);
}


enum media_transport_transition media_transport_member_transition(
	const struct media_transport *mt, const struct stream *stream)
{
	const struct le *le;

	if (!mt || !stream)
		return MEDIA_TRANSPORT_NONE;
	for (le = list_head(&mt->members); le; le = le->next) {
		const struct media_transport_member *member = le->data;

		if (member->stream == stream)
			return member->transition;
	}
	return MEDIA_TRANSPORT_NONE;
}


static void mnat_connected_handler(const struct sa *raddr1,
				   const struct sa *raddr2, void *arg)
{
	struct media_transport *mt = arg;

	(void)raddr2;
	if (!raddr1 || !sa_isset(raddr1, SA_ALL)) {
		mt->terminal_err = EPROTO;
		notify_change(mt);
		return;
	}
	if (media_transport_set_remote(mt, raddr1)) {
		mt->terminal_err = EPROTO;
		notify_change(mt);
		return;
	}
	mt->connected = true;
	notify_change(mt);
}


static void mnat_attempt_handler(int err, const struct sa *raddr1,
				 const struct sa *raddr2, void *arg)
{
	struct media_transport *mt = arg;

	(void)raddr2;
	mt->attempt_running = false;
	if (err) {
		mt->terminal_err = err;
		notify_change(mt);
		return;
	}
	if (!raddr1 || !sa_isset(raddr1, SA_ALL)) {
		mt->terminal_err = EPROTO;
		notify_change(mt);
		return;
	}
	if (media_transport_set_remote(mt, raddr1)) {
		if (!mt->terminal_err)
			mt->terminal_err = EPROTO;
	}
	else {
		mt->connected = true;
	}
	notify_change(mt);
}


static void mnat_gather_handler(int err, void *arg)
{
	struct media_transport *mt = arg;

	mt->gather_waiting = false;
	if (err) {
		mt->terminal_err = err;
		notify_change(mt);
		return;
	}

	/* Gathering only makes this generation signalable.  Connectivity starts
	 * from pc_transport_session_start(), after the local offer/answer has been
	 * published to the peer.  Starting checks here races the signaling API:
	 * the peer cannot authenticate credentials it has not received yet. */
	notify_change(mt);
}


static void transport_recv_handler(struct mbuf *mb, void *arg)
{
	struct media_transport_token *token = arg;
	struct media_transport *mt;
	struct media_transport_packet *packet;
	menc_transport_recv_h *recvh;
	void (*callback_arg_deref)(void *arg);
	void *callback_arg;
	size_t len;

	if (!token)
		return;
	bundle_publication_lock(token->publication);
	mt = token_receiver_ref(token);
	if (!mt) {
		bundle_publication_unlock(token->publication);
		return;
	}
	if (!mt->recvh) {
		mem_deref(mt);
		bundle_publication_unlock(token->publication);
		return;
	}
	if (mt->terminal_err) {
		mem_deref(mt);
		bundle_publication_unlock(token->publication);
		return;
	}

	if (mt->consumer_ready) {
		recvh = mt->recvh;
		callback_arg_deref = mt->arg_ref && mt->arg_deref
					     ? mt->arg_deref : NULL;
		callback_arg = mt->arg_ref && mt->arg_deref && mt->arg
				     ? mt->arg_ref(mt->arg) : mt->arg;
		bundle_publication_unlock(token->publication);
		recvh(mb, callback_arg);
		if (callback_arg_deref && callback_arg)
			callback_arg_deref(callback_arg);
		mem_deref(mt);
		return;
	}
	len = mbuf_get_left(mb);
	if (mt->queued_packets >= TRANSPORT_PACKET_LIMIT ||
	    len > TRANSPORT_BYTE_LIMIT - mt->queued_bytes) {
		mt->terminal_err = ENOBUFS;
		list_flush(&mt->packets);
		mt->queued_bytes = 0;
		mt->queued_packets = 0;
		bundle_publication_unlock(token->publication);
		notify_change(mt);
		mem_deref(mt);
		return;
	}
	packet = mem_zalloc(sizeof(*packet), packet_destructor);
	if (!packet) {
		mt->terminal_err = ENOMEM;
		bundle_publication_unlock(token->publication);
		notify_change(mt);
		mem_deref(mt);
		return;
	}
	packet->mb = mem_ref(mb);
	list_append(&mt->packets, &packet->le, packet);
	mt->queued_bytes += len;
	++mt->queued_packets;
	mem_deref(mt);
	bundle_publication_unlock(token->publication);
}


static void transport_estab_handler(int err, enum menc_dtls_role role,
				    void *arg)
{
	struct media_transport_token *token = arg;
	struct media_transport *mt;
	menc_transport_estab_h *estabh = NULL;
	void (*arg_deref)(void *arg) = NULL;
	void *callback_arg = NULL;
	bool changed = false;

	if (!token)
		return;
	bundle_publication_lock(token->publication);
	mt = token_active_ref(token);
	if (!mt) {
		bundle_publication_unlock(token->publication);
		return;
	}
	if (err)
		mt->terminal_err = err;
	else {
		mt->established = true;
		mt->role = role;
		if (mt->menc_prepared && !mt->menc_ready) {
			err = prepare_members(mt);
			if (err)
				mt->terminal_err = err;
			else
				mt->menc_ready = true;
		}
	}
	if (mt->finalized && mt->estabh) {
		estabh = mt->estabh;
		arg_deref = mt->arg_ref && mt->arg_deref
				    ? mt->arg_deref : NULL;
		callback_arg = arg_deref && mt->arg
				     ? mt->arg_ref(mt->arg) : mt->arg;
	}
	else
		changed = true;
	bundle_publication_unlock(token->publication);
	if (estabh)
		estabh(err, role, callback_arg);
	else if (changed)
		notify_change(mt);
	if (arg_deref && callback_arg)
		arg_deref(callback_arg);
	mem_deref(mt);
}


static void transport_close_handler(int err, void *arg)
{
	struct media_transport_token *token = arg;
	struct media_transport *mt;
	menc_transport_close_h *closeh = NULL;
	void (*arg_deref)(void *arg) = NULL;
	void *callback_arg = NULL;
	int terminal_err;
	bool changed = false;

	if (!token)
		return;
	bundle_publication_lock(token->publication);
	mt = token_active_ref(token);
	if (!mt) {
		bundle_publication_unlock(token->publication);
		return;
	}
	mt->terminal_err = err ? err : EPIPE;
	terminal_err = mt->terminal_err;
	if (mt->finalized && mt->closeh) {
		closeh = mt->closeh;
		arg_deref = mt->arg_ref && mt->arg_deref
				    ? mt->arg_deref : NULL;
		callback_arg = arg_deref && mt->arg
				     ? mt->arg_ref(mt->arg) : mt->arg;
	}
	else
		changed = true;
	bundle_publication_unlock(token->publication);
	if (closeh)
		closeh(terminal_err, callback_arg);
	else if (changed)
		notify_change(mt);
	if (arg_deref && callback_arg)
		arg_deref(callback_arg);
	mem_deref(mt);
}


static int prepare_members(struct media_transport *mt)
{
	struct le *le;
	int err;

	for (le = list_head(&mt->members); le; le = le->next) {
		struct media_transport_member *member = le->data;

		if (!member->previous)
			member->previous =
				stream_menc_transport_ref(member->stream);
		if (mt->reconfigure &&
		    (member->transition == MEDIA_TRANSPORT_RETAIN ||
		     member->transition == MEDIA_TRANSPORT_MOVE_OUT))
			continue;
		if (member->target) {
			const bool rtcp_mux = sdp_media_rattr(
				stream_sdpmedia(member->stream), "rtcp-mux");

			if (member->previous == mt->menc_transport)
				continue;
			err = stream_prepare_menc_transport_mux(
				member->stream, mt->menc_transport, rtcp_mux);
		}
		else {
			if (member->previous != mt->menc_transport)
				continue;
			err = stream_prepare_menc_transport_removal(
				member->stream, mt->menc_transport);
		}
		if (err)
			return err;
	}
	return menc_transport_members_prepare(mt->menc, mt->menc_transport);
}


static bool member_transaction_required(const struct media_transport *mt)
{
	const struct le *le;

	for (le = list_head(&mt->members); le; le = le->next) {
		const struct media_transport_member *member = le->data;

		if (member->transition == MEDIA_TRANSPORT_ADD ||
		    member->transition == MEDIA_TRANSPORT_REMOVE)
			return true;
	}
	return false;
}


int media_transport_prepare(struct media_transport *mt)
{
	struct sa laddr;
	const struct sa *sdp_remote;
	struct le *le;
	int err;

	if (!mt || mt->prepared || mt->activated || mt->adopted)
		return mt ? EALREADY : EINVAL;
	if (mt->reconfigure) {
		err = bundle_transport_alloc(&mt->route, mt->group,
					     &mt->route_streams, mt->data_mid);
		if (!err)
			err = bundle_transport_prepare(
				mt->route, mt->group, mt->sock,
				&mt->route_generation);
		if (!err)
			mt->route_prepared = true;
		if (!err && mt->remote_set)
			err = bundle_transport_set_remote(
				mt->route, mt->route_generation, &mt->remote);
		if (!err && mt->ice_restart) {
			if (!mt->previous_runtime ||
			    !sa_isset(&mt->previous_runtime->remote, SA_ALL))
				err = EDESTADDRREQ;
			else {
				err = menc_transport_peer_set(
					mt->menc, mt->menc_transport, NULL,
					&mt->old_peer);
				if (!err && !sa_cmp(
						    &mt->old_peer,
						    &mt->previous_runtime->remote,
						    SA_ALL))
					err = ESTALE;
			}
			if (!err)
				mt->peer_prepared = true;
		}
		if (!err && mt->ice_restart) {
			err = mt->mnat->mediaprepareh(
				mt->previous_runtime->mnat_media, false);
			if (!err)
				mt->previous_mnat_prepared = true;
			if (!err)
				err = mt->mnat->mediaprepareh(
					mt->mnat_media, true);
			if (!err)
				mt->mnat_prepared = true;
		}
		if (!err && member_transaction_required(mt)) {
			mt->menc_prepared = true;
			err = prepare_members(mt);
			mt->menc_ready = !err;
		}
		else if (!err)
			mt->menc_ready = true;
		if (!err)
			mt->prepared = true;
		if (err)
			media_transport_abort(mt);
		return err;
	}
	err = sdp_media_save_lattrs(&mt->saved_attrs, mt->transport_sdpm);
	if (err)
		return err;
	sa_cpy(&mt->saved_laddr, sdp_media_laddr(mt->transport_sdpm));
	mt->sdp_staged = true;
	sa_init(&laddr, mt->af);
	err = udp_listen(&mt->sock, &laddr, NULL, NULL);
	if (!err)
		err = udp_local_get(mt->sock, &laddr);
	if (!err) {
		sdp_media_set_laddr(mt->transport_sdpm, &laddr);
		sdp_media_set_lport(mt->transport_sdpm, sa_port(&laddr));
		sdp_media_del_lattr(mt->transport_sdpm, "candidate");
	}
	if (!err)
		err = bundle_transport_alloc(&mt->route, mt->group,
					     &mt->route_streams,
					     mt->data_mid);
	if (!err)
		err = bundle_transport_prepare(mt->route, mt->group, mt->sock,
					       &mt->route_generation);
	if (!err)
		mt->route_prepared = true;
	if (!err && mt->mnat && mt->mnat->mediah) {
		if (!mt->mnat->mediaprepareh || !mt->mnat->mediaactivateh ||
		    !mt->mnat->mediarollbackh || !mt->mnat->mediafinalizeh ||
		    !mt->mnat->mediaaborth)
			err = ENOTSUP;
		else
			err = mt->mnat->mediah(&mt->mnat_media, mt->mnats,
					       mt->sock, NULL, mt->transport_sdpm,
					       mnat_connected_handler, mt);
		if (!err)
			err = mt->mnat->mediaprepareh(mt->mnat_media, true);
		if (!err)
			mt->mnat_prepared = true;
	}
	if (!err)
		err = token_alloc(&mt->token);
	if (!err) {
		token_promote(mt->token, mt);
		token_clear_pending_consumer(mt->token, mt);
			err = mt->menc->transporth(
				&mt->menc_transport, mt->mencs, mt->sock, NULL,
				mt->transport_sdpm, mt->offerer, transport_recv_handler,
				transport_estab_handler, transport_close_handler,
				mt->token);
		}
		if (!err)
			err = menc_transport_commit_identity(
				mt->menc, mt->menc_transport);
		if (!err) {
		mt->owns_menc_transport = true;
		/* From the first staged member onward abort must unwind the MENC
		 * transaction even if its validating prepare step fails. */
		mt->menc_prepared = true;
		err = prepare_members(mt);
		if (err == EAGAIN)
			err = 0;
		else if (!err)
			mt->menc_ready = true;
	}
	if (!err && (!mt->mnat || !mt->mnat->mediah)) {
		sdp_remote = sdp_media_raddr(mt->transport_sdpm);
		if (sdp_remote && sa_isset(sdp_remote, SA_ALL)) {
			err = media_transport_set_remote(mt, sdp_remote);
			mt->connected = !err;
		}
	}
	if (!err && mt->remote_set && !mt->menc_started) {
		err = menc_transport_start(mt->menc, mt->menc_transport,
					   &mt->remote);
		if (!err || err == EALREADY) {
			mt->menc_started = true;
			err = 0;
		}
	}
	if (!err) {
		/* Validate the exact member snapshot while allocation is allowed. */
		for (le = list_head(&mt->members); le; le = le->next) {
			struct media_transport_member *member = le->data;

			if (!exact_member(mt, member->stream)) {
				err = ESTALE;
				break;
			}
		}
	}
	if (!err)
		mt->prepared = true;
	if (err)
		media_transport_abort(mt);
	return err;
}


int media_transport_rekey(struct media_transport *mt, const char *semantic_key)
{
	char *key = NULL;
	int err;

	if (!mt || !str_isset(semantic_key))
		return EINVAL;
	if (!mt->prepared || mt->activated || mt->adopted || mt->finalized)
		return EALREADY;
	if (!str_cmp(mt->key, semantic_key))
		return 0;
	err = str_dup(&key, semantic_key);
	if (err)
		return err;
	mem_deref(mt->key);
	mt->key = key;
	return 0;
}


bool media_transport_gathered(const struct media_transport *mt)
{
	if (!mt)
		return false;
	if ((mt->reconfigure && !mt->ice_restart) || !mt->mnat_media || !mt->mnat ||
	    !mt->mnat->mediah)
		return true;
	return mt->mnat->mediagatheredh &&
		mt->mnat->mediagatheredh(mt->mnat_media);
}


bool media_transport_remote_set(const struct media_transport *mt)
{
	return mt && mt->remote_set;
}


static bool restart_remote_attr_handler(const char *name, const char *value,
					void *arg)
{
	struct media_transport *mt = arg;

	mt->mnat->attrh(mt->mnat_media, name, value);
	return false;
}


int media_transport_gather_start(struct media_transport *mt)
{
	int err;

	if (!mt || !mt->prepared || !mt->mnat_media ||
	    !mt->mnat || !mt->mnat->mediagatheredh)
		return EINVAL;
	if (mt->mnat->mediagatheredh(mt->mnat_media))
		return 0;
	if (!mt->mnat->mediagatherwaith || !mt->mnat->mediagathercancelh)
		return ENOTSUP;
	if (mt->gather_waiting)
		return EAGAIN;
	err = mt->mnat->mediagatherwaith(mt->mnat_media,
					  mnat_gather_handler, mt);
	if (err == EAGAIN)
		mt->gather_waiting = true;
	return err;
}


struct restart_attr_copy {
	struct sdp_media *destination;
	const char *name;
	int err;
};


static bool restart_attr_copy_handler(const char *name, const char *value,
				      void *arg)
{
	struct restart_attr_copy *copy = arg;

	(void)name;
	if (!copy->err)
		copy->err = value
			? sdp_media_set_lattr(copy->destination, false,
					      copy->name, "%s", value)
			: sdp_media_set_lattr(copy->destination, false,
					      copy->name, NULL);
	return copy->err != 0;
}


int media_transport_restart_apply_sdp(struct media_transport *mt)
{
	static const char *attrs[] = {
		"ice-ufrag", "ice-pwd", "candidate", "end-of-candidates",
	};

	if (!mt || !mt->ice_restart || !mt->restart_sdpm)
		return EINVAL;
	for (size_t i = 0; i < RE_ARRAY_SIZE(attrs); ++i) {
		struct restart_attr_copy copy = {
			.destination = mt->transport_sdpm,
			.name = attrs[i],
		};

		sdp_media_del_lattr(mt->transport_sdpm, attrs[i]);
		(void)sdp_media_lattr_apply(mt->restart_sdpm, attrs[i],
					    restart_attr_copy_handler, &copy);
		if (copy.err)
			return copy.err;
	}
	return 0;
}


bool media_transport_mnat_attr(struct media_transport *mt, const char *name,
			       const char *value)
{
	if (!mt || !name || !mt->mnat_media || !mt->mnat || !mt->mnat->attrh ||
	    (!mt->prepared && !mt->activated && !mt->adopted))
		return false;

	mt->mnat->attrh(mt->mnat_media, name, value);
	return true;
}


int media_transport_attempt_start(struct media_transport *mt)
{
	int err;

	if (!mt || !mt->prepared)
		return EINVAL;
	if (mt->terminal_err)
		return mt->terminal_err;
	if ((mt->reconfigure && !mt->ice_restart) || mt->remote_set)
		return 0;
	if (mt->ice_restart && !mt->remote_ice_loaded && mt->mnat->attrh) {
		/* The session-wide MNAT update intentionally ignores inactive
		 * replacement generations.  Decode the now-committed remote ICE
		 * attributes into this exact candidate before its isolated check. */
		(void)sdp_media_rattr_apply(mt->transport_sdpm, NULL,
					    restart_remote_attr_handler, mt);
		mt->remote_ice_loaded = true;
	}
	if (!mt->mnat_media || !mt->mnat || !mt->mnat->mediah)
		return EDESTADDRREQ;
	if (!mt->mnat->mediaattemptstarth ||
	    !mt->mnat->mediaattemptcancelh ||
	    !mt->mnat->mediagatheredh)
		return ENOTSUP;
	if (!mt->mnat->mediagatheredh(mt->mnat_media)) {
		if (!mt->mnat->mediagatherwaith ||
		    !mt->mnat->mediagathercancelh)
			return ENOTSUP;
		if (mt->gather_waiting)
			return EALREADY;
		err = mt->mnat->mediagatherwaith(
			mt->mnat_media, mnat_gather_handler, mt);
		if (err == EAGAIN) {
			mt->gather_waiting = true;
			return 0;
		}
		if (err)
			return err;
	}
	if (mt->attempt_running)
		return EALREADY;
	mt->attempt_running = true;
	err = mt->mnat->mediaattemptstarth(
		mt->mnat_media, mnat_attempt_handler, mt);
	if (err) {
		mt->attempt_running = false;
		/* Once local gathering has completed, EAGAIN cannot describe a
		 * future local-gather transition.  Report missing remote ICE state as
		 * terminal so a candidate generation cannot wait forever. */
		if (err == EAGAIN)
			err = EDESTADDRREQ;
	}
	return err;
}


int media_transport_set_remote(struct media_transport *mt,
			       const struct sa *remote)
{
	int err;

	if (!mt || !remote || !sa_isset(remote, SA_ALL))
		return EINVAL;
	if (mt->route_generation) {
		err = bundle_transport_set_remote(mt->route,
					  mt->route_generation, remote);
		if (err) {
			mt->terminal_err = err;
			notify_change(mt);
			return err;
		}
	}
	sa_cpy(&mt->remote, remote);
	mt->remote_set = true;
	if (mt->menc_transport && !mt->established && !mt->menc_started) {
		err = menc_transport_start(mt->menc, mt->menc_transport, remote);
		if (!err || err == EALREADY) {
			mt->menc_started = true;
			return 0;
		}
		mt->terminal_err = err;
		notify_change(mt);
		return err;
	}
	notify_change(mt);
	return 0;
}


bool media_transport_ready(const struct media_transport *mt)
{
	return mt && mt->prepared && !mt->terminal_err && mt->remote_set &&
		mt->established && mt->menc_ready &&
		bundle_transport_ready(mt->route,
							  mt->route_generation);
}


bool media_transport_published(const struct media_transport *mt)
{
	return mt && mt->activated && mt->finalized;
}


int media_transport_error(const struct media_transport *mt)
{
	return mt ? mt->terminal_err : EINVAL;
}


int media_transport_send(struct media_transport *mt, struct mbuf *mb)
{
	struct media_transport *active;
	struct media_transport *sender;
	int err;

	if (!mt || !mb)
		return EINVAL;
	if (mt->terminal_err)
		return mt->terminal_err;
	bundle_publication_lock(mt->token ? mt->token->publication : NULL);
	/* A prepared candidate SCTP association must be able to emit INIT/DCEP
	 * over its isolated DTLS socket before session publication.  The token
	 * still prevents it from using any active runtime's association. */
	active = token_active_ref(mt->token);
	/* A reused data binding deliberately keeps a stable runtime handle across
	 * publication.  Resolve that handle through the shared token so sends
	 * follow activate/rollback atomically without rebinding the SCTP object. */
	sender = active && active->menc_transport == mt->menc_transport
		       ? active : mt;
	if (!active ||
	    (sender != active &&
	     !(mt->reconfigure && mt->previous_runtime == active)) ||
	    (!sender->prepared && !sender->activated) ||
	    !sender->established || !sender->menc_transport) {
		mem_deref(active);
		bundle_publication_unlock(
			mt->token ? mt->token->publication : NULL);
		return ENOTCONN;
	}
	err = menc_transport_send(sender->menc, sender->menc_transport, mb);
	mem_deref(active);
	bundle_publication_unlock(mt->token ? mt->token->publication : NULL);
	return err;
}


void media_transport_detach_consumer(struct media_transport *mt)
{
	void (*arg_deref)(void *arg) = NULL;
	void *old_arg = NULL;

	if (!mt)
		return;
	bundle_publication_lock(mt->token ? mt->token->publication : NULL);
	mt->connectedh = NULL;
	mt->recvh = NULL;
	mt->estabh = NULL;
	mt->closeh = NULL;
	mt->changeh = NULL;
	if (mt->arg_ref && mt->arg_deref) {
		old_arg = mt->arg;
		arg_deref = mt->arg_deref;
	}
	mt->arg_ref = NULL;
	mt->arg_deref = NULL;
	mt->arg = NULL;
	mt->consumer_ready = false;
	/* The signaling session can be destroyed by an application callback while
	 * this runtime is still pinned by the receive trampoline.  Drop the SDP
	 * media reference during terminal consumer teardown, while its owning
	 * session and the rest of the PeerConnection are still alive. */
	mt->transport_sdpm = mem_deref(mt->transport_sdpm);
	list_flush(&mt->packets);
	mt->queued_bytes = 0;
	mt->queued_packets = 0;
	bundle_publication_unlock(mt->token ? mt->token->publication : NULL);
	if (arg_deref && old_arg)
		arg_deref(old_arg);
}


void media_transport_stop_members(struct media_transport *mt)
{
	struct le *le;

	if (!mt)
		return;
	for (le = list_head(&mt->members); le; le = le->next) {
		struct media_transport_member *member = le->data;

		stream_stop(member->stream);
	}
}


int media_transport_consumer_ready(struct media_transport *mt)
{
	struct media_transport *hold;
	struct bundle_publication *publication;

	if (!mt || !mt->recvh)
		return EINVAL;
	if (mt->terminal_err)
		return mt->terminal_err;
	hold = mem_ref(mt);
	publication = mt->token ? mt->token->publication : NULL;
	bundle_publication_lock(publication);
	mt->consumer_ready = true;
	bundle_publication_unlock(publication);
	for (;;) {
		menc_transport_recv_h *recvh;
		void (*callback_arg_deref)(void *arg);
		void *callback_arg;
		struct media_transport_packet *packet =
			NULL;
		size_t len;

		bundle_publication_lock(publication);
		if (!mt->consumer_ready || !mt->recvh || !mt->packets.head) {
			if (!mt->consumer_ready || !mt->recvh) {
				list_flush(&mt->packets);
				mt->queued_bytes = 0;
				mt->queued_packets = 0;
			}
			bundle_publication_unlock(publication);
			break;
		}
		packet = mt->packets.head->data;
		len = mbuf_get_left(packet->mb);
		list_unlink(&packet->le);
		mt->queued_bytes -= len;
		--mt->queued_packets;
		recvh = mt->recvh;
		callback_arg_deref = mt->arg_ref && mt->arg_deref
					     ? mt->arg_deref : NULL;
		callback_arg = mt->arg_ref && mt->arg_deref && mt->arg
				     ? mt->arg_ref(mt->arg) : mt->arg;
		bundle_publication_unlock(publication);
		recvh(packet->mb, callback_arg);
		if (callback_arg_deref && callback_arg)
			callback_arg_deref(callback_arg);
		mem_deref(packet);
	}
	mem_deref(hold);
	return 0;
}


bool media_transport_has_consumer(const struct media_transport *mt)
{
	return mt && mt->recvh;
}


int media_transport_activate(struct media_transport *mt)
{
	struct le *le;
	struct stream *owner;
	int err;

	if (!media_transport_ready(mt))
		return EAGAIN;
	err = bundle_transport_activate(mt->route, mt->route_generation);
	if (err)
		return err;
	if (mt->peer_prepared) {
		struct sa ignored;

		err = menc_transport_peer_set(mt->menc, mt->menc_transport,
					      &mt->remote, &ignored);
		if (err) {
			(void)bundle_transport_rollback(
				mt->route, mt->route_generation);
			return err;
		}
		mt->peer_activated = true;
	}
	if (mt->mnat_prepared)
		mt->mnat->mediaactivateh(mt->mnat_media);
	if (mt->previous_mnat_prepared)
		mt->mnat->mediaactivateh(mt->previous_runtime->mnat_media);
	owner = mt->ice_restart ? mt->tag_owner : NULL;
	if (owner)
		stream_publish_mnat_media(owner, mt->mnat_media);
	if (mt->ice_restart && mt->mnat_publishh)
		mt->mnat_publishh(mt->mnat_media, mt->arg);
	if (mt->menc_prepared)
		menc_transport_members_activate(mt->menc, mt->menc_transport);
	for (le = list_head(&mt->members); le; le = le->next) {
		struct media_transport_member *member = le->data;

		if (member->target)
			stream_publish_menc_transport(member->stream,
						      mt->menc_transport);
	}
	if (mt->reconfigure) {
		mt->previous_runtime->owns_menc_transport = false;
		mt->owns_menc_transport = true;
		mt->previous_binding_retained =
			mt->previous_runtime->previous_binding_retained;
		mt->previous_runtime->previous_binding_retained = false;
		token_promote(mt->token, mt);
	}
	mt->prepared = false;
	mt->activated = true;
	return 0;
}


void media_transport_rollback(struct media_transport *mt)
{
	struct le *le;
	struct stream *owner;

	if (!mt || !mt->activated || mt->finalized || mt->adopted)
		return;
	if (mt->peer_activated) {
		(void)menc_transport_peer_set(mt->menc, mt->menc_transport,
					      &mt->old_peer, NULL);
		mt->peer_activated = false;
	}
	if (mt->reconfigure) {
		token_clear_pending_consumer(mt->token, mt);
		token_set_active(mt->token, mt->previous_runtime);
		mt->owns_menc_transport = false;
		mt->previous_runtime->owns_menc_transport = true;
		mt->previous_runtime->previous_binding_retained =
			mt->previous_binding_retained;
		mt->previous_binding_retained = false;
	}
	for (le = list_head(&mt->members); le; le = le->next) {
		struct media_transport_member *member = le->data;

		stream_publish_menc_transport(member->stream, member->previous);
	}
	(void)bundle_transport_rollback(mt->route, mt->route_generation);
	if (mt->menc_prepared)
		menc_transport_members_rollback(mt->menc, mt->menc_transport);
	if (mt->previous_mnat_prepared)
		mt->mnat->mediarollbackh(mt->previous_runtime->mnat_media);
	if (mt->mnat_prepared)
		mt->mnat->mediarollbackh(mt->mnat_media);
	owner = mt->ice_restart ? mt->tag_owner : NULL;
	if (owner)
		stream_publish_mnat_media(
			owner, mt->previous_runtime->mnat_media);
	if (mt->ice_restart && mt->mnat_publishh)
		mt->mnat_publishh(mt->previous_runtime->mnat_media, mt->arg);
	if (mt->sdp_staged) {
		sdp_media_set_laddr(mt->transport_sdpm, &mt->saved_laddr);
		sdp_media_set_lport(mt->transport_sdpm,
				    sa_port(&mt->saved_laddr));
		sdp_media_restore_lattrs(mt->transport_sdpm, mt->saved_attrs);
		mt->saved_attrs = NULL;
		mt->sdp_staged = false;
	}
	mt->activated = false;
}


void media_transport_finalize(struct media_transport *mt)
{
	if (!mt || !mt->activated || mt->finalized || mt->adopted)
		return;
	(void)bundle_transport_finalize(mt->route, mt->route_generation);
	if (mt->menc_prepared)
		menc_transport_members_retire(mt->menc, mt->menc_transport);
	if (mt->mnat_prepared)
		mt->mnat->mediafinalizeh(mt->mnat_media);
	if (mt->previous_mnat_prepared)
		mt->mnat->mediafinalizeh(mt->previous_runtime->mnat_media);
	mt->previous_runtime = mem_deref(mt->previous_runtime);
	mt->saved_attrs = mem_deref(mt->saved_attrs);
	mt->sdp_staged = false;
	mt->finalized = true;
	mt->peer_prepared = false;
	mt->peer_activated = false;
	/* Protocol-consumer callbacks are enabled explicitly before activation.
	 * Finalization only retires resources; it never enters application-owned
	 * callback state. */
}


void media_transport_notify_members(struct media_transport *mt)
{
	if (mt && mt->finalized && mt->menc_prepared)
		menc_transport_members_notify(mt->menc, mt->menc_transport);
}


void media_transport_abort(struct media_transport *mt)
{
	if (!mt || mt->adopted)
		return;
	token_clear_pending_consumer(mt->token, mt);
	if (mt->attempt_running && mt->mnat &&
	    mt->mnat->mediaattemptcancelh) {
		mt->mnat->mediaattemptcancelh(mt->mnat_media);
		mt->attempt_running = false;
	}
	if (mt->gather_waiting && mt->mnat &&
	    mt->mnat->mediagathercancelh) {
		mt->mnat->mediagathercancelh(mt->mnat_media);
		mt->gather_waiting = false;
	}
	if (mt->activated) {
		media_transport_rollback(mt);
		return;
	}
	if (mt->menc_prepared)
		menc_transport_members_abort(mt->menc, mt->menc_transport);
	if (mt->mnat_prepared)
		mt->mnat->mediaaborth(mt->mnat_media);
	if (mt->previous_mnat_prepared)
		mt->mnat->mediaaborth(mt->previous_runtime->mnat_media);
	if (mt->route_prepared)
		(void)bundle_transport_abort(mt->route, mt->route_generation);
	if (mt->sdp_staged) {
		sdp_media_set_laddr(mt->transport_sdpm, &mt->saved_laddr);
		sdp_media_set_lport(mt->transport_sdpm,
				    sa_port(&mt->saved_laddr));
		sdp_media_restore_lattrs(mt->transport_sdpm, mt->saved_attrs);
		mt->saved_attrs = NULL;
		mt->sdp_staged = false;
	}
	mt->prepared = false;
	mt->menc_prepared = false;
	mt->menc_ready = false;
	mt->mnat_prepared = false;
	mt->previous_mnat_prepared = false;
	mt->peer_prepared = false;
	mt->peer_activated = false;
	mt->route_prepared = false;
	list_flush(&mt->packets);
	mt->queued_bytes = 0;
	mt->queued_packets = 0;
	token_clear_active(mt->token, mt);
}


void media_transport_release(struct media_transport *mt)
{
	if (!mt)
		return;
	token_clear_pending_consumer(mt->token, mt);
	token_clear_active(mt->token, mt);
}


void media_transport_retire_callbacks(struct media_transport *mt)
{
	struct menc_transport_binding detached = {0};
	struct le *le;

	if (!mt)
		return;
	mt->restore_binding = false;
	if (mt->owns_menc_transport && mt->menc_transport) {
		(void)menc_transport_rebind(mt->menc, mt->menc_transport,
					    &detached, mt->token, NULL, NULL);
		mt->owns_menc_transport = false;
	}
	/* Terminal consumer teardown can release the transports captured from
	 * streams before this runtime was published.  Retire their legacy raw
	 * callback arguments before member destruction drops the last reference. */
	for (le = list_head(&mt->members); le; le = le->next) {
		struct media_transport_member *member = le->data;

		if (member->previous &&
		    member->previous != mt->menc_transport)
			menc_transport_detach(mt->menc, member->previous);
	}
	if (mt->previous_binding_retained &&
	    mt->previous_binding.arg_deref && mt->previous_binding.arg) {
		mt->previous_binding.arg_deref(mt->previous_binding.arg);
		mt->previous_binding.arg = NULL;
		mt->previous_binding_retained = false;
	}
}


const char *media_transport_key(const struct media_transport *mt)
{
	return mt ? mt->key : NULL;
}


const struct bundle_group *media_transport_group(const struct media_transport *mt)
{
	return mt ? mt->group : NULL;
}


struct udp_sock *media_transport_socket_ref(const struct media_transport *mt)
{
	return mt ? mem_ref(mt->sock) : NULL;
}


struct mnat_media *media_transport_mnat_media_ref(
	const struct media_transport *mt)
{
	return mt ? mem_ref(mt->mnat_media) : NULL;
}


enum menc_dtls_role media_transport_role(const struct media_transport *mt)
{
	return mt ? mt->role : MENC_DTLS_ROLE_UNKNOWN;
}


int media_transport_bind_publication(struct media_transport *mt,
				     struct bundle_publication *publication)
{
	if (!mt || !mt->route || !publication)
		return EINVAL;
	if (mt->token) {
		mtx_lock(&mt->token->lock);
		if (mt->token->publication &&
		    mt->token->publication != publication) {
			mtx_unlock(&mt->token->lock);
			return EBUSY;
		}
		if (!mt->token->publication)
			mt->token->publication = mem_ref(publication);
		mtx_unlock(&mt->token->lock);
	}
	return bundle_transport_bind_publication(mt->route, publication);
}


void media_transport_set_observer(struct media_transport *mt,
				  void (*changeh)(void *arg), void *arg)
{
	if (!mt)
		return;
	mt->observerh = changeh;
	mt->observer_arg = arg;
}
