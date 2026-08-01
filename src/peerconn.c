/**
 * @file src/peerconn.c  RTC Peer Connection
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


enum { AUDIO_PTIME = 20 };


struct pc_stream_state {
	struct le le;
	struct stream_jsep_state *state;
};


struct pc_media_state {
	struct list stream_states;
};


struct pending_candidate {
	struct le le;
	char *candidate;
	char *mid;
};


struct pc_adopted_runtime {
	struct le le;
	const struct pc_transport_group *group;
	struct media_transport *transport;
};


static void pc_stream_state_destructor(void *data)
{
	struct pc_stream_state *entry = data;

	list_unlink(&entry->le);
	mem_deref(entry->state);
}


static void pc_media_state_destructor(void *data)
{
	struct pc_media_state *state = data;

	list_flush(&state->stream_states);
}


static int pc_media_state_save(struct pc_media_state **statep,
			       const struct list *streaml)
{
	struct pc_media_state *state;
	const struct le *le;
	int err;

	if (!statep || !streaml)
		return EINVAL;
	state = mem_zalloc(sizeof(*state), pc_media_state_destructor);
	if (!state)
		return ENOMEM;
	for (le = list_head(streaml); le; le = le->next) {
		struct pc_stream_state *entry;

		entry = mem_zalloc(sizeof(*entry), pc_stream_state_destructor);
		if (!entry) {
			err = ENOMEM;
			goto out;
		}
		err = stream_jsep_state_save(&entry->state, le->data);
		if (err) {
			mem_deref(entry);
			goto out;
		}
		list_append(&state->stream_states, &entry->le, entry);
	}
	*statep = state;
	return 0;

out:
	mem_deref(state);
	return err;
}


static void pc_media_state_restore(struct pc_media_state *state)
{
	struct le *le;

	if (!state)
		return;
	for (le = list_head(&state->stream_states); le; le = le->next) {
		struct pc_stream_state *entry = le->data;

		stream_jsep_state_restore(entry->state);
	}
}


static void pending_candidate_destructor(void *data)
{
	struct pending_candidate *candidate = data;

	list_unlink(&candidate->le);
	mem_deref(candidate->candidate);
	mem_deref(candidate->mid);
}


#ifdef USE_DATACHANNEL
static void pc_adopted_runtime_destructor(void *data)
{
	struct pc_adopted_runtime *entry = data;

	list_unlink(&entry->le);
	mem_deref(entry->transport);
	mem_deref((void *)entry->group);
}
#endif


struct peer_connection {
	struct stream_param stream_prm;
	struct list streaml;             /* XXX: remove, use medial */
	struct list medial;
	struct list pending_candidates;
	struct list adopted_transport_runtimes;
	struct list restart_transport_runtimes;
	struct sdp_session *sdp;
	struct sdp_session_state *pending_sdp;
	struct pc_transport_generation *active_transport_topology;
	struct pc_transport_generation *candidate_transport_topology;
	struct pc_transport_session *transport_session;
	const struct mnat *mnat;
	struct mnat_sess *mnats;
	const struct menc *menc;
	struct menc_sess *mencs;
#ifdef USE_DATACHANNEL
	struct data_context *data;
#endif
	char cname[16];
	enum signaling_st signaling_state;
	peerconnection_gather_h *gatherh;
	peerconnection_estab_h *estabh;
	peerconnection_close_h *closeh;
	void *arg;

	/* steps: */
	bool gather_ok;
	bool offerer;
	bool transport_session_staged;
	bool transport_session_started;
	bool ice_restart_requested;
	bool ice_restart_offer_pending;
	unsigned sdp_enc_ok;
	unsigned sdp_dec_ok;
};


static void pc_close(struct peer_connection *pc, int err);
static void transport_generation_retire(
	const struct pc_transport_generation *generation, void *arg);
#ifdef USE_DATACHANNEL
static int try_bootstrap_transport_session(struct peer_connection *pc);
static const struct pc_adopted_runtime *runtime_entry(
	const struct peer_connection *pc, const struct pc_transport_group *group);
static struct pc_adopted_runtime *restart_runtime_entry(
	const struct peer_connection *pc,
	const struct pc_transport_group *active_group);
static struct pc_adopted_runtime *prepared_runtime_entry(
	const struct peer_connection *pc,
	const struct pc_transport_group *group);
static int prepared_fresh_gather_status(const struct peer_connection *pc);
#endif


static void transport_generation_publish(
	const struct pc_transport_generation *generation, void *arg)
{
	struct peer_connection *pc = arg;

	/* Called under the session publication gate.  Reference transfers only:
	 * no allocation, failure, or application callback is permitted here. */
	pc->active_transport_topology =
		mem_deref(pc->active_transport_topology);
	pc->active_transport_topology = mem_ref((void *)generation);
	pc->candidate_transport_topology =
		mem_deref(pc->candidate_transport_topology);
	pc->transport_session_staged = false;
	pc->transport_session_started = false;
}


#ifdef USE_DATACHANNEL
static void abort_candidate_transport_generation(struct peer_connection *pc)
{
	if (!pc)
		return;
	if (pc->transport_session_staged || pc->candidate_transport_topology)
		pc_transport_session_abort(pc->transport_session);
	pc->candidate_transport_topology =
		mem_deref(pc->candidate_transport_topology);
	pc->transport_session_staged = false;
	pc->transport_session_started = false;
	if (pc->active_transport_topology)
		transport_generation_retire(pc->active_transport_topology, pc);
}
#endif


static void transport_session_error(int err, void *arg)
{
	struct peer_connection *pc = arg;

	/* Once a stable description has returned to the application, a later
	 * transport failure cannot roll that signaling transaction back.  Do not
	 * silently retain the old runtime under the newly signaled ICE identity:
	 * expose an unambiguous terminal PeerConnection failure instead. */
	pc_close(pc, err);
}


static void transport_generation_retire(
	const struct pc_transport_generation *generation, void *arg)
{
	struct peer_connection *pc = arg;
	struct le *le;
	struct le *next;

	for (le = pc->adopted_transport_runtimes.head; le; le = next) {
		struct pc_adopted_runtime *entry = le->data;
		bool group_retained = false;
		bool runtime_retained = false;

		next = le->next;
		for (size_t i = 0; i < pc_transport_generation_count(generation);
		     ++i) {
			if (entry->group ==
			    pc_transport_generation_group(generation, i)) {
				group_retained = true;
				break;
			}
		}
		if (group_retained)
			continue;
		for (struct le *other_le =
			     pc->adopted_transport_runtimes.head;
			     other_le; other_le = other_le->next) {
				struct pc_adopted_runtime *other = other_le->data;

				if (other == entry ||
				    other->transport != entry->transport)
					continue;
				for (size_t i = 0;
				     i < pc_transport_generation_count(generation);
				     ++i) {
					if (other->group ==
					    pc_transport_generation_group(generation,
								  i)) {
						runtime_retained = true;
						break;
					}
				}
				if (runtime_retained)
					break;
		}
		if (!runtime_retained)
			media_transport_release(entry->transport);
		mem_deref(entry);
	}
}



#ifdef USE_DATACHANNEL
static const char *candidate_ufrag(const char *candidate, struct pl *value)
{
	struct pl input;
	struct pl token;

	if (!candidate || !value)
		return NULL;
	input.p = candidate;
	input.l = str_len(candidate);
	while (re_regex(input.p, input.l, "[ \t]*[^ \t]+", NULL, &token) == 0) {
		size_t consumed = (size_t)(token.p + token.l - input.p);

		input.p += consumed;
		input.l -= consumed;
		if (pl_strcasecmp(&token, "ufrag"))
			continue;
		if (re_regex(input.p, input.l, "[ \t]*[^ \t]+", NULL, value))
			return NULL;
		return value->p;
	}
	return NULL;
}


static bool candidate_matches_generation(
	const struct peer_connection *pc,
	const struct pc_transport_group *group, const char *candidate)
{
	const char *ufrag;
	struct pl supplied = PL_INIT;

	if (!candidate_ufrag(candidate, &supplied))
		return true;
	ufrag = sdp_media_session_rattr(pc_transport_group_sdpmedia(group),
					pc->sdp, "ice-ufrag");
	return !ufrag || !pl_strcmp(&supplied, ufrag);
}


static bool apply_restart_candidate(struct peer_connection *pc,
				    const char *cand, const char *mid)
{
	const struct pc_transport_group *target;
	const struct pc_transport_group *active;
	const struct pc_adopted_runtime *entry;

	if (!pc->candidate_transport_topology ||
	    !pc->active_transport_topology)
		return false;
	target = pc_transport_generation_find_mid(
		pc->candidate_transport_topology, mid);
	if (!target)
		return false;
	active = pc_transport_generation_find_reusable(
		pc->active_transport_topology, target);
	if (!active || pc_transport_group_reuses_ice(active, target))
		return false;

	/* An optional candidate ufrag identifies its ICE generation.  Consume a
	 * stale candidate instead of allowing it to fall through to the active
	 * checklist.  Candidates without the extension remain interoperable and
	 * are scoped by the pending description transaction. */
	if (!candidate_matches_generation(pc, target, cand))
		return true;

	entry = restart_runtime_entry(pc, active);
	if (!entry)
		entry = runtime_entry(pc, target);
	if (entry)
		(void)media_transport_mnat_attr(entry->transport, "candidate",
					 cand);
	/* The candidate belongs to the replacement credentials even if their
	 * runtime has already failed or been aborted.  Never leak it into the
	 * still-active checklist. */
	return true;
}


static bool apply_prepared_fresh_candidate(struct peer_connection *pc,
					   const char *candidate,
					   const char *mid)
{
	struct pc_adopted_runtime *entry = NULL;
	const struct pc_transport_group *target = NULL;

	/* Remote-offer split lowers are prepared before createAnswer(), while the
	 * final candidate topology does not exist yet.  Resolve that provisional
	 * generation from its own exact membership instead of falling through to
	 * the still-active checklist. */
	for (struct le *le = pc->adopted_transport_runtimes.head; le;
	     le = le->next) {
		struct pc_adopted_runtime *candidate_entry = le->data;
		const struct bundle_group *bundle;
		bool active = false;

		for (size_t i = 0; pc->active_transport_topology &&
		     i < pc_transport_generation_count(
			     pc->active_transport_topology); ++i) {
			if (pc_transport_generation_group(
				    pc->active_transport_topology, i) ==
			    candidate_entry->group) {
				active = true;
				break;
			}
		}
		if (active)
			continue;
		bundle = pc_transport_group_bundle(candidate_entry->group);
		if (!bundle || !bundle_group_contains(bundle, mid))
			continue;
		entry = candidate_entry;
		target = candidate_entry->group;
		break;
	}
	if (!entry)
		return false;
	/* Once a candidate is scoped to the fresh credentials it is consumed even
	 * if stale or the exact runtime has already reached a terminal state. */
	if (candidate_matches_generation(pc, target, candidate))
		(void)media_transport_mnat_attr(entry->transport, "candidate",
					 candidate);
	return true;
}


static bool candidate_is_stale(const struct peer_connection *pc,
			       const char *candidate, const char *mid)
{
	const struct pc_transport_group *group;

	if (!pc->active_transport_topology)
		return false;
	group = pc_transport_generation_find_mid(pc->active_transport_topology,
						 mid);
	return group && !candidate_matches_generation(pc, group, candidate);
}


static bool apply_active_candidate(struct peer_connection *pc,
				   const char *candidate, const char *mid)
{
	const struct pc_transport_group *group;
	const struct pc_adopted_runtime *entry;

	if (!pc->active_transport_topology)
		return false;
	group = pc_transport_generation_find_mid(pc->active_transport_topology,
						 mid);
	if (!group)
		return false;
	entry = runtime_entry(pc, group);
	return entry && media_transport_mnat_attr(entry->transport, "candidate",
						  candidate);
}
#endif


static void apply_remote_candidate(struct peer_connection *pc,
				   const char *cand, const char *mid)
{
	struct stream *strm = stream_lookup_mid(&pc->streaml, mid,
						 str_len(mid));

#ifdef USE_DATACHANNEL
	if (apply_restart_candidate(pc, cand, mid))
		return;
	if (apply_prepared_fresh_candidate(pc, cand, mid))
		return;
	if (candidate_is_stale(pc, cand, mid))
		return;
	/* Coordinator-owned groups are authoritative after publication.  This is
	 * essential for a data-owned BUNDLE tag: no stream exists whose legacy
	 * MNAT pointer can be updated, and data_context deliberately retains its
	 * stable SCTP binding across an ICE-only lower replacement. */
	if (apply_active_candidate(pc, cand, mid))
		return;
#endif
	if (strm)
		stream_mnat_attr(strm, "candidate", cand);
#ifdef USE_DATACHANNEL
	else if (pc->data)
		(void)data_context_mnat_attr(pc->data, mid, "candidate", cand);
#endif
}


static void apply_pending_candidates(struct peer_connection *pc)
{
	struct le *le;

	while ((le = list_head(&pc->pending_candidates))) {
		struct pending_candidate *candidate = le->data;

		apply_remote_candidate(pc, candidate->candidate, candidate->mid);
		mem_deref(candidate);
	}
}


static const char *signaling_state_name(enum signaling_st ss)
{
	switch (ss) {

	case SS_STABLE:            return "stable";
	case SS_HAVE_LOCAL_OFFER:  return "have-local-offer";
	case SS_HAVE_REMOTE_OFFER: return "have-remote-offer";
	default: return "???";
	}
}


static void pc_summary(const struct peer_connection *pc)
{
	size_t i = 0;

	info("*** RTCPeerConnection summary ***\n");

	info("signaling_state: %s\n",
	     signaling_state_name(pc->signaling_state));

	info("steps:\n");
	info(".. gather:   %d\n", pc->gather_ok);
	info(".. sdp_enc:  %u\n", pc->sdp_enc_ok);
	info(".. sdp_dec:  %u\n", pc->sdp_dec_ok);
	info("\n");

	for (struct le *le = pc->medial.head; le; le = le->next, ++i) {
		struct media_track *media = le->data;

		info(".. #%zu '%s'\n", i,
		     media_kind_name(mediatrack_kind(media)));

		mediatrack_summary(media);
	}

	info("\n");
}


static void pc_close(struct peer_connection *pc, int err)
{
	peerconnection_close_h *closeh = pc->closeh;

	pc->closeh = NULL;

	if (closeh)
		closeh(err, pc->arg);
}


static void destructor(void *data)
{
	struct peer_connection *pc = data;
	struct le *le;

	pc_summary(pc);

	for (le = pc->medial.head; le; le = le->next) {
		struct media_track *media = le->data;

		debug("%H\n", mediatrack_debug, media);
	}

	for (le = pc->medial.head; le; le = le->next)
		mediatrack_stop(le->data);
	for (le = pc->streaml.head; le; le = le->next)
		stream_stop(le->data);
	pc_transport_session_stop(pc->transport_session);
	for (le = pc->adopted_transport_runtimes.head; le; le = le->next) {
		struct pc_adopted_runtime *entry = le->data;

		media_transport_stop_members(entry->transport);
		media_transport_retire_callbacks(entry->transport);
		media_transport_detach_consumer(entry->transport);
		media_transport_release(entry->transport);
	}
	for (le = pc->restart_transport_runtimes.head; le; le = le->next) {
		struct pc_adopted_runtime *entry = le->data;

		media_transport_stop_members(entry->transport);
		media_transport_retire_callbacks(entry->transport);
		media_transport_detach_consumer(entry->transport);
		media_transport_release(entry->transport);
	}

	list_flush(&pc->pending_candidates);
	list_flush(&pc->restart_transport_runtimes);
	/* Transport generations retain streams.  Release every such owner while
	 * the media tracks (and their RTP receive callback arguments) are still
	 * alive; otherwise a retained stream can run a jitter-buffer timer after
	 * its audio/video object has already been destroyed. */
	pc->transport_session = mem_deref(pc->transport_session);
	list_flush(&pc->adopted_transport_runtimes);
	pc->candidate_transport_topology =
		mem_deref(pc->candidate_transport_topology);
	pc->active_transport_topology =
		mem_deref(pc->active_transport_topology);
	list_flush(&pc->medial);
#ifdef USE_DATACHANNEL
	pc->data = mem_deref(pc->data);
#endif

	pc_close(pc, 0);

	mem_deref(pc->sdp);
	mem_deref(pc->pending_sdp);
	mem_deref(pc->mnats);
	mem_deref(pc->mencs);
}


/* Derive the complete accepted transport topology while the full SDP
 * transaction is still rollback-capable.  The active topology is deliberately
 * retained until peerconnection_start_ice() has published and finalized every
 * runtime change, so endpoint owners cannot disappear between signaling and
 * transport publication. */
static int stage_transport_topology(
	struct pc_transport_generation **generationp,
	struct peer_connection *pc)
{
	struct pc_transport_generation *generation = NULL;
	const struct pc_transport_data *datap = NULL;
#ifdef USE_DATACHANNEL
	struct pc_transport_data data;
#endif
	int err;

	if (!generationp || !pc)
		return EINVAL;
	*generationp = NULL;
#ifdef USE_DATACHANNEL
	if (pc->data) {
		memset(&data, 0, sizeof(data));
		data.mid = data_context_mid(pc->data);
		data.sdpm = data_context_sdpmedia(pc->data);
		data.socket_identity = data_context_socket_identity(pc->data);
		data.accepted = data_context_transport_accepted(pc->data);
		data.local_sctp_port =
			data_context_local_sctp_port(pc->data);
		data.remote_sctp_port =
			data_context_remote_sctp_port(pc->data);
		datap = &data;
	}
#endif
	err = pc_transport_generation_alloc(&generation, pc->sdp,
					    &pc->streaml, datap);
	if (err)
		return err;
	*generationp = generation;
	return 0;
}


static void audio_error_handler(int err, const char *str, void *arg)
{
	struct media_track *media = arg;

	warning("peerconnection: audio error: %m (%s)\n", err, str);

	mediatrack_close(media, err);
}


static void video_error_handler(int err, const char *str, void *arg)
{
	struct media_track *media = arg;

	warning("peerconnection: video error: %m (%s)\n", err, str);

	mediatrack_close(media, err);
}


static void mnat_estab_handler(int err, uint16_t scode, const char *reason,
			       void *arg)
{
	struct peer_connection *pc = arg;

	if (err) {
		warning("peerconnection: medianat failed: %m\n", err);
		pc_close(pc, err);
		return;
	}
	else if (scode) {
		warning("peerconnection: medianat failed: %u %s\n",
			scode, reason);
		pc_close(pc, EPROTO);
		return;
	}

	info("peerconnection: medianat gathered (%s)\n",
	     signaling_state_name(pc->signaling_state));

	pc->gather_ok = true;

	if (pc->gatherh)
		pc->gatherh(pc->arg);
}


static void menc_event_handler(enum menc_event event,
			       const char *prm, struct stream *strm,
			       void *arg)
{
	struct peer_connection *pc = arg;
	struct media_track *media;
	struct le *le;
	bool legacy_base;

	mem_ref(pc);
	media = mediatrack_lookup_media(&pc->medial, strm);
	if (!media)
		goto out;

	info("peerconnection: mediaenc event '%s' (%s)\n",
	     menc_event_name(event), prm);

	switch (event) {

	case MENC_EVENT_SECURE:
		/* Transport-group adoption can install shared SRTP keys before a
		 * superseded per-stream DTLS callback drains.  Secure publication is
		 * idempotent; never re-run media establishment for that stale event. */
		if (media->dtls_ok)
			break;
		legacy_base =
			bundle_state(stream_bundle(strm)) == BUNDLE_BASE &&
			!stream_has_menc_transport(strm);
		media->dtls_ok = true;

		stream_set_secure(strm, true);
		stream_start_rtcp(strm);

		if (legacy_base) {
			for (le = pc->medial.head; le; le = le->next) {
				struct media_track *member = le->data;
				struct stream *member_stream =
					media_get_stream(member);
				enum bundle_state state =
					bundle_state(
						stream_bundle(member_stream));

				if (member == media)
					continue;
				if (state != BUNDLE_MUX)
					continue;
				member->dtls_ok = true;
				stream_set_secure(member_stream, true);
				stream_start_rtcp(member_stream);
				if (pc->estabh)
					pc->estabh(member, pc->arg);
			}
		}
		if (pc->estabh)
			pc->estabh(media, pc->arg);
		break;

	default:
		break;
	}
#ifdef USE_DATACHANNEL
	if (event == MENC_EVENT_SECURE)
		(void)try_bootstrap_transport_session(pc);
#endif

out:
	mem_deref(pc);
}


static void menc_error_handler(int err, void *arg)
{
	struct peer_connection *pc = arg;

	warning("peerconnection: mediaenc error: %m\n", err);

	if (pc->closeh)
		pc->closeh(err, pc->arg);
}


int peerconnection_new(struct peer_connection **pcp,
		       const struct rtc_configuration *config,
		       const struct mnat *mnat, const struct menc *menc,
		       peerconnection_gather_h *gatherh,
		       peerconnection_estab_h *estabh,
		       peerconnection_close_h *closeh, void *arg)
{
	struct peer_connection *pc;
	struct sa laddr;
	bool offerer = config->offerer;
	int err;

	if (!pcp)
		return EINVAL;

	if (!mnat || !menc)
		return EINVAL;

	sa_set_str(&laddr, "127.0.0.1", 0);

	info("peerconnection: new: sdp=%s\n",
	     offerer ? "Offerer" : "Answerer");

	pc = mem_zalloc(sizeof(*pc), destructor);
	if (!pc)
		return ENOMEM;

	pc->signaling_state = SS_STABLE;
	pc->offerer = offerer;

	/* RFC 7022 */
	rand_str(pc->cname, sizeof(pc->cname));

	pc->stream_prm.use_rtp	= true;
	pc->stream_prm.rtcp_mux = true; /* [RFC8829] (section 4.1.1.) */
	pc->stream_prm.af	= sa_af(&laddr);
	pc->stream_prm.cname	= pc->cname;

	err = sdp_session_alloc(&pc->sdp, &laddr);
	if (err)
		goto out;

	if (mnat->sessh) {

		info("peerconnection: using mnat '%s'\n", mnat->id);

		pc->mnat = mnat;

		err = mnat->sessh(&pc->mnats, mnat,
				  net_dnsc(baresip_network()),
				  sa_af(&laddr),
				  config->ice_server,
				  config->stun_user, config->credential,
				  pc->sdp, offerer,
				  mnat_estab_handler, pc);
		if (err) {
			warning("peerconnection: medianat session: %m\n", err);
			goto out;
		}
	}

	if (menc->sessh) {
		info("peerconnection: using menc '%s'\n", menc->id);

		pc->menc = menc;

		err = menc->sessh(&pc->mencs, pc->sdp, offerer,
				  menc_event_handler,
				  menc_error_handler, pc);
		if (err) {
			warning("peerconnection: mediaenc session: %m\n", err);
			goto out;
		}
	}

	err = pc_transport_session_alloc(&pc->transport_session,
					 transport_generation_publish,
					 transport_session_error, pc);
	if (err)
		goto out;
	pc_transport_session_set_retire_handler(pc->transport_session,
						transport_generation_retire);

	pc->gatherh = gatherh;
	pc->estabh = estabh;
	pc->closeh = closeh;
	pc->arg = arg;

 out:
	if (err)
		mem_deref(pc);
	else
		*pcp = pc;

	return err;
}


static void mediatrack_close_handler(int err, void *arg)
{
	struct peer_connection *pc = arg;

	pc_close(pc, err);
}


#ifdef USE_DATACHANNEL
static void data_mnatconn_handler(struct stream *strm, void *arg)
{
	struct peer_connection *pc = arg;
	int err;

	(void)strm;
	if (!pc->data)
		return;

	err = data_context_start(pc->data);
	if (err)
		pc_close(pc, err);
}
#endif


#ifdef USE_DATACHANNEL
static void data_error_handler(int err, void *arg)
{
	struct peer_connection *pc = arg;

	warning("peerconnection: data transport error: %m\n", err);
	pc_close(pc, err);
}


static uint32_t data_dispatch_refs(void *arg)
{
	return mem_nrefs(arg);
}


static void data_transport_ready_handler(void *arg)
{
	struct peer_connection *pc = mem_ref(arg);
	struct pc_transport_session *session = NULL;
	int gather_status;

	if (pc->ice_restart_requested && !pc->ice_restart_offer_pending &&
	    pc->signaling_state == SS_STABLE) {
		if (pc->gatherh)
			pc->gatherh(pc->arg);
	}
	else if (pc->signaling_state == SS_HAVE_REMOTE_OFFER &&
		 !pc->candidate_transport_topology) {
		gather_status = prepared_fresh_gather_status(pc);
		if (gather_status != EAGAIN && pc->gatherh)
			pc->gatherh(pc->arg);
	}
	else if (pc->transport_session_started) {
		session = mem_ref(pc->transport_session);
		pc_transport_session_changed(session);
	}
	else
		(void)try_bootstrap_transport_session(pc);
	mem_deref(session);
	mem_deref(pc);
}


static int data_ensure(struct peer_connection *pc)
{
	struct stream *base = NULL;
	struct le *le;
	int err;

	if (pc->data)
		return 0;

	if (pc->streaml.head) {
		base = pc->streaml.head->data;
		for (le = pc->streaml.head; le; le = le->next) {
			struct stream *stream = le->data;

			if (!stream_bundle(stream)) {
				err = stream_bundle_init(stream, pc->offerer);
				if (err)
					return err;
			}
		}
	}

	err = data_context_alloc(&pc->data, pc->sdp, pc->mnat, pc->mnats,
				 pc->menc, pc->mencs, base, &pc->streaml,
				 pc->stream_prm.af, pc->offerer,
				 data_error_handler, pc);
	if (err)
		warning("peerconnection: data context allocation failed (%m)\n",
			err);
	if (!err)
		data_context_set_dispatch_refs(
			pc->data, data_dispatch_refs, pc);
	if (!err)
		data_context_set_transport_ready_handler(
			pc->data, data_transport_ready_handler, pc);
	if (!err && base)
		err = data_context_bundle_encode(pc->data, &pc->streaml);
	if (err)
		warning("peerconnection: data BUNDLE encode failed (%m)\n", err);

	return err;
}


int peerconnection_set_datachannel_handler(
	struct peer_connection *pc, peerconnection_datachannel_h *channelh,
	void *arg)
{
	int err;

	if (!pc)
		return EINVAL;

	err = data_ensure(pc);
	if (err)
		return err;

	return data_context_set_handler(pc->data, channelh, arg);
}


int peerconnection_create_datachannel(struct peer_connection *pc,
				      const char *label,
				      const struct data_channel_config *cfg,
				      struct data_channel **dcp)
{
	int err;

	if (!pc)
		return EINVAL;
	err = data_context_channel_validate(label, cfg, dcp);
	if (err)
		return err;

	err = data_ensure(pc);
	if (err)
		return err;

	return data_context_channel_create(pc->data, label, cfg, dcp);
}
#endif


static int bundle_ensure(struct peer_connection *pc, struct stream *strm)
{
	if (stream_bundle(strm))
		return 0;

	return stream_bundle_init(strm, pc->offerer);
}


/*
 * RTCPeerConnection.addTrack()
 */
int peerconnection_add_audio_track(struct peer_connection *pc,
				   const struct config *cfg,
				   struct list *aucodecl, enum sdp_dir dir)
{
	struct media_track *media;
	bool offerer;
	int err;

	if (!pc || !cfg || !aucodecl)
		return EINVAL;

	info("peerconnection: add audio (codecs=%u)\n", list_count(aucodecl));

	offerer = (pc->signaling_state != SS_HAVE_REMOTE_OFFER);

	media = media_track_add(&pc->medial, MEDIA_KIND_AUDIO,
				mediatrack_close_handler, pc);

	err = audio_alloc(&media->u.au, &pc->streaml, &pc->stream_prm, cfg,
			  NULL, pc->sdp, pc->mnat, pc->mnats,
			  pc->menc, pc->mencs, AUDIO_PTIME, aucodecl, offerer,
			  NULL, NULL, audio_error_handler, media);
	if (err) {
		warning("peerconnection: audio alloc failed (%m)\n", err);
		return err;
	}

	err = bundle_ensure(pc, media_get_stream(media));
	if (err)
		return err;
#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_add_stream(
			pc->data, media_get_stream(media));
		if (err)
			return err;
	}
#endif

	stream_set_ldir(media_get_stream(media), dir);

	mediatrack_set_handlers(media);
#ifdef USE_DATACHANNEL
	mediatrack_set_mnatconn_handler(media, data_mnatconn_handler, pc);
#endif

	return 0;
}


#ifdef USE_DATACHANNEL


static bool adopted_group_exists(const struct peer_connection *pc,
				 const struct pc_transport_group *group)
{
	for (struct le *le = pc->adopted_transport_runtimes.head; le;
	     le = le->next) {
		const struct pc_adopted_runtime *entry = le->data;

		if (entry->group == group)
			return true;
	}
	return false;
}


static int existing_data_prepare(void *object, enum menc_dtls_role role)
{
	struct data_context *ctx = object;

	if (!ctx || !data_context_sctp_started(ctx))
		return EAGAIN;
	return role == MENC_DTLS_ROLE_CLIENT || role == MENC_DTLS_ROLE_SERVER
		? 0 : EPROTO;
}


static int pending_data_prepare(void *object, enum menc_dtls_role role)
{
	return data_context_media_binding_prepare(object, role);
}


static void pending_data_finalize(void *object)
{
	data_context_media_binding_finalize(object);
}


static void pending_data_abort(void *object)
{
	data_context_media_binding_abort(object);
}


static const struct pc_adopted_runtime *runtime_entry(
	const struct peer_connection *pc, const struct pc_transport_group *group)
{
	for (struct le *le = pc->adopted_transport_runtimes.head; le;
	     le = le->next) {
		const struct pc_adopted_runtime *entry = le->data;

		if (entry->group == group)
			return entry;
	}
	return NULL;
}


static struct pc_adopted_runtime *restart_runtime_entry(
	const struct peer_connection *pc,
	const struct pc_transport_group *active_group)
{
	for (struct le *le = pc->restart_transport_runtimes.head; le;
	     le = le->next) {
		struct pc_adopted_runtime *entry = le->data;

		if (entry->group == active_group)
			return entry;
	}
	return NULL;
}


static bool topology_contains_group(
	const struct pc_transport_generation *generation,
	const struct pc_transport_group *group)
{
	for (size_t i = 0; generation &&
	     i < pc_transport_generation_count(generation); ++i) {
		if (pc_transport_generation_group(generation, i) == group)
			return true;
	}
	return false;
}


static bool transport_groups_equal(const struct pc_transport_group *left,
				   const struct pc_transport_group *right)
{
	const struct bundle_group *a;
	const struct bundle_group *b;

	if (!left || !right)
		return false;
	a = pc_transport_group_bundle(left);
	b = pc_transport_group_bundle(right);
	if (!a || !b || bundle_group_count(a) != bundle_group_count(b))
		return false;
	for (size_t i = 0; i < bundle_group_count(a); ++i) {
		if (str_cmp(bundle_group_mid(a, i), bundle_group_mid(b, i)))
			return false;
	}
	return true;
}


/* A remote BUNDLE split has to advertise the answerer's new lower in the
 * answer.  Prepare it while the remote-offer SDP transaction can still be
 * rolled back; the session coordinator adopts it only after the final answer
 * topology has been derived. */
static struct pc_adopted_runtime *prepared_runtime_entry(
	const struct peer_connection *pc,
	const struct pc_transport_group *group)
{
	for (struct le *le = pc->adopted_transport_runtimes.head; le;
	     le = le->next) {
		struct pc_adopted_runtime *entry = le->data;

		if (!topology_contains_group(pc->active_transport_topology,
					     entry->group) &&
		    transport_groups_equal(entry->group, group))
			return entry;
	}
	return NULL;
}


static int prepared_fresh_gather_status(const struct peer_connection *pc)
{
	for (struct le *le = pc->adopted_transport_runtimes.head; le;
	     le = le->next) {
		const struct pc_adopted_runtime *entry = le->data;
		int err;

		if (topology_contains_group(pc->active_transport_topology,
					    entry->group))
			continue;
		err = media_transport_error(entry->transport);
		if (err)
			return err;
		if (!media_transport_gathered(entry->transport))
			return EAGAIN;
	}
	return 0;
}


static int prepare_fresh_transport_groups(
	struct peer_connection *pc,
	const struct pc_transport_generation *target)
{
	int err;

	if (!pc || !pc->active_transport_topology || !target)
		return EINVAL;
	for (size_t i = 0; i < pc_transport_generation_count(target); ++i) {
		const struct pc_transport_group *group =
			pc_transport_generation_group(target, i);
		struct pc_adopted_runtime *entry = NULL;
		struct media_transport_prm prm = {0};
		struct media_transport *runtime = NULL;

		if (pc_transport_generation_find_reusable(
			    pc->active_transport_topology, group) ||
		    prepared_runtime_entry(pc, group))
			continue;
		/* A new RTP-owned group has no lower to adopt.  Data-owned groups are
		 * prepared by data_context's pending binding, which also owns the SCTP
		 * consumer transaction. */
		if (!pc_transport_group_owner_stream(group) ||
		    pc_transport_group_carries_sctp(group))
			continue;

		prm.group = pc_transport_group_bundle(group);
		prm.transport_sdpm = pc_transport_group_sdpmedia(group);
		prm.streaml = &pc->streaml;
		prm.data_mid = pc->data ? data_context_mid(pc->data) : NULL;
		prm.semantic_key = pc_transport_group_reuse_key(group);
		prm.mnat = pc->mnat;
		prm.mnats = pc->mnats;
		prm.menc = pc->menc;
		prm.mencs = pc->mencs;
		prm.af = pc->stream_prm.af;
		prm.offerer = pc->offerer;
		err = media_transport_alloc(&runtime, &prm);
		if (!err)
			err = media_transport_prepare(runtime);
		if (!err)
			media_transport_set_observer(
				runtime, data_transport_ready_handler, pc);
		if (!err && !media_transport_gathered(runtime)) {
			err = media_transport_gather_start(runtime);
			if (err == EAGAIN)
				err = 0;
		}
		if (err) {
			warning("peerconnection: fresh transport group '%s'"
				" prepare failed (%m)\n",
				pc_transport_group_tag(group), err);
			mem_deref(runtime);
			return err;
		}
		entry = mem_zalloc(sizeof(*entry),
				   pc_adopted_runtime_destructor);
		if (!entry) {
			media_transport_abort(runtime);
			mem_deref(runtime);
			return ENOMEM;
		}
		entry->group = mem_ref((void *)group);
		entry->transport = runtime;
		list_append(&pc->adopted_transport_runtimes, &entry->le, entry);
	}
	return 0;
}


static int prepare_restart_transport_generations(
	struct peer_connection *pc,
	const struct pc_transport_generation *target, bool force)
{
	int err;

	if (!pc || !pc->active_transport_topology || !target)
		return EINVAL;
	for (size_t i = 0; i < pc_transport_generation_count(target); ++i) {
		const struct pc_transport_group *group =
			pc_transport_generation_group(target, i);
		const struct pc_transport_group *active_group =
			pc_transport_generation_find_reusable(
				pc->active_transport_topology, group);
		const struct pc_adopted_runtime *active_entry;
		struct pc_adopted_runtime *restart_entry;
		struct media_transport *runtime = NULL;

		if (!active_group ||
		    (!force && pc_transport_group_reuses_ice(active_group, group)))
			continue;
		restart_entry = restart_runtime_entry(pc, active_group);
		if (!restart_entry) {
			active_entry = runtime_entry(pc, active_group);
			if (!active_entry)
				return ESTALE;
			err = media_transport_restart_alloc(
				&runtime, active_entry->transport,
				pc_transport_group_bundle(group), &pc->streaml,
				&pc->streaml,
				pc->data ? data_context_mid(pc->data) : NULL,
				pc_transport_group_reuse_key(group), NULL);
			if (err)
				return err;
			media_transport_set_observer(
				runtime, data_transport_ready_handler, pc);
			err = media_transport_prepare(runtime);
			if (err) {
				mem_deref(runtime);
				return err;
			}
			restart_entry = mem_zalloc(
				sizeof(*restart_entry),
				pc_adopted_runtime_destructor);
			if (!restart_entry) {
				media_transport_abort(runtime);
				mem_deref(runtime);
				return ENOMEM;
			}
			restart_entry->group = mem_ref((void *)active_group);
			restart_entry->transport = runtime;
			list_append(&pc->restart_transport_runtimes,
				    &restart_entry->le, restart_entry);
		}
		err = media_transport_gather_start(restart_entry->transport);
		if (err && err != EAGAIN)
			return err;
		if (err == EAGAIN)
			return EAGAIN;
		err = media_transport_restart_apply_sdp(
			restart_entry->transport);
		if (err)
			return err;
	}
	return 0;
}


static int exact_route_alloc(struct bundle_transport **routep,
			     uint64_t *route_generationp,
			     const struct pc_transport_group *group,
			     struct peer_connection *pc,
			     struct udp_sock *sock)
{
	const struct bundle_group *bundle = pc_transport_group_bundle(group);
	const struct sa *remote = pc_transport_group_remote(group);
	struct bundle_transport *route = NULL;
	uint64_t route_generation;
	int err;

	if (!routep || !route_generationp || !sock || !remote ||
	    !sa_isset(remote, SA_ALL))
		return EAGAIN;
	err = bundle_transport_alloc(&route, bundle, &pc->streaml,
				     pc->data ? data_context_mid(pc->data) : NULL);
	if (!err)
		err = bundle_transport_prepare(route, bundle, sock,
					       &route_generation);
	if (!err)
		err = bundle_transport_set_remote(route, route_generation, remote);
	if (err) {
		mem_deref(route);
		return err;
	}
	*routep = route;
	*route_generationp = route_generation;
	return 0;
}


static int adopt_transport_group(struct peer_connection *pc,
				 const struct pc_transport_group *group)
{
	struct pc_adopted_runtime *entry = NULL;
	struct media_transport_prm prm = {0};
	struct media_transport *runtime = NULL;
	struct bundle_transport *route = NULL;
	struct menc_transport *transport = NULL;
	struct mnat_media *mnat_media = NULL;
	struct udp_sock *sock = NULL;
	struct stream *owner = pc_transport_group_owner_stream(group);
	struct transport_binding *binding = NULL;
	struct pc_transport_data_binding data = {0};
	bool carries_data = pc_transport_group_carries_sctp(group);
	bool data_owned = pc_transport_group_owner_is_data(group);
	uint64_t route_generation = 0;
	int err;

	if (adopted_group_exists(pc, group))
		return 0;
	if (carries_data && data_owned) {
		if (!pc->data || !data_context_sctp_started(pc->data))
			return EAGAIN;
		transport = data_context_menc_transport_ref(pc->data);
		mnat_media = data_context_mnat_media_ref(pc->data);
		sock = mem_ref((void *)data_context_socket_identity(pc->data));
	}
	else if (owner) {
		transport = stream_menc_transport_ref(owner);
		if (!transport) {
			err = stream_promote_menc_transport(owner, &transport,
						    NULL, NULL, NULL, NULL);
			if (err == EAGAIN || err == ENOENT)
				return EAGAIN;
			if (err)
				return err;
			stream_set_menc_transport(owner, transport);
		}
		mnat_media = stream_mnat_media_ref(owner);
		sock = mem_ref(rtp_sock(stream_rtp_sock(owner)));
	}
	if (!transport || !sock) {
		err = EAGAIN;
		goto out;
	}
	err = exact_route_alloc(&route, &route_generation, group, pc, sock);
	if (err) {
		goto out;
	}
	prm.group = pc_transport_group_bundle(group);
	prm.transport_sdpm = pc_transport_group_sdpmedia(group);
	prm.streaml = &pc->streaml;
	prm.data_mid = pc->data ? data_context_mid(pc->data) : NULL;
	prm.semantic_key = pc_transport_group_reuse_key(group);
	prm.mnat = pc->mnat;
	prm.mnats = pc->mnats;
	prm.menc = pc->menc;
	prm.mencs = pc->mencs;
	prm.af = pc->stream_prm.af;
	prm.offerer = pc->offerer;
	if (carries_data && data_owned)
		data_context_media_adopt_prm(pc->data, &prm);
	else if (carries_data) {
		if (!pc->data) {
			err = EINVAL;
			goto out;
		}
		err = data_context_media_binding_alloc(&binding, pc->data, &prm);
		if (err) {
			goto out;
		}
	}
	err = media_transport_adopt_pending(&runtime, &prm, sock, mnat_media,
					    transport, route,
					    route_generation);
	if (err)
		goto out;
	if (binding) {
		err = data_context_media_binding_attach(binding, runtime);
		if (err)
			goto out;
	}
	if (carries_data) {
		err = media_transport_consumer_ready(runtime);
		if (err)
			goto out;
		data.object = binding ? (void *)binding : (void *)pc->data;
		data.prepareh = binding ? pending_data_prepare
					: existing_data_prepare;
		data.finalizeh = binding ? pending_data_finalize : NULL;
		data.aborth = binding ? pending_data_abort : NULL;
	}
	err = pc_transport_session_add(pc->transport_session, group, runtime,
				       carries_data ? &data : NULL);
	if (err)
		goto out;
	entry = mem_zalloc(sizeof(*entry), pc_adopted_runtime_destructor);
	if (!entry) {
		err = ENOMEM;
		goto out;
	}
	entry->group = mem_ref((void *)group);
	entry->transport = mem_ref(runtime);
	list_append(&pc->adopted_transport_runtimes, &entry->le, entry);
	binding = NULL;

out:
	if (binding)
		data_context_media_binding_abort(binding);
	mem_deref(runtime);
	mem_deref(route);
	mem_deref(sock);
	mem_deref(mnat_media);
	mem_deref(transport);
	return err;
}


static int ensure_active_runtime(
	struct peer_connection *pc, const struct pc_transport_group *group)
{
	struct pc_adopted_runtime *entry = NULL;
	struct media_transport_prm prm = {0};
	struct media_transport *runtime = NULL;
	struct bundle_transport *route = NULL;
	struct menc_transport *transport = NULL;
	struct mnat_media *mnat_media = NULL;
	struct udp_sock *sock = NULL;
	struct stream *owner;
	uint64_t route_generation = 0;
	bool route_activated = false;
	int err;

	if (!pc || !group)
		return EINVAL;
	if (runtime_entry(pc, group))
		return 0;
	owner = pc_transport_group_owner_stream(group);
	if (!owner)
		return ENOTSUP;
	transport = stream_menc_transport_ref(owner);
	if (!transport) {
		err = stream_promote_menc_transport(owner, &transport,
					    NULL, NULL, NULL, NULL);
		if (err) {
			warning("peerconnection: active group '%s' MENC promotion"
				" failed (%m)\n", pc_transport_group_tag(group), err);
			goto out;
		}
		stream_set_menc_transport(owner, transport);
	}
	mnat_media = stream_mnat_media_ref(owner);
	sock = mem_ref(rtp_sock(stream_rtp_sock(owner)));
	if (!sock) {
		err = EAGAIN;
		goto out;
	}
	err = exact_route_alloc(&route, &route_generation, group, pc, sock);
	if (err) {
		warning("peerconnection: active group '%s' route allocation"
			" failed (%m)\n", pc_transport_group_tag(group), err);
		goto out;
	}
	prm.group = pc_transport_group_bundle(group);
	prm.transport_sdpm = pc_transport_group_sdpmedia(group);
	prm.streaml = &pc->streaml;
	prm.data_mid = pc->data ? data_context_mid(pc->data) : NULL;
	prm.semantic_key = pc_transport_group_reuse_key(group);
	prm.mnat = pc->mnat;
	prm.mnats = pc->mnats;
	prm.menc = pc->menc;
	prm.mencs = pc->mencs;
	prm.af = pc->stream_prm.af;
	prm.offerer = pc->offerer;
	err = media_transport_adopt(&runtime, &prm, sock, mnat_media,
				    transport, route);
	if (err) {
		warning("peerconnection: active group '%s' adoption failed"
			" (%m)\n", pc_transport_group_tag(group), err);
		goto out;
	}
	entry = mem_zalloc(sizeof(*entry), pc_adopted_runtime_destructor);
	if (!entry) {
		err = ENOMEM;
		goto out;
	}
	entry->group = mem_ref((void *)group);
	entry->transport = mem_ref(runtime);
	err = bundle_transport_activate(route, route_generation);
	if (err)
		goto out;
	route_activated = true;
	err = bundle_transport_finalize(route, route_generation);
	if (err)
		goto out;
	list_append(&pc->adopted_transport_runtimes, &entry->le, entry);
	entry = NULL;
	err = 0;

out:
	if (err && route) {
		if (route_activated)
			(void)bundle_transport_rollback(route, route_generation);
		else
			(void)bundle_transport_abort(route, route_generation);
	}
	mem_deref(entry);
	mem_deref(runtime);
	mem_deref(route);
	mem_deref(sock);
	mem_deref(mnat_media);
	mem_deref(transport);
	return err;
}


static int reconfigure_transport_group(
	struct peer_connection *pc, const struct pc_transport_group *group)
{
	const struct pc_transport_group *active_group;
	const struct pc_adopted_runtime *active_entry;
	struct pc_adopted_runtime *entry = NULL;
	struct media_transport_prm consumer = {0};
	struct media_transport *runtime = NULL;
	struct transport_binding *binding = NULL;
	struct pc_transport_data_binding data = {0};
	bool active_data;
	bool new_data;
	bool remove_data;
	bool prepared_restart = false;
	int err;

	if (adopted_group_exists(pc, group))
		return 0;
	active_group = pc_transport_generation_find_reusable(
		pc->active_transport_topology, group);
	if (!active_group) {
		struct pc_adopted_runtime *prepared =
			prepared_runtime_entry(pc, group);
		struct media_transport_prm prm = {0};
		struct udp_sock *sock = NULL;
		struct mnat_media *mnat_media = NULL;
		struct menc_transport *transport = NULL;
		bool remote_ready;

		if (prepared) {
			err = media_transport_rekey(
				prepared->transport,
				pc_transport_group_reuse_key(group));
			if (err)
				return err;
			mem_deref((void *)prepared->group);
			prepared->group = mem_ref((void *)group);
			err = pc_transport_session_add(pc->transport_session, group,
					       prepared->transport, NULL);
			if (err)
				return err;
			return 0;
		}
		if (!pc_transport_group_carries_sctp(group) || !pc->data)
			return ENOTSUP;
		prm.group = pc_transport_group_bundle(group);
		prm.transport_sdpm = pc_transport_group_sdpmedia(group);
		prm.streaml = &pc->streaml;
		prm.data_mid = data_context_mid(pc->data);
		prm.semantic_key = pc_transport_group_reuse_key(group);
		prm.mnat = pc->mnat;
		prm.mnats = pc->mnats;
		prm.menc = pc->menc;
		prm.mencs = pc->mencs;
		prm.af = pc->stream_prm.af;
		prm.offerer = pc->offerer;
		err = data_context_media_binding_take_pending(
			&binding, pc->data, &prm, &sock, &mnat_media,
			&transport);
		if (!err)
			err = media_transport_import_pending(
				&runtime, &prm, sock, mnat_media, transport);
		if (!err)
			err = data_context_media_binding_attach(binding, runtime);
		mem_deref(transport);
		mem_deref(mnat_media);
		mem_deref(sock);
		if (err)
			goto out;
		entry = mem_zalloc(sizeof(*entry),
				   pc_adopted_runtime_destructor);
		if (!entry) {
			err = ENOMEM;
			goto out;
		}
		entry->group = mem_ref((void *)group);
		entry->transport = mem_ref(runtime);
		data.object = binding;
		data.prepareh = pending_data_prepare;
		data.finalizeh = pending_data_finalize;
		data.aborth = pending_data_abort;
		err = pc_transport_session_add(pc->transport_session, group,
					       runtime, &data);
		if (err)
			goto out;
		list_append(&pc->adopted_transport_runtimes, &entry->le,
			    entry);
		remote_ready = media_transport_remote_set(runtime);
		entry = NULL;
		mem_deref(binding);
		binding = NULL;
		mem_deref(runtime);
		/* If ICE connected before the coordinator took ownership, start the
		 * candidate now.  Otherwise its generation-local connected callback will
		 * re-enter this function after committed candidates are available. */
		return remote_ready ? 0 : EAGAIN;
	}
	active_entry = runtime_entry(pc, active_group);
	if (!active_entry) {
		err = ensure_active_runtime(pc, active_group);
		if (err)
			return err;
		active_entry = runtime_entry(pc, active_group);
		if (!active_entry)
			return ESTALE;
	}
	if (pc_transport_group_role(group) != MENC_DTLS_ROLE_UNKNOWN &&
	    pc_transport_group_role(group) !=
		media_transport_role(active_entry->transport))
		return EPROTO;
	active_data = pc_transport_group_carries_sctp(active_group);
	new_data = pc_transport_group_carries_sctp(group) && !active_data;
	remove_data = active_data && !pc_transport_group_carries_sctp(group);
	if (active_data && pc_transport_group_carries_sctp(group) &&
	    !pc_transport_group_reuses_sctp(active_group, group))
		return ENOTSUP;
	if (new_data) {
		if (!pc->data)
			return EINVAL;
		err = data_context_media_binding_alloc(&binding, pc->data,
						       &consumer);
		if (err)
			return err;
	}
	if (pc_transport_group_reuses_ice(active_group, group))
		err = media_transport_reconfigure_alloc(
			&runtime, active_entry->transport,
			pc_transport_group_bundle(group), &pc->streaml,
			&pc->streaml,
			pc->data ? data_context_mid(pc->data) : NULL,
			pc_transport_group_reuse_key(group),
			new_data || remove_data ? &consumer : NULL);
	else
	{
		struct pc_adopted_runtime *prepared =
			restart_runtime_entry(pc, active_group);

		if (prepared) {
			runtime = mem_ref(prepared->transport);
			prepared_restart = true;
			err = 0;
		}
		else
			err = media_transport_restart_alloc(
				&runtime, active_entry->transport,
				pc_transport_group_bundle(group), &pc->streaml,
				&pc->streaml,
				pc->data ? data_context_mid(pc->data) : NULL,
				pc_transport_group_reuse_key(group),
				new_data || remove_data ? &consumer : NULL);
	}
	if (err) {
		warning("peerconnection: group '%s' runtime allocation failed"
			" (%m)\n", pc_transport_group_tag(group), err);
		goto out;
	}
	if (binding) {
		err = data_context_media_binding_attach(binding, runtime);
		if (err)
			goto out;
	}
	err = prepared_restart ? 0 : media_transport_prepare(runtime);
	if (err) {
		warning("peerconnection: group '%s' runtime prepare failed"
			" (%m)\n", pc_transport_group_tag(group), err);
		goto out;
	}
	entry = mem_zalloc(sizeof(*entry), pc_adopted_runtime_destructor);
	if (!entry) {
		err = ENOMEM;
		goto out;
	}
	entry->group = mem_ref((void *)group);
	entry->transport = mem_ref(runtime);
	if (binding) {
		data.object = binding;
		data.prepareh = pending_data_prepare;
		data.finalizeh = pending_data_finalize;
		data.aborth = pending_data_abort;
	}
	err = pc_transport_session_add(pc->transport_session, group, runtime,
				       binding ? &data : NULL);
	if (err)
		warning("peerconnection: group '%s' session add failed (%m)\n",
			pc_transport_group_tag(group), err);
	if (err)
		goto out;
	list_append(&pc->adopted_transport_runtimes, &entry->le, entry);
	entry = NULL;
	{
		struct pc_adopted_runtime *prepared =
			restart_runtime_entry(pc, active_group);

		if (prepared)
			mem_deref(prepared);
	}
	/* The context list owns the allocation reference.  The coordinator owns
	 * an additional pin until finalize/abort. */
	binding = NULL;

out:
	mem_deref(entry);
	if (err)
		media_transport_abort(runtime);
	mem_deref(runtime);
	if (binding)
		data_context_media_binding_abort(binding);
	return err;
}


static int try_bootstrap_transport_session(struct peer_connection *pc)
{
	const struct pc_transport_generation *topology;
	int err = 0;

	if (!pc || !pc->candidate_transport_topology)
		return 0;
	topology = pc->candidate_transport_topology;
	if (!pc->transport_session_staged) {
		err = pc_transport_session_stage(pc->transport_session, topology);
		if (err)
			goto fail;
		pc->transport_session_staged = true;
	}
	for (size_t i = 0; i < pc_transport_generation_count(topology); ++i) {
		const struct pc_transport_group *group =
			pc_transport_generation_group(topology, i);

		err = pc->active_transport_topology
			? reconfigure_transport_group(pc, group)
			: adopt_transport_group(pc, group);
		if (err == EAGAIN)
			return 0;
		if (err) {
			warning("peerconnection: transport group '%s' bootstrap"
				" failed (%m)\n",
				pc_transport_group_tag(group), err);
			goto fail;
		}
	}
	if (pc->transport_session_started)
		return 0;
	err = pc_transport_session_start(pc->transport_session);
	if (err)
		goto fail;
	/* Synchronous readiness may publish and clear the candidate inside
	 * pc_transport_session_start().  Do not resurrect a stale started flag. */
	pc->transport_session_started =
		pc->candidate_transport_topology != NULL;
	return 0;

fail:
	pc_transport_session_abort(pc->transport_session);
	pc->transport_session_staged = false;
	pc->transport_session_started = false;
	if (pc->active_transport_topology)
		transport_generation_retire(pc->active_transport_topology, pc);
	else
		pc_close(pc, err);
	return err;
}
#endif


/*
 * RTCPeerConnection.addTrack()
 */
int peerconnection_add_video_track(struct peer_connection *pc,
				   const struct config *cfg,
				   struct list *vidcodecl, enum sdp_dir dir)
{
	struct media_track *media;
	bool offerer;
	int err;

	if (!pc || !cfg || !vidcodecl)
		return EINVAL;

	info("peerconnection: add video (codecs=%u)\n", list_count(vidcodecl));

	if (list_isempty(vidcodecl)) {
		warning("peerconnection: no video codecs!\n");
		return EINVAL;
	}

	offerer = (pc->signaling_state != SS_HAVE_REMOTE_OFFER);

	media = media_track_add(&pc->medial, MEDIA_KIND_VIDEO,
				mediatrack_close_handler, pc);

	err = video_alloc(&media->u.vid, &pc->streaml, &pc->stream_prm, cfg,
			  NULL, pc->sdp, pc->mnat, pc->mnats, pc->menc,
			  pc->mencs, NULL, vidcodecl, NULL, offerer,
			  video_error_handler, media);
	if (err) {
		warning("peerconnection: video alloc failed (%m)\n", err);
		return err;
	}

	err = bundle_ensure(pc, media_get_stream(media));
	if (err)
		return err;
#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_add_stream(
			pc->data, media_get_stream(media));
		if (err)
			return err;
	}
#endif

	stream_set_ldir(media_get_stream(media), dir);

	mediatrack_set_handlers(media);
#ifdef USE_DATACHANNEL
	mediatrack_set_mnatconn_handler(media, data_mnatconn_handler, pc);
#endif

	return 0;
}


/*
 * RTCPeerConnection.setRemoteDescription()
 */
static int set_remote_description(struct peer_connection *pc,
				  const struct session_description *sd)
{
	struct le *le;
	struct sdp_session_state *operation = NULL;
	struct pc_media_state *media_operation = NULL;
	struct pc_transport_generation *topology_operation = NULL;
	bool offer;
	int err;

	if (!pc || !sd)
		return EINVAL;

	info("peerconnection: set remote description. type=%s\n",
	     sdptype_name(sd->type));

	if (sd->type == SDP_ROLLBACK) {
		enum signaling_st pending_state = pc->signaling_state;

		if (pc->signaling_state == SS_STABLE || !pc->pending_sdp)
			return EPROTO;
#ifdef USE_DATACHANNEL
		if (pc->data)
			data_context_rollback(pc->data);
#endif
		sdp_session_state_restore(pc->pending_sdp);
		pc->pending_sdp = NULL;
		if (pending_state == SS_HAVE_LOCAL_OFFER &&
		    pc->sdp_enc_ok)
			--pc->sdp_enc_ok;
		else if (pending_state == SS_HAVE_REMOTE_OFFER &&
			 pc->sdp_dec_ok)
			--pc->sdp_dec_ok;
		pc->signaling_state = SS_STABLE;
		if (pending_state == SS_HAVE_LOCAL_OFFER)
			pc->ice_restart_offer_pending = false;
		list_flush(&pc->restart_transport_runtimes);
		if (pc->active_transport_topology)
			transport_generation_retire(
				pc->active_transport_topology, pc);
		list_flush(&pc->pending_candidates);
#ifdef USE_DATACHANNEL
		if (pc->data)
			data_context_notify_channels(pc->data, false);
#endif
		return 0;
	}
	if (sd->type != SDP_OFFER && sd->type != SDP_ANSWER)
		return EINVAL;

	offer = (sd->type == SDP_OFFER);
	if ((offer && pc->signaling_state != SS_STABLE) ||
	    (!offer && pc->signaling_state != SS_HAVE_LOCAL_OFFER)) {
		warning("peerconnection: set remote descr:"
			" invalid signaling state (%s)\n",
			signaling_state_name(pc->signaling_state));
		return EPROTO;
	}
#ifdef USE_DATACHANNEL
	/* A newer stable negotiation supersedes an unpublished lower candidate.
	 * Abort only that candidate; the active generation and packet routes stay
	 * untouched until the replacement reaches atomic publication. */
	if (offer)
		abort_candidate_transport_generation(pc);
	if (offer && pc->ice_restart_requested && pc->data)
		data_context_abort_transport_restart(pc->data);
#endif

	err = sdp_session_state_save(&operation, pc->sdp);
	if (err)
		goto out;
#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_description_begin(pc->data);
		if (err)
			goto out;
	}
#endif

	if (LEVEL_DEBUG == log_level_get()) {
		info("- - %s - -\n", sdptype_name(sd->type));
		info("%b\n", (sd->sdp)->buf, (sd->sdp)->end);
		info("- - - - - - -\n");
	}

	err = sdp_decode(pc->sdp, sd->sdp, offer);
	if (err) {
		warning("peerconnection: sdp decode failed (%m)\n", err);
		goto out;
	}

#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_remote_update(pc->data, offer);
		if (err)
			goto out;
	}
	if (offer && pc->active_transport_topology) {
		err = stage_transport_topology(&topology_operation, pc);
		if (err)
			goto out;
		err = prepare_restart_transport_generations(
			pc, topology_operation, false);
		if (!err)
			err = prepare_fresh_transport_groups(
				pc, topology_operation);
		topology_operation = mem_deref(topology_operation);
		if (err)
			goto out;
	}
#endif
	/* A remote offer is provisional.  Keep its SDP in the pending snapshot,
	 * but do not publish any RTP/BUNDLE/MNAT runtime state until the local
	 * answer makes the negotiation stable. */
	if (!offer && pc->streaml.head) {
		err = pc_media_state_save(&media_operation, &pc->streaml);
		if (err)
			goto out;
		err = bundle_sdp_decode_stage(pc->sdp, &pc->streaml);
		if (err)
			goto out;
	}

	/* must be done after sdp_decode() */
	for (le = offer ? NULL : pc->streaml.head; le; le = le->next) {
		struct stream *strm = le->data;

		err = stream_update_jsep(strm);
		if (err)
			goto out;
	}
	/* ICE/TURN update starts externally visible work and has no rollback
	 * contract.  Keep it outside the description transaction; the caller
	 * starts the committed generation through peerconnection_start_ice(). */
	if (!offer) {
		err = stage_transport_topology(&topology_operation, pc);
		if (err)
			goto out;
	}
#ifdef USE_DATACHANNEL
		if (pc->data) {
			err = data_context_description_prepare(pc->data, offer);
			if (err)
				goto out;
			data_context_description_publish(pc->data, offer);
			data_context_description_finalize(pc->data, offer);
		}
#endif
	if (!offer) {
		pc->candidate_transport_topology =
			mem_deref(pc->candidate_transport_topology);
		pc->candidate_transport_topology = topology_operation;
		topology_operation = NULL;
		/* These decoders cannot fail.  Run them only after every fallible
		 * publication step has succeeded so they never need rollback. */
		for (le = pc->medial.head; le; le = le->next)
			mediatrack_sdp_attr_decode(le->data);
	}

	if (offer) {
		++pc->sdp_dec_ok;
		pc->pending_sdp = mem_deref(pc->pending_sdp);
		pc->pending_sdp = operation;
		operation = NULL;
		pc->signaling_state = SS_HAVE_REMOTE_OFFER;
	}
		else {
		/* Keep the local-offer signaling state and its rollback snapshot
		 * intact until every synchronously-ready transport group has crossed
		 * the atomic publication gate.  A failed activation can then return
		 * the exact pre-answer transaction to the caller. */
		apply_pending_candidates(pc);
	#ifdef USE_DATACHANNEL
		if (pc->active_transport_topology) {
			if (pc->mnat->updateh && pc->mnats) {
				err = pc->mnat->updateh(pc->mnats);
				if (err)
					goto out;
			}
			err = try_bootstrap_transport_session(pc);
			if (err)
				goto out;
		}
	#endif
		pc->pending_sdp = mem_deref(pc->pending_sdp);
		pc->signaling_state = SS_STABLE;
		++pc->sdp_dec_ok;
			if (pc->ice_restart_offer_pending) {
			pc->ice_restart_offer_pending = false;
			pc->ice_restart_requested = false;
			}
		}
		#ifdef USE_DATACHANNEL
		if (pc->data && !offer)
			data_context_description_retire(pc->data);
		#endif

#ifdef USE_DATACHANNEL
	/*
	 * Negotiated-channel handlers may destroy the peer connection.  Dispatch
	 * only after every SDP/BUNDLE/media update is complete, and do not touch
	 * pc after entering application code.
	 */
	if (pc->data) {
		data_context_notify_channels(pc->data, !offer);
		goto out;
	}
#endif

out:
	if (err && operation) {
#ifdef USE_DATACHANNEL
		if (pc->data)
			data_context_description_abort(pc->data);
#endif
		pc_media_state_restore(media_operation);
		sdp_session_state_restore(operation);
		operation = NULL;
	}
	if (err && pc->active_transport_topology)
		pc->candidate_transport_topology =
			mem_deref(pc->candidate_transport_topology);
	if (err && pc->active_transport_topology)
		transport_generation_retire(pc->active_transport_topology, pc);
#ifdef USE_DATACHANNEL
	if (err && pc->data)
		data_context_notify_channels(pc->data, false);
#endif
	mem_deref(media_operation);
	mem_deref(topology_operation);
	mem_deref(operation);
	return err;
}


int peerconnection_set_remote_descr(struct peer_connection *pc,
				    const struct session_description *sd)
{
	int err;

	if (!pc || !sd)
		return EINVAL;

	mem_ref(pc);
	err = set_remote_description(pc, sd);
	mem_deref(pc);
	return err;
}


/*
 * RTCPeerConnection.createOffer()
 */
int peerconnection_create_offer(struct peer_connection *pc, struct mbuf **mb)
{
	struct sdp_session_state *operation = NULL;
	int err;

	if (!pc || !mb)
		return EINVAL;
	*mb = NULL;

	info("peerconnection: create offer\n");

	if (!pc->gather_ok) {
		warning("peerconnection: create_offer: ice not gathered\n");
		return EPROTO;
	}

	if (pc->signaling_state != SS_STABLE) {
		warning("peerconnection: create offer:"
			" invalid signaling state (%s)\n",
			signaling_state_name(pc->signaling_state));
		return EPROTO;
	}
#ifdef USE_DATACHANNEL
	abort_candidate_transport_generation(pc);
#endif
	mem_ref(pc);
	err = sdp_session_state_save(&operation, pc->sdp);
	if (err)
		goto out;
#ifdef USE_DATACHANNEL
	if (pc->ice_restart_requested && pc->active_transport_topology) {
		err = prepare_restart_transport_generations(
			pc, pc->active_transport_topology, true);
		if (err)
			goto out;
	}
#endif
#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_description_begin(pc->data);
		if (err)
			goto out;
	}
#endif

#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_bundle_encode(pc->data, &pc->streaml);
		if (err)
			goto out;
	}
	else
#endif
	if (pc->streaml.head) {
		err = bundle_sdp_encode(pc->sdp, &pc->streaml);
		if (err)
			goto out;
	}

	err = sdp_encode(mb, pc->sdp, true);
	if (err)
		goto out;

#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_local_description(pc->data, true);
		if (err)
			goto out;
		err = data_context_description_commit(pc->data, true);
		if (err)
			goto out;
	}
#endif

	if (LEVEL_DEBUG == log_level_get()) {
		info("- - offer - -\n");
		info("%b\n", (*mb)->buf, (*mb)->end);
		info("- - - - - - -\n");
	}

	pc->signaling_state = SS_HAVE_LOCAL_OFFER;
	if (pc->ice_restart_requested)
		pc->ice_restart_offer_pending = true;
	pc->pending_sdp = mem_deref(pc->pending_sdp);
	pc->pending_sdp = operation;
	operation = NULL;

	++pc->sdp_enc_ok;

#ifdef USE_DATACHANNEL
	if (pc->data) {
		data_context_notify_channels(pc->data, false);
		goto out;
	}
#endif

out:
	if (err && operation) {
#ifdef USE_DATACHANNEL
		if (pc->data)
			data_context_description_abort(pc->data);
#endif
		sdp_session_state_restore(operation);
		operation = NULL;
		if (mb)
			*mb = mem_deref(*mb);
	}
	if (err && pc->active_transport_topology)
		pc->candidate_transport_topology =
			mem_deref(pc->candidate_transport_topology);
#ifdef USE_DATACHANNEL
	if (err && pc->data)
		data_context_notify_channels(pc->data, false);
#endif
	mem_deref(operation);
	mem_deref(pc);
	return err;
}


/*
 * RTCPeerConnection.createAnswer()
 */
int peerconnection_create_answer(struct peer_connection *pc,
				 struct mbuf **mb)
{
	struct sdp_session_state *operation = NULL;
	struct pc_media_state *media_operation = NULL;
	struct pc_transport_generation *topology_operation = NULL;
	struct le *le;
	int err;

	if (!pc || !mb)
		return EINVAL;
	*mb = NULL;

	if (!pc->gather_ok) {
		warning("peerconnection: create_answer: ice not gathered\n");
		return EPROTO;
	}

	info("peerconnection: create answer\n");

	if (pc->signaling_state != SS_HAVE_REMOTE_OFFER) {
		warning("peerconnection: create answer:"
			" invalid signaling state (%s)\n",
			signaling_state_name(pc->signaling_state));
		return EPROTO;
	}
#ifdef USE_DATACHANNEL
	err = prepared_fresh_gather_status(pc);
	if (err) {
		warning("peerconnection: fresh gather status failed (%m)\n", err);
		return err;
	}
#endif
	mem_ref(pc);
	err = sdp_session_state_save(&operation, pc->sdp);
	if (err)
		goto out;
#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_description_begin(pc->data);
		if (err)
			goto out;
	}
#endif
	if (pc->streaml.head) {
		err = pc_media_state_save(&media_operation, &pc->streaml);
		if (err)
			goto out;
		err = bundle_sdp_decode_stage(pc->sdp, &pc->streaml);
		if (err)
			goto out;
	}
	/* Prepare all negotiated local attributes before encoding the answer.
	 * This path stages scalar runtime state only; transport publication is
	 * deferred until the stable description starts ICE. */
	for (le = pc->streaml.head; le; le = le->next) {
		err = stream_update_jsep(le->data);
		if (err)
			goto out;
	}

#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_bundle_encode(pc->data, &pc->streaml);
		if (err)
			goto out;
	}
	else
#endif
	if (pc->streaml.head) {
		err = bundle_sdp_encode(pc->sdp, &pc->streaml);
		if (err)
			goto out;
	}

	err = sdp_encode(mb, pc->sdp, false);
	if (err)
		goto out;

#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_local_description(pc->data, false);
		if (err)
			goto out;
	}
#endif
#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_description_prepare(pc->data, false);
		if (err)
			goto out;
		data_context_description_publish(pc->data, false);
		data_context_description_finalize(pc->data, false);
	}
#endif
	/* The data transaction publishes its selected local DTLS role and exact
	 * lower identity into the still-rollback-capable SDP graph.  Plan from
	 * that committed candidate state, not from the provisional offer role. */
	err = stage_transport_topology(&topology_operation, pc);
	if (err) {
		warning("peerconnection: answer topology stage failed (%m)\n", err);
		goto out;
	}
	pc->candidate_transport_topology =
		mem_deref(pc->candidate_transport_topology);
	pc->candidate_transport_topology = topology_operation;
	topology_operation = NULL;
	for (le = pc->medial.head; le; le = le->next)
		mediatrack_sdp_attr_decode(le->data);
	/* Preserve HAVE_REMOTE_OFFER and its stable rollback snapshot until a
	 * synchronously-ready replacement has actually published. */
	apply_pending_candidates(pc);
	#ifdef USE_DATACHANNEL
	if (pc->active_transport_topology) {
		if (pc->mnat->updateh && pc->mnats) {
			err = pc->mnat->updateh(pc->mnats);
			if (err)
				goto out;
		}
		err = try_bootstrap_transport_session(pc);
		if (err) {
			warning("peerconnection: answer transport bootstrap failed"
				" (%m)\n", err);
			goto out;
		}
	}
	#endif
	pc->signaling_state = SS_STABLE;
	pc->pending_sdp = mem_deref(pc->pending_sdp);
	#ifdef USE_DATACHANNEL
	if (pc->data)
		data_context_description_retire(pc->data);
	#endif

	if (LEVEL_DEBUG == log_level_get()) {
		info("- - answer - -\n");
		info("%b\n", (*mb)->buf, (*mb)->end);
		info("- - - - - - -\n");
	}

	++pc->sdp_enc_ok;
	operation = mem_deref(operation);
#ifdef USE_DATACHANNEL
	if (pc->data) {
		data_context_notify_channels(pc->data, true);
		goto out;
	}
#endif

out:
	if (err && operation) {
#ifdef USE_DATACHANNEL
		if (pc->data)
			data_context_description_abort(pc->data);
#endif
		pc_media_state_restore(media_operation);
		sdp_session_state_restore(operation);
		operation = NULL;
		if (mb)
			*mb = mem_deref(*mb);
	}
#ifdef USE_DATACHANNEL
	if (err && pc->data)
		data_context_notify_channels(pc->data, false);
#endif
	mem_deref(media_operation);
	mem_deref(topology_operation);
	mem_deref(operation);
	mem_deref(pc);
	return err;
}


/*
 * RTCPeerConnection.addIceCandidate()
 */
void peerconnection_add_ice_candidate(struct peer_connection *pc,
				      const char *cand, const char *mid)
{
	struct pending_candidate *candidate;
	int err;

	if (!pc || !cand || !mid)
		return;
	if (pc->signaling_state == SS_STABLE) {
		apply_remote_candidate(pc, cand, mid);
		return;
	}
	candidate = mem_zalloc(sizeof(*candidate),
				 pending_candidate_destructor);
	if (!candidate)
		return;
	err = str_dup(&candidate->candidate, cand);
	err |= str_dup(&candidate->mid, mid);
	if (err) {
		mem_deref(candidate);
		return;
	}
	list_append(&pc->pending_candidates, &candidate->le, candidate);
}


int peerconnection_start_ice(struct peer_connection *pc)
{
	struct le *le;
	struct le *prepared_end = NULL;
	struct sdp_session_state *transport_operation = NULL;
	bool activated = false;
	int err;

	if (!pc)
		return EINVAL;
	mem_ref(pc);

	info("peerconnection: start ice\n");

	if (pc->signaling_state != SS_STABLE || !pc->sdp_dec_ok) {
		warning("peerconnection: ice: sdp not ready\n");
		err = EPROTO;
		goto out;
	}
	/* Transport modules may migrate session-level setup/fingerprint state
	 * while preparing a candidate.  Snapshot the complete SDP graph, not only
	 * one tag m-line, so every failure path can restore the exact stable
	 * description without allocating. */
	err = sdp_session_state_save(&transport_operation, pc->sdp);
	if (err)
		goto out;
	for (le = pc->streaml.head; le; le = le->next) {
		err = stream_prepare_bundle(le->data);
		if (err)
			goto abort;
		prepared_end = le;
	}
	for (le = pc->streaml.head; le; le = le->next)
		stream_commit_bundle(le->data);
	activated = true;

	if (pc->mnat->updateh && pc->mnats) {
		err = pc->mnat->updateh(pc->mnats);
		if (err) {
			warning("peerconnection: mnat update failed (%m)\n",
				err);
			goto rollback;
		}
	}

#ifdef USE_DATACHANNEL
	if (pc->data) {
		err = data_context_start(pc->data);
		if (err)
			goto rollback;
	}
	err = try_bootstrap_transport_session(pc);
	if (err)
		goto rollback;
#endif

	for (le = pc->streaml.head; le; le = le->next)
		stream_finalize_bundle(le->data);

	err = 0;
	goto out;

rollback:
	if (activated) {
		for (le = pc->streaml.tail; le; le = le->prev)
			stream_rollback_bundle(le->data);
	}
	goto out;

abort:
	for (le = pc->streaml.head; le; le = le->next) {
		stream_abort_bundle(le->data);
		if (le == prepared_end)
			break;
	}

out:
	if (err && transport_operation) {
		sdp_session_state_restore(transport_operation);
		transport_operation = NULL;
	}
	mem_deref(transport_operation);
	mem_deref(pc);
	return err;
}


int peerconnection_restart_ice(struct peer_connection *pc)
{
#ifdef USE_DATACHANNEL
	if (!pc)
		return EINVAL;
	if (!pc->menc->transportpeerseth || (pc->mnat->mediah &&
	    (!pc->mnat->mediarestartalloch ||
	     !pc->mnat->mediagatheredh ||
	     !pc->mnat->mediagatherwaith ||
	     !pc->mnat->mediagathercancelh ||
	     !pc->mnat->mediaattemptstarth ||
	     !pc->mnat->mediaattemptcancelh ||
	     !pc->mnat->mediaprepareh ||
	     !pc->mnat->mediaactivateh ||
	     !pc->mnat->mediarollbackh ||
	     !pc->mnat->mediafinalizeh ||
	     !pc->mnat->mediaaborth)))
		return ENOTSUP;
	pc->ice_restart_requested = true;
	return 0;
#else
	(void)pc;
	return ENOTSUP;
#endif
}


enum signaling_st peerconnection_signaling(const struct peer_connection *pc)
{
	return pc ? pc->signaling_state : SS_STABLE;
}


void peerconnection_close(struct peer_connection *pc)
{
	if (!pc)
		return;

	pc->closeh = NULL;
	pc->mnats = mem_deref(pc->mnats);
}
