/**
 * @file menc.c  Media encryption
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Register a new Media encryption module
 *
 * @param mencl List of Media-encryption modules
 * @param menc  Media encryption module
 */
void menc_register(struct list *mencl, struct menc *menc)
{
	if (!mencl || !menc)
		return;

	list_append(mencl, &menc->le, menc);

	info("mediaenc: %s\n", menc->id);
}


/**
 * Unregister a Media encryption module
 *
 * @param menc Media encryption module
 */
void menc_unregister(struct menc *menc)
{
	if (!menc)
		return;

	list_unlink(&menc->le);
}


/**
 * Find a Media Encryption module by name
 *
 * @param mencl List of Media-encryption modules
 * @param id    Name of the Media Encryption module to find
 *
 * @return Matching Media Encryption module if found, otherwise NULL
 */
const struct menc *menc_find(const struct list *mencl, const char *id)
{
	struct le *le;

	if (!mencl)
		return NULL;

	for (le = mencl->head; le; le = le->next) {
		struct menc *me = le->data;

		if (0 == str_casecmp(id, me->id))
			return me;
	}

	return NULL;
}


/**
 * Send DTLS application data through a media-encryption transport
 *
 * @param menc Media-encryption module
 * @param mt   Media-encryption transport
 * @param mb   Application data
 *
 * @return 0 if success, otherwise errorcode
 */
int menc_transport_send(const struct menc *menc, struct menc_transport *mt,
			struct mbuf *mb)
{
	if (!menc || !mt || !mb)
		return EINVAL;

	if (!menc->transportsendh)
		return ENOTSUP;

	return menc->transportsendh(mt, mb);
}


/**
 * Retarget an established media-encryption transport to a selected ICE peer
 *
 * The operation is allocation-free.  When old_peer is non-NULL, it receives
 * the peer that was installed before this call and can be supplied to a later
 * call to roll the change back.
 *
 * @param menc     Media-encryption module
 * @param mt       Established media-encryption transport
 * Passing a null peer performs a non-mutating query and requires old_peer.
 * Calls which change the peer must be serialized with transport publication
 * and sends by the owner.
 *
 * @param peer     New selected remote address, or NULL to query
 * @param old_peer Optional previous remote transport address
 *
 * @return 0 if success, otherwise errorcode
 */
int menc_transport_peer_set(const struct menc *menc,
			    struct menc_transport *mt,
			    const struct sa *peer, struct sa *old_peer)
{
	if (!menc || !mt || (!peer && !old_peer) ||
	    (peer && !sa_isset(peer, SA_ALL)))
		return EINVAL;

	if (!menc->transportpeerseth)
		return ENOTSUP;

	return menc->transportpeerseth(mt, peer, old_peer);
}


/**
 * Start a media-encryption transport after its remote address is selected
 *
 * @param menc  Media-encryption module
 * @param mt    Media-encryption transport
 * @param raddr Selected remote transport address
 *
 * @return 0 if success, otherwise errorcode
 */
int menc_transport_start(const struct menc *menc, struct menc_transport *mt,
			 const struct sa *raddr)
{
	if (!menc || !mt || !raddr)
		return EINVAL;

	if (!menc->transportstarth)
		return ENOTSUP;

	return menc->transportstarth(mt, raddr);
}


/**
 * Commit the negotiated DTLS identity before asynchronous transport start.
 *
 * @param menc Media-encryption module
 * @param mt   Media-encryption transport
 *
 * @return 0 if success, otherwise errorcode
 */
int menc_transport_commit_identity(const struct menc *menc,
				   struct menc_transport *mt)
{
	if (!menc || !mt)
		return EINVAL;

	if (!menc->transportcommitidentityh)
		return ENOTSUP;

	return menc->transportcommitidentityh(mt);
}


void menc_transport_detach(const struct menc *menc, struct menc_transport *mt)
{
	if (menc && menc->transportdetachh && mt)
		menc->transportdetachh(mt);
}


int menc_transport_rebind(const struct menc *menc, struct menc_transport *mt,
			  const struct menc_transport_binding *binding,
			  void *expected_arg,
			  struct menc_transport_binding *previous,
			  struct menc_transport_state *state)
{
	if (!menc || !mt || !binding)
		return EINVAL;
	if (!menc->transportrebindh)
		return ENOTSUP;
	return menc->transportrebindh(mt, binding, expected_arg, previous, state);
}


int menc_transport_members_prepare(const struct menc *menc,
				   struct menc_transport *mt)
{
	if (!menc || !mt)
		return EINVAL;
	if (!menc->transportmembersprepareh)
		return ENOTSUP;

	return menc->transportmembersprepareh(mt);
}


void menc_transport_members_activate(const struct menc *menc,
				     struct menc_transport *mt)
{
	if (menc && menc->transportmembersactivateh && mt)
		menc->transportmembersactivateh(mt);
}


void menc_transport_members_rollback(const struct menc *menc,
				     struct menc_transport *mt)
{
	if (menc && menc->transportmembersrollbackh && mt)
		menc->transportmembersrollbackh(mt);
}


void menc_transport_members_finalize(const struct menc *menc,
				     struct menc_transport *mt)
{
	menc_transport_members_retire(menc, mt);
	menc_transport_members_notify(menc, mt);
}


void menc_transport_members_retire(const struct menc *menc,
				   struct menc_transport *mt)
{
	if (menc && menc->transportmembersfinalizeh && mt)
		menc->transportmembersfinalizeh(mt);
}


void menc_transport_members_notify(const struct menc *menc,
				   struct menc_transport *mt)
{
	if (menc && menc->transportmembersnotifyh && mt)
		menc->transportmembersnotifyh(mt);
}


void menc_transport_members_abort(const struct menc *menc,
				  struct menc_transport *mt)
{
	if (menc && menc->transportmembersaborth && mt)
		menc->transportmembersaborth(mt);
}


int menc_transport_member_remove(const struct menc *menc,
				 struct menc_transport *mt,
				 struct menc_media *media)
{
	if (!menc || !mt || !media)
		return EINVAL;
	if (!menc->transportmemberremoveh)
		return ENOTSUP;

	return menc->transportmemberremoveh(mt, media);
}


int menc_transport_member_add(const struct menc *menc,
			      struct menc_transport *mt,
			      struct menc_media *media, bool mux)
{
	if (!menc || !mt || !media)
		return EINVAL;
	if (!menc->transportmemberaddh)
		return ENOTSUP;

	return menc->transportmemberaddh(mt, media, mux);
}


/**
 * Get the name of a media encryption event
 *
 * @param event Media encryption event
 *
 * @return String with media encryption event name
 */
const char *menc_event_name(enum menc_event event)
{
	switch (event) {

	case MENC_EVENT_SECURE:         return "Secure";
	case MENC_EVENT_VERIFY_REQUEST: return "Verify Request";
	case MENC_EVENT_PEER_VERIFIED:  return "Peer Verified";
	default: return "?";
	}
}
