/**
 * @file bundle.c Media Multiplexing Using SDP (BUNDLE)
 *
 * Copyright (C) 2020 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static const char uri_mid[] = "urn:ietf:params:rtp-hdrext:sdes:mid";


struct bundle {
	struct udp_helper *uh;
	struct udp_sock *app_sock;
	struct bundle_endpoint *endpoint;
	struct bundle_transport *transport;
	struct list *streaml;
	const struct stream *stream;
	mtx_t lock;
	enum bundle_state state;
	uint8_t extmap_mid;         /* Range 1-14  */
	RE_ATOMIC bool stopped;
	bool initialized;
};

struct bundle_endpoint {
	struct bundle *bundle;
	struct bundle_publication *publication;
	mtx_t lock;
	bool initialized;
};

struct bundle_publication {
	mtx_t lock;
	bool initialized;
};

struct bundle_mid {
	struct le le;
	char *value;
};

struct bundle_group {
	struct le le;
	struct list mids;
};

struct bundle_set {
	struct list groups;
};

struct bundle_wire {
	struct le le;
	struct list dispositions;
	struct udp_helper *uh;
	struct udp_sock *sock;
	struct bundle_transport *transport;
	const struct bundle_group *group;
	struct sa remote;
	uint64_t generation;
	bool remote_set;
	bool private_helper;
};

struct bundle_disposition {
	struct le le;
	struct bundle_endpoint *endpoint;
	struct bundle_transport *previous_transport;
	enum bundle_state state;
	bool exact_member;
	bool attachment_changed;
};

struct bundle_endpoint_ref {
	struct le le;
	struct bundle_endpoint *endpoint;
};

struct bundle_transport {
	struct list retired;
	struct bundle_wire *active;
	struct bundle_wire *pending;
	struct bundle_wire *previous;
	struct list endpoints;
	struct bundle_publication *publication;
	uint64_t next_route_generation;
	mtx_t lock;
	bool initialized;
};


struct attr_copy {
	struct sdp_media *media;
	const char *name;
	int err;
	bool copied;
};


static void bundle_transport_detach_endpoint(
	struct bundle_transport *transport, const struct bundle *bun);
static bool stream_in_group(const struct bundle_group *group,
			    const struct stream *stream);
static const char *stream_remote_mid(const struct stream *stream);


static void destructor(void *data)
{
	struct bundle *bun = data;
	struct bundle_transport *transport = NULL;
	int err;

	/* Prevent a helper retained by an in-flight self-callback from entering
	 * packet routing again before its deferred final dereference unlinks it. */
	re_atomic_rls_set(&bun->stopped, true);
	if (bun->endpoint && bun->endpoint->initialized) {
		mtx_lock(&bun->endpoint->lock);
		bun->endpoint->bundle = NULL;
		mtx_unlock(&bun->endpoint->lock);
	}
	if (bun->initialized) {
		mtx_lock(&bun->lock);
		transport = bun->transport;
		bun->transport = NULL;
		mtx_unlock(&bun->lock);
	}
	if (transport)
		bundle_transport_detach_endpoint(transport, bun);
	mem_deref(transport);
	err = bun->uh ? udp_helper_quiesce(bun->uh) : 0;
	if (err && err != EDEADLK)
		warning("bundle: endpoint helper quiescence failed (%m)\n", err);
	mem_deref(bun->uh);
	mem_deref(bun->app_sock);
	mem_deref(bun->endpoint);
	if (bun->initialized)
		mtx_destroy(&bun->lock);
}


static void endpoint_destructor(void *data)
{
	struct bundle_endpoint *endpoint = data;

	mem_deref(endpoint->publication);
	if (endpoint->initialized)
		mtx_destroy(&endpoint->lock);
}


static void publication_destructor(void *data)
{
	struct bundle_publication *publication = data;

	if (publication->initialized)
		mtx_destroy(&publication->lock);
}


static void bundle_wire_destructor(void *data)
{
	struct bundle_wire *wire = data;

	list_unlink(&wire->le);
	list_flush(&wire->dispositions);
	mem_deref(wire->uh);
	mem_deref(wire->sock);
	mem_deref((void *)wire->group);
}


static void disposition_destructor(void *data)
{
	struct bundle_disposition *disposition = data;

	list_unlink(&disposition->le);
	mem_deref(disposition->previous_transport);
	mem_deref(disposition->endpoint);
}


static void endpoint_ref_destructor(void *data)
{
	struct bundle_endpoint_ref *ref = data;

	list_unlink(&ref->le);
	mem_deref(ref->endpoint);
}


static int bundle_wire_snapshot(struct bundle_wire *wire,
				const struct list *endpoints,
				const struct bundle_group *group,
				bool unbundled)
{
	const struct le *le;

	for (le = list_head(endpoints); le; le = le->next) {
		const struct bundle_endpoint_ref *ref = le->data;
		struct bundle_endpoint *endpoint = ref->endpoint;
		const struct stream *stream;
		const struct bundle *bun;
		struct bundle_disposition *disposition;

		mtx_lock(&endpoint->lock);
		bun = endpoint->bundle;
		stream = bun ? bun->stream : NULL;
		if (!stream) {
			mtx_unlock(&endpoint->lock);
			continue;
		}
		disposition = mem_zalloc(sizeof(*disposition),
					   disposition_destructor);
		if (!disposition) {
			mtx_unlock(&endpoint->lock);
			return ENOMEM;
		}
		disposition->endpoint = mem_ref(endpoint);
		disposition->exact_member =
			group && stream_in_group(group, stream);
		if (unbundled || (group && !disposition->exact_member))
			disposition->state = BUNDLE_NONE;
		else if (group)
			disposition->state = !str_cmp(
				stream_remote_mid(stream), bundle_group_tag(group))
				? BUNDLE_BASE : BUNDLE_MUX;
		else
			disposition->state = bundle_state(bun);
		mtx_unlock(&endpoint->lock);
		list_append(&wire->dispositions, &disposition->le,
			    disposition);
	}
	return 0;
}


static bool wire_exact_member(const struct bundle_wire *wire,
			      const struct bundle *bun)
{
	const struct le *le;

	for (le = list_head(&wire->dispositions); le; le = le->next) {
		const struct bundle_disposition *disposition = le->data;

		if (disposition->endpoint == bun->endpoint)
			return disposition->exact_member;
	}
	return false;
}


static enum bundle_state wire_bundle_state(const struct bundle_wire *wire,
					   const struct bundle *bun)
{
	const struct le *le;

	for (le = list_head(&wire->dispositions); le; le = le->next) {
		const struct bundle_disposition *disposition = le->data;

		if (disposition->endpoint == bun->endpoint)
			return disposition->state;
	}
	return BUNDLE_NONE;
}


/* UDP traversal retains both its helper and socket until the callback has
 * returned.  Every BUNDLE callback takes its own owner reference and releases
 * it as its final action, so destroying that owner here cannot invalidate the
 * traversal frame. */
static void callback_deref(void *arg)
{
	mem_deref(arg);
}


/* Must be called without holding the bundle transport lock. */
static int bundle_wire_release(struct bundle_wire **wirep)
{
	struct bundle_wire *wire;
	int err = 0;

	if (!wirep || !*wirep)
		return 0;
	wire = *wirep;
	if (wire->private_helper) {
		err = udp_helper_quiesce(wire->uh);
		if (err && err != EDEADLK)
			warning("bundle: helper quiescence failed (%m)\n", err);
	}
	/* A callback reserved before closing pins both helper and wire.  Dropping
	 * the route owner is therefore safe even when the current callback cannot
	 * synchronously wait for itself, or a platform condition wait fails. */
	*wirep = NULL;
	mem_deref(wire);
	return 0;
}


static void bundle_transport_destructor(void *data)
{
	struct bundle_transport *transport = data;
	struct bundle_wire *pending;
	struct bundle_wire *previous;
	struct bundle_wire *active;
	struct list retired = {0};

	if (transport->initialized)
		mtx_lock(&transport->lock);
	pending = transport->pending;
	previous = transport->previous;
	active = transport->active;
	transport->pending = NULL;
	transport->previous = NULL;
	transport->active = NULL;
	while (transport->retired.head) {
		struct le *le = transport->retired.head;

		list_unlink(le);
		list_append(&retired, le, le->data);
	}
	list_flush(&transport->endpoints);
	if (transport->initialized) {
		mtx_unlock(&transport->lock);
		if (bundle_wire_release(&pending))
			warning("bundle: pending helper still active at teardown\n");
		if (bundle_wire_release(&previous))
			warning("bundle: previous helper still active at teardown\n");
		if (bundle_wire_release(&active))
			warning("bundle: active helper still active at teardown\n");
		while (retired.head) {
			struct bundle_wire *wire = retired.head->data;

			list_unlink(&wire->le);
			if (bundle_wire_release(&wire))
				warning("bundle: retired helper still active at teardown\n");
		}
		mtx_destroy(&transport->lock);
	}
	mem_deref(transport->publication);
}


static void retire_handler(int err, void *arg)
{
	struct bundle_transport *transport = arg;
	struct bundle_wire *wire;

	(void)err;
	for (;;) {
		mtx_lock(&transport->lock);
		wire = transport->retired.head
			? transport->retired.head->data : NULL;
		if (wire)
			list_unlink(&wire->le);
		mtx_unlock(&transport->lock);
		if (!wire)
			break;
		(void)bundle_wire_release(&wire);
	}
	mem_deref(transport);
}


static void bundle_wire_retire(struct bundle_transport *transport,
			       struct bundle_wire *wire)
{
	struct bundle_transport *ref;

	mtx_lock(&transport->lock);
	list_append(&transport->retired, &wire->le, wire);
	mtx_unlock(&transport->lock);
	ref = mem_ref(transport);
	if (re_thread_async_main(NULL, retire_handler, ref)) {
		warning("bundle: deferred helper retirement not scheduled\n");
		mem_deref(ref);
	}
}


static bool wire_uses_endpoint(const struct bundle_wire *wire,
			       const struct bundle *bun)
{
	return wire && wire->sock == bun->app_sock && wire->uh == bun->uh;
}


/* Attach only the endpoints represented by this immutable route.  The old
 * attachment is retained by the disposition until finalize so activation can
 * be reversed without allocation. */
static void bundle_wire_attach(struct bundle_wire *wire)
{
	struct le *le;

	for (le = wire->dispositions.head; le; le = le->next) {
		struct bundle_disposition *disposition = le->data;
		struct bundle_endpoint *endpoint = disposition->endpoint;
		struct bundle *bun;

		if (!disposition->exact_member)
			continue;
		mtx_lock(&endpoint->lock);
		bun = endpoint->bundle;
		if (!bun) {
			mtx_unlock(&endpoint->lock);
			continue;
		}
		mtx_lock(&bun->lock);
		if (bun->transport != wire->transport) {
			disposition->previous_transport = bun->transport;
			bun->transport = mem_ref(wire->transport);
			disposition->attachment_changed = true;
		}
		mtx_unlock(&bun->lock);
		mtx_unlock(&endpoint->lock);
	}
}


/* Called with the route lock held.  Returned references are all references to
 * wire->transport and must be released after unlocking. */
static unsigned bundle_wire_restore_attachments(struct bundle_wire *wire)
{
	struct le *le;
	unsigned release_count = 0;

	for (le = wire->dispositions.head; le; le = le->next) {
		struct bundle_disposition *disposition = le->data;
		struct bundle_endpoint *endpoint = disposition->endpoint;
		struct bundle *bun;

		if (!disposition->attachment_changed)
			continue;
		mtx_lock(&endpoint->lock);
		bun = endpoint->bundle;
		if (!bun) {
			disposition->attachment_changed = false;
			mtx_unlock(&endpoint->lock);
			continue;
		}
		mtx_lock(&bun->lock);
		if (bun->transport == wire->transport) {
			bun->transport = disposition->previous_transport;
			disposition->previous_transport = NULL;
			++release_count;
		}
		disposition->attachment_changed = false;
		mtx_unlock(&bun->lock);
		mtx_unlock(&endpoint->lock);
	}
	return release_count;
}


static void bundle_wire_finalize_attachments(struct bundle_wire *wire)
{
	struct le *le;

	for (le = wire->dispositions.head; le; le = le->next) {
		struct bundle_disposition *disposition = le->data;

		disposition->attachment_changed = false;
	}
}


static void bundle_wire_release_saved_transports(struct bundle_wire *wire)
{
	struct le *le;

	for (le = wire->dispositions.head; le; le = le->next) {
		struct bundle_disposition *disposition = le->data;
		struct bundle_transport *previous =
			disposition->previous_transport;

		disposition->previous_transport = NULL;
		mem_deref(previous);
	}
}


/* Called with the route lock held.  Returned references are all references to
 * old->transport and must be released after unlocking. */
static unsigned bundle_wire_detach_removed(struct bundle_wire *old,
					   const struct bundle_wire *active)
{
	struct le *le;
	unsigned release_count = 0;

	if (!old)
		return 0;
	for (le = old->dispositions.head; le; le = le->next) {
		const struct bundle_disposition *disposition = le->data;
		struct bundle_endpoint *endpoint = disposition->endpoint;
		struct bundle *bun;

		if (!disposition->exact_member)
			continue;
		mtx_lock(&endpoint->lock);
		bun = endpoint->bundle;
		if (!bun || wire_exact_member(active, bun)) {
			mtx_unlock(&endpoint->lock);
			continue;
		}
		mtx_lock(&bun->lock);
		if (bun->transport == old->transport) {
			bun->transport = NULL;
			++release_count;
		}
		mtx_unlock(&bun->lock);
		mtx_unlock(&endpoint->lock);
	}
	return release_count;
}


static void bundle_transport_detach_endpoint(
	struct bundle_transport *transport, const struct bundle *bun)
{
	struct bundle_wire *active = NULL;
	struct bundle_wire *pending = NULL;
	struct bundle_wire *previous = NULL;

	if (!transport || !bun)
		return;

	mtx_lock(&transport->lock);
	if (wire_uses_endpoint(transport->active, bun)) {
		active = transport->active;
		transport->active = NULL;
	}
	if (wire_uses_endpoint(transport->pending, bun)) {
		pending = transport->pending;
		transport->pending = NULL;
	}
	if (wire_uses_endpoint(transport->previous, bun)) {
		previous = transport->previous;
		transport->previous = NULL;
	}
	mtx_unlock(&transport->lock);
	(void)bundle_wire_release(&pending);
	(void)bundle_wire_release(&previous);
	(void)bundle_wire_release(&active);
}


static void bundle_mid_destructor(void *data)
{
	struct bundle_mid *mid = data;

	list_unlink(&mid->le);
	mem_deref(mid->value);
}


static void bundle_group_destructor(void *data)
{
	struct bundle_group *group = data;

	list_unlink(&group->le);
	list_flush(&group->mids);
}


static void bundle_set_destructor(void *data)
{
	struct bundle_set *set = data;

	list_flush(&set->groups);
}


const char *bundle_group_mid(const struct bundle_group *group, size_t index)
{
	const struct le *le;

	if (!group)
		return NULL;

	le = list_head(&group->mids);
	while (le && index--)
		le = le->next;

	return le ? ((const struct bundle_mid *)le->data)->value : NULL;
}


const char *bundle_group_tag(const struct bundle_group *group)
{
	return bundle_group_mid(group, 0);
}


size_t bundle_group_count(const struct bundle_group *group)
{
	return group ? list_count(&group->mids) : 0;
}


bool bundle_group_contains(const struct bundle_group *group, const char *value)
{
	const struct le *le;

	if (!group || !str_isset(value))
		return false;

	for (le = list_head(&group->mids); le; le = le->next) {
		const struct bundle_mid *mid = le->data;

		if (!str_cmp(mid->value, value))
			return true;
	}

	return false;
}


static int bundle_group_append(struct bundle_group *group,
			       const char *value, size_t length);


int bundle_group_clone(struct bundle_group **groupp,
		       const struct bundle_group *group)
{
	struct bundle_group *copy;
	int err = 0;

	if (!groupp || !group || !bundle_group_count(group))
		return EINVAL;

	copy = mem_zalloc(sizeof(*copy), bundle_group_destructor);
	if (!copy)
		return ENOMEM;

	for (size_t i = 0; i < bundle_group_count(group); ++i) {
		const char *mid = bundle_group_mid(group, i);

		err = bundle_group_append(copy, mid, str_len(mid));
		if (err)
			break;
	}
	if (err)
		mem_deref(copy);
	else
		*groupp = copy;
	return err;
}


int bundle_group_singleton(struct bundle_group **groupp, const char *mid)
{
	struct bundle_group *group;
	int err;

	if (!groupp || !str_isset(mid))
		return EINVAL;

	group = mem_zalloc(sizeof(*group), bundle_group_destructor);
	if (!group)
		return ENOMEM;
	err = bundle_group_append(group, mid, str_len(mid));
	if (err)
		mem_deref(group);
	else
		*groupp = group;
	return err;
}


size_t bundle_set_count(const struct bundle_set *set)
{
	return set ? list_count(&set->groups) : 0;
}


const struct bundle_group *bundle_set_group(const struct bundle_set *set,
					     size_t index)
{
	const struct le *le;

	if (!set)
		return NULL;

	le = list_head(&set->groups);
	while (le && index--)
		le = le->next;

	return le ? le->data : NULL;
}


const struct bundle_group *bundle_set_find_mid(const struct bundle_set *set,
						const char *mid)
{
	const struct le *le;

	if (!set || !str_isset(mid))
		return NULL;

	for (le = list_head(&set->groups); le; le = le->next) {
		const struct bundle_group *group = le->data;

		if (bundle_group_contains(group, mid))
			return group;
	}

	return NULL;
}


static int bundle_group_append(struct bundle_group *group,
			       const char *value, size_t len)
{
	struct bundle_mid *mid;
	const struct le *le;
	int err;

	if (!group || !value || !len)
		return EPROTO;

	for (le = list_head(&group->mids); le; le = le->next) {
		const struct bundle_mid *existing = le->data;

		if (str_len(existing->value) == len &&
		    !memcmp(existing->value, value, len))
			return EPROTO;
	}

	mid = mem_zalloc(sizeof(*mid), bundle_mid_destructor);
	if (!mid)
		return ENOMEM;

	err = re_sdprintf(&mid->value, "%b", value, len);
	if (err) {
		mem_deref(mid);
		return err;
	}

	list_append(&group->mids, &mid->le, mid);
	return 0;
}


struct bundle_set_decode {
	struct bundle_set *set;
	int err;
};


static bool bundle_set_decode_handler(const char *name, const char *value,
				      void *arg)
{
	struct bundle_set_decode *decode = arg;
	struct bundle_group *group;
	const char *cursor;
	(void)name;

	if (decode->err || !value || strncmp(value, "BUNDLE", 6) ||
	    (value[6] && value[6] != ' ' && value[6] != '\t'))
		return false;

	group = mem_zalloc(sizeof(*group), bundle_group_destructor);
	if (!group) {
		decode->err = ENOMEM;
		return true;
	}
	list_append(&decode->set->groups, &group->le, group);

	cursor = value + 6;
	while (*cursor) {
		const char *start;

		while (*cursor == ' ' || *cursor == '\t')
			++cursor;
		if (!*cursor)
			break;

		start = cursor;
		while (*cursor && *cursor != ' ' && *cursor != '\t')
			++cursor;

		decode->err = bundle_group_append(
			group, start, (size_t)(cursor - start));
		if (decode->err)
			return true;
	}

	if (!bundle_group_count(group))
		decode->err = EPROTO;
	for (const struct le *le = list_head(&decode->set->groups);
	     !decode->err && le; le = le->next) {
		const struct bundle_group *other = le->data;

		if (other == group)
			continue;
		for (size_t i = 0; i < bundle_group_count(group); ++i) {
			if (bundle_group_contains(
				    other, bundle_group_mid(group, i))) {
				decode->err = EPROTO;
				break;
			}
		}
	}

	return decode->err != 0;
}


int bundle_set_decode(struct bundle_set **setp, const struct sdp_session *sdp)
{
	struct bundle_set_decode decode;
	struct bundle_set *set;

	if (!setp || !sdp)
		return EINVAL;

	*setp = NULL;
	set = mem_zalloc(sizeof(*set), bundle_set_destructor);
	if (!set)
		return ENOMEM;

	memset(&decode, 0, sizeof(decode));
	decode.set = set;
	(void)sdp_session_rattr_apply(sdp, "group",
				     bundle_set_decode_handler, &decode);
	if (decode.err) {
		mem_deref(set);
		return decode.err;
	}
	if (!bundle_set_count(set)) {
		mem_deref(set);
		return 0;
	}

	*setp = set;
	return 0;
}


static unsigned bundle_mid_matches(const struct list *streaml,
				   const char *value)
{
	const struct le *le;
	unsigned matches = 0;

	for (le = list_head(streaml); le; le = le->next) {
		const struct stream *stream = le->data;
		const char *mid = sdp_media_rattr(
			stream_sdpmedia(stream), "mid");

		if (!str_cmp(str_isset(mid) ? mid : stream_mid(stream), value))
			++matches;
	}

	return matches;
}


int bundle_set_validate(const struct bundle_set *set,
			const struct list *streaml, const char *data_mid)
{
	if (!set || !streaml)
		return EINVAL;

	for (size_t i = 0; i < bundle_set_count(set); ++i) {
		const struct bundle_group *group = bundle_set_group(set, i);

		for (size_t j = 0; j < bundle_group_count(group); ++j) {
			const char *mid = bundle_group_mid(group, j);
			unsigned matches = bundle_mid_matches(streaml, mid);

			if (!str_cmp(mid, data_mid))
				++matches;
			if (matches != 1)
				return EPROTO;
		}
	}

	return 0;
}


static int print_bundle_group(struct re_printf *pf, void *arg)
{
	const struct bundle_group *group = arg;
	int err = 0;

	for (size_t i = 0; i < bundle_group_count(group); ++i)
		err |= re_hprintf(pf, " %s", bundle_group_mid(group, i));

	return err;
}


int bundle_set_encode(struct sdp_session *sdp, const struct bundle_set *set)
{
	int err = 0;

	if (!sdp || !set)
		return EINVAL;

	sdp_session_del_lattr(sdp, "group");
	for (size_t i = 0; !err && i < bundle_set_count(set); ++i) {
		const struct bundle_group *group = bundle_set_group(set, i);

		err = sdp_session_set_lattr(sdp, false, "group", "BUNDLE%H",
					    print_bundle_group, (void *)group);
	}

	return err;
}


const char *bundle_state_name(enum bundle_state st)
{
	switch (st) {

	case BUNDLE_NONE: return "None";
	case BUNDLE_BASE: return "Base";
	case BUNDLE_MUX:  return "Mux";
	default: return "???";
	}
}


void bundle_set_state(struct bundle *bun, enum bundle_state st)
{
	if (!bun)
		return;

	debug("bundle: set state: %s\n", bundle_state_name(st));

	bun->state = st;
}


int bundle_alloc(struct bundle **bunp, const struct stream *stream)
{
	struct bundle *bun;

	if (!bunp || !stream)
		return EINVAL;

	info("bundle: alloc\n");

	bun = mem_zalloc(sizeof(*bun), destructor);
	if (!bun)
		return ENOMEM;
	if (mtx_init(&bun->lock, mtx_plain) != thrd_success) {
		mem_deref(bun);
		return ENOMEM;
	}
	bun->initialized = true;
	bun->stream = stream;
	bun->endpoint = mem_zalloc(sizeof(*bun->endpoint), endpoint_destructor);
	if (!bun->endpoint) {
		mem_deref(bun);
		return ENOMEM;
	}
	if (mtx_init(&bun->endpoint->lock, mtx_plain) != thrd_success) {
		mem_deref(bun);
		return ENOMEM;
	}
	bun->endpoint->initialized = true;
	bun->endpoint->bundle = bun;

	*bunp = bun;

	return 0;
}


static enum sdp_dir remote_direction(const struct sdp_session *sdp,
				     const struct sdp_media *media)
{
	if (sdp_media_rattr(media, "inactive"))
		return SDP_INACTIVE;
	if (sdp_media_rattr(media, "sendonly"))
		return SDP_SENDONLY;
	if (sdp_media_rattr(media, "recvonly"))
		return SDP_RECVONLY;
	if (sdp_media_rattr(media, "sendrecv"))
		return SDP_SENDRECV;
	if (sdp_session_rattr(sdp, "inactive"))
		return SDP_INACTIVE;
	if (sdp_session_rattr(sdp, "sendonly"))
		return SDP_SENDONLY;
	if (sdp_session_rattr(sdp, "recvonly"))
		return SDP_RECVONLY;

	return SDP_SENDRECV;
}


static int bundle_sdp_decode_internal(struct sdp_session *sdp,
				      struct list *streaml, bool staged)
{
	struct bundle_set *set = NULL;
	struct le *le;
	int err;

	if (!sdp || !streaml)
		return EINVAL;

	if (staged) {
		/* PeerConnection derives exact topology for every description. */
		for (le = streaml->head; le; le = le->next)
			stream_stage_bundle(le->data, BUNDLE_NONE);
	}

	for (le = streaml->head; le; le = le->next) {

		struct stream *strm = le->data;

		err = stream_parse_mid(strm);
		if (err)
			return err;
	}

	err = bundle_set_decode(&set, sdp);
	if (err || !set)
		return err;

	for (size_t i = 0; i < bundle_set_count(set); ++i) {
		const struct bundle_group *group = bundle_set_group(set, i);
		struct stream *base = NULL;

		for (size_t j = 0; j < bundle_group_count(group); ++j) {
			const char *mid = bundle_group_mid(group, j);
			struct stream *strm = stream_lookup_mid(
				streaml, mid, str_len(mid));
			bool first_rtp;

			if (!strm) {
				debug("bundle: non-RTP member (mid=%s)\n", mid);
				continue;
			}
			first_rtp = base == NULL;
			if (staged)
				stream_stage_bundle(strm, first_rtp
					? BUNDLE_BASE : BUNDLE_MUX);
			else
				stream_enable_bundle(strm, first_rtp
					? BUNDLE_BASE : BUNDLE_MUX);
			if (first_rtp)
				base = strm;
		}

		/* The legacy stream adapter uses the first RTP member as its base when
		 * a non-RTP MID is the group tag.  Exact route ownership remains in the
		 * bundle transport API until the session topology coordinator can own
		 * the non-RTP transport end to end. */
		if (!base)
			continue;
		for (size_t j = 0; j < bundle_group_count(group); ++j) {
			const char *mid = bundle_group_mid(group, j);
			struct stream *strm = stream_lookup_mid(
				streaml, mid, str_len(mid));
			struct sdp_media *media;

			if (!strm || strm == base)
				continue;
			media = stream_sdpmedia(strm);
			if (!sdp_media_rattr(media, "bundle-only"))
				continue;
			sdp_media_set_rdir(media, remote_direction(sdp, media));
			sdp_media_set_rport(media,
					    sdp_media_rport(stream_sdpmedia(base)));
			sdp_media_align_formats(media, true);
		}
	}

	mem_deref(set);
	return err;
}


int bundle_sdp_decode(struct sdp_session *sdp, struct list *streaml)
{
	return bundle_sdp_decode_internal(sdp, streaml, false);
}


int bundle_sdp_decode_stage(struct sdp_session *sdp, struct list *streaml)
{
	return bundle_sdp_decode_internal(sdp, streaml, true);
}


int bundle_set_extmap(struct bundle *bun, struct sdp_media *sdp,
		      uint8_t extmap_mid)
{
	int err;

	if (!sdp || !bun)
		return EINVAL;

	err = sdp_media_set_lattr(sdp, true, "extmap",
				  "%u %s", extmap_mid, uri_mid);
	if (!err)
		bun->extmap_mid = extmap_mid;

	return err;
}


struct extmap_decode {
	struct bundle *bun;
	struct sdp_media *sdp;
	int err;
};


static bool extmap_handler(const char *name, const char *value, void *arg)
{
	struct extmap_decode *decode = arg;
	struct sdp_extmap extmap;
	int err;
	(void)name;

	err = sdp_extmap_decode(&extmap, value);
	if (err) {
		warning("bundle: sdp_extmap_decode error (%m)\n", err);
		return false;
	}

	if (0 == pl_strcasecmp(&extmap.name, uri_mid)) {
		decode->err = bundle_set_extmap(decode->bun, decode->sdp,
						 extmap.id);
		return true;
	}

	return false;
}


int bundle_handle_extmap(struct bundle *bun, struct sdp_media *sdp)
{
	struct extmap_decode decode = {
		.bun = bun,
		.sdp = sdp,
	};

	if (!bun || !sdp)
		return EINVAL;

	sdp_media_rattr_apply(sdp, "extmap", extmap_handler, &decode);
	return decode.err;
}


static int print_bundle(struct re_printf *pf, const struct list *streaml)
{
	struct le *le;
	int err = 0;

	for (le = list_head(streaml); le; le = le->next) {

		struct stream *strm = le->data;
		const char *mid = stream_mid(strm);

		if (mid)
			err |= re_hprintf(pf, " %s", mid);
	}

	return err;
}


static bool copy_attr_handler(const char *name, const char *value, void *arg)
{
	struct attr_copy *copy = arg;
	(void)name;

	copy->err = sdp_media_set_lattr(copy->media, false, copy->name,
					"%s", value);
	copy->copied = !copy->err;
	return copy->err != 0;
}


static int copy_transport_attrs(const struct sdp_session *sdp,
				const struct list *streaml)
{
	static const char *names[] = {
		"setup",
		"fingerprint",
		"ice-ufrag",
		"ice-pwd",
	};
	const struct sdp_media *base = NULL;
	struct le *le;

	for (le = streaml->head; le; le = le->next) {
		struct stream *strm = le->data;
		struct sdp_media *media = stream_sdpmedia(strm);

		if (bundle_state(stream_bundle(strm)) == BUNDLE_BASE) {
			base = media;
			continue;
		}
		if (!base ||
		    bundle_state(stream_bundle(strm)) != BUNDLE_MUX ||
		    !sdp_media_rattr(media, "bundle-only"))
			continue;

		for (size_t i = 0; i < RE_ARRAY_SIZE(names); ++i) {
			struct attr_copy copy = {
				.media = media,
				.name = names[i],
			};

			sdp_media_del_lattr(media, names[i]);
			(void)sdp_media_lattr_apply(base, names[i],
						   copy_attr_handler, &copy);
			if (!copy.err && !copy.copied)
				(void)sdp_session_lattr_apply(
					sdp, names[i], copy_attr_handler,
					&copy);
			if (copy.err)
				return copy.err;
		}
	}

	return 0;
}


int bundle_sdp_encode(struct sdp_session *sdp, const struct list *streaml)
{
	int err;

	if (!sdp || !streaml)
		return EINVAL;

	err = copy_transport_attrs(sdp, streaml);
	if (err)
		return err;

	return sdp_session_set_lattr(sdp, true, "group", "BUNDLE%H",
				     print_bundle, streaml);
}


static struct stream *lookup_remote_ssrc(const struct list *streaml,
					 const struct bundle_wire *wire,
					 uint32_t ssrc)
{
	struct le *le;

	for (le = streaml->head; le; le = le->next) {
		struct stream *strm = le->data;
		struct bundle *bun = stream_bundle(strm);
		uint32_t rssrc;

		if (wire && wire_bundle_state(wire, bun) == BUNDLE_NONE)
			continue;
		if (stream_ssrc_rx(strm, &rssrc))
			continue;

		if (ssrc == rssrc)
			return strm;
	}

	return NULL;
}


static const char *stream_remote_mid(const struct stream *stream)
{
	const char *mid;

	if (!stream)
		return NULL;
	mid = sdp_media_rattr(stream_sdpmedia(stream), "mid");
	return str_isset(mid) ? mid : stream_mid(stream);
}


static bool dispatch_group_ssrc(const struct bundle_wire *wire, uint32_t ssrc,
				struct sa *src, struct mbuf *mb, size_t pos)
{
	struct le *le;

	for (le = wire->dispositions.head; le; le = le->next) {
		const struct bundle_disposition *disposition = le->data;
		struct bundle_endpoint *endpoint = disposition->endpoint;
		struct bundle *bun;
		struct stream *stream;
		struct udp_sock *sock = NULL;
		struct udp_helper *helper = NULL;
		uint32_t rssrc;

		if (!disposition->exact_member)
			continue;
		mtx_lock(&endpoint->lock);
		bun = endpoint->bundle;
		stream = bun ? (struct stream *)bun->stream : NULL;
		if (stream && !stream_ssrc_rx(stream, &rssrc) && ssrc == rssrc) {
			sock = mem_ref(bun->app_sock);
			helper = mem_ref(bun->uh);
		}
		mtx_unlock(&endpoint->lock);
		if (!sock || !helper) {
			mem_deref(helper);
			mem_deref(sock);
			continue;
		}
		mb->pos = pos;
		udp_recv_helper(sock, src, mb, helper);
		mem_deref(helper);
		mem_deref(sock);
		return true;
	}

	return false;
}


static int get_rtcp_ssrc(const struct rtcp_msg *msg, uint32_t *ssrcp)
{
	uint32_t ssrc;

	switch (msg->hdr.pt) {

	case RTCP_APP:
		ssrc = msg->r.app.src;
		break;

	case RTCP_SR:
		ssrc = msg->r.sr.ssrc;
		break;

	case RTCP_SDES:
		ssrc = msg->r.sdesv->src;
		break;

	case RTCP_PSFB:
		ssrc = msg->r.fb.ssrc_packet;
		break;

	case RTCP_BYE:
		ssrc = msg->r.bye.srcv[0];
		break;

	case RTCP_RR:
		ssrc = msg->r.rr.ssrc;
		break;

	default:
		warning("bundle: rtcp not sup (pt=%d)\n", msg->hdr.pt);
		return ENOTSUP;
	}

	*ssrcp = ssrc;

	return 0;
}


static struct stream *bundle_find_base(const struct list *streaml,
				       const struct bundle_wire *wire)
{
	struct le *le;

	for (le = list_head(streaml); le; le = le->next) {
		struct stream *strm = le->data;
		struct bundle *bun = stream_bundle(strm);

		if (bun && (wire ? wire_bundle_state(wire, bun)
				 : bun->state) == BUNDLE_BASE)
			return strm;
	}

	return NULL;
}


static struct bundle_wire *active_wire(struct bundle *bun, bool *attached)
{
	struct bundle_publication *publication;
	struct bundle_transport *transport;
	struct bundle_wire *wire = NULL;

	*attached = false;
	mtx_lock(&bun->endpoint->lock);
	publication = mem_ref(bun->endpoint->publication);
	mtx_unlock(&bun->endpoint->lock);
	bundle_publication_lock(publication);
	mtx_lock(&bun->lock);
	transport = mem_ref(bun->transport);
	mtx_unlock(&bun->lock);
	if (!transport) {
		bundle_publication_unlock(publication);
		mem_deref(publication);
		return NULL;
	}
	mtx_lock(&transport->lock);
	if (transport->active) {
		wire = mem_ref(transport->active);
		*attached = wire->remote_set && wire_exact_member(wire, bun);
	}
	mtx_unlock(&transport->lock);
	mem_deref(transport);
	bundle_publication_unlock(publication);
	mem_deref(publication);
	return wire;
}


static bool demux_packet(struct bundle_wire *wire, struct sa *src,
			 struct mbuf *mb)
{
	size_t pos = mb->pos;
	uint32_t ssrc;
	int err;

	/* DTLS and other UDP helpers share this socket.  Helper traversal may
	 * legitimately expose a non-RTP datagram here; only parse packets carrying
	 * the RTP/RTCP version bits, otherwise quietly leave them consumed. */
	if (mbuf_get_left(mb) < 2 || (mb->buf[mb->pos] & 0xc0) != 0x80)
		return true;

	if (!rtp_is_rtcp_packet(mb)) {
		struct rtp_header hdr;

		err = rtp_hdr_decode(&hdr, mb);
		if (err) {
			warning("bundle: rtp decode error (%m)\n", err);
			return true;
		}
		ssrc = hdr.ssrc;
	}
	else {
		struct rtcp_msg *msg;

		err = rtcp_decode(&msg, mb);
		if (err) {
			warning("bundle: rtcp decode error (%m)\n", err);
			return true;
		}
		err = get_rtcp_ssrc(msg, &ssrc);
		mem_deref(msg);
		if (err)
			return true;
	}

	if (!dispatch_group_ssrc(wire, ssrc, src, mb, pos)) {
		warning("bundle: group stream not found (ssrc=%x)\n", ssrc);
	}

	return true;
}


/* send: used by all exact-group endpoints and legacy muxed streams */
static bool udp_helper_send_handler(int *err, struct sa *dst,
				    struct mbuf *mb, void *arg)
{
	struct bundle *bun = mem_ref(arg);
	struct bundle_wire *wire;
	struct stream *strm;
	bool attached;

	if (re_atomic_acq(&bun->stopped)) {
		callback_deref(bun);
		return false;
	}

	wire = active_wire(bun, &attached);
	if (wire && attached) {
		if (wire->sock == bun->app_sock) {
			sa_cpy(dst, &wire->remote);
			mem_deref(wire);
			callback_deref(bun);
			return false;
		}

		*err = udp_send_helper(wire->sock, &wire->remote, mb,
				       wire->uh);
		if (*err)
			warning("bundle: group send: %m\n", *err);
		mem_deref(wire);
		callback_deref(bun);
		return true;
	}
	if (wire)
		attached = wire_bundle_state(wire, bun) == BUNDLE_MUX;
	else
		attached = bun->state == BUNDLE_MUX;
	if (!attached) {
		mem_deref(wire);
		callback_deref(bun);
		return false;
	}

	strm = bundle_find_base(bun->streaml, wire);
	mem_deref(wire);
	if (strm) {
		struct udp_sock *us = rtp_sock(stream_rtp_sock(strm));
		struct bundle *bun2 = stream_bundle(strm);
		int lerr;

		lerr = udp_send_helper(us, dst, mb, bun2->uh);
		if (lerr) {
			warning("bundle: send: %m\n", lerr);
			*err = lerr;
		}

		callback_deref(bun);
		return true;  /* handled */
	}

	callback_deref(bun);
	return false; /* continue */
}


/* recv: used by an exact-group wire endpoint or a legacy base stream */
static bool udp_helper_recv_handler(struct sa *src, struct mbuf *mb, void *arg)
{
	struct bundle *bun = mem_ref(arg);
	struct bundle_wire *wire;
	struct stream *strm;
	size_t pos = mb->pos;
	uint32_t ssrc;
	bool attached;
	int err;

	if (re_atomic_acq(&bun->stopped)) {
		callback_deref(bun);
		return false;
	}

	wire = active_wire(bun, &attached);
	if (wire && attached) {
		bool handled = wire->sock == bun->app_sock
			? demux_packet(wire, src, mb)
			: true;

		mem_deref(wire);
		callback_deref(bun);
		return handled;
	}
	if (wire) {
		attached = wire_bundle_state(wire, bun) == BUNDLE_BASE;
	}
	else {
		attached = bun->state == BUNDLE_BASE;
	}
	if (!attached) {
		mem_deref(wire);
		callback_deref(bun);
		return false;
	}
	/* This legacy endpoint helper shares its socket with DTLS.  It must not
	 * diagnose or consume a datagram which is not recognizably RTP/RTCP. */
	if (mbuf_get_left(mb) < 2 || (mb->buf[mb->pos] & 0xc0) != 0x80) {
		mb->pos = pos;
		mem_deref(wire);
		callback_deref(bun);
		return false;
	}

	if (!rtp_is_rtcp_packet(mb)) {

		struct rtp_header hdr;

		err = rtp_hdr_decode(&hdr, mb);
		if (err) {
			warning("bundle: rtp decode error (%m)\n", err);
			mb->pos = pos;
			mem_deref(wire);
			callback_deref(bun);
			return true;
		}

		ssrc = hdr.ssrc;
	}
	else {
		struct rtcp_msg *msg;

		err = rtcp_decode(&msg, mb);
		if (err) {
			warning("rtcp decode error (%m)\n", err);
			mb->pos = pos;
			mem_deref(wire);
			callback_deref(bun);
			return true;
		}

		err = get_rtcp_ssrc(msg, &ssrc);
		mem_deref(msg);

		if (err) {
			mb->pos = pos;
			mem_deref(wire);
			callback_deref(bun);
			return true;
		}
	}

	strm = lookup_remote_ssrc(bun->streaml, wire, ssrc);
	mem_deref(wire);
	if (strm) {
		struct udp_sock *us = rtp_sock(stream_rtp_sock(strm));
		struct bundle *bun2 = stream_bundle(strm);

		mb->pos = pos;

		udp_recv_helper(us, src, mb, bun2->uh);
	}
	else {
		warning("bundle: stream not found (ssrc=%x)\n",
			ssrc);
	}

	callback_deref(bun);
	return true; /* stop */
}


static bool wire_recv_handler(struct sa *src, struct mbuf *mb, void *arg)
{
	struct bundle_wire *wire = mem_ref(arg);
	struct bundle_transport *transport = mem_ref(wire->transport);
	struct bundle_publication *publication;
	bool active;
	bool handled;

	mtx_lock(&transport->lock);
	publication = mem_ref(transport->publication);
	mtx_unlock(&transport->lock);
	bundle_publication_lock(publication);
	mtx_lock(&transport->lock);
	active = transport->active == wire;
	mtx_unlock(&transport->lock);
	bundle_publication_unlock(publication);
	mem_deref(publication);

	/* A prepared route must not consume packets before publication. */
	handled = active ? demux_packet(wire, src, mb) : false;
	callback_deref(transport);
	callback_deref(wire);
	return handled;
}


int bundle_start_socket(struct bundle *bun, struct udp_sock *us,
			struct list *streaml)
{
	enum { RTP_TRANSP_LAYER = 40 };
	int err;

	info("bundle: start socket <%p>\n", us);

	if (!bun || !us)
		return EINVAL;

	if (bun->uh)
		return EALREADY;

	/* NOTE: UDP helper must be injected below the RTP stack */
	err = udp_register_helper(&bun->uh, us, RTP_TRANSP_LAYER,
				  udp_helper_send_handler,
				  udp_helper_recv_handler, bun);
	if (err)
		return err;
	bun->app_sock = mem_ref(us);
	bun->streaml = streaml;

	return 0;
}


void bundle_stop(struct bundle *bun)
{
	int err;

	if (!bun)
		return;

	/* Close the routing gate before quiescing.  A teardown initiated by this
	 * helper's own callback receives EDEADLK because that callback cannot wait
	 * for itself.  Its bundle reference pins this object through the final
	 * callback action; UDP traversal independently pins the helper and socket,
	 * so their destructors run safely after traversal releases those refs.  The
	 * gate also makes a concurrently reserved callback pass through without
	 * touching withdrawn state.
	 *
	 * The legacy helper callbacks dereference streaml directly.  Retire the
	 * helper before the owning stream list can be destroyed, then withdraw the
	 * endpoint's raw stream pointer used by published-wire dispatch. */
	re_atomic_rls_set(&bun->stopped, true);
	err = bun->uh ? udp_helper_quiesce(bun->uh) : 0;
	if (err && err != EDEADLK)
		warning("bundle: endpoint helper quiescence failed (%m)\n", err);

	if (bun->endpoint && bun->endpoint->initialized) {
		mtx_lock(&bun->endpoint->lock);
		bun->stream = NULL;
		mtx_unlock(&bun->endpoint->lock);
	}
	mtx_lock(&bun->lock);
	bun->streaml = NULL;
	mtx_unlock(&bun->lock);
}


static bool stream_in_group(const struct bundle_group *group,
			    const struct stream *stream)
{
	return group && stream &&
		bundle_group_contains(group, stream_remote_mid(stream));
}


int bundle_transport_alloc(struct bundle_transport **transportp,
			   const struct bundle_group *group,
			   struct list *streaml, const char *data_mid)
{
	struct bundle_transport *transport;
	struct bundle_wire *legacy;
	int err;

	if (!transportp || !group || !streaml)
		return EINVAL;
	(void)data_mid;

	transport = mem_zalloc(sizeof(*transport),
			       bundle_transport_destructor);
	if (!transport)
		return ENOMEM;
	if (mtx_init(&transport->lock, mtx_plain) != thrd_success) {
		mem_deref(transport);
		return ENOMEM;
	}
	transport->initialized = true;
	for (struct le *le = streaml->head; le; le = le->next) {
		struct bundle_endpoint_ref *ref;
		struct bundle *bun = stream_bundle(le->data);

		if (!bun)
			continue;
		ref = mem_zalloc(sizeof(*ref), endpoint_ref_destructor);
		if (!ref) {
			mem_deref(transport);
			return ENOMEM;
		}
		ref->endpoint = mem_ref(bun->endpoint);
		list_append(&transport->endpoints, &ref->le, ref);
	}
	legacy = mem_zalloc(sizeof(*legacy), bundle_wire_destructor);
	if (!legacy) {
		mem_deref(transport);
		return ENOMEM;
	}
	legacy->transport = transport;
	err = bundle_wire_snapshot(legacy, &transport->endpoints, NULL, false);
	if (err) {
		mem_deref(legacy);
		mem_deref(transport);
		return err;
	}
	transport->active = legacy;

	*transportp = transport;
	return 0;
}


int bundle_publication_alloc(struct bundle_publication **publicationp)
{
	struct bundle_publication *publication;

	if (!publicationp)
		return EINVAL;

	publication = mem_zalloc(sizeof(*publication), publication_destructor);
	if (!publication)
		return ENOMEM;
	if (mtx_init(&publication->lock, mtx_plain) != thrd_success) {
		mem_deref(publication);
		return ENOMEM;
	}
	publication->initialized = true;
	*publicationp = publication;
	return 0;
}


int bundle_transport_bind_publication(
	struct bundle_transport *transport,
	struct bundle_publication *publication)
{
	struct le *le;
	int err = 0;

	if (!transport || !publication)
		return EINVAL;

	/* Keep the established route lock ordering: transport, endpoint, bundle.
	 * Lock every endpoint before validation so disjoint transports cannot
	 * partially bind one session to different publication gates. */
	mtx_lock(&transport->lock);
	for (le = transport->endpoints.head; le; le = le->next) {
		struct bundle_endpoint_ref *ref = le->data;

		mtx_lock(&ref->endpoint->lock);
	}
	if (transport->publication &&
	    transport->publication != publication)
		err = EBUSY;
	for (le = transport->endpoints.head; !err && le; le = le->next) {
		struct bundle_endpoint_ref *ref = le->data;

		if (ref->endpoint->publication &&
		    ref->endpoint->publication != publication)
			err = EBUSY;
	}
	if (!err) {
		if (!transport->publication)
			transport->publication = mem_ref(publication);
		for (le = transport->endpoints.head; le; le = le->next) {
			struct bundle_endpoint_ref *ref = le->data;

			if (!ref->endpoint->publication)
				ref->endpoint->publication =
					mem_ref(publication);
		}
	}
	for (le = transport->endpoints.head; le; le = le->next) {
		struct bundle_endpoint_ref *ref = le->data;

		mtx_unlock(&ref->endpoint->lock);
	}
	mtx_unlock(&transport->lock);
	return err;
}


void bundle_publication_lock(struct bundle_publication *publication)
{
	if (publication)
		mtx_lock(&publication->lock);
}


void bundle_publication_unlock(struct bundle_publication *publication)
{
	if (publication)
		mtx_unlock(&publication->lock);
}


static struct udp_helper *wire_endpoint_helper(const struct bundle_wire *wire,
					       struct udp_sock *sock)
{
	struct le *le;

	for (le = wire->dispositions.head; le; le = le->next) {
		const struct bundle_disposition *disposition = le->data;
		struct bundle_endpoint *endpoint = disposition->endpoint;
		struct udp_helper *helper = NULL;
		struct bundle *bun;

		if (!disposition->exact_member)
			continue;
		mtx_lock(&endpoint->lock);
		bun = endpoint->bundle;
		if (bun && bun->app_sock == sock)
			helper = mem_ref(bun->uh);
		mtx_unlock(&endpoint->lock);
		if (helper)
			return helper;
	}

	return NULL;
}


int bundle_transport_stage(struct bundle_transport *transport,
			   const struct bundle_group *group,
			   struct udp_sock *sock, uint64_t generation)
{
	enum { RTP_TRANSP_LAYER = 40 };
	struct bundle_wire *wire;
	struct bundle_wire *old;
	uint64_t pending_generation;
	uint64_t previous_generation;
	bool endpoint_helper;
	int err = 0;

	if (!transport || !group || !sock || !generation)
		return EINVAL;

	mtx_lock(&transport->lock);
	if ((transport->active &&
	     transport->active->generation == generation) ||
	    (transport->pending &&
	     transport->pending->generation == generation)) {
		mtx_unlock(&transport->lock);
		return EALREADY;
	}
	mtx_unlock(&transport->lock);

	wire = mem_zalloc(sizeof(*wire), bundle_wire_destructor);
	if (!wire)
		return ENOMEM;
	wire->sock = mem_ref(sock);
	wire->transport = transport;
	wire->group = mem_ref((void *)group);
	wire->generation = generation;
	err = bundle_wire_snapshot(wire, &transport->endpoints, group, false);
	if (err) {
		mem_deref(wire);
		return err;
	}
	wire->uh = wire_endpoint_helper(wire, sock);
	endpoint_helper = wire->uh != NULL;
	if (!wire->uh)
		err = udp_register_helper(&wire->uh, wire->sock,
					  RTP_TRANSP_LAYER, NULL,
					  wire_recv_handler, wire);
	if (!err && !endpoint_helper)
		wire->private_helper = true;
	if (err) {
		mem_deref(wire);
		return err;
	}

	mtx_lock(&transport->lock);
	old = transport->pending ? transport->pending : transport->previous;
	pending_generation = transport->pending
				     ? transport->pending->generation : 0;
	previous_generation = transport->previous
				      ? transport->previous->generation : 0;
	if (!old) {
		transport->pending = wire;
		if (generation > transport->next_route_generation)
			transport->next_route_generation = generation;
	}
	mtx_unlock(&transport->lock);
	if (old) {
		warning("bundle: route prepare busy"
			" (requested=%llu pending=%llu previous=%llu)\n",
			(unsigned long long)generation,
			(unsigned long long)pending_generation,
			(unsigned long long)previous_generation);
		(void)bundle_wire_release(&wire);
		return EBUSY;
	}
	return 0;
}


int bundle_transport_stage_legacy(struct bundle_transport *transport,
				  uint64_t route_generation)
{
	struct bundle_wire *wire;
	int err;

	if (!transport || !route_generation)
		return EINVAL;

	wire = mem_zalloc(sizeof(*wire), bundle_wire_destructor);
	if (!wire)
		return ENOMEM;
	wire->transport = transport;
	wire->generation = route_generation;
	err = bundle_wire_snapshot(wire, &transport->endpoints, NULL, true);
	if (err) {
		mem_deref(wire);
		return err;
	}

	mtx_lock(&transport->lock);
	if (transport->pending || transport->previous ||
	    (transport->active &&
	     transport->active->generation == route_generation)) {
		err = transport->pending || transport->previous
			? EBUSY : EALREADY;
		mtx_unlock(&transport->lock);
		mem_deref(wire);
		return err;
	}
	transport->pending = wire;
	if (route_generation > transport->next_route_generation)
		transport->next_route_generation = route_generation;
	mtx_unlock(&transport->lock);
	return 0;
}


static uint64_t next_route_generation(struct bundle_transport *transport)
{
	uint64_t generation;

	mtx_lock(&transport->lock);
	do {
		generation = ++transport->next_route_generation;
	} while (!generation ||
		 (transport->active &&
		  transport->active->generation == generation) ||
		 (transport->pending &&
		  transport->pending->generation == generation) ||
		 (transport->previous &&
		  transport->previous->generation == generation));
	mtx_unlock(&transport->lock);
	return generation;
}


int bundle_transport_prepare(struct bundle_transport *transport,
			     const struct bundle_group *group,
			     struct udp_sock *sock,
			     uint64_t *route_generationp)
{
	uint64_t generation;
	int err;

	if (!transport || !group || !sock || !route_generationp)
		return EINVAL;
	generation = next_route_generation(transport);
	err = bundle_transport_stage(transport, group, sock, generation);
	if (!err)
		*route_generationp = generation;
	return err;
}


int bundle_transport_prepare_legacy(struct bundle_transport *transport,
				    uint64_t *route_generationp)
{
	uint64_t generation;
	int err;

	if (!transport || !route_generationp)
		return EINVAL;
	generation = next_route_generation(transport);
	err = bundle_transport_stage_legacy(transport, generation);
	if (!err)
		*route_generationp = generation;
	return err;
}


int bundle_transport_set_remote(struct bundle_transport *transport,
				uint64_t generation, const struct sa *remote)
{
	int err = ENOENT;

	if (!transport || !remote || !sa_isset(remote, SA_ALL))
		return EINVAL;

	mtx_lock(&transport->lock);
	if (transport->pending &&
	    transport->pending->generation == generation) {
		sa_cpy(&transport->pending->remote, remote);
		transport->pending->remote_set = true;
		err = 0;
	}
	mtx_unlock(&transport->lock);
	return err;
}


bool bundle_transport_ready(const struct bundle_transport *transport,
			    uint64_t route_generation)
{
	bool ready;

	if (!transport || !route_generation)
		return false;
	mtx_lock((mtx_t *)&transport->lock);
	ready = transport->pending &&
		transport->pending->generation == route_generation &&
		!transport->previous &&
		(!transport->pending->group ||
		 transport->pending->remote_set);
	mtx_unlock((mtx_t *)&transport->lock);
	return ready;
}


int bundle_transport_activate(struct bundle_transport *transport,
			      uint64_t route_generation)
{
	if (!transport)
		return EINVAL;

	mtx_lock(&transport->lock);
	if (!transport->pending ||
	    transport->pending->generation != route_generation ||
	    transport->previous ||
	    (transport->pending->group &&
	     !transport->pending->remote_set)) {
		mtx_unlock(&transport->lock);
		return EAGAIN;
	}

	/* Attachment changes are allocation-free and become visible together with
	 * the active route. */
	bundle_wire_attach(transport->pending);
	transport->previous = transport->active;
	transport->active = transport->pending;
	transport->pending = NULL;
	mtx_unlock(&transport->lock);
	return 0;
}


int bundle_transport_rollback(struct bundle_transport *transport,
			      uint64_t route_generation)
{
	struct bundle_wire *wire;
	bool published;
	unsigned release_count;
	int err;

	if (!transport || !route_generation)
		return EINVAL;

	mtx_lock(&transport->lock);
	if (!transport->previous || !transport->active ||
	    transport->active->generation != route_generation) {
		mtx_unlock(&transport->lock);
		return ENOENT;
	}
	wire = transport->active;
	transport->active = transport->previous;
	transport->previous = NULL;
	published = transport->publication != NULL;
	release_count = bundle_wire_restore_attachments(wire);
	mtx_unlock(&transport->lock);
	while (release_count--)
		mem_deref(transport);
	bundle_wire_release_saved_transports(wire);
	/* The coordinator holds the gate while rolling back all route groups.
	 * Quiescing a private helper here would wait for an RX
	 * callback that is itself waiting at that gate.  Retire it after the
	 * caller releases the gate instead. */
	if (published) {
		bundle_wire_retire(transport, wire);
		return 0;
	}

	err = bundle_wire_release(&wire);
	if (err) {
		bundle_wire_retire(transport, wire);
		return 0;
	}
	return err;
}


int bundle_transport_finalize(struct bundle_transport *transport,
			      uint64_t route_generation)
{
	struct bundle_wire *wire;
	unsigned release_count;
	int err;

	if (!transport || !route_generation)
		return EINVAL;

	mtx_lock(&transport->lock);
	if (!transport->previous || !transport->active ||
	    transport->active->generation != route_generation) {
		mtx_unlock(&transport->lock);
		return ENOENT;
	}
	wire = transport->previous;
	transport->previous = NULL;
	bundle_wire_finalize_attachments(transport->active);
	release_count = bundle_wire_detach_removed(wire, transport->active);
	mtx_unlock(&transport->lock);
	while (release_count--)
		mem_deref(transport);
	bundle_wire_release_saved_transports(transport->active);

	err = bundle_wire_release(&wire);
	if (err) {
		bundle_wire_retire(transport, wire);
		return 0;
	}
	return err;
}


int bundle_transport_commit(struct bundle_transport *transport,
			    uint64_t route_generation)
{
	int err;

	err = bundle_transport_activate(transport, route_generation);
	if (err)
		return err;
	return bundle_transport_finalize(transport, route_generation);
}


int bundle_transport_abort(struct bundle_transport *transport,
			   uint64_t route_generation)
{
	struct bundle_wire *wire = NULL;
	int err;

	if (!transport || !route_generation)
		return EINVAL;

	mtx_lock(&transport->lock);
	if (transport->pending &&
	    transport->pending->generation == route_generation) {
		wire = transport->pending;
		transport->pending = NULL;
	}
	mtx_unlock(&transport->lock);
	if (!wire)
		return ENOENT;
	err = bundle_wire_release(&wire);
	if (err) {
		bundle_wire_retire(transport, wire);
		return 0;
	}
	return err;
}


uint64_t bundle_transport_active_generation(
	const struct bundle_transport *transport)
{
	uint64_t generation;

	if (!transport)
		return 0;
	mtx_lock((mtx_t *)&transport->lock);
	generation = transport->active ? transport->active->generation : 0;
	mtx_unlock((mtx_t *)&transport->lock);
	return generation;
}


const struct bundle_group *bundle_transport_active_group_ref(
	const struct bundle_transport *transport)
{
	const struct bundle_group *group;

	if (!transport)
		return NULL;
	mtx_lock((mtx_t *)&transport->lock);
	group = transport->active
		? mem_ref((void *)transport->active->group) : NULL;
	mtx_unlock((mtx_t *)&transport->lock);
	return group;
}


bool bundle_transport_attached(const struct bundle_transport *transport,
			       const struct bundle *bun)
{
	bool attached;

	if (!transport || !bun)
		return false;
	mtx_lock((mtx_t *)&bun->lock);
	attached = bun->transport == transport;
	mtx_unlock((mtx_t *)&bun->lock);
	return attached;
}


enum bundle_state bundle_transport_endpoint_state(
	const struct bundle_transport *transport, const struct bundle *bun)
{
	enum bundle_state state = BUNDLE_NONE;

	if (!transport || !bun)
		return state;
	mtx_lock((mtx_t *)&transport->lock);
	if (transport->active && wire_exact_member(transport->active, bun))
		state = wire_bundle_state(transport->active, bun);
	mtx_unlock((mtx_t *)&transport->lock);
	return state;
}


enum bundle_state bundle_state(const struct bundle *bun)
{
	return bun ? bun->state : BUNDLE_NONE;
}


uint8_t bundle_extmap_mid(const struct bundle *bun)
{
	return bun ? bun->extmap_mid : 0;
}


void bundle_restore_extmap(struct bundle *bun, uint8_t extmap_mid)
{
	if (bun)
		bun->extmap_mid = extmap_mid;
}


int bundle_debug(struct re_printf *pf, const struct bundle *bun)
{
	int err = 0;

	if (!bun)
		return 0;

	err |= re_hprintf(pf, "*Bundle:\n");
	err |= re_hprintf(pf, " state:         %s\n",
			  bundle_state_name(bun->state));
	err |= re_hprintf(pf, " extmap_mid:    %u\n", bun->extmap_mid);
	err |= re_hprintf(pf, "\n");

	return err;
}
