/**
 * @file pc_transport.c Immutable PeerConnection transport topology planner
 *
 * Copyright (C) 2026 The baresip contributors
 */

#include <ctype.h>
#include <stdlib.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


struct pc_transport_member {
	struct le le;
	char *mid;
	struct stream *stream;
	bool data;
};


struct pc_transport_group {
	struct le le;
	struct list members;
	struct bundle_group *bundle;
	char *tag;
	char *ice_reuse_key;
	char *reuse_key;
	char *sctp_reuse_key;
	struct sdp_media *sdpm;
	struct stream *owner_stream;
	enum menc_dtls_role role;
	uint16_t local_sctp_port;
	uint16_t remote_sctp_port;
	bool owner_data;
	bool carries_sctp;
	const void *socket_identity;
};


struct pc_transport_generation {
	struct list groups;
};


struct nth_attr {
	const char *value;
	size_t target;
	size_t index;
};


static void member_destructor(void *arg)
{
	struct pc_transport_member *member = arg;

	list_unlink(&member->le);
	mem_deref(member->stream);
	mem_deref(member->mid);
}


static void group_destructor(void *arg)
{
	struct pc_transport_group *group = arg;

	list_unlink(&group->le);
	list_flush(&group->members);
	mem_deref(group->owner_stream);
	mem_deref(group->sdpm);
	mem_deref(group->bundle);
	mem_deref(group->sctp_reuse_key);
	mem_deref(group->reuse_key);
	mem_deref(group->ice_reuse_key);
	mem_deref(group->tag);
}


static void generation_destructor(void *arg)
{
	struct pc_transport_generation *generation = arg;

	list_flush(&generation->groups);
}


static bool nth_attr_handler(const char *name, const char *value, void *arg)
{
	struct nth_attr *nth = arg;

	(void)name;
	if (nth->index++ != nth->target)
		return false;
	nth->value = value;
	return true;
}


static const char *media_attr_nth(const struct sdp_media *sdpm,
				  const char *name, size_t index, bool local)
{
	struct nth_attr nth = {.target = index};

	if (local)
		(void)sdp_media_lattr_apply(sdpm, name, nth_attr_handler, &nth);
	else
		(void)sdp_media_rattr_apply(sdpm, name, nth_attr_handler, &nth);
	return nth.value;
}


static const char *session_attr_nth(const struct sdp_session *sdp,
				    const char *name, size_t index, bool local)
{
	struct nth_attr nth = {.target = index};

	if (local)
		(void)sdp_session_lattr_apply(sdp, name, nth_attr_handler, &nth);
	else
		(void)sdp_session_rattr_apply(sdp, name, nth_attr_handler, &nth);
	return nth.value;
}


static const char *transport_attr_nth(const struct sdp_session *sdp,
				      const struct sdp_media *sdpm,
				      const char *name, size_t index,
				      bool local)
{
	if (media_attr_nth(sdpm, name, 0, local))
		return media_attr_nth(sdpm, name, index, local);
	return session_attr_nth(sdp, name, index, local);
}


static int string_pointer_compare(const void *left, const void *right)
{
	const char *const *a = left;
	const char *const *b = right;

	return strcmp(*a, *b);
}


static int normalize_fingerprint(char **normalizedp, const char *value)
{
	struct mbuf *mb;
	int err = 0;

	if (!normalizedp || !value)
		return EINVAL;
	mb = mbuf_alloc(str_len(value) + 1);
	if (!mb)
		return ENOMEM;
	for (; *value; ++value) {
		unsigned char ch = (unsigned char)*value;

		if (ch == ':' || isspace(ch))
			continue;
		err = mbuf_write_u8(mb, (uint8_t)tolower(ch));
		if (err)
			break;
	}
	if (!err) {
		mb->pos = 0;
		err = mbuf_strdup(mb, normalizedp, mbuf_get_left(mb));
	}
	mem_deref(mb);
	return err;
}


static int resolved_role(enum menc_dtls_role *rolep,
			 const struct sdp_session *sdp,
			 const struct sdp_media *sdpm)
{
	const char *remote = transport_attr_nth(
		sdp, sdpm, "setup", 0, false);
	const char *local = transport_attr_nth(
		sdp, sdpm, "setup", 0, true);
	enum menc_dtls_role role = MENC_DTLS_ROLE_UNKNOWN;

	if (!rolep)
		return EINVAL;
	if (remote && !str_casecmp(remote, "active")) {
		if (local && !str_casecmp(local, "active"))
			return EPROTO;
		role = MENC_DTLS_ROLE_SERVER;
	}
	else if (remote && !str_casecmp(remote, "passive")) {
		if (local && !str_casecmp(local, "passive"))
			return EPROTO;
		role = MENC_DTLS_ROLE_CLIENT;
	}
	else if (remote && str_casecmp(remote, "actpass")) {
		return EPROTO;
	}
	else if (local && !str_casecmp(local, "active")) {
		role = MENC_DTLS_ROLE_CLIENT;
	}
	else if (local && !str_casecmp(local, "passive")) {
		role = MENC_DTLS_ROLE_SERVER;
	}
	else if (local && str_casecmp(local, "actpass")) {
		return EPROTO;
	}
	*rolep = role;
	return 0;
}


static int build_reuse_key(char **keyp, const struct sdp_session *sdp,
			   const struct pc_transport_group *group)
{
	const char *rtls_id = transport_attr_nth(
		sdp, group->sdpm, "tls-id", 0, false);
	const char *ltls_id = transport_attr_nth(
		sdp, group->sdpm, "tls-id", 0, true);
	const char *proto = sdp_media_proto(group->sdpm);
	char **remote_fingerprints = NULL;
	char **local_fingerprints = NULL;
	struct mbuf *key = NULL;
	size_t remote_count = 0;
	size_t local_count = 0;
	int err = 0;

	while (transport_attr_nth(sdp, group->sdpm, "fingerprint",
				  remote_count, false))
		++remote_count;
	while (transport_attr_nth(sdp, group->sdpm, "fingerprint",
				  local_count, true))
		++local_count;
	if (remote_count) {
		remote_fingerprints = mem_zalloc(
			remote_count * sizeof(*remote_fingerprints), NULL);
		if (!remote_fingerprints)
			return ENOMEM;
	}
	if (local_count) {
		local_fingerprints = mem_zalloc(
			local_count * sizeof(*local_fingerprints), NULL);
		if (!local_fingerprints) {
			err = ENOMEM;
			goto out;
		}
	}
	for (size_t i = 0; i < remote_count; ++i) {
		err = normalize_fingerprint(&remote_fingerprints[i],
			transport_attr_nth(sdp, group->sdpm, "fingerprint", i,
					   false));
		if (err)
			goto out;
	}
	for (size_t i = 0; i < local_count; ++i) {
		err = normalize_fingerprint(&local_fingerprints[i],
			transport_attr_nth(sdp, group->sdpm, "fingerprint", i,
					   true));
		if (err)
			goto out;
	}
	if (remote_count > 1)
		qsort(remote_fingerprints, remote_count,
		      sizeof(*remote_fingerprints),
		      string_pointer_compare);
	if (local_count > 1)
		qsort(local_fingerprints, local_count,
		      sizeof(*local_fingerprints), string_pointer_compare);

	key = mbuf_alloc(256);
	if (!key) {
		err = ENOMEM;
		goto out;
	}
	err = mbuf_printf(key,
		"owner=%s|proto=%zu:%s|"
		"ltls-id=%u:%zu:%s|rtls-id=%u:%zu:%s|"
		"lfp=%zu:",
		group->owner_data ? "data" : "rtp",
		proto ? str_len(proto) : 0, proto ? proto : "",
		ltls_id != NULL, ltls_id ? str_len(ltls_id) : 0,
		ltls_id ? ltls_id : "", rtls_id != NULL,
		rtls_id ? str_len(rtls_id) : 0, rtls_id ? rtls_id : "",
		local_count);
	for (size_t i = 0; !err && i < local_count; ++i)
		err = mbuf_printf(key, "%s%s", i ? "," : "",
				  local_fingerprints[i]);
	if (!err)
		err = mbuf_printf(key, "|rfp=%zu:", remote_count);
	for (size_t i = 0; !err && i < remote_count; ++i)
		err = mbuf_printf(key, "%s%s", i ? "," : "",
				  remote_fingerprints[i]);
	if (!err) {
		key->pos = 0;
		err = mbuf_strdup(key, keyp, mbuf_get_left(key));
	}

out:
	for (size_t i = 0; i < remote_count; ++i)
		mem_deref(remote_fingerprints[i]);
	for (size_t i = 0; i < local_count; ++i)
		mem_deref(local_fingerprints[i]);
	mem_deref(remote_fingerprints);
	mem_deref(local_fingerprints);
	mem_deref(key);
	return err;
}


static int build_ice_reuse_key(char **keyp, const struct sdp_session *sdp,
			       const struct pc_transport_group *group)
{
	const char *rufrag = transport_attr_nth(
		sdp, group->sdpm, "ice-ufrag", 0, false);
	const char *rpwd = transport_attr_nth(
		sdp, group->sdpm, "ice-pwd", 0, false);
	const char *lufrag = transport_attr_nth(
		sdp, group->sdpm, "ice-ufrag", 0, true);
	const char *lpwd = transport_attr_nth(
		sdp, group->sdpm, "ice-pwd", 0, true);
	return re_sdprintf(keyp,
		"lice=%u:%zu:%s:%u:%zu:%s|rice=%u:%zu:%s:%u:%zu:%s",
		lufrag != NULL, lufrag ? str_len(lufrag) : 0,
		lufrag ? lufrag : "", lpwd != NULL,
		lpwd ? str_len(lpwd) : 0, lpwd ? lpwd : "",
		rufrag != NULL, rufrag ? str_len(rufrag) : 0,
		rufrag ? rufrag : "", rpwd != NULL,
		rpwd ? str_len(rpwd) : 0, rpwd ? rpwd : "");
}


static const char *stream_mid_value(const struct stream *stream)
{
	const char *mid = sdp_media_rattr(stream_sdpmedia(stream), "mid");

	return str_isset(mid) ? mid : stream_mid(stream);
}


static struct stream *find_stream(const struct list *streaml, const char *mid)
{
	struct le *le;

	for (le = list_head(streaml); le; le = le->next) {
		struct stream *stream = le->data;

		if (!str_cmp(stream_mid_value(stream), mid))
			return stream;
	}
	return NULL;
}


static int validate_mid_ownership(const struct list *streaml,
				  const struct pc_transport_data *data)
{
	struct le *outer;

	for (outer = list_head(streaml); outer; outer = outer->next) {
		const struct stream *stream = outer->data;
		const char *mid = stream_mid_value(stream);
		struct le *inner;

		if (!str_isset(mid))
			return EPROTO;
		if (data && !str_cmp(mid, data->mid))
			return EPROTO;
		for (inner = outer->next; inner; inner = inner->next) {
			const struct stream *other = inner->data;

			if (!str_cmp(mid, stream_mid_value(other)))
				return EPROTO;
		}
	}
	return 0;
}


static bool stream_accepted(const struct stream *stream, bool bundled)
{
	const struct sdp_media *sdpm = stream_sdpmedia(stream);

	return sdp_media_has_media(sdpm) ||
		(bundled && sdp_media_rformat(sdpm, NULL) &&
		 sdp_media_rattr(sdpm, "bundle-only"));
}


static int append_member(struct pc_transport_group *group, const char *mid,
			 struct stream *stream, bool data)
{
	struct pc_transport_member *member;
	int err;

	member = mem_zalloc(sizeof(*member), member_destructor);
	if (!member)
		return ENOMEM;
	err = str_dup(&member->mid, mid);
	if (err) {
		mem_deref(member);
		return err;
	}
	member->stream = mem_ref(stream);
	member->data = data;
	list_append(&group->members, &member->le, member);
	return 0;
}


static int capture_exact_bundle(struct bundle_group **bundlep,
				const struct list *members)
{
	struct sdp_session *local = NULL;
	struct sdp_session *remote = NULL;
	struct bundle_set *set = NULL;
	struct mbuf *encoded = NULL;
	struct mbuf *value = NULL;
	struct sa address;
	struct le *le;
	char *group_value = NULL;
	int err;

	if (!bundlep || !list_count(members))
		return EINVAL;
	value = mbuf_alloc(64);
	if (!value)
		return ENOMEM;
	err = mbuf_printf(value, "BUNDLE");
	for (le = list_head(members); !err && le; le = le->next) {
		const struct pc_transport_member *member = le->data;

		err = mbuf_printf(value, " %s", member->mid);
	}
	if (err)
		goto out;
	value->pos = 0;
	err = mbuf_strdup(value, &group_value, mbuf_get_left(value));
	if (err)
		goto out;
	sa_set_str(&address, "127.0.0.1", 9);
	err = sdp_session_alloc(&local, &address);
	if (!err)
		err = sdp_session_alloc(&remote, &address);
	if (!err)
		err = sdp_session_set_lattr(local, false, "group", "%s",
					    group_value);
	if (!err)
		err = sdp_encode(&encoded, local, true);
	if (!err)
		err = sdp_decode(remote, encoded, true);
	if (!err)
		err = bundle_set_decode(&set, remote);
	if (!err && (!set || bundle_set_count(set) != 1))
		err = EPROTO;
	if (!err)
		err = bundle_group_clone(bundlep, bundle_set_group(set, 0));

out:
	mem_deref(group_value);
	mem_deref(value);
	mem_deref(encoded);
	mem_deref(set);
	mem_deref(remote);
	mem_deref(local);
	return err;
}


static int finalize_group(struct pc_transport_group *group,
			  const struct sdp_session *sdp,
			  const struct pc_transport_data *data)
{
	struct pc_transport_member *owner = list_ledata(list_head(&group->members));
	int err;

	if (!owner)
		return EPROTO;
	err = capture_exact_bundle(&group->bundle, &group->members);
	if (err)
		return err;
	err = str_dup(&group->tag, owner->mid);
	if (err)
		return err;
	group->owner_data = owner->data;
	group->owner_stream = mem_ref(owner->stream);
	group->socket_identity = owner->data
		? (data->socket_identity ? data->socket_identity
					 : (const void *)data->sdpm)
		: (const void *)rtp_sock(stream_rtp_sock(owner->stream));
	group->sdpm = mem_ref(owner->data ? data->sdpm
					       : stream_sdpmedia(owner->stream));
	err = resolved_role(&group->role, sdp, group->sdpm);
	if (err)
		return err;
	if (group->carries_sctp) {
		group->local_sctp_port = data->local_sctp_port;
		group->remote_sctp_port = data->remote_sctp_port;
	}
	err = build_reuse_key(&group->reuse_key, sdp, group);
	if (err)
		return err;
	err = build_ice_reuse_key(&group->ice_reuse_key, sdp, group);
	if (err)
		return err;
	return re_sdprintf(&group->sctp_reuse_key,
		"%s|carries-sctp=%u|ports=%u:%u", group->reuse_key,
		group->carries_sctp, group->local_sctp_port,
		group->remote_sctp_port);
}


static int append_bundle_group(struct pc_transport_generation *generation,
			       const struct sdp_session *sdp,
			       const struct list *streaml,
			       const struct pc_transport_data *data,
			       const struct bundle_group *source)
{
	struct pc_transport_group *group;
	const char *source_tag = bundle_group_tag(source);
	int err = 0;

	group = mem_zalloc(sizeof(*group), group_destructor);
	if (!group)
		return ENOMEM;
	for (size_t i = 0; i < bundle_group_count(source); ++i) {
		const char *mid = bundle_group_mid(source, i);
		struct stream *stream = find_stream(streaml, mid);
		bool is_data = data && !str_cmp(data->mid, mid);
		bool accepted = is_data ? data->accepted
					: stream && stream_accepted(stream, true);

		if (!accepted)
			continue;
		err = append_member(group, mid, stream, is_data);
		if (err)
			goto out;
		group->carries_sctp |= is_data;
	}
	if (!list_count(&group->members))
		goto out;
	/* A rejected BUNDLE tag cannot own a runtime for accepted members. */
	if (str_cmp(source_tag,
		    ((struct pc_transport_member *)list_head(
			    &group->members)->data)->mid)) {
		err = EPROTO;
		goto out;
	}
	err = finalize_group(group, sdp, data);
	if (err)
		goto out;
	list_append(&generation->groups, &group->le, group);
	return 0;

out:
	mem_deref(group);
	return err;
}


static int append_singleton(struct pc_transport_generation *generation,
			    const struct sdp_session *sdp, const char *mid,
			    struct stream *stream,
			    const struct pc_transport_data *data,
			    bool is_data)
{
	struct pc_transport_group *group;
	int err;

	group = mem_zalloc(sizeof(*group), group_destructor);
	if (!group)
		return ENOMEM;
	err = append_member(group, mid, stream, is_data);
	if (err)
		goto out;
	group->carries_sctp = is_data;
	err = finalize_group(group, sdp, data);
	if (err)
		goto out;
	list_append(&generation->groups, &group->le, group);
	return 0;

out:
	mem_deref(group);
	return err;
}


int pc_transport_generation_alloc(
	struct pc_transport_generation **generationp,
	const struct sdp_session *sdp, const struct list *streaml,
	const struct pc_transport_data *data)
{
	struct pc_transport_generation *generation;
	struct bundle_set *set = NULL;
	struct le *le;
	int err;

	if (!generationp || !sdp || !streaml ||
	    (data && (!str_isset(data->mid) || !data->sdpm)))
		return EINVAL;
	*generationp = NULL;
	err = bundle_set_decode(&set, sdp);
	if (err)
		return err;
	err = validate_mid_ownership(streaml, data);
	if (err)
		goto out_set;
	err = set ? bundle_set_validate(set, streaml,
					data ? data->mid : NULL) : 0;
	if (err)
		goto out_set;
	generation = mem_zalloc(sizeof(*generation), generation_destructor);
	if (!generation) {
		err = ENOMEM;
		goto out_set;
	}
	for (size_t i = 0; set && i < bundle_set_count(set); ++i) {
		err = append_bundle_group(generation, sdp, streaml, data,
					  bundle_set_group(set, i));
		if (err)
			goto out;
	}
	for (le = list_head(streaml); le; le = le->next) {
		struct stream *stream = le->data;
		const char *mid = stream_mid_value(stream);

		if (!str_isset(mid)) {
			err = EPROTO;
			goto out;
		}
		if ((set && bundle_set_find_mid(set, mid)) ||
		    !stream_accepted(stream, false))
			continue;
		err = append_singleton(generation, sdp, mid, stream, data, false);
		if (err)
			goto out;
	}
	if (data && data->accepted &&
	    (!set || !bundle_set_find_mid(set, data->mid))) {
		err = append_singleton(generation, sdp, data->mid, NULL,
				       data, true);
		if (err)
			goto out;
	}
	mem_deref(set);
	*generationp = generation;
	return 0;

out:
	mem_deref(generation);
out_set:
	mem_deref(set);
	return err;
}


size_t pc_transport_generation_count(
	const struct pc_transport_generation *generation)
{
	return generation ? list_count(&generation->groups) : 0;
}


const struct pc_transport_group *pc_transport_generation_group(
	const struct pc_transport_generation *generation, size_t index)
{
	struct le *le;

	if (!generation)
		return NULL;
	le = list_head(&generation->groups);
	while (le && index--)
		le = le->next;
	return le ? le->data : NULL;
}


const struct pc_transport_group *pc_transport_generation_find_mid(
	const struct pc_transport_generation *generation, const char *mid)
{
	for (size_t i = 0; i < pc_transport_generation_count(generation); ++i) {
		const struct pc_transport_group *group =
			pc_transport_generation_group(generation, i);

		for (size_t j = 0; j < pc_transport_group_member_count(group); ++j)
			if (!str_cmp(mid, pc_transport_member_mid(
				pc_transport_group_member(group, j))))
				return group;
	}
	return NULL;
}


const struct pc_transport_group *pc_transport_generation_find_reusable(
	const struct pc_transport_generation *generation,
	const struct pc_transport_group *wanted)
{
	for (size_t i = 0; i < pc_transport_generation_count(generation); ++i) {
		const struct pc_transport_group *group =
			pc_transport_generation_group(generation, i);

		if (pc_transport_group_reuses(group, wanted))
			return group;
	}
	return NULL;
}


const char *pc_transport_group_tag(const struct pc_transport_group *group)
{
	return group ? group->tag : NULL;
}


const struct bundle_group *pc_transport_group_bundle(
	const struct pc_transport_group *group)
{
	return group ? group->bundle : NULL;
}


const char *pc_transport_group_reuse_key(const struct pc_transport_group *group)
{
	return group ? group->reuse_key : NULL;
}


const char *pc_transport_group_ice_reuse_key(
	const struct pc_transport_group *group)
{
	return group ? group->ice_reuse_key : NULL;
}


const char *pc_transport_group_sctp_reuse_key(
	const struct pc_transport_group *group)
{
	return group ? group->sctp_reuse_key : NULL;
}


const void *pc_transport_group_socket_identity(
	const struct pc_transport_group *group)
{
	return group ? group->socket_identity : NULL;
}


struct sdp_media *pc_transport_group_sdpmedia(
	const struct pc_transport_group *group)
{
	return group ? group->sdpm : NULL;
}


struct stream *pc_transport_group_owner_stream(
	const struct pc_transport_group *group)
{
	return group ? group->owner_stream : NULL;
}


const struct sa *pc_transport_group_remote(
	const struct pc_transport_group *group)
{
	const struct sa *remote = group && group->owner_stream
				  ? stream_raddr(group->owner_stream) : NULL;

	/* ICE owns the committed peer for a media-tagged BUNDLE group. Browser
	 * SDP commonly retains a discard address after nomination. */
	if (remote && sa_isset(remote, SA_ALL))
		return remote;
	return group ? sdp_media_raddr(group->sdpm) : NULL;
}


bool pc_transport_group_owner_is_data(const struct pc_transport_group *group)
{
	return group && group->owner_data;
}


bool pc_transport_group_carries_sctp(const struct pc_transport_group *group)
{
	return group && group->carries_sctp;
}


enum menc_dtls_role pc_transport_group_role(
	const struct pc_transport_group *group)
{
	return group ? group->role : MENC_DTLS_ROLE_UNKNOWN;
}


uint16_t pc_transport_group_local_sctp_port(
	const struct pc_transport_group *group)
{
	return group ? group->local_sctp_port : 0;
}


uint16_t pc_transport_group_remote_sctp_port(
	const struct pc_transport_group *group)
{
	return group ? group->remote_sctp_port : 0;
}


size_t pc_transport_group_member_count(const struct pc_transport_group *group)
{
	return group ? list_count(&group->members) : 0;
}


const struct pc_transport_member *pc_transport_group_member(
	const struct pc_transport_group *group, size_t index)
{
	struct le *le;

	if (!group)
		return NULL;
	le = list_head(&group->members);
	while (le && index--)
		le = le->next;
	return le ? le->data : NULL;
}


const char *pc_transport_member_mid(const struct pc_transport_member *member)
{
	return member ? member->mid : NULL;
}


struct stream *pc_transport_member_stream(
	const struct pc_transport_member *member)
{
	return member ? member->stream : NULL;
}


bool pc_transport_member_is_data(const struct pc_transport_member *member)
{
	return member && member->data;
}


bool pc_transport_group_reuses(const struct pc_transport_group *left,
			       const struct pc_transport_group *right)
{
	return left && right &&
		left->socket_identity == right->socket_identity &&
		!str_cmp(left->reuse_key, right->reuse_key);
}


bool pc_transport_group_reuses_ice(const struct pc_transport_group *left,
				   const struct pc_transport_group *right)
{
	return left && right &&
		left->socket_identity == right->socket_identity &&
		!str_cmp(left->ice_reuse_key, right->ice_reuse_key);
}


bool pc_transport_group_reuses_sctp(const struct pc_transport_group *left,
				    const struct pc_transport_group *right)
{
	return pc_transport_group_reuses(left, right) &&
		left->carries_sctp && right->carries_sctp &&
		!str_cmp(left->sctp_reuse_key, right->sctp_reuse_key);
}
