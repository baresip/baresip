#ifndef BARESIP_DATACHANNEL_INTERNAL_H
#define BARESIP_DATACHANNEL_INTERNAL_H

enum {
	DATACHANNEL_SCTP_PORT = 5000,
	DATACHANNEL_STREAMS = 256,
	DATACHANNEL_MESSAGE_LIMIT = 16384,
	DATACHANNEL_SEND_BUDGET = 256 * 1024,
	/*
	 * Application callbacks are held while an SDP transaction is
	 * provisional.  Charge both payload and queue metadata so a peer
	 * cannot grow this adapter-owned receive queue with either large or
	 * empty messages.
	 */
	DATACHANNEL_DEFERRED_RECV_BUDGET = 256 * 1024,
};

struct data_context {
	struct list channels;
	struct list transport_bindings;
	struct sdp_session *sdp;
	struct sdp_media *sdpm;
	struct sdp_media *transport_sdpm;
	struct udp_sock *sock;
	struct mnat_sess *mnat_session;
	struct mnat_media *mnats;
	struct menc_sess *mencs;
	struct menc_transport *transport;
	struct bundle_set *remote_bundles;
	struct bundle_transport *bundle_transport;
	struct mqueue *associationq;
	struct transport_binding *association_queued;
	struct transport_binding *pending_binding;
	struct data_context_state *operation_state;
	struct data_context_state *pending_state;
	const struct bundle_group *active_group;
	struct stream *bundle_base;
	char *mid;
	char *remote_identity;
	char *remote_ice_identity;
	char *local_ice_identity;
	const struct mnat *mnat;
	const struct menc *menc;
	struct list *streaml;
	peerconnection_datachannel_h *channelh;
	void *channel_arg;
	data_context_error_h *errorh;
	void *arg;
	data_context_dispatch_refs_h *dispatch_refsh;
	void *dispatch_refs_arg;
	void (*transport_readyh)(void *arg);
	void *transport_ready_arg;
	size_t send_limit;
	size_t deferred_message_bytes;
	int af;
	uint16_t remote_port;
	uint64_t transport_generation;
	uint64_t next_generation;
	uint64_t dispatch_generation;
	enum menc_dtls_role dtls_role;
	bool offerer;
	bool bundled;
	bool bundle_data_first;
	bool rejected;
	bool transport_started;
	bool dtls_ready;
	bool remote_accepted;
	bool local_committed;
	bool offered_ice_restart;
	bool transport_replacing;
	bool description_applying;
	bool description_prepared;
	bool description_provisional;
	bool callbacks_deferred;
	bool rejection_pending;
	int deferred_error;
	bool closing;
};

enum dc_callback_role {
	DC_CALLBACK_CANDIDATE,
	DC_CALLBACK_ACTIVE,
	DC_CALLBACK_RETIRING,
};

struct dc_callback_token {
	struct transport_binding *binding;
	struct dc_transport *transport;
	uint64_t generation;
	enum dc_callback_role role;
	bool suppress_transport_error;
};

struct data_channel_snapshot {
	struct le le;
	struct data_channel *channel;
	bool sdp_remote;
	bool sdp_offered;
	bool sdp_seen;
	bool sdp_staged;
	bool sdp_provisional;
	bool sdp_remove_pending;
};

struct data_context_state {
	struct list channels;
	struct sdp_media *transport_sdpm;
	struct udp_sock *sock;
	struct bundle_set *remote_bundles;
	const struct bundle_group *active_group;
	struct stream *bundle_base;
	char *mid;
	char *remote_identity;
	char *remote_ice_identity;
	char *local_ice_identity;
	size_t send_limit;
	uint64_t pending_generation;
	uint16_t remote_port;
	enum menc_dtls_role dtls_role;
	bool bundled;
	bool bundle_data_first;
	bool rejected;
	bool transport_started;
	bool dtls_ready;
	bool remote_accepted;
	bool local_committed;
	bool offered_ice_restart;
	bool transport_replacing;
	bool callbacks_deferred;
	bool rejection_pending;
	int deferred_error;
};


struct transport_binding {
	struct le le;
	struct list member_mappings;
	struct data_context *ctx;
	const struct menc *menc;
	const struct mnat *mnat;
	struct udp_sock *sock;
	struct mnat_media *mnats;
	struct menc_sess *shadow_mencs;
	struct menc_transport *transport;
	struct media_transport *media_transport;
	struct dc_transport *dc;
	struct dc_transport *candidate_dc;
	struct dc_callback_token *dc_token;
	struct dc_callback_token *candidate_token;
	struct sdp_media *transport_sdpm;
	struct sdp_session *shadow_sdp;
	struct sdp_media *shadow_sdpm;
	struct sdp_media_lattr_state *saved_attrs;
	struct sdp_media_lattr_state *context_saved_attrs;
	struct sdp_media_lattr_state *proposed_attrs;
	const struct bundle_group *group;
	struct sa saved_laddr;
	struct sa context_saved_laddr;
	struct sa proposed_laddr;
	struct sa remote;
	uint64_t generation;
	uint64_t route_generation;
	enum menc_dtls_role role;
	bool connected;
	bool dtls_established;
	bool pending;
	bool sdp_staged;
	bool members_prepared;
	bool members_activated;
	bool members_published;
	bool session_owned;
	bool association_notified;
	bool local_restart;
	bool gather_waiting;
	bool gathered;
	bool mnat_prepared;
	bool mnat_activated;
	int gather_err;
};

struct transport_member_mapping {
	struct le le;
	struct stream *stream;
	struct menc_transport *old_transport;
	struct menc_transport *desired_transport;
};

struct data_channel {
	struct le le;
	struct list pending_messages;
	struct data_context *ctx;
	struct dc_channel *dc;
	struct dc_channel *pending_dc;
	char *label;
	char *protocol;
	struct data_channel_config config;
	datachannel_message_h *messageh;
	datachannel_state_h *stateh;
	datachannel_buffered_low_h *buffered_lowh;
	void *arg;
	enum data_channel_state state;
	enum data_channel_state pending_state;
	int id;
	int pending_id;
	uint64_t pending_generation;
	uint16_t priority;
	bool sdp_remote;
	bool sdp_offered;
	bool sdp_seen;
	bool sdp_staged;
	bool sdp_provisional;
	bool sdp_remove_pending;
	bool pending_incoming;
	bool dispatch_incoming;
	bool dispatch_state;
	bool dispatch_buffered_low;
	int dispatch_state_err;
	enum data_channel_state dispatch_state_value;
};

struct pending_message {
	struct le le;
	enum dc_message_type type;
	size_t len;
	size_t charge;
	uint8_t data[];
};

#if defined(__GNUC__)
#pragma GCC visibility push(hidden)
#endif

uint32_t dispatch_owner_refs(const struct data_context *ctx);
bool dispatch_callback_complete(struct data_context *ctx,
				uint32_t refs, uint64_t generation);
void data_context_report_error(struct data_context *ctx, int err);
int data_context_state_save(struct data_context_state **statep,
			    const struct data_context *ctx);
void data_context_state_restore(struct data_context *ctx,
				struct data_context_state *state);
bool local_mid_used(const struct sdp_session *sdp,
		    const struct list *streaml, unsigned mid);
struct data_channel *channel_lookup_id(struct data_context *ctx, uint16_t id);
void channel_destructor(void *arg);
void context_destructor(void *arg);
struct transport_binding *transport_binding_alloc(struct data_context *ctx,
						   uint64_t generation);
struct transport_binding *transport_binding_lookup(
	const struct data_context *ctx, uint64_t generation);
struct transport_binding *active_transport_binding(
	const struct data_context *ctx);
bool transport_binding_is_live(const struct transport_binding *binding);
struct menc_transport *transport_binding_transport(
	const struct transport_binding *binding);
void transport_binding_restore_sdp(struct transport_binding *binding);
int transport_binding_capture_sdp(struct transport_binding *binding);
int transport_binding_apply_sdp(struct transport_binding *binding);
void transport_binding_abort(struct transport_binding *binding,
			     bool restore_sdp);
void transport_bindings_reap(struct data_context *ctx);
bool bundle_groups_equal(const struct bundle_group *a,
			 const struct bundle_group *b);
int ensure_bundle_transport(struct data_context *ctx,
			    const struct bundle_group *group);
void attach_transport_members(struct data_context *ctx,
			      struct menc_transport *transport);
bool notify_incoming_channel(struct data_context *ctx,
			     struct data_channel *dc);
int channel_bind(struct data_channel *dc, struct transport_binding *binding);
void abort_pending_channels(struct data_context *ctx, uint64_t generation);
void promote_pending_channels(struct transport_binding *binding);
bool dispatch_promoted_channels(struct data_context *ctx);
void transport_association_ready_queue_handler(int id, void *data, void *arg);
void transport_recv_handler(struct mbuf *mb, void *arg);
int try_start_sctp(struct transport_binding *binding);
int transport_binding_maybe_ready(struct transport_binding *binding);
void transport_estab_handler(int err, enum menc_dtls_role role, void *arg);
void transport_close_handler(int err, void *arg);
int stage_transport_member_mappings(struct transport_binding *binding);
struct stream *stream_lookup_remote_mid(const struct list *streaml,
					const char *mid);
int transport_alloc_bound(struct data_context *ctx,
			  struct transport_binding *binding);
int transport_alloc(struct data_context *ctx);
int start_transport_binding(struct transport_binding *binding);
void mnat_connected_handler(const struct sa *raddr1,
			    const struct sa *raddr2, void *arg);

#if defined(__GNUC__)
#pragma GCC visibility pop
#endif

#endif
