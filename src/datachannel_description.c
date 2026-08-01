/**
 * @file datachannel_description.c
 *
 * Data-channel SDP negotiation and description transactions.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <re_datachannel.h>
#include <baresip.h>
#include "core.h"
#include "datachannel_internal.h"


static int parse_remote_limit(struct data_context *ctx, size_t *limit)
{
	const char *value = sdp_media_rattr(ctx->sdpm, "max-message-size");
	size_t parsed = 0;

	if (!value) {
		*limit = 65536;
		return 0;
	}

	if (!*value)
		return EPROTO;
	for (const char *cursor = value; *cursor; ++cursor) {
		unsigned digit;

		if (!isdigit((unsigned char)*cursor))
			return EPROTO;
		digit = (unsigned)(*cursor - '0');
		if (parsed > (SIZE_MAX - digit) / 10)
			return EPROTO;
		parsed = parsed * 10 + digit;
	}
	*limit = parsed == 0 ? DATACHANNEL_MESSAGE_LIMIT : (size_t)parsed;
	return 0;
}


static int parse_sctp_port(const char *value, uint16_t *port)
{
	uint32_t parsed = 0;

	if (!value || !*value || !port)
		return EPROTO;
	for (const char *cursor = value; *cursor; ++cursor) {
		unsigned digit;

		if (!isdigit((unsigned char)*cursor))
			return EPROTO;
		digit = (unsigned)(*cursor - '0');
		if (parsed > (UINT16_MAX - digit) / 10)
			return EPROTO;
		parsed = parsed * 10 + digit;
	}
	if (!parsed)
		return EPROTO;

	*port = (uint16_t)parsed;
	return 0;
}


struct nth_attr {
	const char *value;
	size_t target;
	size_t index;
};


static bool nth_attr_handler(const char *name, const char *value, void *arg)
{
	struct nth_attr *nth = arg;
	(void)name;

	if (nth->index++ != nth->target)
		return false;

	nth->value = value;
	return true;
}


static const char *transport_attr_nth(const struct data_context *ctx,
				      const struct sdp_media *transport,
				      const char *name, size_t index)
{
	struct nth_attr nth = {
		.target = index,
	};

	if (sdp_media_rattr(transport, name))
		(void)sdp_media_rattr_apply(transport, name,
					    nth_attr_handler, &nth);
	else
		(void)sdp_session_rattr_apply(ctx->sdp, name,
					      nth_attr_handler, &nth);

	return nth.value;
}


static const char *local_transport_attr_nth(const struct data_context *ctx,
					    const struct sdp_media *transport,
					    const char *name, size_t index)
{
	struct nth_attr nth = {.target = index};

	if (sdp_media_lattr_apply(transport, name, NULL, NULL))
		(void)sdp_media_lattr_apply(transport, name,
					   nth_attr_handler, &nth);
	else
		(void)sdp_session_lattr_apply(ctx->sdp, name,
					     nth_attr_handler, &nth);
	return nth.value;
}


static int string_pointer_compare(const void *left, const void *right)
{
	const char *const *a = left;
	const char *const *b = right;

	return strcmp(*a, *b);
}


static int normalized_attr(char **valuep, const char *value)
{
	char *normalized;
	int err;

	err = str_dup(&normalized, value);
	if (err)
		return err;
	for (char *cursor = normalized; *cursor; ++cursor)
		*cursor = (char)tolower((unsigned char)*cursor);
	*valuep = normalized;
	return 0;
}


static int remote_transport_identity(char **identityp,
				     const struct data_context *ctx,
				     const struct sdp_media *transport)
{
	const char *tls_id = transport_attr_nth(ctx, transport, "tls-id", 0);
	const char *setup = transport_attr_nth(ctx, transport, "setup", 0);
	char **fingerprints = NULL;
	struct mbuf *identity = NULL;
	size_t count = 0;
	int err;

	if (!identityp || !setup)
		return EPROTO;

	while (transport_attr_nth(ctx, transport, "fingerprint", count))
		++count;
	if (!count)
		return EPROTO;

	fingerprints = mem_zalloc(count * sizeof(*fingerprints), NULL);
	identity = mbuf_alloc(128);
	if (!fingerprints || !identity) {
		err = ENOMEM;
		goto out;
	}

	err = 0;
	for (size_t i = 0; !err && i < count; ++i) {
		err = normalized_attr(&fingerprints[i],
				      transport_attr_nth(ctx, transport,
							 "fingerprint", i));
	}
	if (err)
		goto out;

	qsort(fingerprints, count, sizeof(*fingerprints),
	      string_pointer_compare);
	err = mbuf_printf(identity, "tls-id=%s|fingerprints=",
			  tls_id ? tls_id : "");
	for (size_t i = 0; !err && i < count; ++i)
		err = mbuf_printf(identity, "%s%s", i ? "," : "",
				  fingerprints[i]);
	if (!err && !tls_id)
		err = mbuf_printf(identity, "|remote=%J",
				  sdp_media_raddr(transport));
	if (!err) {
		identity->pos = 0;
		err = mbuf_strdup(identity, identityp, mbuf_get_left(identity));
	}

out:
	for (size_t i = 0; i < count; ++i)
		mem_deref(fingerprints[i]);
	mem_deref(fingerprints);
	mem_deref(identity);
	return err;
}


static int remote_ice_identity(char **identityp,
			       const struct data_context *ctx,
			       const struct sdp_media *transport)
{
	const char *ufrag = transport_attr_nth(ctx, transport,
					 "ice-ufrag", 0);
	const char *pwd = transport_attr_nth(ctx, transport, "ice-pwd", 0);

	if (!identityp || (!!str_isset(ufrag) != !!str_isset(pwd)))
		return EPROTO;
	if (!str_isset(ufrag))
		return str_dup(identityp, "ice=none");
	return re_sdprintf(identityp, "ufrag=%s|pwd=%s", ufrag, pwd);
}


static int local_ice_identity(char **identityp,
			      const struct data_context *ctx,
			      const struct sdp_media *transport)
{
	const char *ufrag = local_transport_attr_nth(
		ctx, transport, "ice-ufrag", 0);
	const char *pwd = local_transport_attr_nth(
		ctx, transport, "ice-pwd", 0);

	if (!identityp || (!!str_isset(ufrag) != !!str_isset(pwd)))
		return EPROTO;
	if (!str_isset(ufrag))
		return str_dup(identityp, "ice=none");
	return re_sdprintf(identityp, "ufrag=%s|pwd=%s", ufrag, pwd);
}


static bool remote_dtls_role_changed(const struct data_context *ctx,
				     const struct sdp_media *transport)
{
	const char *setup = transport_attr_nth(ctx, transport, "setup", 0);
	enum menc_dtls_role expected;

	if (!setup || ctx->dtls_role == MENC_DTLS_ROLE_UNKNOWN ||
	    !str_casecmp(setup, "actpass"))
		return false;

	if (!str_casecmp(setup, "active"))
		expected = MENC_DTLS_ROLE_SERVER;
	else if (!str_casecmp(setup, "passive"))
		expected = MENC_DTLS_ROLE_CLIENT;
	else
		return true;

	return ctx->dtls_role != expected;
}


struct attr_compare {
	const struct data_context *ctx;
	const struct sdp_media *transport;
	const char *name;
	size_t count;
	bool case_sensitive;
	bool conflict;
};


static bool compare_attr_handler(const char *name, const char *value,
				 void *arg)
{
	struct attr_compare *compare = arg;
	const char *base;
	(void)name;

	base = transport_attr_nth(compare->ctx, compare->transport, compare->name,
				  compare->count++);
	compare->conflict = !base ||
		(compare->case_sensitive ? strcmp(base, value)
					 : str_casecmp(base, value));
	return compare->conflict;
}


static bool media_attr_conflicts(const struct data_context *ctx,
				 const struct sdp_media *transport,
				 const struct sdp_media *member,
				 const char *name, bool case_sensitive)
{
	struct attr_compare compare = {
		.ctx = ctx,
		.transport = transport,
		.name = name,
		.case_sensitive = case_sensitive,
	};

	if (!member || member == transport)
		return false;

	if (!sdp_media_rattr(member, name))
		return false;

	(void)sdp_media_rattr_apply(member, name, compare_attr_handler,
				    &compare);

	return compare.conflict ||
		transport_attr_nth(ctx, transport, name, compare.count);
}


static bool transport_attr_conflicts(const struct data_context *ctx,
				      const struct sdp_media *transport,
				      const struct bundle_group *group,
				      const char *name, bool case_sensitive)
{
	struct le *le;

	if (!ctx->bundled)
		return false;
	if (media_attr_conflicts(ctx, transport, ctx->sdpm, name,
				 case_sensitive))
		return true;

	for (le = ctx->streaml->head; le; le = le->next) {
		const struct stream *stream = le->data;
		const char *mid = sdp_media_rattr(
			stream_sdpmedia(stream), "mid");

		if (group &&
		    !bundle_group_contains(group, str_isset(mid)
							  ? mid
							  : stream_mid(stream)))
			continue;
		if (media_attr_conflicts(ctx, transport,
					 stream_sdpmedia(stream), name,
					 case_sensitive))
			return true;
	}

	return false;
}


static int transport_binding_shadow_alloc(struct transport_binding *binding,
					  struct sdp_media *live)
{
	struct sdp_media_lattr_state *attrs = NULL;
	struct sdp_media *media = NULL;
	const struct sa *live_addr;
	int err;

	if (!binding || !live)
		return EINVAL;
	live_addr = sdp_media_laddr(live);
	err = sdp_session_alloc(&binding->shadow_sdp, live_addr);
	if (!err)
		err = sdp_media_add(&media, binding->shadow_sdp,
				    sdp_media_name(live), sa_port(live_addr),
				    sdp_media_proto(live));
	if (!err)
		err = sdp_format_add(
			NULL, media, false,
			!str_casecmp(sdp_media_name(live), "application")
				? "webrtc-datachannel" : "0",
			NULL, 0, 0, NULL, NULL, NULL, false, NULL);
	if (!err)
		err = sdp_media_save_lattrs(&attrs, live);
	if (!err)
		err = sdp_media_apply_lattrs(media, attrs);
	if (!err) {
		sdp_media_set_laddr(media, live_addr);
		sdp_media_set_lport(media, sa_port(live_addr));
		binding->shadow_sdpm = mem_ref(media);
	}
	mem_deref(attrs);
	return err;
}


struct shadow_remote_print {
	struct mbuf *mb;
	int err;
};


static bool shadow_remote_attr_handler(const char *name, const char *value,
				       void *arg)
{
	struct shadow_remote_print *print = arg;

	if (!print->err)
		print->err = value
			? mbuf_printf(print->mb, "a=%s:%s\r\n", name, value)
			: mbuf_printf(print->mb, "a=%s\r\n", name);
	return print->err != 0;
}


static int transport_binding_sync_remote(struct transport_binding *binding,
					 struct sdp_media *source)
{
	static const char *attrs[] = {
		"mid", "setup", "fingerprint", "tls-id", "ice-ufrag",
		"ice-pwd", "candidate", "end-of-candidates",
	};
	struct shadow_remote_print print;
	const struct sa *remote;
	struct mbuf *mb = NULL;
	const char *af;
	int err;

	if (!binding || !binding->shadow_sdp || !binding->shadow_sdpm ||
	    !source)
		return EINVAL;
	remote = sdp_media_raddr(source);
	if (!remote || !sa_isset(remote, SA_ALL))
		return EDESTADDRREQ;
	af = sa_af(remote) == AF_INET6 ? "IP6" : "IP4";
	mb = mbuf_alloc(1024);
	if (!mb)
		return ENOMEM;
	err = mbuf_printf(
		mb, "v=0\r\no=- 1 1 IN %s %j\r\ns=-\r\n"
		    "c=IN %s %j\r\nt=0 0\r\n"
		    "m=%s %u %s %s\r\n",
		af, remote, af, remote, sdp_media_name(source),
		sdp_media_rport(source), sdp_media_proto(source),
		!str_casecmp(sdp_media_name(source), "application")
			? "webrtc-datachannel" : "0");
	print.mb = mb;
	print.err = err;
	for (size_t i = 0; !print.err && i < RE_ARRAY_SIZE(attrs); ++i) {
		if (sdp_media_rattr(source, attrs[i]))
			(void)sdp_media_rattr_apply(
				source, attrs[i], shadow_remote_attr_handler, &print);
		else {
			const char *value = sdp_session_rattr(
				binding->ctx->sdp, attrs[i]);

			if (value)
				(void)shadow_remote_attr_handler(
					attrs[i], value, &print);
		}
	}
	if (!print.err) {
		mb->pos = 0;
		print.err = sdp_decode(binding->shadow_sdp, mb, false);
	}
	mem_deref(mb);
	return print.err;
}


static void transport_gather_handler(int err, void *arg)
{
	struct transport_binding *binding = arg;
	struct data_context *ctx;

	if (!binding || !transport_binding_is_live(binding))
		return;
	ctx = binding->ctx;
	mem_ref(binding);
	mem_ref(ctx);
	binding->gather_waiting = false;
	binding->gather_err = err;
	if (!err) {
		binding->gathered = true;
		binding->gather_err = transport_binding_capture_sdp(binding);
	}
	if (ctx->transport_readyh)
		ctx->transport_readyh(ctx->transport_ready_arg);
	mem_deref(ctx);
	mem_deref(binding);
}


static int replace_transport(struct data_context *ctx,
			     struct sdp_media *transport_sdpm,
			     bool local_offerer, bool shadow_local)
{
	struct transport_binding *binding = NULL;
	struct sdp_media *allocation_sdpm;
	struct sa laddr;
	uint64_t generation;
	int err;

	if (ctx->pending_binding)
		transport_binding_abort(ctx->pending_binding, true);
	transport_bindings_reap(ctx);

	generation = ++ctx->next_generation;
	binding = transport_binding_alloc(ctx, generation);
	if (!binding)
		return ENOMEM;
	binding->pending = true;
	binding->transport_sdpm = mem_ref(transport_sdpm);
	binding->local_restart = shadow_local;
	if (shadow_local) {
		err = transport_binding_shadow_alloc(binding, transport_sdpm);
		if (err)
			goto out;
		if (!ctx->menc->sessh) {
			err = ENOTSUP;
			goto out;
		}
		err = ctx->menc->sessh(&binding->shadow_mencs,
				       binding->shadow_sdp, true,
				       NULL, NULL, NULL);
		if (err)
			goto out;
		allocation_sdpm = binding->shadow_sdpm;
	}
	else {
		allocation_sdpm = transport_sdpm;
		err = sdp_media_save_lattrs(&binding->saved_attrs,
					    transport_sdpm);
		if (err)
			goto out;
		sa_cpy(&binding->saved_laddr, sdp_media_laddr(transport_sdpm));
		binding->sdp_staged = true;
	}

	sa_init(&laddr, ctx->af);
	err = udp_listen(&binding->sock, &laddr, NULL, NULL);
	if (!err)
		err = udp_local_get(binding->sock, &laddr);
	if (err)
		goto out;

	sdp_media_set_laddr(allocation_sdpm, &laddr);
	sdp_media_set_lport(allocation_sdpm, sa_port(&laddr));
	sdp_media_del_lattr(allocation_sdpm, "candidate");

	if (ctx->mnat->mediah) {
		if (ctx->mnats) {
			if (!ctx->mnat->mediarestartalloch) {
				err = ENOTSUP;
				goto out;
			}
			err = ctx->mnat->mediarestartalloch(
				&binding->mnats, ctx->mnats, binding->sock,
				allocation_sdpm, mnat_connected_handler, binding);
		}
		else {
			/* Splitting application out of an RTP-owned BUNDLE group is a
			 * new media component, not an ICE restart of the tag stream. */
			err = ctx->mnat->mediah(
				&binding->mnats, ctx->mnat_session, binding->sock,
				NULL, allocation_sdpm, mnat_connected_handler,
				binding);
		}
		if (err)
			goto out;
		if (shadow_local) {
			if (!ctx->mnat->mediaprepareh ||
			    !ctx->mnat->mediaaborth ||
			    !ctx->mnat->mediagatheredh ||
			    !ctx->mnat->mediagatherwaith ||
			    !ctx->mnat->mediagathercancelh) {
				err = ENOTSUP;
				goto out;
			}
			err = ctx->mnat->mediaprepareh(binding->mnats, true);
			if (err)
				goto out;
			binding->mnat_prepared = true;
		}
	}

	err = ctx->menc->transporth(
		&binding->transport,
		shadow_local ? binding->shadow_mencs : ctx->mencs,
		binding->sock, NULL,
		allocation_sdpm,
		local_offerer, transport_recv_handler, transport_estab_handler,
		transport_close_handler, binding);
	if (err)
		goto out;
	if (!ctx->mnat->mediah) {
		const struct sa *remote =
			sdp_media_raddr(allocation_sdpm);

		if (sa_isset(remote, SA_ALL)) {
			sa_cpy(&binding->remote, remote);
			binding->connected = true;
		}
	}

	binding->gathered = !shadow_local || !ctx->mnat->mediah ||
		ctx->mnat->mediagatheredh(binding->mnats);
	if (binding->gathered) {
		err = transport_binding_capture_sdp(binding);
		if (err)
			goto out;
	}
	ctx->pending_binding = binding;
	return 0;

out:
	transport_binding_restore_sdp(binding);
	mem_deref(binding);
	return err;
}


bool data_context_transport_restart_supported(const struct data_context *ctx)
{
	if (!ctx || !ctx->menc || !ctx->menc->transporth ||
	    !ctx->menc->sessh)
		return false;
	if (!ctx->mnat || !ctx->mnat->mediah)
		return true;
	return ctx->mnats && ctx->mnat->mediarestartalloch &&
		ctx->mnat->mediaprepareh && ctx->mnat->mediaaborth &&
		ctx->mnat->mediagatheredh && ctx->mnat->mediagatherwaith &&
		ctx->mnat->mediagathercancelh;
}


int data_context_prepare_transport_restart(struct data_context *ctx)
{
	struct transport_binding *binding;
	int err;

	if (!ctx || !ctx->transport_sdpm || !ctx->transport || ctx->rejected)
		return EINVAL;
	if (!data_context_transport_restart_supported(ctx))
		return ENOTSUP;
	if (!ctx->pending_binding) {
		err = replace_transport(ctx, ctx->transport_sdpm, true, true);
		if (err)
			return err;
	}
	binding = ctx->pending_binding;
	if (!binding->local_restart)
		return EBUSY;
	if (binding->gather_err)
		return binding->gather_err;
	if (binding->gathered)
		return 0;
	if (ctx->mnat->mediagatheredh(binding->mnats)) {
		binding->gathered = true;
		return transport_binding_capture_sdp(binding);
	}
	if (binding->gather_waiting)
		return EAGAIN;
	err = ctx->mnat->mediagatherwaith(
		binding->mnats, transport_gather_handler, binding);
	if (err == EAGAIN) {
		binding->gather_waiting = true;
		return EAGAIN;
	}
	if (err)
		return err;
	binding->gathered = true;
	return transport_binding_capture_sdp(binding);
}


int data_context_sync_prepared_transport_remote(struct data_context *ctx)
{
	struct transport_binding *binding;
	const struct sa *remote;
	int err;

	if (!ctx || !ctx->transport_sdpm)
		return EINVAL;
	binding = ctx->pending_binding;
	if (!binding || !binding->local_restart)
		return ENOENT;
	err = transport_binding_sync_remote(binding, ctx->transport_sdpm);
	if (err || ctx->mnat->mediah)
		return err;
	remote = sdp_media_raddr(binding->shadow_sdpm);
	if (!remote || !sa_isset(remote, SA_ALL))
		return EDESTADDRREQ;
	sa_cpy(&binding->remote, remote);
	binding->connected = true;
	return 0;
}


void data_context_abort_transport_restart(struct data_context *ctx)
{
	if (!ctx || !ctx->pending_binding ||
	    !ctx->pending_binding->local_restart)
		return;
	transport_binding_abort(ctx->pending_binding, true);
}


int data_context_alloc(struct data_context **ctxp,
		       struct sdp_session *sdp,
		       const struct mnat *mnat, struct mnat_sess *mnats,
		       const struct menc *menc, struct menc_sess *mencs,
		       struct stream *bundle_base, struct list *streaml,
		       int af, bool offerer,
		       data_context_error_h *errorh, void *arg)
{
	struct data_context *ctx;
	struct transport_binding *binding = NULL;
	struct sa laddr;
	uint32_t mid;
	int err;

	if (!ctxp || !sdp || !mnat || !menc || !menc->transporth)
		return EINVAL;

	ctx = mem_zalloc(sizeof(*ctx), context_destructor);
	if (!ctx)
		return ENOMEM;
	err = mqueue_alloc(&ctx->associationq,
			   transport_association_ready_queue_handler, ctx);
	if (err) {
		mem_deref(ctx);
		return err;
	}

	ctx->sdp = mem_ref(sdp);
	ctx->mnat = mnat;
	ctx->menc = menc;
	ctx->mencs = mem_ref(mencs);
	ctx->mnat_session = mem_ref(mnats);
	ctx->offerer = offerer;
	ctx->af = af;
	ctx->streaml = streaml;
	ctx->send_limit = DATACHANNEL_MESSAGE_LIMIT;
	ctx->remote_port = DATACHANNEL_SCTP_PORT;
	ctx->errorh = errorh;
	ctx->arg = arg;

	ctx->bundled = bundle_base != NULL;
	if (bundle_base) {
		ctx->bundle_base = mem_ref(bundle_base);
		ctx->sock = mem_ref(rtp_sock(stream_rtp_sock(bundle_base)));
		ctx->transport_sdpm =
			mem_ref(stream_sdpmedia(bundle_base));
		err = udp_local_get(ctx->sock, &laddr);
	}
	else {
		sa_init(&laddr, af);
		err = udp_listen(&ctx->sock, &laddr, NULL, NULL);
		if (!err)
			err = udp_local_get(ctx->sock, &laddr);
	}
	if (!err)
		err = sdp_media_add(&ctx->sdpm, sdp, "application",
				    sa_port(&laddr), "UDP/DTLS/SCTP");
	if (!err)
		err = sdp_format_add(NULL, ctx->sdpm, false,
				     "webrtc-datachannel", NULL, 0, 0,
				     NULL, NULL, NULL, false, NULL);
	if (!err)
		sdp_media_set_ldir(ctx->sdpm, SDP_SENDRECV);

	for (mid = 0; local_mid_used(sdp, streaml, mid); ++mid)
		;
	if (!err)
		err = sdp_media_set_lattr(ctx->sdpm, true, "mid", "%u", mid);
	if (!err)
		err = re_sdprintf(&ctx->mid, "%u", mid);
	if (!err)
		err = sdp_media_set_lattr(ctx->sdpm, true, "sctp-port", "%u",
					  DATACHANNEL_SCTP_PORT);
	if (!err)
		err = sdp_media_set_lattr(ctx->sdpm, true,
					  "max-message-size", "%u",
					  DATACHANNEL_MESSAGE_LIMIT);
	if (!err && !ctx->transport_sdpm)
		ctx->transport_sdpm = mem_ref(ctx->sdpm);
	if (!err && !ctx->bundled) {
		binding = transport_binding_alloc(
			ctx, ++ctx->next_generation);
		if (!binding)
			err = ENOMEM;
		else
			ctx->transport_generation = binding->generation;
	}
	if (!err && mnat->mediah && !ctx->bundled)
		err = mnat->mediah(&ctx->mnats, mnats, ctx->sock, NULL,
				   ctx->sdpm, mnat_connected_handler, binding);
	if (!err && offerer)
		err = binding ? transport_alloc_bound(ctx, binding)
			      : transport_alloc(ctx);

	if (err) {
		if (binding)
			mem_deref(binding);
		mem_deref(ctx);
	}
	else
		*ctxp = ctx;

	return err;
}

struct bundle_print {
	const struct data_context *ctx;
	const struct list *streaml;
};

struct bundle_attr_copy {
	struct sdp_media *media;
	const char *name;
	int err;
	bool copied;
};


static bool bundle_attr_copy_handler(const char *name, const char *value,
				     void *arg)
{
	struct bundle_attr_copy *copy = arg;
	(void)name;

	copy->err = sdp_media_set_lattr(copy->media, false, copy->name,
					"%s", value);
	copy->copied = !copy->err;
	return copy->err != 0;
}


static int copy_transport_attr(struct data_context *ctx,
			       struct sdp_media *media,
			       struct sdp_media *source, const char *name)
{
	struct bundle_attr_copy copy = {
		.media = media,
		.name = name,
	};

	if (source == media)
		return 0;

	sdp_media_del_lattr(media, name);
	(void)sdp_media_lattr_apply(source, name,
				    bundle_attr_copy_handler, &copy);
	if (!copy.err && !copy.copied)
		(void)sdp_session_lattr_apply(ctx->sdp, name,
					      bundle_attr_copy_handler, &copy);
	return copy.err;
}


static int copy_bundle_attr(struct data_context *ctx,
			    struct sdp_media *source, const char *name)
{
	return copy_transport_attr(ctx, ctx->sdpm, source, name);
}


static int print_bundle_mids(struct re_printf *pf, void *arg)
{
	const struct bundle_print *print = arg;
	const struct le *le;
	int err = 0;

	if (print->ctx->bundle_data_first)
		err |= re_hprintf(pf, " %s", print->ctx->mid);

	for (le = list_head(print->streaml); le; le = le->next) {
		const struct stream *stream = le->data;
		const char *mid = stream_mid(stream);

		if (mid && str_cmp(mid, print->ctx->mid))
			err |= re_hprintf(pf, " %s", mid);
	}

	if (!print->ctx->bundle_data_first)
		err |= re_hprintf(pf, " %s", print->ctx->mid);
	return err;
}

static int encode_negotiated_channels(struct data_context *ctx)
{
	struct le *le;
	int err = 0;

	sdp_media_del_lattr(ctx->sdpm, "dcmap");
	if (ctx->rejected)
		return 0;

	for (le = ctx->channels.head; le; le = le->next) {
		struct data_channel *dc = le->data;
		struct dcmap map;

		if (!dc->config.negotiated ||
		    dc->sdp_remove_pending ||
		    dc->state == DATACHANNEL_CLOSING ||
		    dc->state == DATACHANNEL_CLOSED)
			continue;

		memset(&map, 0, sizeof(map));
		map.id = dc->config.id;
		map.label = dc->label;
		map.protocol = dc->protocol;
		map.ordered = dc->config.ordered;
		map.max_retransmits = dc->config.max_retransmits;
		map.max_packet_lifetime =
			dc->config.max_packet_lifetime;
		map.priority = dc->priority;
		err = sdp_media_set_lattr(ctx->sdpm, false, "dcmap",
					  "%H", dcmap_print, &map);
		if (err)
			break;
		dc->sdp_offered = true;
	}

	return err;
}

int data_context_bundle_encode(struct data_context *ctx,
			       const struct list *streaml)
{
	static const char *transport_attrs[] = {
		"setup",
		"fingerprint",
		"tls-id",
		"ice-ufrag",
		"ice-pwd",
		"candidate",
		"end-of-candidates",
	};
	struct bundle_print print = {
		.ctx = ctx,
		.streaml = streaml,
	};
	const struct sa *wire_laddr;
	int err;

	if (!ctx)
		return EINVAL;
	if (ctx->pending_binding) {
		if (ctx->pending_binding->local_restart &&
		    !ctx->pending_binding->gathered)
			return ctx->pending_binding->gather_err
				       ? ctx->pending_binding->gather_err : EAGAIN;
		if (ctx->pending_binding->local_restart &&
		    !ctx->description_applying)
			return EBUSY;
		err = transport_binding_apply_sdp(
			ctx->pending_binding);
		if (err)
			return err;
	}

	err = encode_negotiated_channels(ctx);
	if (err || !streaml)
		return err;
	if (!ctx->bundled) {
		if (ctx->remote_bundles && !ctx->local_committed)
			return bundle_set_encode(ctx->sdp, ctx->remote_bundles);
		return list_isempty(streaml)
			       ? 0
			       : bundle_sdp_encode(ctx->sdp, streaml);
	}
	if (ctx->rejected)
		return bundle_sdp_encode(ctx->sdp, streaml);
	{
		const struct sdp_media *wire_media =
			ctx->bundle_base
				? stream_sdpmedia(ctx->bundle_base)
				: ctx->transport_sdpm;
		wire_laddr =
			sdp_media_laddr(wire_media);

		sdp_media_set_laddr(ctx->sdpm, wire_laddr);
		sdp_media_set_lport(ctx->sdpm, sa_port(wire_laddr));
	}
	for (size_t i = 0; i < RE_ARRAY_SIZE(transport_attrs); ++i) {
		struct sdp_media *source =
			i < 3 || !ctx->bundle_base
				? ctx->transport_sdpm
				: stream_sdpmedia(ctx->bundle_base);

		err = copy_bundle_attr(ctx, source, transport_attrs[i]);
		if (err)
			return err;
		for (struct le *le = streaml->head; le; le = le->next) {
			struct stream *stream = le->data;
			const struct bundle_group *group =
				ctx->remote_bundles
					? bundle_set_find_mid(
						  ctx->remote_bundles,
						  ctx->mid)
					: NULL;

				if (group &&
				    !bundle_group_contains(group, stream_mid(stream)))
					continue;
				sdp_media_set_laddr(
					stream_sdpmedia(stream),
					wire_laddr);
				sdp_media_set_lport(
					stream_sdpmedia(stream),
					sa_port(wire_laddr));
				err = copy_transport_attr(
				ctx, stream_sdpmedia(stream), source,
				transport_attrs[i]);
			if (err)
				return err;
		}
	}
	err = bundle_sdp_encode(ctx->sdp, streaml);
	if (err)
		return err;

	if (ctx->remote_bundles && !ctx->local_committed)
		return bundle_set_encode(ctx->sdp, ctx->remote_bundles);

	return sdp_session_set_lattr(ctx->sdp, true, "group", "BUNDLE%H",
				     print_bundle_mids, &print);
}


int data_context_add_stream(struct data_context *ctx, struct stream *stream)
{
	struct sa laddr;
	uint32_t mid;
	int err;

	if (!ctx || !stream)
		return EINVAL;
	if (!str_cmp(stream_mid(stream), ctx->mid)) {
		char value[16];

		for (mid = 0; local_mid_used(ctx->sdp, ctx->streaml, mid);
		     ++mid)
			;
		re_snprintf(value, sizeof(value), "%u", mid);
		err = stream_set_mid(stream, value);
		if (err)
			return err;
	}
	if (ctx->bundled) {
		if (ctx->transport)
			stream_set_menc_transport(stream, ctx->transport);
		return 0;
	}
	if (ctx->remote_identity || ctx->transport_started ||
	    ctx->remote_accepted || ctx->local_committed)
		return EPROTO;

	{
		struct transport_binding *binding =
			active_transport_binding(ctx);

		if (binding)
			binding->dc = mem_deref(binding->dc);
	}
	ctx->transport_generation = 0;
	ctx->transport = mem_deref(ctx->transport);
	ctx->mnats = mem_deref(ctx->mnats);
	ctx->sock = mem_deref(ctx->sock);
	ctx->transport_sdpm = mem_deref(ctx->transport_sdpm);
	ctx->bundle_base = mem_ref(stream);
	ctx->sock = mem_ref(rtp_sock(stream_rtp_sock(stream)));
	ctx->transport_sdpm = mem_ref(stream_sdpmedia(stream));
	ctx->bundled = true;
	err = udp_local_get(ctx->sock, &laddr);
	if (!err) {
		sdp_media_set_laddr(ctx->sdpm, &laddr);
		sdp_media_set_lport(ctx->sdpm, sa_port(&laddr));
	}
	if (!err && ctx->offerer)
		err = transport_alloc(ctx);

	return err;
}


int data_context_set_handler(struct data_context *ctx,
		     peerconnection_datachannel_h *channelh, void *arg)
{
	if (!ctx)
		return EINVAL;

	ctx->channelh = channelh;
	ctx->channel_arg = arg;
	return 0;
}


void data_context_set_dispatch_refs(struct data_context *ctx,
				    data_context_dispatch_refs_h *refsh,
				    void *arg)
{
	if (!ctx)
		return;

	ctx->dispatch_refsh = refsh;
	ctx->dispatch_refs_arg = arg;
}


int data_context_channel_validate(const char *label,
				  const struct data_channel_config *cfg,
				  struct data_channel **dcp)
{
	if (!label || !dcp)
		return EINVAL;
	if (!cfg)
		return 0;
	if (cfg->max_retransmits < -1 || cfg->max_packet_lifetime < -1 ||
	    (cfg->max_retransmits >= 0 && cfg->max_packet_lifetime >= 0))
		return EINVAL;
	if (cfg->negotiated && cfg->id >= DATACHANNEL_STREAMS)
		return EINVAL;

	return 0;
}


static int channel_create(struct data_context *ctx, const char *label,
			  const struct data_channel_config *cfg,
			  uint16_t priority, struct data_channel **dcp)
{
	struct data_channel_config defaults = {
		.ordered = true,
		.max_retransmits = -1,
		.max_packet_lifetime = -1,
	};
	struct data_channel *dc;
	int err;

	if (!ctx)
		return EINVAL;
	if (!cfg)
		cfg = &defaults;
	err = data_context_channel_validate(label, cfg, dcp);
	if (err)
		return err;
	if (cfg->negotiated && channel_lookup_id(ctx, cfg->id))
		return EADDRINUSE;

	dc = mem_zalloc(sizeof(*dc), channel_destructor);
	if (!dc)
		return ENOMEM;

	dc->ctx = ctx;
	dc->config = *cfg;
	dc->priority = priority;
	dc->state = DATACHANNEL_CONNECTING;
	dc->id = cfg->negotiated ? cfg->id : -1;
	err = str_dup(&dc->label, label);
	if (!err)
		err = str_dup(&dc->protocol,
			      cfg->protocol ? cfg->protocol : "");
	if (!err)
		list_append(&ctx->channels, &dc->le, dc);
	if (!err && !ctx->description_applying &&
	    active_transport_binding(ctx) &&
	    active_transport_binding(ctx)->dc)
		err = channel_bind(dc, active_transport_binding(ctx));
	if (err) {
		mem_deref(dc);
		return err;
	}

	*dcp = dc;
	return 0;
}

int data_context_channel_create(struct data_context *ctx, const char *label,
				const struct data_channel_config *cfg,
				struct data_channel **dcp)
{
	if (ctx && ctx->rejected)
		return ENOTSUP;

	return channel_create(ctx, label, cfg, 256, dcp);
}


int data_context_set_rejected(struct data_context *ctx, bool rejected)
{
	struct sa laddr;
	int err;

	if (!ctx)
		return EINVAL;
	if (rejected) {
		int close_err = 0;

		ctx->rejected = true;
		ctx->remote_accepted = false;
		ctx->local_committed = false;
		sdp_media_set_lport(ctx->sdpm, 0);
		if (ctx->description_applying)
			return 0;

		for (;;) {
			struct data_channel *dc = NULL;

			for (struct le *le = ctx->channels.head; le;
			     le = le->next) {
				struct data_channel *candidate = le->data;

				if (candidate->state != DATACHANNEL_CLOSING &&
				    candidate->state != DATACHANNEL_CLOSED) {
					dc = mem_ref(candidate);
					break;
				}
			}
			if (!dc)
				break;
			err = datachannel_close(dc);
			mem_deref(dc);
			if (err && !close_err)
				close_err = err;
		}
		{
			struct transport_binding *binding =
				active_transport_binding(ctx);

			if (binding && binding->dc) {
				if (binding->dc_token)
					binding->dc_token->suppress_transport_error =
						true;
				err = dc_transport_close(binding->dc, 0);
				if (err && !close_err)
					close_err = err;
				binding->dc = mem_deref(binding->dc);
				binding->dc_token =
					mem_deref(binding->dc_token);
			}
		}
		return close_err;
	}

	err = udp_local_get(ctx->sock, &laddr);
	if (err)
		return err;
	ctx->rejected = false;
	sdp_media_set_laddr(ctx->sdpm, &laddr);
	sdp_media_set_lport(ctx->sdpm, sa_port(&laddr));
	return 0;
}

struct remote_dcmap {
	struct le le;
	struct dcmap map;
};

struct remote_dcmaps {
	struct list maps;
	int err;
};

static void remote_dcmap_destructor(void *arg)
{
	struct remote_dcmap *entry = arg;

	list_unlink(&entry->le);
	dcmap_reset(&entry->map);
}

static struct remote_dcmap *remote_dcmap_find(const struct list *maps,
					      uint16_t id)
{
	struct le *le;

	for (le = maps->head; le; le = le->next) {
		struct remote_dcmap *entry = le->data;

		if (entry->map.id == id)
			return entry;
	}
	return NULL;
}

static bool remote_dcmap_handler(const char *name, const char *value,
				 void *arg)
{
	struct remote_dcmaps *remote = arg;
	struct remote_dcmap *entry;
	(void)name;

	entry = mem_zalloc(sizeof(*entry), remote_dcmap_destructor);
	if (!entry) {
		remote->err = ENOMEM;
		return true;
	}
	remote->err = dcmap_decode(&entry->map, value);
	if (!remote->err &&
	    remote_dcmap_find(&remote->maps, entry->map.id))
		remote->err = EPROTO;
	if (remote->err) {
		mem_deref(entry);
		return true;
	}
	list_append(&remote->maps, &entry->le, entry);
	return false;
}

static bool remote_dcsa_handler(const char *name, const char *value,
				void *arg)
{
	struct remote_dcmaps *remote = arg;
	struct pl attribute;
	uint16_t id;
	(void)name;

	remote->err = dcsa_decode(&id, &attribute, value);
	if (remote->err)
		return true;

	/*
	 * Unknown and unsupported subprotocol attributes are intentionally
	 * ignored. Syntax is checked before they cross the trust boundary.
	 */
	(void)remote_dcmap_find(&remote->maps, id);
	(void)attribute;
	return false;
}

static bool channel_matches_map(const struct data_channel *dc,
				const struct dcmap *map)
{
	return dc->config.negotiated &&
		dc->config.id == map->id &&
		dc->config.ordered == map->ordered &&
		dc->config.max_retransmits == map->max_retransmits &&
		dc->config.max_packet_lifetime ==
			map->max_packet_lifetime &&
		dc->priority == map->priority &&
		!str_cmp(dc->label, map->label) &&
			!str_cmp(dc->protocol, map->protocol);
}


static int validate_remote_dcmaps(struct data_context *ctx,
				  const struct remote_dcmaps *remote)
{
	struct le *le;

	for (le = remote->maps.head; le; le = le->next) {
		const struct remote_dcmap *entry = le->data;
		const struct dcmap *map = &entry->map;
		const struct data_channel *dc = channel_lookup_id(ctx, map->id);

		if (dc && !channel_matches_map(dc, map))
			return EPROTO;
	}

	return 0;
}


static void rollback_staged_channels(struct data_context *ctx)
{
	struct le *le = ctx->channels.head;

	while (le) {
		struct data_channel *dc = le->data;
		struct le *next = le->next;

		dc->sdp_remove_pending = false;
		if (dc->sdp_staged || dc->sdp_provisional) {
			if (dc->dc)
				(void)dc_channel_close(dc->dc);
			mem_deref(dc);
		}
		le = next;
	}
}


static int apply_remote_dcmaps(struct data_context *ctx,
			       struct remote_dcmaps *remote, bool remove_missing)
{
	struct le *le;
	int err;

	for (le = ctx->channels.head; le; le = le->next) {
		struct data_channel *dc = le->data;

		if (dc->config.negotiated)
			dc->sdp_seen = false;
	}

	for (le = remote->maps.head; le; le = le->next) {
		struct remote_dcmap *entry = le->data;
		struct dcmap *map = &entry->map;
		struct data_channel *dc = channel_lookup_id(ctx, map->id);

		if (dc) {
			dc->sdp_seen = true;
			continue;
		}

		struct data_channel_config config = {
			.ordered = map->ordered,
			.max_retransmits = map->max_retransmits,
			.max_packet_lifetime = map->max_packet_lifetime,
			.protocol = map->protocol,
			.negotiated = true,
			.id = map->id,
		};
		err = channel_create(ctx, map->label, &config, map->priority,
				     &dc);
		if (err)
			goto out;
		dc->sdp_remote = true;
		dc->sdp_seen = true;
		dc->sdp_staged = true;
	}

	if (!remove_missing)
		return 0;

	for (;;) {
		struct data_channel *dc = NULL;

		for (le = ctx->channels.head; le; le = le->next) {
			struct data_channel *candidate = le->data;

			if (candidate->config.negotiated &&
			    !candidate->sdp_seen &&
			    !candidate->sdp_remove_pending &&
			    (candidate->sdp_remote ||
			     candidate->sdp_offered)) {
				dc = mem_ref(candidate);
				break;
			}
		}
		if (!dc)
			break;

		dc->sdp_remove_pending = true;
		mem_deref(dc);
	}

	return 0;

out:
	rollback_staged_channels(ctx);
	return err;
}


void data_context_notify_channels(struct data_context *ctx, bool stable)
{
	uint64_t generation;

	if (!ctx)
		return;
	/* A committed remote offer is still provisional until answer or
	 * rollback.  Keep all channel callbacks behind the publication barrier. */
	if (!stable && ctx->pending_state)
		return;

	mem_ref(ctx);
	generation = ++ctx->dispatch_generation;
	ctx->callbacks_deferred = false;
	/* A coordinator-owned pending consumer could not create SCTP while the
	 * description publication barrier was active.  Re-enter the session gate
	 * now that prepare may allocate and start the association. */
	if (stable && ctx->pending_binding &&
	    ctx->pending_binding->session_owned) {
		int prepare_err = data_context_media_binding_prepare(
			ctx->pending_binding, ctx->pending_binding->role);

		if (prepare_err && prepare_err != EAGAIN)
			ctx->deferred_error = prepare_err;
		else if (!prepare_err && ctx->transport_readyh)
			ctx->transport_readyh(ctx->transport_ready_arg);
	}
	if (ctx->deferred_error) {
		struct le *le;

		/* A replacement may have promoted a staged channel between the
		 * terminal receive failure and stable publication.  The association
		 * failure wins: publish CLOSED for every now-active wrapper before the
		 * context error can destroy the peer connection. */
		for (le = ctx->channels.head; le; le = le->next) {
			struct data_channel *dc = le->data;

			if (!dc->dc && dc->pending_dc)
				continue;
			if (dc->state == DATACHANNEL_CLOSED &&
			    !dc->dispatch_state)
				continue;
			dc->state = DATACHANNEL_CLOSED;
			dc->dispatch_state = true;
			dc->dispatch_state_err = ctx->deferred_error;
			dc->dispatch_state_value = DATACHANNEL_CLOSED;
		}
	}
	{
		struct transport_binding *binding =
			active_transport_binding(ctx);

		if (stable && binding)
			(void)try_start_sctp(binding);
	}
	if (!dispatch_promoted_channels(ctx) ||
	    ctx->dispatch_generation != generation)
		goto out;
	if (ctx->deferred_error) {
		int err = ctx->deferred_error;

		ctx->deferred_error = 0;
		if (ctx->errorh) {
			uint32_t refs = dispatch_owner_refs(ctx);

			ctx->errorh(err, ctx->arg);
			(void)dispatch_callback_complete(
				ctx, refs, generation);
		}
		/* A terminal transport error ends this publication pass.  In
		 * particular, do not expose staged channels/removals/rejection after
		 * CLOSED or error callbacks may have destroyed their owner. */
		goto out;
	}
	if (!stable || ctx->closing)
		goto out;
	while (!ctx->closing) {
		struct data_channel *dc = NULL;
		struct le *le;

		for (le = ctx->channels.head; le; le = le->next) {
			struct data_channel *candidate = le->data;

			if (candidate->sdp_staged) {
				dc = mem_ref(candidate);
				candidate->sdp_staged = false;
				candidate->sdp_provisional = false;
				break;
			}
		}
		if (!dc)
			break;

		if (!dc->dc) {
			struct transport_binding *binding =
				active_transport_binding(ctx);
			int err = binding && binding->dc
					? channel_bind(dc, binding) : EAGAIN;

			if (err != EAGAIN)
				data_context_report_error(ctx, err);
		}
		if (dc->ctx && !notify_incoming_channel(ctx, dc)) {
			mem_deref(dc);
			goto out;
		}
		mem_deref(dc);
	}
	if (!ctx->closing) {
		struct le *le;

		for (le = ctx->channels.head; le; le = le->next) {
			struct data_channel *dc = le->data;

			dc->sdp_provisional = false;
		}
		while (!ctx->closing) {
			struct data_channel *dc = NULL;

			for (le = ctx->channels.head; le; le = le->next) {
				struct data_channel *candidate = le->data;

				if (candidate->sdp_remove_pending) {
					dc = mem_ref(candidate);
					break;
				}
			}
			if (!dc)
				break;
			uint32_t refs = dispatch_owner_refs(ctx);

			dc->sdp_remove_pending = false;
			dc->sdp_remote = false;
			dc->sdp_offered = false;
			(void)datachannel_close(dc);
			mem_deref(dc);
			if (!dispatch_callback_complete(
				    ctx, refs, generation))
				goto out;
		}
	}
	if (!ctx->closing && ctx->rejection_pending) {
		uint32_t refs = dispatch_owner_refs(ctx);

		ctx->rejection_pending = false;
		(void)data_context_set_rejected(ctx, true);
		if (!dispatch_callback_complete(ctx, refs, generation))
			goto out;
	}
out:
	mem_deref(ctx);
}


struct bundle_attr_forward {
	struct data_context *ctx;
};


static bool bundle_attr_forward_handler(const char *name, const char *value,
					void *arg)
{
	struct bundle_attr_forward *forward = arg;

	stream_mnat_attr(forward->ctx->bundle_base, name, value);
	return false;
}


int data_context_remote_update(struct data_context *ctx, bool remote_offer)
{
	const struct sdp_format *format;
	const char *remote_mid;
	const char *sctp_port;
	char *mid = NULL;
	size_t remote_limit;
	struct remote_dcmaps remote = {0};
	char *identity = NULL;
	char *ice_identity = NULL;
	struct bundle_set *remote_bundles = NULL;
	struct bundle_set *old_bundles = NULL;
	struct sdp_media_lattr_state *local_attrs = NULL;
	const struct bundle_group *data_group = NULL;
	struct sdp_media *selected_transport;
	struct stream *tag_stream = NULL;
	struct stream *old_bundle_base = NULL;
	struct udp_sock *old_sock = NULL;
	struct sa local_laddr;
	uint16_t port;
	bool bundle_data_first = false;
	bool dtls_identity_changed;
	bool ice_identity_changed;
	bool topology_changed;
	bool lower_ownership_changed;
	bool coordinator_media_tag;
	bool negotiated_bundled;
	bool old_bundled = false;
	int err = 0;

	if (!ctx)
		return EINVAL;
	mem_ref(ctx);
	err = sdp_media_save_lattrs(&local_attrs, ctx->sdpm);
	if (err)
		goto out;
	sa_cpy(&local_laddr, sdp_media_laddr(ctx->sdpm));
	if (remote_offer)
		ctx->local_committed = false;
	if (!sdp_media_rport(ctx->sdpm) &&
	    !(ctx->bundled && sdp_media_rattr(ctx->sdpm, "bundle-only"))) {
		err = data_context_set_rejected(ctx, true);
		goto out;
	}
	remote_mid = sdp_media_rattr(ctx->sdpm, "mid");
	if (!str_isset(remote_mid)) {
		err = EPROTO;
		goto out;
	}
	if (remote_offer) {
		err = str_dup(&mid, remote_mid);
		if (err)
			goto out;
	}
	else if (str_cmp(ctx->mid, remote_mid)) {
		err = EPROTO;
		goto out;
	}
	err = bundle_set_decode(&remote_bundles, ctx->sdp);
	if (err)
		goto out;
	if (remote_bundles) {
		err = bundle_set_validate(
			remote_bundles, ctx->streaml, remote_mid);
		if (err) {
			warning("datachannel: invalid BUNDLE MID topology (%m)\n",
				err);
			goto out;
		}
		data_group = bundle_set_find_mid(
			remote_bundles, remote_mid);
	}
	/*
	 * A singleton BUNDLE group has no shared transport. Keep the
	 * application m-line on its ordinary data transport while preserving
	 * the exact group for signaling.
	 */
	negotiated_bundled = data_group &&
		bundle_group_count(data_group) > 1;
	if (negotiated_bundled) {
		const char *tag;

		tag = bundle_group_tag(data_group);
		bundle_data_first = !str_cmp(tag, remote_mid);
		if (!bundle_data_first) {
			tag_stream = stream_lookup_remote_mid(ctx->streaml, tag);
			if (!tag_stream) {
				err = EPROTO;
				goto out;
			}
		}
	}
	selected_transport = negotiated_bundled && bundle_data_first
				   ? ctx->sdpm
				   : negotiated_bundled
					     ? stream_sdpmedia(tag_stream)
					     : ctx->sdpm;
	if (negotiated_bundled &&
	    (transport_attr_conflicts(ctx, selected_transport, data_group,
				     "setup", false) ||
	    transport_attr_conflicts(ctx, selected_transport,
				     data_group, "fingerprint", false) ||
	    transport_attr_conflicts(ctx, selected_transport, data_group,
				     "tls-id", true) ||
	    (bundle_data_first &&
	     (transport_attr_conflicts(ctx, selected_transport, data_group,
				      "ice-ufrag", true) ||
	      transport_attr_conflicts(ctx, selected_transport, data_group,
				      "ice-pwd", true) ||
	      transport_attr_conflicts(ctx, selected_transport, data_group,
				      "candidate", true) ||
	      transport_attr_conflicts(ctx, selected_transport, data_group,
				      "end-of-candidates", true))))) {
		err = EPROTO;
		warning("datachannel: conflicting BUNDLE transport"
			" attributes\n");
		goto out;
	}

	if (0 != str_casecmp(sdp_media_proto(ctx->sdpm), "UDP/DTLS/SCTP")) {
		warning("datachannel: unsupported remote protocol '%s'\n",
			sdp_media_proto(ctx->sdpm));
		err = EPROTONOSUPPORT;
		goto out;
	}
	format = sdp_media_format(ctx->sdpm, false, "webrtc-datachannel",
				  -1, NULL, -1, -1);
	if (!format) {
		warning("datachannel: missing webrtc-datachannel format\n");
		err = EPROTONOSUPPORT;
		goto out;
	}

	sctp_port = sdp_media_rattr(ctx->sdpm, "sctp-port");
	err = parse_sctp_port(sctp_port, &port);
	if (err)
		goto out;
	err = parse_remote_limit(ctx, &remote_limit);
	if (err)
		goto out;

	err = remote_transport_identity(&identity, ctx, selected_transport);
	if (err) {
		warning("datachannel: invalid remote DTLS identity (%m)\n",
			err);
		goto out;
	}
	err = remote_ice_identity(&ice_identity, ctx, selected_transport);
	if (err) {
		warning("datachannel: invalid remote ICE identity (%m)\n", err);
		goto out;
	}

	(void)sdp_media_rattr_apply(ctx->sdpm, "dcmap",
				    remote_dcmap_handler, &remote);
	if (!remote.err)
		(void)sdp_media_rattr_apply(ctx->sdpm, "dcsa",
					    remote_dcsa_handler, &remote);
	if (!remote.err)
		remote.err = validate_remote_dcmaps(ctx, &remote);
	if (remote.err) {
		err = remote.err;
		goto out;
	}

	dtls_identity_changed = (ctx->remote_identity &&
				 strcmp(ctx->remote_identity, identity)) ||
		remote_dtls_role_changed(ctx, selected_transport);
	ice_identity_changed = ctx->remote_ice_identity &&
		strcmp(ctx->remote_ice_identity, ice_identity);
	if (ctx->remote_identity && dtls_identity_changed &&
	    (!ice_identity_changed ||
	     (!remote_offer && !ctx->offered_ice_restart))) {
		warning("datachannel: DTLS identity changed without ICE restart\n");
		err = EPROTO;
		goto out;
	}
	topology_changed = ctx->bundled != negotiated_bundled ||
		(negotiated_bundled && ctx->active_group &&
		 !bundle_groups_equal(ctx->active_group, data_group));
	old_bundled = ctx->bundled;
	lower_ownership_changed = ctx->bundled != negotiated_bundled;
	if (remote_offer) {
		err = sdp_media_set_lattr(ctx->sdpm, true, "mid", "%s",
					  remote_mid);
		if (err)
			goto out;
	}
	err = apply_remote_dcmaps(ctx, &remote, false);
	if (err)
		goto out;
	if (topology_changed)
		ctx->bundled = negotiated_bundled;
	if (dtls_identity_changed || lower_ownership_changed) {
		err = ctx->pending_binding && !remote_offer
			? 0 : replace_transport(ctx, selected_transport,
					remote_offer ? false : true, false);
		if (err) {
			ctx->bundled = old_bundled;
			rollback_staged_channels(ctx);
			goto out;
		}
		if (ctx->pending_binding->transport_sdpm == ctx->sdpm) {
			ctx->pending_binding->saved_attrs =
				mem_deref(
					ctx->pending_binding->saved_attrs);
			ctx->pending_binding->saved_attrs = local_attrs;
			sa_cpy(&ctx->pending_binding->saved_laddr,
			       &local_laddr);
		}
		else {
			ctx->pending_binding->context_saved_attrs =
				local_attrs;
			sa_cpy(
				&ctx->pending_binding->context_saved_laddr,
				&local_laddr);
		}
		local_attrs = NULL;
	}
	err = apply_remote_dcmaps(ctx, &remote, true);
	if (err)
		goto out;

	sdp_media_set_ldir(ctx->sdpm, SDP_SENDRECV);
	if (sdp_media_rattr(ctx->sdpm, "bundle-only")) {
		sdp_media_set_rdir(ctx->sdpm, SDP_SENDRECV);
		sdp_media_set_rport(ctx->sdpm,
				    sdp_media_rport(ctx->transport_sdpm));
	}
	if (remote_offer) {
		ctx->mid = mem_deref(ctx->mid);
		ctx->mid = mid;
		mid = NULL;
	}
	ctx->bundle_data_first = bundle_data_first;
	if (ctx->transport_sdpm != selected_transport) {
		ctx->transport_sdpm = mem_deref(ctx->transport_sdpm);
		ctx->transport_sdpm = mem_ref(selected_transport);
	}
	if (ctx->pending_binding && ctx->pending_binding->local_restart &&
	    !remote_offer) {
		err = data_context_sync_prepared_transport_remote(ctx);
		if (err)
			goto out;
	}
	if (tag_stream && ctx->bundle_base != tag_stream) {
		old_bundle_base = ctx->bundle_base;
		ctx->bundle_base = mem_ref(tag_stream);
		old_sock = ctx->sock;
		ctx->sock = mem_ref(rtp_sock(stream_rtp_sock(tag_stream)));
	}
	ctx->remote_port = port;
	ctx->send_limit = MIN(remote_limit, (size_t)DATACHANNEL_MESSAGE_LIMIT);
	ctx->remote_identity = mem_deref(ctx->remote_identity);
	ctx->remote_identity = identity;
	identity = NULL;
	ctx->remote_ice_identity = mem_deref(ctx->remote_ice_identity);
	ctx->remote_ice_identity = ice_identity;
	ice_identity = NULL;

	old_bundles = ctx->remote_bundles;
	ctx->remote_bundles = remote_bundles;
	remote_bundles = NULL;
	coordinator_media_tag = ctx->transport_readyh &&
		selected_transport != ctx->sdpm;
	err = ctx->pending_binding || coordinator_media_tag
		      ? 0 : transport_alloc(ctx);
	if (!err && !coordinator_media_tag)
		err = ensure_bundle_transport(
			ctx, negotiated_bundled ? data_group : NULL);
	if (!err && ctx->pending_binding)
		err = stage_transport_member_mappings(ctx->pending_binding);
	if (!err && ctx->bundled && ctx->local_committed &&
	    !ctx->transport_started && ctx->bundle_base) {
		const struct sa *raddr = stream_raddr(ctx->bundle_base);
		struct transport_binding *binding =
			transport_binding_lookup(
				ctx, ctx->transport_generation);

		if (binding && binding->route_generation &&
		    sa_isset(raddr, SA_ALL))
			err = bundle_transport_set_remote(
				ctx->bundle_transport,
				binding->route_generation, raddr);
		if (!err && sa_isset(raddr, SA_ALL)) {
			if (binding)
				err = transport_binding_maybe_ready(binding);
		}
		if (!err && sa_isset(raddr, SA_ALL))
			err = menc_transport_start(
				ctx->menc, ctx->transport, raddr);
		if (!err && sa_isset(raddr, SA_ALL))
			ctx->transport_started = true;
	}
	if (!err) {
		struct transport_binding *binding =
			ctx->pending_binding
				? ctx->pending_binding
				: transport_binding_lookup(
					  ctx,
					  ctx->transport_generation);

		ctx->remote_accepted = true;
		if (binding)
			err = start_transport_binding(binding);
	}
	if (!err && !ctx->pending_binding) {
		struct transport_binding *binding =
			active_transport_binding(ctx);

		if (binding)
			err = try_start_sctp(binding);
	}
	if (err) {
		if (ctx->pending_binding)
			transport_binding_abort(
				ctx->pending_binding, true);
		remote_bundles = ctx->remote_bundles;
		ctx->remote_bundles = old_bundles;
		old_bundles = NULL;
		if (old_bundle_base) {
			ctx->bundle_base = mem_deref(ctx->bundle_base);
			ctx->bundle_base = old_bundle_base;
			old_bundle_base = NULL;
			ctx->sock = mem_deref(ctx->sock);
			ctx->sock = old_sock;
			old_sock = NULL;
		}
		attach_transport_members(ctx, ctx->transport);
	}
	if (!err && ctx->bundled && bundle_data_first && ctx->bundle_base) {
		struct bundle_attr_forward forward = {
			.ctx = ctx,
		};

		(void)sdp_media_rattr_apply(
			ctx->sdpm, NULL, bundle_attr_forward_handler,
			&forward);
	}

out:
	if (err && local_attrs) {
		sdp_media_set_laddr(ctx->sdpm, &local_laddr);
		sdp_media_set_lport(ctx->sdpm,
				    sa_port(&local_laddr));
		sdp_media_restore_lattrs(ctx->sdpm, local_attrs);
		local_attrs = NULL;
	}
	list_flush(&remote.maps);
	mem_deref(local_attrs);
	mem_deref(ice_identity);
	mem_deref(old_bundles);
	mem_deref(remote_bundles);
	mem_deref(old_bundle_base);
	mem_deref(old_sock);
	mem_deref(identity);
	mem_deref(mid);
	mem_deref(ctx);
	return err;
}


int data_context_local_description(struct data_context *ctx, bool offer)
{
	char *ice_identity = NULL;
	int err;

	if (!ctx)
		return EINVAL;

	if (offer) {
		err = local_ice_identity(&ice_identity, ctx,
					 ctx->transport_sdpm);
		if (err)
			return err;
		ctx->offered_ice_restart = ctx->local_ice_identity &&
			strcmp(ctx->local_ice_identity, ice_identity);
		ctx->local_ice_identity = mem_deref(ctx->local_ice_identity);
		ctx->local_ice_identity = ice_identity;
		ctx->remote_accepted = false;
	}
	ctx->local_committed = true;
	/* Publication owns transport activation.  SDP construction and veto
	 * callbacks must remain rollbackable until the signaling boundary. */
	if (ctx->description_applying)
		return 0;
	/* PeerConnection owns publication through its transport-session
	 * coordinator.  Do not start a legacy data lower while the answer is
	 * still being assembled; start_ice() will import and start the exact
	 * planned group, including a newly independent application group. */
	if (ctx->transport_readyh)
		return 0;
	/* A media-tagged BUNDLE group is started by the session transport
	 * coordinator after it adopts the tag stream's committed MENC runtime. */
	if (!ctx->pending_binding && ctx->transport_sdpm != ctx->sdpm)
		return 0;
	err = ctx->remote_accepted ? data_context_start(ctx) : 0;
	if (err || ctx->pending_binding)
		return err;
	return try_start_sctp(active_transport_binding(ctx));
}


static void abort_operation_sctp(struct data_context *ctx)
{
	struct transport_binding *binding = active_transport_binding(ctx);
	struct dc_transport *candidate;

	if (!binding || !binding->candidate_dc)
		return;
	candidate = binding->candidate_dc;
	abort_pending_channels(ctx, binding->generation);
	if (binding->candidate_token)
		binding->candidate_token->role = DC_CALLBACK_RETIRING;
	(void)dc_transport_close(candidate, ECANCELED);
	binding->candidate_dc = NULL;
	binding->candidate_token = mem_deref(binding->candidate_token);
	mem_deref(candidate);
}


void data_context_rollback(struct data_context *ctx)
{
	if (!ctx)
		return;
	ctx->description_applying = false;
	ctx->description_prepared = false;
	abort_operation_sctp(ctx);
	if (ctx->pending_binding)
		transport_binding_abort(ctx->pending_binding, false);
	rollback_staged_channels(ctx);
	if (ctx->operation_state) {
		data_context_state_restore(ctx, ctx->operation_state);
		ctx->operation_state = mem_deref(ctx->operation_state);
	}
	if (ctx->pending_state) {
		data_context_state_restore(ctx, ctx->pending_state);
		ctx->pending_state = mem_deref(ctx->pending_state);
	}
}


void data_context_description_abort(struct data_context *ctx)
{
	if (!ctx)
		return;
	ctx->description_applying = false;
	ctx->description_prepared = false;
	abort_operation_sctp(ctx);
	if (ctx->pending_binding &&
	    (!ctx->operation_state ||
	     ctx->pending_binding->generation !=
		     ctx->operation_state->pending_generation))
		transport_binding_abort(ctx->pending_binding, false);
	if (ctx->operation_state) {
		data_context_state_restore(ctx, ctx->operation_state);
		ctx->operation_state = mem_deref(ctx->operation_state);
	}
}


int data_context_description_begin(struct data_context *ctx)
{
	int err;

	if (!ctx)
		return EINVAL;
	if (ctx->operation_state)
		return EALREADY;
	err = data_context_state_save(&ctx->operation_state, ctx);
	if (!err) {
		ctx->description_applying = true;
		ctx->description_prepared = false;
		ctx->callbacks_deferred = true;
	}
	return err;
}


int data_context_description_prepare(struct data_context *ctx,
				     bool provisional)
{
	if (!ctx || !ctx->operation_state || ctx->description_prepared)
		return EINVAL;
	ctx->description_provisional = provisional;
	ctx->description_prepared = true;
	return 0;
}


void data_context_description_publish(struct data_context *ctx,
				      bool provisional)
{
	if (!ctx || !ctx->operation_state || !ctx->description_prepared ||
	    ctx->description_provisional != provisional)
		return;

	ctx->description_applying = false;
	ctx->description_prepared = false;
	if (provisional) {
		/* Rollback must restore the state preceding the first provisional
		 * response, not merely the most recent 18x update. */
		if (!ctx->pending_state) {
			ctx->pending_state = ctx->operation_state;
			ctx->operation_state = NULL;
		}
		else
			ctx->operation_state = mem_deref(ctx->operation_state);
		return;
	}

	if (ctx->rejected) {
		if (ctx->pending_binding)
			transport_binding_abort(ctx->pending_binding, false);
		ctx->rejection_pending = true;
	}
}


void data_context_description_finalize(struct data_context *ctx,
				       bool provisional)
{
	struct transport_binding *binding;
	struct transport_binding *identity_binding;
	int err = 0;

	if (!ctx || provisional)
		return;
	/* Publication makes the prepared state usable by the transport
	 * coordinator.  PeerConnection retains the saved description until every
	 * synchronous coordinator step succeeds; direct SIP ownership can retire it
	 * immediately. */
	if (!ctx->transport_readyh) {
		ctx->operation_state = mem_deref(ctx->operation_state);
		ctx->pending_state = mem_deref(ctx->pending_state);
	}
	if (ctx->rejected)
		return;
	binding = ctx->pending_binding
			? ctx->pending_binding
			: active_transport_binding(ctx);
	identity_binding = binding;
	if (!ctx->pending_binding && ctx->transport_sdpm != ctx->sdpm)
		identity_binding = NULL;
	/* Everything after publication is deliberately non-rejecting.  Runtime
	 * activation failures are surfaced through the context error callback. */
	if (identity_binding) {
		struct menc_transport *transport =
			transport_binding_transport(identity_binding);

		err = transport
			? menc_transport_commit_identity(ctx->menc, transport)
			: EPROTO;
	}
	/* PeerConnection's session coordinator owns SCTP consumer readiness and
	 * promotion.  Once its generation has published, repeating those operations
	 * here would target an already-finalized pending binding. */
	if (!err && ctx->transport_readyh)
		return;
	if (!err && binding)
		err = transport_binding_maybe_ready(binding);
	if (!err && binding)
		err = start_transport_binding(binding);
	if (!err && binding && binding->candidate_dc) {
		binding->dc = binding->candidate_dc;
		binding->candidate_dc = NULL;
		binding->dc_token = binding->candidate_token;
		binding->candidate_token = NULL;
		binding->dc_token->role = DC_CALLBACK_ACTIVE;
		promote_pending_channels(binding);
	}
	if (err) {
		warning("datachannel: published transport activation failed (%m)\n",
			err);
		data_context_report_error(ctx, err);
	}
}


void data_context_description_retire(struct data_context *ctx)
{
	if (!ctx || ctx->description_applying || ctx->description_prepared)
		return;
	ctx->operation_state = mem_deref(ctx->operation_state);
	ctx->pending_state = mem_deref(ctx->pending_state);
}


int data_context_description_commit(struct data_context *ctx,
				    bool provisional)
{
	int err = data_context_description_prepare(ctx, provisional);

	if (err)
		return err;
	data_context_description_publish(ctx, provisional);
	data_context_description_finalize(ctx, provisional);
	return 0;
}


bool data_context_sctp_started(const struct data_context *ctx)
{
	{
		struct transport_binding *binding =
			active_transport_binding(ctx);

		return binding && binding->dc;
	}
}


int data_context_start(struct data_context *ctx)
{
	const struct sa *raddr;
	int err;

	if (!ctx)
		return EINVAL;
	if (ctx->rejected)
		return 0;
	if (ctx->pending_binding)
		return start_transport_binding(ctx->pending_binding);
	/* A media-tagged group adopts the tag stream's exact established lower
	 * through pc_transport_session; allocating a parallel DTLS transport on
	 * the same socket here would have no committed identity. */
	if (ctx->transport_readyh && ctx->transport_sdpm != ctx->sdpm)
		return 0;
	if (ctx->transport_started ||
	    (!ctx->bundled && ctx->mnat->wait_connected && ctx->mnats))
		return 0;

	raddr = ctx->bundled && ctx->bundle_base
		      ? stream_raddr(ctx->bundle_base)
		      : sdp_media_raddr(ctx->transport_sdpm);
	if (!sa_isset(raddr, SA_ALL))
		return 0;
	err = transport_alloc(ctx);
	if (!err)
		err = menc_transport_commit_identity(ctx->menc, ctx->transport);
	if (!err && ctx->bundle_transport) {
		struct transport_binding *binding =
			transport_binding_lookup(
				ctx, ctx->transport_generation);

		if (binding && binding->route_generation)
			err = bundle_transport_set_remote(
				ctx->bundle_transport,
				binding->route_generation, raddr);
		if (!err && binding)
			err = transport_binding_maybe_ready(binding);
	}
	if (!err)
		err = menc_transport_start(ctx->menc, ctx->transport, raddr);
	if (!err)
		ctx->transport_started = true;
	return err;
}

bool data_context_mnat_attr(struct data_context *ctx, const char *mid,
			    const char *name, const char *value)
{
	if (!ctx || str_cmp(ctx->mid, mid))
		return false;

	if (ctx->bundled && ctx->bundle_base && !ctx->bundle_data_first) {
		stream_mnat_attr(ctx->bundle_base, name, value);
		return true;
	}
	if (ctx->mnats && ctx->mnat->attrh)
		ctx->mnat->attrh(ctx->mnats, name, value);
	return true;
}


const char *data_context_mid(const struct data_context *ctx)
{
	return ctx ? ctx->mid : NULL;
}


struct sdp_media *data_context_sdpmedia(const struct data_context *ctx)
{
	return ctx ? ctx->sdpm : NULL;
}


bool data_context_transport_accepted(const struct data_context *ctx)
{
	return ctx && !ctx->rejected && ctx->remote_accepted;
}


uint16_t data_context_local_sctp_port(const struct data_context *ctx)
{
	return ctx ? DATACHANNEL_SCTP_PORT : 0;
}


uint16_t data_context_remote_sctp_port(const struct data_context *ctx)
{
	return ctx ? ctx->remote_port : 0;
}


const void *data_context_socket_identity(const struct data_context *ctx)
{
	return ctx ? ctx->sock : NULL;
}


struct menc_transport *data_context_menc_transport_ref(
	const struct data_context *ctx)
{
	return ctx ? mem_ref(ctx->transport) : NULL;
}


struct mnat_media *data_context_mnat_media_ref(
	const struct data_context *ctx)
{
	return ctx ? mem_ref(ctx->mnats) : NULL;
}


struct bundle_transport *data_context_bundle_transport_ref(
	const struct data_context *ctx)
{
	return ctx ? mem_ref(ctx->bundle_transport) : NULL;
}
