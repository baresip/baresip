/**
 * @file pc_transport_session.c PeerConnection transport generation publisher
 *
 * Copyright (C) 2026 The baresip contributors
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


struct pc_transport_runtime_entry {
	struct le le;
	const struct pc_transport_group *group;
	struct media_transport *transport;
	struct pc_transport_data_binding data;
	size_t activation_sequence;
	bool data_reused;
	bool data_ready;
	bool media_activated;
	bool data_activated;
	bool aborted;
};


struct pc_transport_runtime_generation {
	struct list entries;
	const struct pc_transport_generation *topology;
	bool started;
	bool published;
	bool finalized;
};


struct pc_transport_session {
	struct pc_transport_runtime_generation *active;
	struct pc_transport_runtime_generation *candidate;
	struct bundle_publication *publication;
	pc_transport_session_publish_h *publishh;
	pc_transport_session_error_h *errorh;
	pc_transport_session_retire_h *retireh;
	void *arg;
	bool progressing;
	bool progress_again;
	bool starting;
};


static int session_progress(struct pc_transport_session *session,
			    bool notify_error);


static void entry_abort(struct pc_transport_runtime_entry *entry)
{
	if (!entry || entry->aborted)
		return;
	media_transport_set_observer(entry->transport, NULL, NULL);
	media_transport_abort(entry->transport);
	if (!entry->data_reused && entry->data.object && entry->data.aborth)
		entry->data.aborth(entry->data.object);
	entry->aborted = true;
}


static void entry_destructor(void *arg)
{
	struct pc_transport_runtime_entry *entry = arg;

	list_unlink(&entry->le);
	if (!entry->aborted && !entry->media_activated)
		entry_abort(entry);
	else
		media_transport_set_observer(entry->transport, NULL, NULL);
	if (entry->media_activated)
		media_transport_retire_callbacks(entry->transport);
	media_transport_detach_consumer(entry->transport);
	mem_deref(entry->data.object);
	media_transport_release(entry->transport);
	mem_deref(entry->transport);
	mem_deref((void *)entry->group);
}


static void generation_destructor(void *arg)
{
	struct pc_transport_runtime_generation *generation = arg;

	list_flush(&generation->entries);
	mem_deref((void *)generation->topology);
}


static void session_destructor(void *arg)
{
	struct pc_transport_session *session = arg;

	mem_deref(session->candidate);
	mem_deref(session->active);
	mem_deref(session->publication);
}


void pc_transport_session_stop(struct pc_transport_session *session)
{
	struct pc_transport_runtime_generation *generations[2];

	if (!session)
		return;
	generations[0] = session->active;
	generations[1] = session->candidate;
	for (size_t i = 0; i < RE_ARRAY_SIZE(generations); ++i) {
		struct pc_transport_runtime_generation *generation =
			generations[i];
		struct le *le;

		for (le = generation ? list_head(&generation->entries) : NULL;
		     le; le = le->next) {
			struct pc_transport_runtime_entry *entry = le->data;

			media_transport_stop_members(entry->transport);
		}
	}
}


static struct pc_transport_runtime_entry *find_entry(
	const struct pc_transport_runtime_generation *generation,
	const struct pc_transport_group *group)
{
	struct le *le;

	if (!generation || !group)
		return NULL;
	for (le = list_head(&generation->entries); le; le = le->next) {
		struct pc_transport_runtime_entry *entry = le->data;

		if (entry->group == group)
			return entry;
	}
	return NULL;
}


static struct pc_transport_runtime_entry *find_sctp_reuse(
	const struct pc_transport_runtime_generation *generation,
	const struct pc_transport_group *wanted)
{
	struct le *le;

	if (!generation || !wanted)
		return NULL;
	for (le = list_head(&generation->entries); le; le = le->next) {
		struct pc_transport_runtime_entry *entry = le->data;

		if (pc_transport_group_reuses_sctp(entry->group, wanted))
			return entry;
	}
	return NULL;
}


static bool group_belongs(const struct pc_transport_generation *generation,
			  const struct pc_transport_group *group)
{
	for (size_t i = 0; i < pc_transport_generation_count(generation); ++i)
		if (pc_transport_generation_group(generation, i) == group)
			return true;
	return false;
}


static bool groups_match(const struct pc_transport_group *group,
			 const struct bundle_group *bundle)
{
	const struct bundle_group *planned = pc_transport_group_bundle(group);

	if (!planned || !bundle ||
	    bundle_group_count(planned) != bundle_group_count(bundle))
		return false;
	for (size_t i = 0; i < bundle_group_count(planned); ++i)
		if (str_cmp(bundle_group_mid(planned, i),
			    bundle_group_mid(bundle, i)))
			return false;
	return true;
}


static void runtime_changed(void *arg)
{
	pc_transport_session_changed(arg);
}


int pc_transport_session_alloc(struct pc_transport_session **sessionp,
	pc_transport_session_publish_h *publishh,
	pc_transport_session_error_h *errorh, void *arg)
{
	struct pc_transport_session *session;
	int err;

	if (!sessionp || !publishh)
		return EINVAL;
	session = mem_zalloc(sizeof(*session), session_destructor);
	if (!session)
		return ENOMEM;
	err = bundle_publication_alloc(&session->publication);
	if (err) {
		mem_deref(session);
		return err;
	}
	session->publishh = publishh;
	session->errorh = errorh;
	session->arg = arg;
	*sessionp = session;
	return 0;
}


void pc_transport_session_set_retire_handler(
	struct pc_transport_session *session,
	pc_transport_session_retire_h *retireh)
{
	if (session)
		session->retireh = retireh;
}


int pc_transport_session_stage(struct pc_transport_session *session,
	const struct pc_transport_generation *generation)
{
	struct pc_transport_runtime_generation *candidate;

	if (!session || !generation)
		return EINVAL;
	if (session->candidate)
		return EBUSY;
	candidate = mem_zalloc(sizeof(*candidate), generation_destructor);
	if (!candidate)
		return ENOMEM;
	candidate->topology = mem_ref((void *)generation);
	session->candidate = candidate;
	return 0;
}


int pc_transport_session_add(struct pc_transport_session *session,
	const struct pc_transport_group *group, struct media_transport *transport,
	const struct pc_transport_data_binding *data)
{
	struct pc_transport_runtime_generation *candidate;
	struct pc_transport_runtime_entry *entry;
	struct pc_transport_runtime_entry *reuse;
	int err;

	if (!session || !group || !transport || !session->candidate)
		return EINVAL;
	candidate = session->candidate;
	if (candidate->started || find_entry(candidate, group) ||
	    !group_belongs(candidate->topology, group) ||
	    !groups_match(group, media_transport_group(transport)) ||
	    str_cmp(pc_transport_group_reuse_key(group),
		    media_transport_key(transport)))
		return EINVAL;

	reuse = find_sctp_reuse(session->active, group);
	if (reuse && data && data->object && data->object != reuse->data.object)
		return EINVAL;
	if (pc_transport_group_carries_sctp(group) &&
	    ((!reuse && (!data || !data->object)) ||
	     (reuse && !reuse->data.object)))
		return EINVAL;
	if (!pc_transport_group_carries_sctp(group) && data && data->object)
		return EINVAL;

	entry = mem_zalloc(sizeof(*entry), entry_destructor);
	if (!entry)
		return ENOMEM;
	entry->group = mem_ref((void *)group);
	entry->transport = mem_ref(transport);
	if (reuse) {
		entry->data = reuse->data;
		entry->data.object = mem_ref(reuse->data.object);
		entry->data_reused = true;
		entry->data_ready = true;
	}
	else if (data) {
		entry->data = *data;
		entry->data.object = mem_ref(data->object);
	}
	err = media_transport_bind_publication(transport,
					       session->publication);
	if (err) {
		mem_deref(entry);
		return err;
	}
	media_transport_set_observer(transport, runtime_changed, session);
	/* The SCTP object is reused, but receives after atomic publication are
	 * delivered through this generation's runtime wrapper.  Arm that copied
	 * consumer before token promotion; until activation the token still routes
	 * all records to the old published runtime. */
	if (reuse && media_transport_has_consumer(transport)) {
		err = media_transport_consumer_ready(transport);
		if (err) {
			mem_deref(entry);
			return err;
		}
	}
	list_append(&candidate->entries, &entry->le, entry);
	return 0;
}


static unsigned entry_priority(const struct pc_transport_runtime_entry *entry)
{
	for (size_t i = 0; i < pc_transport_group_member_count(entry->group); ++i) {
		const struct pc_transport_member *member =
			pc_transport_group_member(entry->group, i);
		struct stream *stream = pc_transport_member_stream(member);

		if (stream && media_transport_member_transition(
				      entry->transport, stream) ==
				      MEDIA_TRANSPORT_ADD)
			return 0;
	}
	return 1;
}


static void rollback_activated(
	struct pc_transport_runtime_generation *generation, size_t count)
{
	while (count) {
		struct le *le;
		struct pc_transport_runtime_entry *found = NULL;

		for (le = list_head(&generation->entries); le; le = le->next) {
			struct pc_transport_runtime_entry *entry = le->data;

			if (entry->media_activated &&
			    entry->activation_sequence == count) {
				found = entry;
				break;
			}
		}
		if (found) {
			if (found->data_activated && found->data.rollbackh)
				found->data.rollbackh(found->data.object);
			found->data_activated = false;
			media_transport_rollback(found->transport);
			found->media_activated = false;
		}
		--count;
	}
}


static int activate_generation(struct pc_transport_session *session,
	struct pc_transport_runtime_generation *generation)
{
	struct pc_transport_runtime_generation *old;
	struct le *le;
	size_t sequence = 0;
	int err = 0;

	bundle_publication_lock(session->publication);
	for (unsigned priority = 0; priority < 2 && !err; ++priority) {
		for (le = list_head(&generation->entries); le; le = le->next) {
			struct pc_transport_runtime_entry *entry = le->data;

			if (entry_priority(entry) != priority)
				continue;
			err = media_transport_activate(entry->transport);
			if (err)
				break;
			entry->media_activated = true;
			entry->activation_sequence = ++sequence;
			if (!entry->data_reused && entry->data.object &&
			    entry->data.activateh) {
				entry->data_activated = true;
				err = entry->data.activateh(entry->data.object);
				if (err)
					break;
			}
		}
	}
	if (err) {
		rollback_activated(generation, sequence);
		bundle_publication_unlock(session->publication);
		return err;
	}

	old = session->active;
	session->active = generation;
	session->candidate = NULL;
	session->publishh(generation->topology, session->arg);
	generation->published = true;
	bundle_publication_unlock(session->publication);

	/* Retirement and application notifications are deliberately outside the
	 * publication gate.  The old generation remains referenced until every
	 * new route and binding has completed finalization. */
	for (le = list_head(&generation->entries); le; le = le->next) {
		struct pc_transport_runtime_entry *entry = le->data;

		media_transport_finalize(entry->transport);
		if (!entry->data_reused && entry->data.object &&
		    entry->data.finalizeh)
			entry->data.finalizeh(entry->data.object);
		media_transport_notify_members(entry->transport);
	}
	generation->finalized = true;
	mem_deref(old);
	if (session->retireh)
		session->retireh(generation->topology, session->arg);
	return 0;
}


static void session_fail(struct pc_transport_session *session, int err,
			 bool notify_error)
{
	struct pc_transport_runtime_generation *candidate;
	struct pc_transport_session *hold;
	struct le *le;
	struct le *prev;

	hold = mem_ref(session);
	candidate = session->candidate;
	session->candidate = NULL;
	for (le = list_tail(&candidate->entries); le; le = prev) {
		prev = le->prev;
		entry_abort(le->data);
	}
	mem_deref(candidate);
	if (notify_error && session->errorh)
		session->errorh(err, session->arg);
	mem_deref(hold);
}


static int session_progress(struct pc_transport_session *session,
			    bool notify_error)
{
	struct pc_transport_runtime_generation *candidate;
	struct pc_transport_session *hold;
	struct le *le;
	int err = 0;

	if (!session || session->progressing)
		return 0;
	hold = mem_ref(session);
	session->progressing = true;
	do {
		session->progress_again = false;
		candidate = session->candidate;
		if (!candidate || !candidate->started)
			break;
		for (le = list_head(&candidate->entries); le; le = le->next) {
			struct pc_transport_runtime_entry *entry = le->data;

			err = media_transport_error(entry->transport);
			if (err)
				break;
			if (!media_transport_ready(entry->transport))
				break;
			if (!entry->data_ready && entry->data.object) {
				err = entry->data.prepareh(
					entry->data.object,
					media_transport_role(entry->transport));
				if (err == EAGAIN) {
					err = 0;
					break;
				}
				if (err)
					break;
				entry->data_ready = true;
			}
		}
		if (err) {
			session_fail(session, err, notify_error);
			break;
		}
		if (le)
			break;
		err = activate_generation(session, candidate);
		if (err)
			session_fail(session, err, notify_error);
	} while (session->progress_again);
	session->progressing = false;
	mem_deref(hold);
	return err;
}


int pc_transport_session_start(struct pc_transport_session *session)
{
	struct pc_transport_runtime_generation *candidate;
	struct pc_transport_session *hold;
	struct le *le;
	int err = 0;

	if (!session || !session->candidate)
		return EINVAL;
	candidate = session->candidate;
	if (candidate->started)
		return EALREADY;
	if (list_count(&candidate->entries) !=
	    pc_transport_generation_count(candidate->topology))
		return EINVAL;
	for (le = list_head(&candidate->entries); le; le = le->next) {
		struct pc_transport_runtime_entry *entry = le->data;

		if (pc_transport_group_carries_sctp(entry->group) &&
		    !entry->data_reused && !entry->data.prepareh)
			return EINVAL;
	}
	/* Keep synchronous activation failures in the caller's description
	 * transaction.  Runtime callbacks raised by attempt_start() are folded
	 * into the scan below; only failures arriving after this function returns
	 * are reported through errorh. */
	hold = mem_ref(session);
	session->starting = true;
	candidate->started = true;
	for (le = list_head(&candidate->entries); le; le = le->next) {
		struct pc_transport_runtime_entry *entry = le->data;

		err = media_transport_attempt_start(entry->transport);
		if (err && err != EAGAIN && err != EALREADY) {
			session_fail(session, err, false);
			goto out;
		}
	}
	err = session_progress(session, false);

out:
	session->starting = false;
	mem_deref(hold);
	return err;
}


int pc_transport_session_bootstrap(struct pc_transport_session *session)
{
	struct pc_transport_runtime_generation *candidate;
	struct pc_transport_session *hold;
	struct le *le;

	if (!session || !session->candidate || session->active)
		return EINVAL;
	candidate = session->candidate;
	if (candidate->started ||
	    list_count(&candidate->entries) !=
		pc_transport_generation_count(candidate->topology))
		return EINVAL;
	for (le = list_head(&candidate->entries); le; le = le->next) {
		struct pc_transport_runtime_entry *entry = le->data;

		if (!media_transport_published(entry->transport) ||
		    (pc_transport_group_carries_sctp(entry->group) &&
		     !entry->data.object))
			return EINVAL;
	}

	bundle_publication_lock(session->publication);
	hold = mem_ref(session);
	session->active = candidate;
	session->candidate = NULL;
	session->publishh(candidate->topology, session->arg);
	for (le = list_head(&candidate->entries); le; le = le->next) {
		struct pc_transport_runtime_entry *entry = le->data;

		entry->media_activated = true;
		entry->data_ready = true;
		media_transport_set_observer(entry->transport, NULL, NULL);
	}
	candidate->started = true;
	candidate->published = true;
	candidate->finalized = true;
	bundle_publication_unlock(session->publication);
	mem_deref(hold);
	return 0;
}


void pc_transport_session_changed(struct pc_transport_session *session)
{
	if (!session)
		return;
	if (session->starting || session->progressing) {
		session->progress_again = true;
		return;
	}
	(void)session_progress(session, true);
}


void pc_transport_session_abort(struct pc_transport_session *session)
{
	struct pc_transport_runtime_generation *candidate;
	struct pc_transport_session *hold;
	struct le *le;
	struct le *prev;

	if (!session || !session->candidate)
		return;
	hold = mem_ref(session);
	candidate = session->candidate;
	session->candidate = NULL;
	for (le = list_tail(&candidate->entries); le; le = prev) {
		prev = le->prev;
		entry_abort(le->data);
	}
	mem_deref(candidate);
	mem_deref(hold);
}


const struct pc_transport_generation *pc_transport_session_active_ref(
	const struct pc_transport_session *session)
{
	return session && session->active
		? mem_ref((void *)session->active->topology) : NULL;
}
