/**
 * @file datachannel.c WebRTC data-channel peer-connection adapter
 *
 * Copyright (C) 2026 The baresip project
 */

#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <re_datachannel.h>
#include <baresip.h>
#include "core.h"
#include "datachannel_internal.h"


void abort_pending_channels(struct data_context *ctx,
				   uint64_t generation);


static void mem_deref_void(void *arg)
{
	(void)mem_deref(arg);
}


static void pending_message_release(struct data_channel *dc,
				    struct pending_message *message)
{
	if (!message)
		return;

	list_unlink(&message->le);
	if (dc->ctx) {
		if (dc->ctx->deferred_message_bytes >= message->charge)
			dc->ctx->deferred_message_bytes -= message->charge;
		else
			dc->ctx->deferred_message_bytes = 0;
	}
	mem_deref(message);
}


static void pending_messages_flush(struct data_channel *dc)
{
	while (dc->pending_messages.head)
		pending_message_release(dc, dc->pending_messages.head->data);
}


void data_context_report_error(struct data_context *ctx, int err)
{
	if (!ctx || !err)
		return;
	if (ctx->callbacks_deferred) {
		if (!ctx->deferred_error)
			ctx->deferred_error = err;
		return;
	}
	if (ctx->errorh)
		ctx->errorh(err, ctx->arg);
}


uint32_t dispatch_owner_refs(const struct data_context *ctx)
{
	uint32_t refs;

	if (!ctx->dispatch_refsh)
		return 0;
	refs = ctx->dispatch_refsh(ctx->dispatch_refs_arg);
	mem_ref(ctx->dispatch_refs_arg);
	return refs;
}


bool dispatch_callback_complete(struct data_context *ctx,
				       uint32_t refs_before,
				       uint64_t generation)
{
	if (ctx->dispatch_refsh) {
		uint32_t refs_after =
			ctx->dispatch_refsh(ctx->dispatch_refs_arg) - 1;

		if (refs_after < refs_before)
			++ctx->dispatch_generation;
		mem_deref(ctx->dispatch_refs_arg);
	}

	return ctx->dispatch_generation == generation;
}


static void data_channel_snapshot_destructor(void *arg)
{
	struct data_channel_snapshot *state = arg;

	list_unlink(&state->le);
	mem_deref(state->channel);
}


static void data_context_state_destructor(void *arg)
{
	struct data_context_state *state = arg;

	list_flush(&state->channels);
	mem_deref(state->transport_sdpm);
	mem_deref(state->sock);
	mem_deref(state->remote_bundles);
	mem_deref((void *)state->active_group);
	mem_deref(state->bundle_base);
	mem_deref(state->mid);
	mem_deref(state->remote_identity);
	mem_deref(state->remote_ice_identity);
	mem_deref(state->local_ice_identity);
}


int data_context_state_save(struct data_context_state **statep,
				   const struct data_context *ctx)
{
	struct data_context_state *state;
	struct le *le;
	int err;

	if (!statep || !ctx)
		return EINVAL;

	state = mem_zalloc(sizeof(*state), data_context_state_destructor);
	if (!state)
		return ENOMEM;

	state->transport_sdpm = mem_ref(ctx->transport_sdpm);
	state->sock = mem_ref(ctx->sock);
	state->remote_bundles = mem_ref(ctx->remote_bundles);
	state->active_group = mem_ref((void *)ctx->active_group);
	state->bundle_base = mem_ref(ctx->bundle_base);
	state->pending_generation = ctx->pending_binding
					  ? ctx->pending_binding->generation
					  : 0;
	state->send_limit = ctx->send_limit;
	state->remote_port = ctx->remote_port;
	state->dtls_role = ctx->dtls_role;
	state->bundled = ctx->bundled;
	state->bundle_data_first = ctx->bundle_data_first;
	state->rejected = ctx->rejected;
	state->transport_started = ctx->transport_started;
	state->dtls_ready = ctx->dtls_ready;
	state->remote_accepted = ctx->remote_accepted;
	state->local_committed = ctx->local_committed;
	state->offered_ice_restart = ctx->offered_ice_restart;
	state->transport_replacing = ctx->transport_replacing;
	state->callbacks_deferred = ctx->callbacks_deferred;
	state->rejection_pending = ctx->rejection_pending;
	state->deferred_error = ctx->deferred_error;

	err = ctx->mid ? str_dup(&state->mid, ctx->mid) : 0;
	if (!err && ctx->remote_identity)
		err = str_dup(&state->remote_identity,
			      ctx->remote_identity);
	if (!err && ctx->remote_ice_identity)
		err = str_dup(&state->remote_ice_identity,
			      ctx->remote_ice_identity);
	if (!err && ctx->local_ice_identity)
		err = str_dup(&state->local_ice_identity,
			      ctx->local_ice_identity);

	for (le = ctx->channels.head; !err && le; le = le->next) {
		const struct data_channel *dc = le->data;
		struct data_channel_snapshot *channel_state =
			mem_zalloc(sizeof(*channel_state),
				   data_channel_snapshot_destructor);

		if (!channel_state) {
			err = ENOMEM;
			break;
		}
		channel_state->channel = mem_ref((void *)dc);
		channel_state->sdp_remote = dc->sdp_remote;
		channel_state->sdp_offered = dc->sdp_offered;
		channel_state->sdp_seen = dc->sdp_seen;
		channel_state->sdp_staged = dc->sdp_staged;
		channel_state->sdp_provisional = dc->sdp_provisional;
		channel_state->sdp_remove_pending =
			dc->sdp_remove_pending;
		list_append(&state->channels, &channel_state->le,
			    channel_state);
	}
	if (err) {
		mem_deref(state);
		return err;
	}

	*statep = state;
	return 0;
}


void data_context_state_restore(struct data_context *ctx,
				       struct data_context_state *state)
{
	struct le *le;

	if (!ctx || !state)
		return;

	le = ctx->channels.head;
	while (le) {
		struct data_channel *dc = le->data;
		struct le *next = le->next;
		struct le *saved;

		for (saved = state->channels.head; saved;
		     saved = saved->next) {
			const struct data_channel_snapshot *channel_state =
				saved->data;

			if (channel_state->channel == dc)
				break;
		}
		if (!saved) {
			if (dc->dc)
				(void)dc_channel_close(dc->dc);
			mem_deref(dc);
		}
		le = next;
	}

	ctx->transport_sdpm = mem_deref(ctx->transport_sdpm);
	ctx->transport_sdpm = state->transport_sdpm;
	state->transport_sdpm = NULL;
	ctx->sock = mem_deref(ctx->sock);
	ctx->sock = state->sock;
	state->sock = NULL;
	ctx->remote_bundles = mem_deref(ctx->remote_bundles);
	ctx->remote_bundles = state->remote_bundles;
	state->remote_bundles = NULL;
	ctx->active_group = mem_deref((void *)ctx->active_group);
	ctx->active_group = state->active_group;
	state->active_group = NULL;
	ctx->bundle_base = mem_deref(ctx->bundle_base);
	ctx->bundle_base = state->bundle_base;
	state->bundle_base = NULL;
	ctx->mid = mem_deref(ctx->mid);
	ctx->mid = state->mid;
	state->mid = NULL;
	ctx->remote_identity = mem_deref(ctx->remote_identity);
	ctx->remote_identity = state->remote_identity;
	state->remote_identity = NULL;
	ctx->remote_ice_identity = mem_deref(ctx->remote_ice_identity);
	ctx->remote_ice_identity = state->remote_ice_identity;
	state->remote_ice_identity = NULL;
	ctx->local_ice_identity = mem_deref(ctx->local_ice_identity);
	ctx->local_ice_identity = state->local_ice_identity;
	state->local_ice_identity = NULL;
	ctx->send_limit = state->send_limit;
	ctx->remote_port = state->remote_port;
	ctx->dtls_role = state->dtls_role;
	ctx->bundled = state->bundled;
	ctx->bundle_data_first = state->bundle_data_first;
	ctx->rejected = state->rejected;
	ctx->transport_started = state->transport_started;
	ctx->dtls_ready = state->dtls_ready;
	ctx->remote_accepted = state->remote_accepted;
	ctx->local_committed = state->local_committed;
	ctx->offered_ice_restart = state->offered_ice_restart;
	ctx->transport_replacing = state->transport_replacing;
	ctx->callbacks_deferred = state->callbacks_deferred;
	ctx->rejection_pending = state->rejection_pending;
	ctx->deferred_error = state->deferred_error;
	for (le = state->channels.head; le; le = le->next) {
		struct data_channel_snapshot *channel_state = le->data;
		struct data_channel *dc = channel_state->channel;

		if (dc->ctx != ctx)
			continue;
		dc->sdp_remote = channel_state->sdp_remote;
		dc->sdp_offered = channel_state->sdp_offered;
		dc->sdp_seen = channel_state->sdp_seen;
		dc->sdp_staged = channel_state->sdp_staged;
		dc->sdp_provisional = channel_state->sdp_provisional;
		dc->sdp_remove_pending =
			channel_state->sdp_remove_pending;
	}
}


bool local_mid_used(const struct sdp_session *sdp,
			   const struct list *streaml, uint32_t mid)
{
	const struct le *le;
	char value[16];

	re_snprintf(value, sizeof(value), "%u", mid);
	for (le = list_head(streaml); le; le = le->next) {
		const struct stream *stream = le->data;

		if (!str_cmp(stream_mid(stream), value))
			return true;
	}
	for (le = list_head(sdp_session_medial(sdp, true));
	     le; le = le->next) {
		const struct sdp_media *media = le->data;

		if (!str_cmp(sdp_media_lattr_apply(media, "mid", NULL, NULL),
			     value))
			return true;
	}
	return false;
}


static enum data_channel_state public_state(enum dc_state state)
{
	switch (state) {
	case DC_STATE_CONNECTING: return DATACHANNEL_CONNECTING;
	case DC_STATE_OPEN:       return DATACHANNEL_OPEN;
	case DC_STATE_CLOSING:    return DATACHANNEL_CLOSING;
	case DC_STATE_CLOSED:     return DATACHANNEL_CLOSED;
	default:                  return DATACHANNEL_CLOSED;
	}
}

static struct data_channel *channel_lookup(struct data_context *ctx,
					   const struct dc_channel *channel)
{
	struct le *le;

	for (le = ctx->channels.head; le; le = le->next) {
		struct data_channel *dc = le->data;

		if (dc->dc == channel || dc->pending_dc == channel)
			return dc;
	}

	return NULL;
}

struct data_channel *channel_lookup_id(struct data_context *ctx,
					      uint16_t id)
{
	struct le *le;

	for (le = ctx->channels.head; le; le = le->next) {
		struct data_channel *dc = le->data;

		if (dc->config.negotiated && dc->config.id == id &&
		    dc->state != DATACHANNEL_CLOSED)
			return dc;
		if (!dc->config.negotiated && dc->id == id &&
		    dc->state != DATACHANNEL_CLOSED)
			return dc;
	}

	return NULL;
}

void channel_destructor(void *arg)
{
	struct data_channel *dc = arg;

	list_unlink(&dc->le);
	pending_messages_flush(dc);
	mem_deref(dc->dc);
	mem_deref(dc->pending_dc);
	mem_deref(dc->label);
	mem_deref(dc->protocol);
}

void context_destructor(void *arg)
{
	struct data_context *ctx = arg;
	struct le *le;

	ctx->closing = true;
	ctx->errorh = NULL;
	ctx->association_queued = mem_deref(ctx->association_queued);
	ctx->operation_state = mem_deref(ctx->operation_state);
	ctx->pending_state = mem_deref(ctx->pending_state);
	for (le = ctx->transport_bindings.head; le; le = le->next) {
		struct transport_binding *binding = le->data;

		binding->ctx = NULL;
	}
	while (ctx->channels.head) {
		struct data_channel *dc = ctx->channels.head->data;

		list_unlink(&dc->le);
		if (dc->dc)
			(void)dc_channel_close(dc->dc);
		dc->dc = mem_deref(dc->dc);
		pending_messages_flush(dc);
		dc->ctx = NULL;
		dc->state = DATACHANNEL_CLOSED;
		mem_deref(dc);
	}

	ctx->transport = mem_deref(ctx->transport);
	ctx->bundle_transport = mem_deref(ctx->bundle_transport);
	ctx->associationq = mem_deref(ctx->associationq);
	ctx->active_group = mem_deref((void *)ctx->active_group);
	ctx->remote_bundles = mem_deref(ctx->remote_bundles);
	ctx->bundle_base = mem_deref(ctx->bundle_base);
	ctx->mnats = mem_deref(ctx->mnats);
	ctx->mnat_session = mem_deref(ctx->mnat_session);
	ctx->mencs = mem_deref(ctx->mencs);
	ctx->sock = mem_deref(ctx->sock);
	ctx->sdpm = mem_deref(ctx->sdpm);
	ctx->transport_sdpm = mem_deref(ctx->transport_sdpm);
	ctx->sdp = mem_deref(ctx->sdp);
	ctx->mid = mem_deref(ctx->mid);
	ctx->remote_identity = mem_deref(ctx->remote_identity);
	ctx->remote_ice_identity = mem_deref(ctx->remote_ice_identity);
	ctx->local_ice_identity = mem_deref(ctx->local_ice_identity);
	list_flush(&ctx->transport_bindings);
}


static void transport_binding_destructor(void *arg)
{
	struct transport_binding *binding = arg;
	struct data_context *ctx = binding->ctx;
	bool terminal = !ctx || ctx->closing || !binding->pending;

	if (binding->members_published) {
		struct le *le;

		for (le = binding->member_mappings.head; le;
		     le = le->next) {
			struct transport_member_mapping *mapping = le->data;

			stream_publish_menc_transport(
				mapping->stream, mapping->old_transport);
		}
		binding->members_published = false;
	}
	if (binding->menc && binding->members_activated)
		menc_transport_members_rollback(binding->menc,
					       binding->transport);
	else if (binding->menc && binding->members_prepared)
		menc_transport_members_abort(binding->menc,
					    binding->transport);
	list_flush(&binding->member_mappings);
	if (binding->mnats && binding->gather_waiting && binding->mnat &&
	    binding->mnat->mediagathercancelh) {
		binding->mnat->mediagathercancelh(binding->mnats);
		binding->gather_waiting = false;
	}
	if (binding->mnats && binding->mnat_prepared && binding->mnat &&
	    binding->mnat->mediaaborth)
		binding->mnat->mediaaborth(binding->mnats);
	else if (binding->mnats && binding->mnat_activated && binding->mnat &&
		 binding->mnat->mediarollbackh)
		binding->mnat->mediarollbackh(binding->mnats);

	if (binding->ctx &&
	    binding->ctx->pending_binding == binding)
		binding->ctx->pending_binding = NULL;
	list_unlink(&binding->le);
	if (binding->transport && binding->ctx)
		menc_transport_detach(binding->ctx->menc,
				      binding->transport);
	if (binding->session_owned) {
		media_transport_detach_consumer(binding->media_transport);
		if (terminal)
			media_transport_retire_callbacks(
				binding->media_transport);
	}
	if (binding->dc_token)
		binding->dc_token->role = DC_CALLBACK_RETIRING;
	if (binding->candidate_token)
		binding->candidate_token->role = DC_CALLBACK_RETIRING;
	mem_deref(binding->dc);
	mem_deref(binding->candidate_dc);
	mem_deref(binding->dc_token);
	mem_deref(binding->candidate_token);
	binding->media_transport = mem_deref(binding->media_transport);
	binding->transport = mem_deref(binding->transport);
	mem_deref(binding->mnats);
	mem_deref(binding->shadow_mencs);
	mem_deref(binding->sock);
	mem_deref(binding->transport_sdpm);
	mem_deref(binding->shadow_sdpm);
	mem_deref(binding->shadow_sdp);
	mem_deref(binding->saved_attrs);
	mem_deref(binding->context_saved_attrs);
	mem_deref(binding->proposed_attrs);
	mem_deref((void *)binding->group);
}


static struct dc_callback_token *dc_callback_token_alloc(
	struct transport_binding *binding, enum dc_callback_role role)
{
	struct dc_callback_token *token;

	token = mem_zalloc(sizeof(*token), NULL);
	if (!token)
		return NULL;
	token->binding = binding;
	token->generation = binding->generation;
	token->role = role;
	return token;
}


static bool dc_callback_token_is_live(const struct dc_callback_token *token)
{
	const struct transport_binding *binding = token ? token->binding : NULL;
	const struct data_context *ctx = binding ? binding->ctx : NULL;

	if (!ctx || ctx->closing || token->generation != binding->generation)
		return false;
	if (token->role == DC_CALLBACK_CANDIDATE)
		return (binding->candidate_token == token &&
			binding->candidate_dc == token->transport) ||
			(ctx->pending_binding == binding &&
			 binding->dc_token == token &&
			 binding->dc == token->transport);
	if (token->role == DC_CALLBACK_ACTIVE)
		return binding->dc_token == token &&
			binding->dc == token->transport &&
			binding->generation == ctx->transport_generation;
	return false;
}


static void transport_member_mapping_destructor(void *arg)
{
	struct transport_member_mapping *mapping = arg;

	list_unlink(&mapping->le);
	mem_deref(mapping->stream);
	mem_deref(mapping->old_transport);
	mem_deref(mapping->desired_transport);
}


struct transport_binding *transport_binding_alloc(
	struct data_context *ctx, uint64_t generation)
{
	struct transport_binding *binding;

	binding = mem_zalloc(sizeof(*binding),
			     transport_binding_destructor);
	if (!binding)
		return NULL;
	binding->ctx = ctx;
	binding->menc = ctx->menc;
	binding->mnat = ctx->mnat;
	binding->generation = generation;
	list_append(&ctx->transport_bindings, &binding->le, binding);
	return binding;
}


struct transport_binding *transport_binding_lookup(
	const struct data_context *ctx, uint64_t generation)
{
	struct le *le;

	for (le = ctx->transport_bindings.head; le; le = le->next) {
		struct transport_binding *binding = le->data;

		if (binding->generation == generation)
			return binding;
	}
	return NULL;
}


struct transport_binding *active_transport_binding(
	const struct data_context *ctx)
{
	return ctx ? transport_binding_lookup(
			     ctx, ctx->transport_generation)
		   : NULL;
}


bool transport_binding_is_live(const struct transport_binding *binding)
{
	const struct data_context *ctx = binding ? binding->ctx : NULL;

	return ctx && !ctx->closing &&
		(binding->generation == ctx->transport_generation ||
		 ctx->pending_binding == binding);
}


struct menc_transport *transport_binding_transport(
	const struct transport_binding *binding)
{
	const struct data_context *ctx = binding ? binding->ctx : NULL;

	if (!ctx)
		return NULL;
	return ctx->pending_binding == binding
		       ? binding->transport
		       : binding->generation == ctx->transport_generation
				 ? ctx->transport
				 : NULL;
}


static bool transport_binding_staging_sctp(
	const struct transport_binding *binding)
{
	const struct data_context *ctx = binding ? binding->ctx : NULL;

	return ctx && (ctx->pending_binding == binding ||
		       binding->candidate_dc);
}


static struct dc_transport *transport_binding_dc(
	const struct transport_binding *binding)
{
	if (!binding)
		return NULL;
	return binding->candidate_dc
		       ? binding->candidate_dc : binding->dc;
}


void transport_binding_restore_sdp(struct transport_binding *binding)
{
	if (!binding || !binding->sdp_staged ||
	    !binding->transport_sdpm)
		return;

	sdp_media_set_laddr(binding->transport_sdpm,
			    &binding->saved_laddr);
	sdp_media_set_lport(binding->transport_sdpm,
			    sa_port(&binding->saved_laddr));
	sdp_media_restore_lattrs(binding->transport_sdpm,
				 binding->saved_attrs);
	binding->saved_attrs = NULL;
	if (binding->context_saved_attrs) {
		sdp_media_set_laddr(binding->ctx->sdpm,
				    &binding->context_saved_laddr);
		sdp_media_set_lport(
			binding->ctx->sdpm,
			sa_port(&binding->context_saved_laddr));
		sdp_media_restore_lattrs(
			binding->ctx->sdpm,
			binding->context_saved_attrs);
		binding->context_saved_attrs = NULL;
	}
	binding->sdp_staged = false;
}


int transport_binding_capture_sdp(struct transport_binding *binding)
{
	struct sdp_media *source;
	struct sdp_media_lattr_state *attrs = NULL;
	int err;

	if (!binding || !binding->transport_sdpm)
		return EINVAL;
	source = binding->shadow_sdpm
			 ? binding->shadow_sdpm : binding->transport_sdpm;
	err = sdp_media_save_lattrs(&attrs, source);
	if (err)
		return err;

	binding->proposed_attrs = mem_deref(binding->proposed_attrs);
	binding->proposed_attrs = attrs;
	sa_cpy(&binding->proposed_laddr,
	       sdp_media_laddr(source));
	return 0;
}


int transport_binding_apply_sdp(struct transport_binding *binding)
{
	int err;

	if (!binding || !binding->transport_sdpm ||
	    !binding->proposed_attrs)
		return EINVAL;
	if (binding->shadow_sdpm && !binding->sdp_staged) {
		err = sdp_media_save_lattrs(&binding->saved_attrs,
					    binding->transport_sdpm);
		if (err)
			return err;
		sa_cpy(&binding->saved_laddr,
		       sdp_media_laddr(binding->transport_sdpm));
		binding->sdp_staged = true;
	}
	err = sdp_media_apply_lattrs(binding->transport_sdpm,
				     binding->proposed_attrs);
	if (err)
		return err;
	sdp_media_set_laddr(binding->transport_sdpm,
			    &binding->proposed_laddr);
	sdp_media_set_lport(binding->transport_sdpm,
			    sa_port(&binding->proposed_laddr));
	return 0;
}


static void transport_binding_cancel_mnat(struct transport_binding *binding)
{
	if (!binding || !binding->mnats || !binding->mnat)
		return;
	if (binding->gather_waiting && binding->mnat->mediagathercancelh) {
		binding->mnat->mediagathercancelh(binding->mnats);
		binding->gather_waiting = false;
	}
	if (binding->mnat_prepared && binding->mnat->mediaaborth) {
		binding->mnat->mediaaborth(binding->mnats);
		binding->mnat_prepared = false;
	}
	else if (binding->mnat_activated && binding->mnat->mediarollbackh) {
		binding->mnat->mediarollbackh(binding->mnats);
		binding->mnat_activated = false;
	}
}


void transport_binding_abort(struct transport_binding *binding,
				    bool restore_sdp)
{
	struct data_context *ctx;

	if (!binding)
		return;
	ctx = binding->ctx;
	if (ctx && ctx->pending_binding == binding) {
		ctx->pending_binding = NULL;
		abort_pending_channels(ctx, binding->generation);
		if (binding->dc_token)
			binding->dc_token->role = DC_CALLBACK_RETIRING;
		binding->dc = mem_deref(binding->dc);
		binding->dc_token = mem_deref(binding->dc_token);
		menc_transport_detach(ctx->menc, binding->transport);
		binding->transport = mem_deref(binding->transport);
		transport_binding_cancel_mnat(binding);
		binding->mnats = mem_deref(binding->mnats);
		if (ctx->bundle_transport)
			bundle_transport_abort(
				ctx->bundle_transport,
				binding->route_generation);
	}
	if (restore_sdp)
		transport_binding_restore_sdp(binding);
	mem_deref(binding);
}


void transport_bindings_reap(struct data_context *ctx)
{
	struct le *le;
	struct le *next;

	for (le = ctx->transport_bindings.head; le; le = next) {
		struct transport_binding *binding = le->data;

		next = le->next;
		if (binding == ctx->pending_binding ||
		    binding->generation == ctx->transport_generation)
			continue;
		mem_deref(binding);
	}
}


bool bundle_groups_equal(const struct bundle_group *a,
				const struct bundle_group *b)
{
	size_t count;

	if (a == b)
		return true;
	if (!a || !b)
		return false;
	count = bundle_group_count(a);
	if (count != bundle_group_count(b))
		return false;

	for (size_t i = 0; i < count; ++i) {
		if (str_cmp(bundle_group_mid(a, i),
			    bundle_group_mid(b, i)))
			return false;
	}
	return true;
}


int ensure_bundle_transport(struct data_context *ctx,
				   const struct bundle_group *group)
{
	struct transport_binding *binding;
	struct bundle_transport *transport = NULL;
	struct udp_sock *sock;
	bool topology_changed;
	int err;

	if (!group && !ctx->bundle_transport)
		return 0;
	if (!ctx->bundle_transport) {
		err = bundle_transport_alloc(&transport, group, ctx->streaml,
					     ctx->mid);
		if (err)
			return err;
		ctx->bundle_transport = transport;
	}

	binding = ctx->pending_binding
			? ctx->pending_binding
			: transport_binding_lookup(
				  ctx, ctx->transport_generation);
	if (!binding)
		return ENOENT;
	sock = binding == ctx->pending_binding
		     ? binding->sock : ctx->sock;
	topology_changed = !bundle_groups_equal(binding->group, group);

	if (!binding->route_generation || topology_changed) {
		err = group
			      ? bundle_transport_prepare(
				      ctx->bundle_transport, group, sock,
				      &binding->route_generation)
			      : bundle_transport_prepare_legacy(
				      ctx->bundle_transport,
				      &binding->route_generation);
		if (err)
			return err;
	}

	if (!bundle_groups_equal(binding->group, group)) {
		binding->group = mem_deref((void *)binding->group);
		binding->group = mem_ref((void *)group);
	}
	return 0;
}

static int packet_handler(struct mbuf *packet, void *arg)
{
	struct dc_callback_token *token = arg;
	struct transport_binding *binding = token->binding;
	struct data_context *ctx = binding->ctx;
	struct menc_transport *transport;

	if (!dc_callback_token_is_live(token))
		return ECANCELED;
	if (binding->session_owned)
		return media_transport_send(binding->media_transport, packet);
	transport = transport_binding_transport(binding);
	if (!transport)
		return ECANCELED;
	return menc_transport_send(ctx->menc, transport, packet);
}


void attach_transport_members(struct data_context *ctx,
				     struct menc_transport *transport);
static int commit_pending_transport(struct transport_binding *binding);


static void deferred_message_failure(struct dc_callback_token *token,
				     struct dc_channel *channel, int err)
{
	struct transport_binding *binding = token->binding;
	struct data_context *ctx = binding->ctx;
	struct data_channel *dc = channel_lookup(ctx, channel);

	mem_ref(binding);
	mem_ref(ctx);
	if (ctx->pending_binding == binding) {
		transport_binding_abort(binding, true);
	}
	else if (dc && channel == dc->pending_dc) {
		if (token->transport)
			(void)dc_transport_close(token->transport, err);
	}
	else {
		struct le *le;

		/*
		 * The component has already accepted this peer message, so its
		 * void receive callback cannot apply upstream backpressure.  Make
		 * exhaustion an explicit terminal association error.  Publication
		 * remains deferred until the SDP transaction becomes stable.
		 */
		data_context_report_error(ctx, err);
		/* dc_transport_close() is allowed to defer its per-channel close
		 * callbacks.  Record the terminal public state now so stable
		 * publication cannot expose the context error without first exposing
		 * CLOSED on every channel in the failed association. */
		for (le = ctx->channels.head; le; le = le->next) {
			struct data_channel *candidate = le->data;
			if ((!candidate->dc && candidate->pending_dc) ||
			    candidate->state == DATACHANNEL_CLOSED)
				continue;
			candidate->state = DATACHANNEL_CLOSED;
			candidate->dispatch_state = true;
			candidate->dispatch_state_err = err;
			candidate->dispatch_state_value = DATACHANNEL_CLOSED;
		}
		if (token->transport)
			(void)dc_transport_close(token->transport, err);
	}
	mem_deref(ctx);
	mem_deref(binding);
}

static void message_handler(struct dc_channel *channel,
			    enum dc_message_type type,
			    const uint8_t *buf, size_t len, void *arg)
{
	struct dc_callback_token *token = arg;
	struct transport_binding *binding = token->binding;
	struct data_context *ctx = binding->ctx;
	struct data_channel *dc;

	if (!dc_callback_token_is_live(token))
		return;
	dc = channel_lookup(ctx, channel);
	if (dc && (channel == dc->pending_dc || ctx->callbacks_deferred)) {
		struct pending_message *message;
		size_t charge;

		if (len > SIZE_MAX - sizeof(*message)) {
			deferred_message_failure(token, channel, ENOBUFS);
			return;
		}
		charge = sizeof(*message) + len;
		if (ctx->deferred_message_bytes >
				DATACHANNEL_DEFERRED_RECV_BUDGET ||
		    charge > DATACHANNEL_DEFERRED_RECV_BUDGET -
				     ctx->deferred_message_bytes) {
			deferred_message_failure(token, channel, ENOBUFS);
			return;
		}
		message = mem_zalloc(charge, NULL);
		if (!message) {
			deferred_message_failure(token, channel, ENOMEM);
			return;
		}
		message->type = type;
		message->len = len;
		message->charge = charge;
		if (len)
			memcpy(message->data, buf, len);
		list_append(&dc->pending_messages, &message->le,
			    message);
		ctx->deferred_message_bytes += charge;
		return;
	}
	if (dc && dc->messageh) {
		mem_ref(ctx);
		mem_ref(dc);
		dc->messageh(dc, type == DC_MESSAGE_TEXT
				    ? DATACHANNEL_MESSAGE_TEXT
				    : DATACHANNEL_MESSAGE_BINARY,
			     buf, len, dc->arg);
		mem_deref(dc);
		mem_deref(ctx);
	}
}

static void state_handler(struct dc_channel *channel, enum dc_state state,
			  int err, void *arg)
{
	struct dc_callback_token *token = arg;
	struct transport_binding *binding = token->binding;
	struct data_context *ctx = binding->ctx;
	struct data_channel *dc;
	bool closed;

	if (!dc_callback_token_is_live(token))
		return;
	dc = channel_lookup(ctx, channel);
	if (!dc)
		return;
	if (channel == dc->pending_dc) {
		dc->pending_id = dc_channel_id(channel);
		dc->pending_state = public_state(state);
		if (dc->pending_state == DATACHANNEL_CLOSED)
			dc->pending_dc = mem_deref(dc->pending_dc);
		return;
	}

	mem_ref(ctx);
	mem_ref(dc);
	if (dc->id < 0)
		dc->id = dc_channel_id(channel);
	dc->state = public_state(state);
	closed = dc->state == DATACHANNEL_CLOSED;
	if (ctx->callbacks_deferred) {
		dc->dispatch_state = true;
		dc->dispatch_state_err = err;
		dc->dispatch_state_value = dc->state;
	}
	else if (dc->stateh)
		dc->stateh(dc, dc->state, err, dc->arg);
	if (closed)
		dc->dc = mem_deref(dc->dc);
	mem_deref(dc);
	mem_deref(ctx);
}

static void buffered_low_handler(struct dc_channel *channel, void *arg)
{
	struct dc_callback_token *token = arg;
	struct transport_binding *binding = token->binding;
	struct data_context *ctx = binding->ctx;
	struct data_channel *dc;

	if (!dc_callback_token_is_live(token))
		return;
	dc = channel_lookup(ctx, channel);
	if (dc && channel == dc->pending_dc)
		return;
	if (dc && ctx->callbacks_deferred) {
		dc->dispatch_buffered_low = true;
	}
	else if (dc && dc->buffered_lowh) {
		mem_ref(ctx);
		mem_ref(dc);
		dc->buffered_lowh(dc, dc->arg);
		mem_deref(dc);
		mem_deref(ctx);
	}
}

static int incoming_channel_alloc(struct data_channel **dcp,
				  struct data_context *ctx,
				  struct dc_channel *channel,
				  struct transport_binding *binding)
{
	struct data_channel *dc;
	int err;

	dc = mem_zalloc(sizeof(*dc), channel_destructor);
	if (!dc)
		return ENOMEM;

	dc->ctx = ctx;
	if (transport_binding_staging_sctp(binding)) {
		dc->pending_dc = mem_ref(channel);
		dc->pending_generation = binding->generation;
		dc->pending_id = dc_channel_id(channel);
		dc->pending_state =
			public_state(dc_channel_state(channel));
		dc->pending_incoming = true;
		dc->id = -1;
		dc->state = DATACHANNEL_CONNECTING;
	}
	else {
		dc->dc = mem_ref(channel);
		dc->id = dc_channel_id(channel);
		dc->state = public_state(dc_channel_state(channel));
	}
	err = str_dup(&dc->label, dc_channel_label(channel));
	if (!err)
		err = str_dup(&dc->protocol, dc_channel_protocol(channel));
	if (err) {
		mem_deref(dc);
		return err;
	}

	list_append(&ctx->channels, &dc->le, dc);
	*dcp = dc;
	return 0;
}

bool notify_incoming_channel(struct data_context *ctx,
				    struct data_channel *dc)
{
	if (ctx->channelh) {
		uint64_t generation = ctx->dispatch_generation;
		uint32_t refs = dispatch_owner_refs(ctx);

		mem_ref(ctx);
		mem_ref(dc);
		ctx->channelh(dc, ctx->channel_arg);
		if (!dispatch_callback_complete(ctx, refs, generation)) {
			mem_deref(dc);
			mem_deref(ctx);
			return false;
		}

		if (!dc->messageh && !dc->stateh &&
		    !dc->buffered_lowh)
			(void)datachannel_close(dc);
		mem_deref(dc);
		mem_deref(ctx);
	}
	else {
		(void)datachannel_close(dc);
	}

	return true;
}

static void incoming_channel_handler(struct dc_channel *channel, void *arg)
{
	struct dc_callback_token *token = arg;
	struct transport_binding *binding = token->binding;
	struct data_context *ctx = binding->ctx;
	struct data_channel *dc;
	int err;

	if (!dc_callback_token_is_live(token))
		return;
	mem_ref(binding);
	mem_ref(ctx);

	err = incoming_channel_alloc(&dc, ctx, channel, binding);
	if (err) {
		(void)dc_channel_close(channel);
		if (ctx->pending_binding == binding)
			transport_binding_abort(binding, true);
		else if (token->role == DC_CALLBACK_CANDIDATE &&
			 token->transport)
			(void)dc_transport_close(token->transport, err);
		else
			data_context_report_error(ctx, err);
		goto out;
	}

	if (!transport_binding_staging_sctp(binding) &&
	    !ctx->callbacks_deferred)
		notify_incoming_channel(ctx, dc);
	else if (!transport_binding_staging_sctp(binding))
		dc->dispatch_incoming = true;
out:
	mem_deref(ctx);
	mem_deref(binding);
}

int channel_bind(struct data_channel *dc,
			struct transport_binding *binding)
{
	struct dc_channel **target;
	struct dc_channel_config config = {
		.protocol = dc->protocol,
		.priority = dc->priority,
		.reliability = DC_RELIABLE,
		.id = dc->config.negotiated ? dc->config.id : -1,
		.ordered = dc->config.ordered,
		.negotiated = dc->config.negotiated,
	};
	int err;

	if (!binding || !transport_binding_dc(binding))
		return EAGAIN;
	target = transport_binding_staging_sctp(binding)
		       ? &dc->pending_dc : &dc->dc;
	if (*target)
		return 0;

	if (dc->config.max_retransmits >= 0) {
		config.reliability = DC_RELIABILITY_RETRANSMITS;
		config.reliability_value =
			(uint32_t)dc->config.max_retransmits;
	}
	else if (dc->config.max_packet_lifetime >= 0) {
		config.reliability = DC_RELIABILITY_LIFETIME;
		config.reliability_value =
			(uint32_t)dc->config.max_packet_lifetime;
	}

	err = dc_channel_create(target, transport_binding_dc(binding),
				dc->label, &config);

	if (!err && target == &dc->pending_dc) {
		dc->pending_generation = binding->generation;
		dc->pending_id = dc_channel_id(dc->pending_dc);
		dc->pending_state =
			public_state(dc_channel_state(dc->pending_dc));
	}
	else if (!err && dc->id < 0)
		dc->id = dc_channel_id(dc->dc);
	return err;
}

static int bind_pending_channels(struct transport_binding *binding)
{
	struct data_context *ctx = binding->ctx;
	struct le *le;
	int err;

	for (le = ctx->channels.head; le; le = le->next) {
		struct data_channel *dc = le->data;

		if (dc->state == DATACHANNEL_CLOSED)
			continue;
		err = channel_bind(dc, binding);
		if (err)
			return err;
	}

	return 0;
}


void abort_pending_channels(struct data_context *ctx,
				   uint64_t generation)
{
	struct le *le;
	struct le *next;

	for (le = ctx->channels.head; le; le = next) {
		struct data_channel *dc = le->data;

		next = le->next;
		if (dc->pending_generation != generation)
			continue;
		if (dc->pending_dc)
			(void)dc_channel_close(dc->pending_dc);
		dc->pending_dc = mem_deref(dc->pending_dc);
		pending_messages_flush(dc);
		dc->pending_generation = 0;
		dc->pending_id = -1;
		dc->pending_state = DATACHANNEL_CLOSED;
		if (dc->pending_incoming && !dc->dc) {
			dc->pending_incoming = false;
			mem_deref(dc);
		}
		else
			dc->pending_incoming = false;
	}
}


static bool deliver_pending_messages(struct data_channel *dc)
{
	while (dc->ctx && dc->state != DATACHANNEL_CLOSING &&
	       dc->state != DATACHANNEL_CLOSED && dc->pending_messages.head) {
		struct pending_message *message =
			dc->pending_messages.head->data;
		struct data_context *ctx = dc->ctx;
		uint64_t generation = ctx->dispatch_generation;

		list_unlink(&message->le);
		if (dc->ctx) {
			if (dc->ctx->deferred_message_bytes >= message->charge)
				dc->ctx->deferred_message_bytes -= message->charge;
			else
				dc->ctx->deferred_message_bytes = 0;
		}
		if (dc->messageh) {
			uint32_t refs = dispatch_owner_refs(ctx);

			dc->messageh(
				dc,
				message->type == DC_MESSAGE_TEXT
					? DATACHANNEL_MESSAGE_TEXT
					: DATACHANNEL_MESSAGE_BINARY,
				message->data, message->len, dc->arg);
			if (!dispatch_callback_complete(
				    ctx, refs, generation)) {
				mem_deref(message);
				return false;
			}
		}
		mem_deref(message);
	}
	if (!dc->ctx || dc->state == DATACHANNEL_CLOSING ||
	    dc->state == DATACHANNEL_CLOSED)
		pending_messages_flush(dc);

	return true;
}


void promote_pending_channels(struct transport_binding *binding)
{
	struct data_context *ctx = binding->ctx;

	mem_ref(ctx);
	while (!ctx->closing) {
		struct data_channel *dc = NULL;
		struct dc_channel *old;
		enum data_channel_state old_state;
		struct le *le;

		for (le = ctx->channels.head; le; le = le->next) {
			struct data_channel *candidate = le->data;

			if (candidate->pending_generation ==
			    binding->generation) {
				dc = mem_ref(candidate);
				break;
			}
		}
		if (!dc)
			break;
		old = dc->dc;
		old_state = dc->state;
		dc->dc = dc->pending_dc;
		dc->pending_dc = NULL;
		dc->id = dc->pending_id;
		dc->state = dc->pending_state;
		dc->pending_generation = 0;
		dc->dispatch_incoming |= dc->pending_incoming;
		if (!old || old_state != dc->state) {
			dc->dispatch_state = true;
			dc->dispatch_state_err = 0;
			dc->dispatch_state_value = dc->state;
		}
		dc->pending_incoming = false;
		if (old)
			(void)dc_channel_close(old);
		mem_deref(old);
		mem_deref(dc);
	}
	mem_deref(ctx);
}


bool dispatch_promoted_channels(struct data_context *ctx)
{
	struct data_channel *dc = NULL;
	bool live = true;

	mem_ref(ctx);
	while (!ctx->closing) {
		struct le *le;

		dc = NULL;

		for (le = ctx->channels.head; le; le = le->next) {
			struct data_channel *candidate = le->data;

			if (candidate->dispatch_incoming ||
			    candidate->dispatch_state ||
			    candidate->dispatch_buffered_low ||
			    candidate->pending_messages.head) {
				dc = mem_ref(candidate);
				break;
			}
		}
		if (!dc)
			break;
		if (dc->dispatch_incoming) {
			dc->dispatch_incoming = false;
			live = notify_incoming_channel(ctx, dc);
			if (!live)
				goto callback_cancelled;
		}
		if (dc->ctx && dc->dispatch_state) {
			int state_err = dc->dispatch_state_err;
			enum data_channel_state state =
				dc->dispatch_state_value;
			uint64_t generation = ctx->dispatch_generation;

			dc->dispatch_state = false;
			dc->dispatch_state_err = 0;
			if (dc->state == state && dc->stateh) {
				uint32_t refs = dispatch_owner_refs(ctx);

				dc->stateh(dc, state, state_err, dc->arg);
				live = dispatch_callback_complete(
					ctx, refs, generation);
				if (!live)
					goto callback_cancelled;
			}
		}
		if (dc->ctx && dc->dispatch_buffered_low) {
			uint64_t generation = ctx->dispatch_generation;

			dc->dispatch_buffered_low = false;
			if (dc->buffered_lowh) {
				uint32_t refs = dispatch_owner_refs(ctx);

				dc->buffered_lowh(dc, dc->arg);
				live = dispatch_callback_complete(
					ctx, refs, generation);
				if (!live)
					goto callback_cancelled;
			}
		}
		if (dc->ctx)
			live = deliver_pending_messages(dc);
		if (!live)
			goto callback_cancelled;
		mem_deref(dc);
	}
	mem_deref(ctx);
	return true;

callback_cancelled:
	mem_deref(dc);
	mem_deref(ctx);
	return false;
}


static void transport_association_ready_handler(int err, void *arg)
{
	struct transport_binding *binding = arg;
	struct data_context *ctx = binding ? binding->ctx : NULL;

	(void)err;
	if (ctx && transport_binding_is_live(binding) &&
	    ctx->pending_binding == binding && ctx->transport_readyh)
		ctx->transport_readyh(ctx->transport_ready_arg);
	mem_deref(binding);
}


void transport_association_ready_queue_handler(int id, void *data,
					      void *arg)
{
	struct data_context *ctx = arg;
	struct transport_binding *binding;

	(void)id;
	(void)data;
	binding = ctx->association_queued;
	ctx->association_queued = NULL;
	if (binding)
		transport_association_ready_handler(0, binding);
}


void transport_recv_handler(struct mbuf *mb, void *arg)
{
	struct transport_binding *binding = arg;
	struct data_context *ctx = binding->ctx;
	struct dc_transport *dc;
	bool candidate;
	bool pending;
	int err;

	dc = transport_binding_dc(binding);
	if (!transport_binding_is_live(binding) || !dc)
		return;

	mem_ref(binding);
	mem_ref(ctx);
	candidate = binding->candidate_dc == dc;
	pending = ctx->pending_binding == binding;
	err = dc_transport_input(dc, mb);
	if (!err && pending && binding->session_owned &&
	    !binding->association_notified &&
	    !ctx->association_queued &&
	    dc_transport_association_ready(dc)) {
		binding->association_notified = true;
		ctx->association_queued = mem_ref(binding);
		if (mqueue_push(ctx->associationq, 0, NULL)) {
			ctx->association_queued =
				mem_deref(ctx->association_queued);
			binding->association_notified = false;
		}
	}
	if (err && candidate) {
		if (binding->candidate_dc == dc)
			(void)dc_transport_close(dc, err);
	}
	else if (err && pending && ctx->pending_binding == binding)
		transport_binding_abort(binding, true);
	else if (err)
		data_context_report_error(ctx, err);
	mem_deref(ctx);
	mem_deref(binding);
}

static void sctp_close_handler(int err, void *arg)
{
	struct dc_callback_token *token = arg;
	struct transport_binding *binding = token->binding;
	struct data_context *ctx = binding->ctx;

	if (!dc_callback_token_is_live(token))
		return;
	if (token->suppress_transport_error)
		return;

	mem_ref(binding);
	mem_ref(ctx);
	if (token->role == DC_CALLBACK_CANDIDATE) {
		struct dc_transport *candidate = binding->candidate_dc;

		abort_pending_channels(ctx, binding->generation);
		if (binding->candidate_token == token) {
			binding->candidate_dc = NULL;
			binding->candidate_token = NULL;
			mem_deref(candidate);
			mem_deref(token);
		}
		else if (binding->dc_token == token &&
			 ctx->pending_binding == binding) {
			transport_binding_abort(binding, true);
		}
		goto out;
	}
	if (ctx->pending_binding == binding) {
		transport_binding_abort(binding, true);
		goto out;
	}
	if (binding->generation != ctx->transport_generation)
		goto out;
	data_context_report_error(ctx, err ? err : ECONNRESET);
out:
	mem_deref(ctx);
	mem_deref(binding);
}

int try_start_sctp(struct transport_binding *binding)
{
	struct data_context *ctx = binding ? binding->ctx : NULL;
	struct dc_transport **dcp;
	struct dc_callback_token **tokenp;
	struct dc_callback_token *token;
	enum menc_dtls_role role;
	bool dtls_ready;
	bool stage_active;
	struct dc_transport_config config = {
		.local_port = DATACHANNEL_SCTP_PORT,
		.remote_port = ctx->remote_port,
		.outbound_streams = DATACHANNEL_STREAMS,
		.inbound_streams = DATACHANNEL_STREAMS,
		.receive_limit = DATACHANNEL_MESSAGE_LIMIT,
		.send_limit = ctx->send_limit,
		.send_high_water = DATACHANNEL_SEND_BUDGET,
		.send_low_water = DATACHANNEL_SEND_BUDGET / 4,
		/*
		 * SCTP's heartbeat, RTO and retry limits yield a conservative
		 * autonomous failure budget below 30 seconds.
		 */
		.heartbeat_interval_ms = 2000,
		.rto_max_ms = 2000,
		.max_retransmissions = 5,
	};
	int err;

	if (!ctx || !transport_binding_is_live(binding))
		return EINVAL;
	/* The session coordinator must be able to construct and handshake its
	 * pending SCTP consumer before publishing the description.  Data-channel
	 * callbacks remain deferred by their individual dispatch paths, so this
	 * does not cross the application callback barrier. */
	if (ctx->callbacks_deferred &&
	    !(ctx->pending_binding == binding && binding->session_owned))
		return 0;
	dtls_ready = ctx->pending_binding == binding
			     ? binding->dtls_established : ctx->dtls_ready;
	role = ctx->pending_binding == binding
		       ? binding->role : ctx->dtls_role;
	stage_active = ctx->pending_binding != binding &&
		ctx->description_applying && !binding->dc;
	dcp = stage_active ? &binding->candidate_dc : &binding->dc;
	tokenp = stage_active ? &binding->candidate_token
			      : &binding->dc_token;
	if (!dtls_ready || !ctx->remote_accepted ||
	    !ctx->local_committed || *dcp)
		return 0;

	token = dc_callback_token_alloc(
		binding, stage_active || ctx->pending_binding == binding
			 ? DC_CALLBACK_CANDIDATE : DC_CALLBACK_ACTIVE);
	if (!token)
		return ENOMEM;
	*tokenp = token;
	err = dc_transport_alloc(dcp, &config, packet_handler,
				 incoming_channel_handler, message_handler,
				 state_handler, buffered_low_handler,
				 sctp_close_handler, token);
	if (err) {
		*tokenp = NULL;
		mem_deref(token);
		return err;
	}
	token->transport = *dcp;
	/* dc_transport_start() and channel creation may synchronously close the
	 * exact transport and consume the binding's token ownership. */
	mem_ref(token);

	err = bind_pending_channels(binding);
	if (err)
		goto out;

	err = dc_transport_start(*dcp,
				 role == MENC_DTLS_ROLE_CLIENT
					 ? DC_ROLE_CLIENT : DC_ROLE_SERVER);
out:
	if (err && stage_active) {
		struct dc_transport *candidate =
			*tokenp == token ? *dcp : NULL;

		abort_pending_channels(ctx, binding->generation);
		token->role = DC_CALLBACK_RETIRING;
		if (candidate)
			(void)dc_transport_close(candidate, err);
		if (*tokenp == token) {
			*dcp = NULL;
			*tokenp = NULL;
			mem_deref(candidate);
			mem_deref(token);
		}
	}
	mem_deref(token);
	return err;
}


int transport_binding_maybe_ready(struct transport_binding *binding)
{
	struct data_context *ctx = binding->ctx;
	int err;

	if (!transport_binding_is_live(binding) ||
	    !binding->dtls_established)
		return 0;
	if (ctx->description_applying)
		return 0;

	if (ctx->pending_binding == binding)
		return commit_pending_transport(binding);

	if (ctx->bundle_transport && binding->route_generation) {
		err = bundle_transport_commit(ctx->bundle_transport,
					      binding->route_generation);
		if (err == EAGAIN)
			return 0;
		if (err)
			return err;
	}
	if (ctx->dtls_ready)
		return 0;

	ctx->dtls_role = binding->role;
	ctx->dtls_ready = true;
	return try_start_sctp(binding);
}


void transport_estab_handler(int err, enum menc_dtls_role role,
				    void *arg)
{
	struct transport_binding *binding = arg;
	struct data_context *ctx = binding ? binding->ctx : NULL;
	void (*readyh)(void *arg);
	void *ready_arg;

	if (!ctx || !transport_binding_is_live(binding))
		return;
	readyh = ctx->transport_readyh;
	ready_arg = ctx->transport_ready_arg;

	mem_ref(ctx);

	if (!err) {
		binding->role = role;
		binding->dtls_established = true;
		err = transport_binding_maybe_ready(binding);
	}

	if (err && ctx->pending_binding == binding) {
		mem_ref(binding);
		transport_binding_abort(binding, true);
		mem_deref(binding);
	}
	else if (err)
		data_context_report_error(ctx, err);
	if (!err && readyh)
		readyh(ready_arg);
	mem_deref(ctx);
}

void transport_close_handler(int err, void *arg)
{
	struct transport_binding *binding = arg;
	struct data_context *ctx = binding->ctx;

	if (!transport_binding_is_live(binding))
		return;

	mem_ref(binding);
	mem_ref(ctx);
	if (ctx->pending_binding == binding) {
		transport_binding_abort(binding, true);
	}
	else if (binding->dc) {
		(void)dc_transport_close(binding->dc,
					 err ? err : ECONNRESET);
		data_context_report_error(ctx, err ? err : ECONNRESET);
	}
	mem_deref(ctx);
	mem_deref(binding);
}

void attach_transport_members(struct data_context *ctx,
				     struct menc_transport *transport)
{
	struct le *le;

	if (!ctx->bundled)
		return;

	const struct bundle_group *group = bundle_set_find_mid(
		ctx->remote_bundles, ctx->mid);

	for (le = ctx->streaml->head; le; le = le->next) {
		struct stream *stream = le->data;
		struct menc_transport *current;
		const char *remote_mid = sdp_media_rattr(
			stream_sdpmedia(stream), "mid");
		bool member = sdp_media_has_media(stream_sdpmedia(stream)) &&
			(!ctx->remote_bundles ||
			(group && bundle_group_contains(
					  group,
					  str_isset(remote_mid)
						  ? remote_mid
						  : stream_mid(stream))));

		current = stream_menc_transport_ref(stream);
		if (member)
			stream_set_menc_transport(stream, transport);
		else if (current == transport)
			stream_set_menc_transport(stream, NULL);
		mem_deref(current);
	}
}


static bool transport_member(const struct data_context *ctx,
			     const struct bundle_group *group,
			     const struct stream *stream)
{
	const char *remote_mid = sdp_media_rattr(
		stream_sdpmedia(stream), "mid");

	return sdp_media_has_media(stream_sdpmedia(stream)) &&
		(!ctx->remote_bundles ||
		(group && bundle_group_contains(
				  group, str_isset(remote_mid)
						 ? remote_mid
						 : stream_mid(stream))));
}


static const struct bundle_group *stream_remote_group(
	const struct data_context *ctx, const struct stream *stream)
{
	const char *remote_mid;

	if (!ctx->remote_bundles)
		return NULL;
	remote_mid = sdp_media_rattr(stream_sdpmedia(stream), "mid");
	return bundle_set_find_mid(ctx->remote_bundles,
				   str_isset(remote_mid)
					   ? remote_mid : stream_mid(stream));
}


static void publish_transport_members(struct transport_binding *binding);
int stage_transport_member_mappings(
	struct transport_binding *binding);


static int prepare_transport_members(struct transport_binding *binding)
{
	struct data_context *ctx = binding->ctx;
	struct menc_transport *transport = binding->transport;
	struct le *le;
	bool structural = false;
	int err;

	if (list_isempty(&binding->member_mappings)) {
		err = stage_transport_member_mappings(binding);
		if (err)
			return err;
	}

	for (le = binding->member_mappings.head; le; le = le->next) {
		struct transport_member_mapping *mapping = le->data;
		struct stream *stream = mapping->stream;

		if (mapping->desired_transport == transport &&
		    mapping->old_transport != transport) {
			err = stream_prepare_menc_transport(stream, transport);
			if (err)
				goto out;
			structural = true;
		}
		else if (!mapping->desired_transport &&
			 mapping->old_transport == ctx->transport) {
			err = stream_prepare_menc_transport_removal(stream,
							     transport);
			if (err)
				goto out;
			structural = true;
		}
	}
	if (!structural)
		return 0;

	err = menc_transport_members_prepare(ctx->menc, transport);
	if (!err) {
		binding->members_prepared = true;
		return 0;
	}
out:
	menc_transport_members_abort(ctx->menc, transport);
	list_flush(&binding->member_mappings);
	return err;
}


int stage_transport_member_mappings(
	struct transport_binding *binding)
{
	struct data_context *ctx = binding->ctx;
	const struct bundle_group *group = binding->group;
	struct le *le;
	int err = 0;

	if (!list_isempty(&binding->member_mappings))
		return 0;
	for (le = ctx->streaml->head; le; le = le->next) {
		struct stream *stream = le->data;
		struct transport_member_mapping *mapping;
		const struct bundle_group *legacy_group =
			stream_remote_group(ctx, stream);
		bool member = ctx->bundled &&
			transport_member(ctx, group, stream);
		bool preserve_shared = sdp_media_has_media(
			stream_sdpmedia(stream)) && legacy_group &&
			legacy_group != group &&
			bundle_group_count(legacy_group) > 1;

		mapping = mem_zalloc(sizeof(*mapping),
				     transport_member_mapping_destructor);
		if (!mapping) {
			err = ENOMEM;
			break;
		}
		mapping->stream = mem_ref(stream);
		mapping->old_transport = stream_menc_transport_ref(stream);
		if (member)
			mapping->desired_transport =
				mem_ref(binding->transport);
		else if (mapping->old_transport &&
			 (mapping->old_transport != ctx->transport ||
			  preserve_shared))
			mapping->desired_transport =
				mem_ref(mapping->old_transport);
		list_append(&binding->member_mappings, &mapping->le,
			    mapping);
	}
	if (err) {
		list_flush(&binding->member_mappings);
		return err;
	}
	return 0;
}


static void publish_transport_members(struct transport_binding *binding)
{
	struct le *le;

	for (le = binding->member_mappings.head; le; le = le->next) {
		struct transport_member_mapping *mapping = le->data;

		stream_publish_menc_transport(mapping->stream,
					      mapping->desired_transport);
	}
	binding->members_published = true;
}


static int commit_pending_transport(struct transport_binding *binding)
{
	struct data_context *ctx;
	struct menc_transport *old_transport;
	struct mnat_media *old_mnats;
	struct udp_sock *old_sock;
	struct sdp_media *old_transport_sdpm;
	struct dc_transport *old_dc;
	struct transport_binding *old_binding;
	bool members_prepared = false;
	bool route_activated = false;
	bool mnat_activated = false;
	bool defer_callbacks;
	int err;

	if (!binding || !binding->ctx)
		return EINVAL;
	ctx = binding->ctx;
	if (ctx->pending_binding != binding ||
	    !binding->transport || !binding->sock ||
	    !binding->dtls_established)
		return EINVAL;
	mem_ref(binding);
	mem_ref(ctx);
	defer_callbacks = ctx->callbacks_deferred;

	err = try_start_sctp(binding);
	if (err)
		goto out;

	err = prepare_transport_members(binding);
	if (err)
		goto out;
	members_prepared = binding->members_prepared;
	if (binding->mnat_prepared) {
		if (!ctx->mnat->mediaactivateh ||
		    !ctx->mnat->mediarollbackh ||
		    !ctx->mnat->mediafinalizeh) {
			err = ENOTSUP;
			goto abort;
		}
		ctx->mnat->mediaactivateh(binding->mnats);
		binding->mnat_prepared = false;
		binding->mnat_activated = true;
		mnat_activated = true;
	}

	if (ctx->bundle_transport && binding->route_generation) {
		err = bundle_transport_activate(ctx->bundle_transport,
						binding->route_generation);
		if (err)
			goto abort;
		route_activated = true;
	}

	if (members_prepared)
		menc_transport_members_activate(ctx->menc,
						binding->transport);
	if (members_prepared)
		binding->members_activated = true;
	publish_transport_members(binding);

	old_transport = ctx->transport;
	old_mnats = ctx->mnats;
	old_sock = ctx->sock;
	old_transport_sdpm = ctx->transport_sdpm;
	old_binding = active_transport_binding(ctx);
	old_dc = old_binding ? old_binding->dc : NULL;
	if (old_binding) {
		if (old_binding->dc_token)
			old_binding->dc_token->role = DC_CALLBACK_RETIRING;
		old_binding->dc = NULL;
	}

	ctx->transport = binding->transport;
	binding->transport = NULL;
	ctx->mnats = binding->mnats;
	binding->mnats = NULL;
	ctx->sock = binding->sock;
	binding->sock = NULL;
	ctx->transport_sdpm = binding->transport_sdpm;
	binding->transport_sdpm = NULL;
	ctx->transport_generation = binding->generation;
	ctx->pending_binding = NULL;
	ctx->transport_started = true;
	ctx->dtls_ready = true;
	ctx->dtls_role = binding->role;
	if (binding->dc_token)
		binding->dc_token->role = DC_CALLBACK_ACTIVE;
	binding->pending = false;
	binding->sdp_staged = false;
	binding->saved_attrs = mem_deref(binding->saved_attrs);
	binding->context_saved_attrs =
		mem_deref(binding->context_saved_attrs);
	binding->proposed_attrs = mem_deref(binding->proposed_attrs);

	if (!bundle_groups_equal(ctx->active_group, binding->group)) {
		ctx->active_group = mem_deref((void *)ctx->active_group);
		ctx->active_group = mem_ref((void *)binding->group);
	}

	if (route_activated) {
		err = bundle_transport_finalize(
			ctx->bundle_transport, binding->route_generation);
		if (err)
			warning("datachannel: failed to retire old BUNDLE"
				" route (%m)\n", err);
	}
	if (members_prepared)
		menc_transport_members_finalize(ctx->menc,
						ctx->transport);
	if (mnat_activated) {
		ctx->mnat->mediafinalizeh(ctx->mnats);
		binding->mnat_activated = false;
	}
	binding->members_prepared = false;
	binding->members_activated = false;
	binding->members_published = false;
	list_flush(&binding->member_mappings);
	promote_pending_channels(binding);
	if (old_dc)
		(void)dc_transport_close(old_dc, ECONNRESET);
	mem_deref(old_dc);

	menc_transport_detach(ctx->menc, old_transport);
	mem_deref(old_transport);
	mem_deref(old_mnats);
	mem_deref(old_sock);
	mem_deref(old_transport_sdpm);
	transport_bindings_reap(ctx);
	if (!defer_callbacks)
		dispatch_promoted_channels(ctx);

	err = 0;
	goto out;

abort:
	if (mnat_activated) {
		ctx->mnat->mediarollbackh(binding->mnats);
		binding->mnat_activated = false;
	}
	if (route_activated)
		(void)bundle_transport_rollback(
			ctx->bundle_transport, binding->route_generation);
	if (members_prepared)
		menc_transport_members_abort(ctx->menc,
					     binding->transport);
	binding->members_prepared = false;
	list_flush(&binding->member_mappings);
out:
	mem_deref(ctx);
	mem_deref(binding);
	return err;
}


struct stream *stream_lookup_remote_mid(const struct list *streaml,
					       const char *mid)
{
	struct le *le;

	if (!streaml || !str_isset(mid))
		return NULL;

	for (le = streaml->head; le; le = le->next) {
		struct stream *stream = le->data;
		const char *remote_mid = sdp_media_rattr(
			stream_sdpmedia(stream), "mid");

		if (!str_cmp(str_isset(remote_mid) ? remote_mid
						 : stream_mid(stream),
			     mid))
			return stream;
	}

	return NULL;
}


int transport_alloc_bound(struct data_context *ctx,
				 struct transport_binding *binding)
{
	int err;

	if (ctx->transport) {
		attach_transport_members(ctx, ctx->transport);
		return 0;
	}
	/* A session transport coordinator may already have promoted the BUNDLE
	 * tag's established DTLS association before data is added on a re-offer.
	 * Reuse that published association instead of attempting a second promote
	 * (which correctly returns EALREADY). */
	if (ctx->bundled && ctx->bundle_base) {
		ctx->transport =
			stream_menc_transport_ref(ctx->bundle_base);
		if (ctx->transport) {
			ctx->transport_generation = binding->generation;
			attach_transport_members(ctx, ctx->transport);
			return 0;
		}
	}

	err = ctx->bundled
		      ? stream_promote_menc_transport(
			      ctx->bundle_base, &ctx->transport,
			      transport_recv_handler, transport_estab_handler,
			      transport_close_handler, binding)
		      : ENOTSUP;
	if (err == ENOTSUP || err == ENOENT) {
		err = ctx->menc->transporth(
			&ctx->transport, ctx->mencs, ctx->sock, NULL,
			ctx->transport_sdpm, ctx->offerer,
			transport_recv_handler, transport_estab_handler,
			transport_close_handler, binding);
	}
	if (err) {
		return err;
	}
	ctx->transport_generation = binding->generation;

	attach_transport_members(ctx, ctx->transport);

	return 0;
}


int transport_alloc(struct data_context *ctx)
{
	struct transport_binding *binding;
	bool allocated = false;
	int err;

	if (ctx->transport) {
		attach_transport_members(ctx, ctx->transport);
		return 0;
	}

	binding = transport_binding_lookup(
		ctx, ctx->transport_generation);
	if (!binding) {
		binding = transport_binding_alloc(
			ctx, ++ctx->next_generation);
		if (!binding)
			return ENOMEM;
		allocated = true;
	}
	err = transport_alloc_bound(ctx, binding);
	if (err && allocated)
		mem_deref(binding);
	return err;
}


int start_transport_binding(struct transport_binding *binding)
{
	struct data_context *ctx = binding->ctx;
	struct menc_transport *transport;
	int err;

	if (!transport_binding_is_live(binding))
		return 0;
	if (!binding->connected)
		return 0;
	if (!ctx->local_committed || !ctx->remote_accepted)
		return 0;
	transport = transport_binding_transport(binding);
	if (!transport)
		return EAGAIN;

	if (ctx->bundle_transport && binding->route_generation) {
		err = bundle_transport_set_remote(ctx->bundle_transport,
						  binding->route_generation,
						  &binding->remote);
		if (err)
			return err;
		err = transport_binding_maybe_ready(binding);
		if (err)
			return err;
	}
	err = menc_transport_start(ctx->menc, transport,
				   &binding->remote);
	if (!err && ctx->pending_binding != binding)
		ctx->transport_started = true;
	return err;
}


void mnat_connected_handler(const struct sa *raddr1,
				   const struct sa *raddr2, void *arg)
{
	struct transport_binding *binding = arg;
	struct data_context *ctx = binding->ctx;
	int err;

	(void)raddr2;

	if (!transport_binding_is_live(binding))
		return;
	mem_ref(binding);
	mem_ref(ctx);
	sa_cpy(&binding->remote, raddr1);
	binding->connected = true;
	if (binding->session_owned && binding->media_transport) {
		err = media_transport_set_remote(binding->media_transport, raddr1);
		if (!err && ctx->transport_readyh)
			ctx->transport_readyh(ctx->transport_ready_arg);
		goto out;
	}
	if (ctx->pending_binding == binding) {
		err = transport_binding_capture_sdp(binding);
		if (err)
			goto out;
	}
	if (!ctx->transport &&
	    binding->generation == ctx->transport_generation)
		err = transport_alloc_bound(ctx, binding);
	else
		err = 0;
	if (!err)
		err = start_transport_binding(binding);
out:
	if (err && ctx->pending_binding == binding)
		transport_binding_abort(binding, true);
	else if (err)
		data_context_report_error(ctx, err);
	mem_deref(ctx);
	mem_deref(binding);
}

static void adopted_media_recv_handler(struct mbuf *mb, void *arg)
{
	struct data_context *ctx = arg;
	struct transport_binding *binding = active_transport_binding(ctx);

	if (binding)
		transport_recv_handler(mb, binding);
}


static void adopted_media_estab_handler(int err, enum menc_dtls_role role,
					void *arg)
{
	struct data_context *ctx = arg;
	struct transport_binding *binding = active_transport_binding(ctx);

	if (binding)
		transport_estab_handler(err, role, binding);
}
static void adopted_media_close_handler(int err, void *arg)
{
	struct data_context *ctx = arg;
	struct transport_binding *binding = active_transport_binding(ctx);

	if (binding)
		transport_close_handler(err, binding);
}


static void publish_mnat_media(struct mnat_media **destination,
			       struct mnat_media *mnats)
{
	struct mnat_media *previous;

	if (!destination || *destination == mnats)
		return;
	previous = *destination;
	*destination = mem_ref(mnats);
	mem_deref(previous);
}


static void media_mnat_publish_handler(struct mnat_media *mnats, void *arg)
{
	struct data_context *ctx = arg;

	if (ctx)
		publish_mnat_media(&ctx->mnats, mnats);
}


static void binding_mnat_publish_handler(struct mnat_media *mnats, void *arg)
{
	struct transport_binding *binding = arg;

	if (!binding)
		return;
	publish_mnat_media(&binding->mnats, mnats);
	if (binding->ctx && active_transport_binding(binding->ctx) == binding)
		publish_mnat_media(&binding->ctx->mnats, mnats);
}


void data_context_media_adopt_prm(struct data_context *ctx,
				  struct media_transport_prm *prm)
{
	if (!ctx || !prm)
		return;
	prm->recvh = adopted_media_recv_handler;
	prm->estabh = adopted_media_estab_handler;
	prm->closeh = adopted_media_close_handler;
	prm->mnat_publishh = media_mnat_publish_handler;
	prm->arg_ref = mem_ref;
	prm->arg_deref = mem_deref_void;
	prm->arg = ctx;
}


void data_context_set_transport_ready_handler(
	struct data_context *ctx, void (*readyh)(void *arg), void *arg)
{
	if (!ctx)
		return;
	ctx->transport_readyh = readyh;
	ctx->transport_ready_arg = arg;
}


int data_context_media_binding_alloc(struct transport_binding **bindingp,
				     struct data_context *ctx,
				     struct media_transport_prm *prm)
{
	struct transport_binding *binding;

	if (!bindingp || !ctx || !prm)
		return EINVAL;
	if (ctx->pending_binding)
		return EBUSY;
	binding = transport_binding_alloc(ctx, ++ctx->next_generation);
	if (!binding)
		return ENOMEM;
	binding->pending = true;
	binding->session_owned = true;
	ctx->pending_binding = binding;
	prm->recvh = transport_recv_handler;
	prm->estabh = NULL;
	prm->closeh = transport_close_handler;
	prm->mnat_publishh = binding_mnat_publish_handler;
	prm->arg_ref = mem_ref;
	prm->arg_deref = mem_deref_void;
	prm->arg = binding;
	*bindingp = binding;
	return 0;
}


int data_context_media_binding_take_pending(
	struct transport_binding **bindingp, struct data_context *ctx,
	struct media_transport_prm *prm, struct udp_sock **sockp,
	struct mnat_media **mnat_mediap, struct menc_transport **transportp)
{
	struct transport_binding *binding;

	if (!bindingp || !ctx || !prm || !sockp || !mnat_mediap ||
	    !transportp)
		return EINVAL;
	binding = ctx->pending_binding;
	if (!binding || binding->session_owned || !binding->transport ||
	    !binding->sock)
		return ENOENT;
	binding->session_owned = true;
	prm->recvh = transport_recv_handler;
	prm->estabh = NULL;
	prm->closeh = transport_close_handler;
	prm->mnat_publishh = binding_mnat_publish_handler;
	prm->arg_ref = mem_ref;
	prm->arg_deref = mem_deref_void;
	prm->arg = binding;
	prm->mnat_prepared = binding->mnat_prepared;
	*sockp = mem_ref(binding->sock);
	*mnat_mediap = mem_ref(binding->mnats);
	*transportp = mem_ref(binding->transport);
	*bindingp = mem_ref(binding);
	return 0;
}


int data_context_media_binding_attach(struct transport_binding *binding,
				      struct media_transport *transport)
{
	if (!binding || !transport || !binding->session_owned ||
	    !binding->ctx || binding->ctx->pending_binding != binding)
		return EINVAL;
	binding->media_transport = mem_ref(transport);
	/* The session transport now owns activation/rollback of this prepared
	 * MNAT generation. */
	binding->mnat_prepared = false;
	return binding->connected
		? media_transport_set_remote(transport, &binding->remote) : 0;
}


int data_context_media_binding_prepare(struct transport_binding *binding,
				       enum menc_dtls_role role)
{
	struct data_context *ctx;
	int err;

	if (!binding || !binding->session_owned || !binding->media_transport ||
	    !binding->ctx)
		return EINVAL;
	ctx = binding->ctx;
	if (ctx->pending_binding != binding)
		return ESTALE;
	if (role != MENC_DTLS_ROLE_CLIENT &&
	    role != MENC_DTLS_ROLE_SERVER)
		return EPROTO;
	binding->role = role;
	binding->dtls_established = true;
	err = try_start_sctp(binding);
	if (err)
		return err;
	if (!binding->dc)
		return EAGAIN;
	err = media_transport_consumer_ready(binding->media_transport);
	if (err)
		return err;
	return dc_transport_association_ready(binding->dc) ? 0 : EAGAIN;
}


void data_context_media_binding_finalize(struct transport_binding *binding)
{
	struct data_context *ctx;
	struct transport_binding *old;
	struct dc_transport *old_dc = NULL;

	if (!binding || !binding->session_owned || !binding->ctx)
		return;
	ctx = binding->ctx;
	if (ctx->pending_binding != binding || !binding->dc)
		return;
	mem_ref(ctx);
	if (binding->transport) {
		ctx->transport = mem_deref(ctx->transport);
		ctx->transport = mem_ref(binding->transport);
		ctx->mnats = mem_deref(ctx->mnats);
		ctx->mnats = mem_ref(binding->mnats);
		ctx->sock = mem_deref(ctx->sock);
		ctx->sock = mem_ref(binding->sock);
		ctx->transport_sdpm = mem_deref(ctx->transport_sdpm);
		ctx->transport_sdpm = mem_ref(binding->transport_sdpm);
		if (!bundle_groups_equal(ctx->active_group, binding->group)) {
			ctx->active_group = mem_deref((void *)ctx->active_group);
			ctx->active_group = mem_ref((void *)binding->group);
		}
		if (ctx->bundle_transport && binding->route_generation)
			(void)bundle_transport_abort(ctx->bundle_transport,
						     binding->route_generation);
		binding->sdp_staged = false;
		binding->saved_attrs = mem_deref(binding->saved_attrs);
		binding->context_saved_attrs =
			mem_deref(binding->context_saved_attrs);
		binding->proposed_attrs = mem_deref(binding->proposed_attrs);
	}
	old = active_transport_binding(ctx);
	if (old && old != binding) {
		old_dc = old->dc;
		old->dc = NULL;
		if (old->dc_token) {
			old->dc_token->suppress_transport_error = true;
			old->dc_token->role = DC_CALLBACK_RETIRING;
		}
	}
	ctx->transport_generation = binding->generation;
	ctx->pending_binding = NULL;
	ctx->transport_started = true;
	ctx->dtls_ready = true;
	ctx->dtls_role = binding->role;
	binding->pending = false;
	if (binding->dc_token)
		binding->dc_token->role = DC_CALLBACK_ACTIVE;
	promote_pending_channels(binding);
	if (old_dc)
		(void)dc_transport_close(old_dc, ECONNRESET);
	mem_deref(old_dc);
	if (!ctx->callbacks_deferred)
		dispatch_promoted_channels(ctx);
	mem_deref(ctx);
}


void data_context_media_binding_abort(struct transport_binding *binding)
{
	struct data_context *ctx;
	struct media_transport *transport;

	if (!binding || !binding->session_owned || !binding->ctx)
		return;
	ctx = binding->ctx;
	if (ctx->pending_binding != binding)
		return;
	/* A prepared consumer can share the active DTLS lower.  Break its retained
	 * callback/owner cycle now, while preserving the lower's published callback
	 * binding.  Only a committed generation or terminal context teardown may
	 * retire callbacks on the underlying MENC transport. */
	transport = binding->media_transport;
	binding->media_transport = NULL;
	media_transport_detach_consumer(transport);
	mem_deref(transport);
	transport_binding_restore_sdp(binding);
	if (ctx->bundle_transport && binding->route_generation)
		(void)bundle_transport_abort(ctx->bundle_transport,
					     binding->route_generation);
	if (binding->dc_token)
		binding->dc_token->suppress_transport_error = true;
	abort_pending_channels(ctx, binding->generation);
	ctx->pending_binding = NULL;
	mem_deref(binding);
}

int datachannel_set_handlers(struct data_channel *dc,
			     datachannel_message_h *messageh,
			     datachannel_state_h *stateh,
			     datachannel_buffered_low_h *buffered_lowh,
			     void *arg)
{
	if (!dc)
		return EINVAL;

	dc->messageh = messageh;
	dc->stateh = stateh;
	dc->buffered_lowh = buffered_lowh;
	dc->arg = arg;
	return 0;
}

int datachannel_send(struct data_channel *dc,
		     enum data_channel_message_type type,
		     const uint8_t *buf, size_t len)
{
	if (!dc || (!buf && len))
		return EINVAL;
	if (!dc->dc)
		return ENOTCONN;
	if (type != DATACHANNEL_MESSAGE_TEXT &&
	    type != DATACHANNEL_MESSAGE_BINARY)
		return EINVAL;

	return dc_channel_send(dc->dc,
			       type == DATACHANNEL_MESSAGE_TEXT
				       ? DC_MESSAGE_TEXT : DC_MESSAGE_BINARY,
			       buf, len);
}

int datachannel_close(struct data_channel *dc)
{
	if (!dc)
		return EINVAL;

	if (!dc->dc) {
		if (dc->state == DATACHANNEL_CLOSED)
			return 0;
		dc->state = DATACHANNEL_CLOSED;
		if (dc->stateh)
			dc->stateh(dc, dc->state, 0, dc->arg);
		return 0;
	}

	return dc_channel_close(dc->dc);
}

const char *datachannel_label(const struct data_channel *dc)
{
	return dc ? dc->label : NULL;
}

const char *datachannel_protocol(const struct data_channel *dc)
{
	return dc ? dc->protocol : NULL;
}

int datachannel_id(const struct data_channel *dc)
{
	return dc ? dc->id : -1;
}

enum data_channel_state datachannel_state(const struct data_channel *dc)
{
	return dc ? dc->state : DATACHANNEL_CLOSED;
}

size_t datachannel_buffered_amount(const struct data_channel *dc)
{
	return dc && dc->dc ? dc_channel_buffered_amount(dc->dc) : 0;
}
