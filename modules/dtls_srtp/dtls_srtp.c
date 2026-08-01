/**
 * @file dtls_srtp.c DTLS-SRTP media encryption
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include <stdlib.h>
#include <string.h>
#include "dtls_srtp.h"


/**
 * @defgroup dtls_srtp dtls_srtp
 *
 * DTLS-SRTP media encryption module
 *
 * This module implements end-to-end media encryption using DTLS-SRTP
 * which is now mandatory for WebRTC endpoints.
 *
 * DTLS-SRTP can be enabled in ~/.baresip/accounts:
 *
 \verbatim
  <sip:user@example.com>;mediaenc=dtls_srtp
 \endverbatim
 *
 *
 * Internally the protocol stack diagram looks something like this:
 *
 \verbatim
 *                    application
 *                        |
 *                        |
 *            [DTLS]   [SRTP]
 *                \      /
 *                 \    /
 *                  \  /
 *                   \/
 *              ( TURN/ICE )
 *                   |
 *                   |
 *                [socket]
 \endverbatim
 *
 */

struct menc_sess {
	struct sdp_session *sdp;
	bool offerer;
	menc_event_h *eventh;
	menc_error_h *errorh;
	void *arg;
};

/* one DTLS association owned by a session transport group */
struct menc_transport {
	mtx_t lock;
	struct list members;
	struct list prepared_members;
	struct list removed_members;
	struct comp wire;
	struct comp *wire_comp;
	const struct menc_sess *sess;
	struct sdp_media *sdpm;
	struct udp_sock *sock;
	struct dtls_sock *dtls_sock;
	struct tls_conn *tls_conn;
	struct sa raddr;
	menc_transport_recv_h *recvh;
	menc_transport_estab_h *estabh;
	menc_transport_close_h *closeh;
	void *arg;
	void *(*binding_arg_ref)(void *arg);
	void (*binding_arg_deref)(void *arg);
	bool binding_arg_owned;
	enum menc_dtls_role role;
	enum srtp_suite suite;
	uint8_t client_key[32+12];
	uint8_t server_key[32+12];
	struct dtls_identity *identity;
	size_t keylen;
	bool established;
	bool notified;
	bool offerer;
	bool started;
	bool members_prepared;
	bool members_activated;
	bool members_finalizing;
	bool lock_initialized;
};

/* media */
struct dtls_srtp {
	struct le transport_le;
	struct le prepared_transport_le;
	struct comp compv[2];
	const struct menc_sess *sess;
	struct sdp_media *sdpm;
	const struct stream *strm;   /**< pointer to parent */
	struct menc_transport *transport;
	struct menc_transport *prepared_transport; /* borrowed by pending owner */
	struct menc_transport *rollback_transport;
	char prepared_event[32];
	bool prepared_active;
	bool prepared_mux;
	bool prepared_negotiated;
	bool prepared_notify;
	bool prepared_remove;
	bool rollback_active;
	bool rollback_mux;
	bool rollback_negotiated;
	bool rollback_valid;
	bool started;
	bool active;
	bool mux;
	bool secure_notified;
};


static int transport_member_identity_check(const struct menc_transport *mt,
					   const struct dtls_srtp *st);

/* RFC 4145 */
enum setup {
	SETUP_ACTIVE = 0,
	SETUP_PASSIVE,
	SETUP_ACTPASS,
};


struct dtls_fingerprint {
	enum tls_fingerprint type;
	uint8_t digest[32];
	size_t len;
};


/* Negotiated identity is copied before asynchronous ICE/DTLS start. */
struct dtls_identity {
	struct dtls_fingerprint *fingerprints;
	size_t fingerprint_count;
	char *local_tls_id;
	char *remote_tls_id;
	enum menc_dtls_role role;
};

static struct tls *tls;
static const char* srtp_profiles =
	"SRTP_AES128_CM_SHA1_80:"
	"SRTP_AES128_CM_SHA1_32:"
	"SRTP_AEAD_AES_128_GCM:"
	"SRTP_AEAD_AES_256_GCM";

static int transport_start(struct menc_transport *mt, const struct sa *raddr);
static int transport_install_wire_srtp(struct menc_transport *mt);
static int transport_install_members(struct menc_transport *mt);
static size_t get_master_keylen(enum srtp_suite suite);




static void transport_notify(struct menc_transport *mt, int err)
{
	struct menc_transport_binding binding;

	mtx_lock(&mt->lock);
	if (mt->notified) {
		mtx_unlock(&mt->lock);
		return;
	}
	mt->notified = true;
	binding.recvh = mt->recvh;
	binding.estabh = mt->estabh;
	binding.closeh = mt->closeh;
	binding.arg_ref = NULL;
	binding.arg_deref = NULL;
	binding.arg = mt->arg;
	/* A reference acquired under the binding lock makes callback-argument
	 * retirement safe without holding this lock across destructive callbacks. */
	if (mt->binding_arg_ref && binding.arg) {
		binding.arg_ref = mt->binding_arg_ref;
		binding.arg_deref = mt->binding_arg_deref;
		binding.arg = binding.arg_ref(binding.arg);
	}
	mtx_unlock(&mt->lock);
	if (binding.estabh)
		binding.estabh(err, err ? MENC_DTLS_ROLE_UNKNOWN : mt->role,
				binding.arg);
	if (binding.arg_deref && binding.arg)
		binding.arg_deref(binding.arg);
}


static void transport_destructor(void *arg)
{
	struct menc_transport *mt = arg;
	void *binding_arg = NULL;
	void (*binding_arg_deref)(void *) = NULL;
	struct le *le;
	struct le *next;

	for (le = mt->prepared_members.head; le; le = next) {
		struct dtls_srtp *st = le->data;

		next = le->next;
		list_unlink(&st->prepared_transport_le);
		if (st->prepared_transport == mt) {
			st->prepared_transport = NULL;
			st->prepared_notify = false;
			st->prepared_event[0] = '\0';
		}
	}

	/* An activated transaction must normally be finalized or rolled back
	 * by its owner. Roll it back here as a last-resort ownership repair. */
	while (mt->removed_members.head) {
		struct dtls_srtp *st = mt->removed_members.head->data;

		list_unlink(&st->prepared_transport_le);
		if (st->rollback_valid && st->rollback_transport) {
			st->transport = st->rollback_transport;
			st->rollback_transport = NULL;
			st->active = st->rollback_active;
			st->mux = st->rollback_mux;
			st->compv[0].negotiated =
				st->rollback_negotiated;
			st->rollback_valid = false;
			list_append(&st->transport->members,
				    &st->transport_le, st);
		}
	}

	/* Preserve the installed callback argument through the terminal callback.
	 * transport_notify pins it independently while invoking application code. */
	if (!mt->lock_initialized)
		mt->notified = true;
	else if (!mt->established)
		transport_notify(mt, ECANCELED);

	if (mt->lock_initialized) {
		mtx_lock(&mt->lock);
		if (mt->binding_arg_owned) {
			binding_arg = mt->arg;
			binding_arg_deref = mt->binding_arg_deref;
			mt->binding_arg_owned = false;
		}
		mt->arg = NULL;
		mt->binding_arg_ref = NULL;
		mt->binding_arg_deref = NULL;
		mtx_unlock(&mt->lock);
	}
	if (binding_arg_deref && binding_arg)
		binding_arg_deref(binding_arg);

	mem_secclean(mt->client_key, sizeof(mt->client_key));
	mem_secclean(mt->server_key, sizeof(mt->server_key));
	mt->keylen = 0;
	mem_deref(mt->tls_conn);
	mem_deref(mt->dtls_sock);
	if (mt->wire_comp == &mt->wire) {
		mem_deref(mt->wire.uh_srtp);
		mem_deref(mt->wire.tx);
		mem_deref(mt->wire.rx);
		mem_deref(mt->wire.app_sock);
	}
	mem_deref(mt->sock);
	mem_deref(mt->sdpm);
	mem_deref(mt->identity);
	if (mt->lock_initialized)
		mtx_destroy(&mt->lock);
}


static enum setup setup_decode(const char *setup)
{
	if (0 == str_casecmp(setup, "active")) return SETUP_ACTIVE;
	if (0 == str_casecmp(setup, "passive")) return SETUP_PASSIVE;
	if (0 == str_casecmp(setup, "actpass")) return SETUP_ACTPASS;

	return (enum setup)-1;
}


static bool tls_id_valid(const char *value)
{
	size_t len;

	if (!value)
		return true;

	len = str_len(value);
	if (len < 20 || len > 255)
		return false;

	for (; *value; ++value) {
		if (!(((*value >= 'a') && (*value <= 'z')) ||
		      ((*value >= 'A') && (*value <= 'Z')) ||
		      ((*value >= '0') && (*value <= '9')) ||
		      *value == '+' || *value == '/' ||
		      *value == '-' || *value == '_'))
			return false;
	}

	return true;
}


static void identity_destructor(void *arg)
{
	struct dtls_identity *identity = arg;

	mem_deref(identity->fingerprints);
	mem_deref(identity->local_tls_id);
	mem_deref(identity->remote_tls_id);
}


struct fingerprint_collect {
	struct dtls_fingerprint *values;
	size_t count;
	bool seen;
	int err;
};


static bool fingerprint_collect_handler(const char *name, const char *value,
					void *arg)
{
	struct fingerprint_collect *collect = arg;
	struct dtls_fingerprint fingerprint;
	struct dtls_fingerprint *values;
	struct pl hash;
	size_t len = sizeof(fingerprint.digest);

	(void)name;
	collect->seen = true;
	if (sdp_fingerprint_decode(value, &hash, NULL, NULL)) {
		collect->err = EPROTO;
		return true;
	}
	if (pl_strcasecmp(&hash, "sha-256"))
		return false;

	memset(&fingerprint, 0, sizeof(fingerprint));
	fingerprint.type = TLS_FINGERPRINT_SHA256;
	if (sdp_fingerprint_decode(value, &hash, fingerprint.digest, &len) ||
	    len != sizeof(fingerprint.digest)) {
		collect->err = EPROTO;
		return true;
	}
	fingerprint.len = len;
	if (collect->count == SIZE_MAX / sizeof(*values)) {
		collect->err = EOVERFLOW;
		return true;
	}

	values = collect->values
		       ? mem_realloc(collect->values,
				     (collect->count + 1) * sizeof(*values))
		       : mem_alloc(sizeof(*values), NULL);
	if (!values) {
		collect->err = ENOMEM;
		return true;
	}
	collect->values = values;
	collect->values[collect->count++] = fingerprint;
	return false;
}


static int fingerprint_compare(const void *arg1, const void *arg2)
{
	const struct dtls_fingerprint *fp1 = arg1;
	const struct dtls_fingerprint *fp2 = arg2;
	int cmp;

	if (fp1->type != fp2->type)
		return fp1->type < fp2->type ? -1 : 1;
	if (fp1->len != fp2->len)
		return fp1->len < fp2->len ? -1 : 1;
	cmp = memcmp(fp1->digest, fp2->digest, fp1->len);
	return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}


static int identity_collect_fingerprints(struct dtls_identity *identity,
					 const struct menc_sess *sess,
					 const struct sdp_media *media)
{
	struct fingerprint_collect collect = {0};
	size_t dst;
	size_t src;

	sdp_media_rattr_apply(media, "fingerprint",
			      fingerprint_collect_handler, &collect);
	if (!collect.seen)
		sdp_session_rattr_apply(sess->sdp, "fingerprint",
					fingerprint_collect_handler, &collect);
	if (collect.err) {
		mem_deref(collect.values);
		return collect.err;
	}
	if (!collect.count) {
		mem_deref(collect.values);
		return EPROTO;
	}

	qsort(collect.values, collect.count, sizeof(*collect.values),
	      fingerprint_compare);
	for (src = 0, dst = 0; src < collect.count; ++src) {
		if (dst && !fingerprint_compare(&collect.values[dst - 1],
						 &collect.values[src]))
			continue;
		if (dst != src)
			collect.values[dst] = collect.values[src];
		++dst;
	}

	identity->fingerprints = collect.values;
	identity->fingerprint_count = dst;
	return 0;
}


static const char *local_attr(const struct menc_sess *sess,
			      const struct sdp_media *media,
			      const char *name)
{
	const char *value = sdp_media_lattr_apply(media, name, NULL, NULL);

	return value ? value
		     : sdp_session_lattr_apply(sess->sdp, name, NULL, NULL);
}


static int identity_alloc(struct dtls_identity **identityp,
			  const struct menc_sess *sess,
			  const struct sdp_media *media, bool offerer)
{
	struct dtls_identity *identity;
	enum setup remote_setup;
	enum setup local_setup;
	const char *remote_tls_id;
	const char *local_tls_id;
	const char *setup;
	int err;

	if (!identityp || !sess || !media)
		return EINVAL;

	setup = sdp_media_session_rattr(media, sess->sdp, "setup");
	remote_setup = setup_decode(setup);
	if ((int)remote_setup < 0 ||
	    (remote_setup == SETUP_ACTPASS && offerer))
		return EPROTO;

	setup = local_attr(sess, media, "setup");
	local_setup = setup_decode(setup);
	if ((int)local_setup < 0)
		return EPROTO;
	if (local_setup == SETUP_ACTPASS) {
		if (!offerer || remote_setup == SETUP_ACTPASS)
			return EPROTO;
		local_setup = remote_setup == SETUP_ACTIVE
				    ? SETUP_PASSIVE : SETUP_ACTIVE;
	}
	else if (remote_setup == SETUP_ACTPASS) {
		if (offerer)
			return EPROTO;
	}
	else if ((remote_setup == SETUP_ACTIVE &&
		  local_setup != SETUP_PASSIVE) ||
		 (remote_setup == SETUP_PASSIVE &&
		  local_setup != SETUP_ACTIVE))
		return EPROTO;

	identity = mem_zalloc(sizeof(*identity), identity_destructor);
	if (!identity)
		return ENOMEM;
	identity->role = local_setup == SETUP_ACTIVE
			       ? MENC_DTLS_ROLE_CLIENT : MENC_DTLS_ROLE_SERVER;

	err = identity_collect_fingerprints(identity, sess, media);
	if (err)
		goto out;

	remote_tls_id = sdp_media_session_rattr(media, sess->sdp,
					       "tls-id");
	local_tls_id = local_attr(sess, media, "tls-id");
	if (!tls_id_valid(remote_tls_id) || !tls_id_valid(local_tls_id)) {
		err = EPROTO;
		goto out;
	}
	if (remote_tls_id) {
		err = str_dup(&identity->remote_tls_id, remote_tls_id);
		if (err)
			goto out;
	}
	if (local_tls_id) {
		err = str_dup(&identity->local_tls_id, local_tls_id);
		if (err)
			goto out;
	}
	*identityp = identity;
	return 0;

out:
	mem_deref(identity);
	return err;
}


static int transport_commit_identity(struct menc_transport *mt)
{
	struct le *le = NULL;
	int err;

	if (!mt)
		return EINVAL;
	if (mt->identity)
		return 0;

	err = identity_alloc(&mt->identity, mt->sess, mt->sdpm,
			     mt->offerer);
	for (le = mt->members.head; !err && le; le = le->next)
		err = transport_member_identity_check(mt, le->data);
	for (le = mt->prepared_members.head; !err && le; le = le->next)
		err = transport_member_identity_check(mt, le->data);
	if (err)
		mt->identity = mem_deref(mt->identity);
	return err;
}


static bool identity_equal(const struct dtls_identity *identity1,
				   const struct dtls_identity *identity2)
{
	if (!identity1 || !identity2 || identity1->role != identity2->role ||
	    identity1->fingerprint_count != identity2->fingerprint_count ||
	    ((identity1->local_tls_id || identity2->local_tls_id) &&
	     (!identity1->local_tls_id || !identity2->local_tls_id ||
	      strcmp(identity1->local_tls_id, identity2->local_tls_id))) ||
	    ((identity1->remote_tls_id || identity2->remote_tls_id) &&
	     (!identity1->remote_tls_id || !identity2->remote_tls_id ||
	      strcmp(identity1->remote_tls_id, identity2->remote_tls_id))))
		return false;

	return !memcmp(identity1->fingerprints, identity2->fingerprints,
		       identity1->fingerprint_count *
			       sizeof(*identity1->fingerprints));
}


static bool remote_identity_omitted(const struct menc_sess *sess,
				    const struct sdp_media *media)
{
	return !sdp_media_rattr(media, "setup") &&
		!sdp_media_rattr(media, "fingerprint") &&
		!sdp_media_rattr(media, "tls-id") &&
		!sdp_session_rattr(sess->sdp, "setup") &&
		!sdp_session_rattr(sess->sdp, "fingerprint") &&
		!sdp_session_rattr(sess->sdp, "tls-id");
}


static int transport_member_identity_check(const struct menc_transport *mt,
					   const struct dtls_srtp *st)
{
	const struct dtls_identity *expected;
	struct dtls_identity *tag_identity = NULL;
	struct dtls_identity *member_identity = NULL;
	int err;

	if (!mt || !st)
		return EPROTO;
	/* RFC 8843 permits bundled non-tag m-lines to omit the transport
	 * attributes carried by the BUNDLE tag.  Such a member inherits the
	 * already-committed group identity.  If it supplies any identity
	 * attribute, retain the strict full comparison below so partial or
	 * conflicting declarations cannot silently alter the association. */
	if (remote_identity_omitted(st->sess, st->sdpm))
		return 0;
	expected = mt->identity;
	if (!expected) {
		/* Local-offer construction attaches members before a remote identity
		 * exists.  Defer that case, but compare immediately whenever the BUNDLE
		 * tag already carries a negotiable remote identity. */
		if (remote_identity_omitted(mt->sess, mt->sdpm))
			return 0;
		err = identity_alloc(&tag_identity, mt->sess, mt->sdpm,
				     mt->offerer);
		if (err)
			return err;
		expected = tag_identity;
	}
	err = identity_alloc(&member_identity, st->sess, st->sdpm,
			     st->sess->offerer);
	if (!err && !identity_equal(expected, member_identity))
		err = EPROTO;
	mem_deref(member_identity);
	mem_deref(tag_identity);
	return err;
}


static void sess_destructor(void *arg)
{
	struct menc_sess *sess = arg;

	mem_deref(sess->sdp);
}


static void destructor(void *arg)
{
	struct dtls_srtp *st = arg;
	size_t i;

	for (i=0; i<2; i++) {
		struct comp *c = &st->compv[i];

		mem_deref(c->uh_srtp);
		mem_deref(c->tls_conn);
		mem_deref(c->dtls_sock);
		mem_deref(c->app_sock);  /* must be freed last */
		mem_deref(c->tx);
		mem_deref(c->rx);
		mem_deref(c->identity);
	}

	list_unlink(&st->prepared_transport_le);
	list_unlink(&st->transport_le);
	st->prepared_transport = NULL;
	mem_deref(st->rollback_transport);
	mem_deref(st->transport);
	mem_deref(st->sdpm);
}


static bool verify_fingerprint(const struct sdp_session *sess,
			       const struct sdp_media *media,
			       struct tls_conn *tc)
{
	struct pl hash;
	uint8_t md_sdp[32], md_dtls[32];
	size_t sz_sdp = sizeof(md_sdp);
	size_t sz_dtls;
	enum tls_fingerprint type;
	int err;

	if (sdp_fingerprint_decode(sdp_media_session_rattr(media, sess,
							   "fingerprint"),
				   &hash, md_sdp, &sz_sdp))
		return false;

	if (0 == pl_strcasecmp(&hash, "sha-256")) {
		type = TLS_FINGERPRINT_SHA256;
		sz_dtls = 32;
	}
	else {
		warning("dtls_srtp: unknown fingerprint '%r'\n", &hash);
		return false;
	}

	err = tls_peer_fingerprint(tc, type, md_dtls, sizeof(md_dtls));
	if (err) {
		warning("dtls_srtp: could not get DTLS fingerprint (%m)\n",
			err);
		return false;
	}

	if (sz_sdp != sz_dtls || 0 != memcmp(md_sdp, md_dtls, sz_sdp)) {
		warning("dtls_srtp: %r fingerprint mismatch\n", &hash);
		info("SDP:  %w\n", md_sdp, sz_sdp);
		info("DTLS: %w\n", md_dtls, sz_dtls);
		return false;
	}

	info("dtls_srtp: verified %r fingerprint OK\n", &hash);

	return true;
}


static bool verify_cached_fingerprint(const struct menc_transport *mt,
				      struct tls_conn *tc)
{
	const struct dtls_identity *identity = mt->identity;
	uint8_t md_dtls[32];
	size_t i;

	if (!identity || !identity->fingerprint_count)
		return false;

	for (i = 0; i < identity->fingerprint_count; ++i) {
		const struct dtls_fingerprint *fingerprint =
			&identity->fingerprints[i];
		int err;

		err = tls_peer_fingerprint(tc, fingerprint->type, md_dtls,
					   sizeof(md_dtls));
		if (err) {
			warning("dtls_srtp: could not get DTLS fingerprint (%m)\n",
				err);
			continue;
		}
		if (fingerprint->len == sizeof(md_dtls) &&
		    !memcmp(fingerprint->digest, md_dtls, fingerprint->len)) {
			info("dtls_srtp: verified sha-256 fingerprint OK "
			     "(committed identity)\n");
			return true;
		}
	}

	warning("dtls_srtp: committed SHA-256 fingerprint mismatch\n");
	return false;
}


static bool transport_has_rtp_members(const struct menc_transport *mt)
{
	struct le *le;

	for (le = mt ? mt->members.head : NULL; le; le = le->next) {
		const struct dtls_srtp *member = le->data;

		/* Application m-lines participate in transport ownership but carry
		 * SCTP rather than RTP and require neither use_srtp nor exported keys. */
		if (str_casecmp(sdp_media_name(member->sdpm), "application"))
			return true;
	}
	for (le = mt ? mt->prepared_members.head : NULL; le; le = le->next) {
		const struct dtls_srtp *member = le->data;

		if (!member->prepared_remove &&
		    str_casecmp(sdp_media_name(member->sdpm), "application"))
			return true;
	}

	return false;
}


static void transport_recv_handler(struct mbuf *mb, void *arg)
{
	struct menc_transport *mt = arg;
	struct menc_transport_binding binding = {0};

	mtx_lock(&mt->lock);
	binding.recvh = mt->recvh;
	binding.arg = mt->arg;
	if (mt->binding_arg_ref && binding.arg) {
		binding.arg = mt->binding_arg_ref(binding.arg);
		binding.arg_deref = mt->binding_arg_deref;
	}
	mtx_unlock(&mt->lock);
	if (binding.recvh)
		binding.recvh(mb, binding.arg);
	if (binding.arg_deref && binding.arg)
		binding.arg_deref(binding.arg);
}


static void transport_estab_handler(void *arg)
{
	struct menc_transport *mt = arg;
	struct menc_sess *sess;
	int err;

	mem_ref(mt);
	sess = mem_ref((void *)mt->sess);

	if (!verify_cached_fingerprint(mt, mt->tls_conn)) {
		mt->tls_conn = mem_deref(mt->tls_conn);
		transport_notify(mt, EAUTH);
		goto out;
	}

	/* A standalone WebRTC application m-line uses DTLS for SCTP without
	 * negotiating the use_srtp extension.  Only transports carrying RTP
	 * members require exported SRTP keys; treating their absence as a DTLS
	 * failure prevents data-only peers (including browsers and aiortc) from
	 * ever starting SCTP after a successful handshake. */
	if (transport_has_rtp_members(mt)) {
		err = tls_srtp_keyinfo(mt->tls_conn, &mt->suite,
				       mt->client_key, sizeof(mt->client_key),
				       mt->server_key, sizeof(mt->server_key));
		if (err) {
			transport_notify(mt, err);
			goto out;
		}

		mt->keylen = get_master_keylen(mt->suite);
		if (!mt->keylen) {
			transport_notify(mt, EPROTO);
			goto out;
		}
	}
	else {
		info("dtls_srtp: ---> DTLS application transport complete\n");
	}
	mt->established = true;
	if (transport_has_rtp_members(mt)) {
		err = transport_install_wire_srtp(mt);
		if (!err)
			err = transport_install_members(mt);
		if (err) {
			mt->established = false;
			transport_notify(mt, err);
			goto out;
		}
	}

	mt->established = true;
	if (mem_nrefs(sess) > 1)
		transport_notify(mt, 0);
	else {
		mtx_lock(&mt->lock);
		mt->recvh = NULL;
		mt->estabh = NULL;
		mt->closeh = NULL;
		mt->arg = NULL;
		mt->binding_arg_ref = NULL;
		mt->binding_arg_deref = NULL;
		mtx_unlock(&mt->lock);
	}

out:
	mem_deref(mt);
	mem_deref(sess);
}


static void transport_close_handler(int err, void *arg)
{
	struct menc_transport *mt = arg;
	struct menc_transport_binding binding = {0};
	struct tls_conn *tc;

	mtx_lock(&mt->lock);
	tc = mt->tls_conn;
	mt->tls_conn = NULL;
	mtx_unlock(&mt->lock);
	mem_deref(tc);
	if (!mt->established) {
		transport_notify(mt, err ? err : EPIPE);
	}
	else {
		mtx_lock(&mt->lock);
		binding.closeh = mt->closeh;
		binding.arg = mt->arg;
		if (mt->binding_arg_ref && binding.arg) {
			binding.arg = mt->binding_arg_ref(binding.arg);
			binding.arg_deref = mt->binding_arg_deref;
		}
		mtx_unlock(&mt->lock);
		if (binding.closeh)
			binding.closeh(err ? err : ECONNRESET, binding.arg);
		if (binding.arg_deref && binding.arg)
			binding.arg_deref(binding.arg);
	}
}


static void transport_conn_handler(const struct sa *peer, void *arg)
{
	struct menc_transport *mt = arg;
	int err;

	if (mt->role != MENC_DTLS_ROLE_SERVER || mt->tls_conn)
		return;

	err = dtls_accept(&mt->tls_conn, tls, mt->dtls_sock,
			  transport_estab_handler, transport_recv_handler,
			  transport_close_handler, mt);
	if (err)
		transport_notify(mt, err);

	(void)peer;
}


static int transport_tls_id(char *buf, size_t size)
{
	uint8_t random[18];
	size_t len;
	int err;

	if (!buf || size < 25)
		return EINVAL;

	rand_bytes(random, sizeof(random));
	len = size - 1;
	err = base64url_encode(random, sizeof(random), buf, &len);
	if (err)
		return err;

	buf[len] = '\0';
	return 0;
}


static int preserve_session_attr(struct menc_sess *sess,
				 struct sdp_media *transport, const char *name)
{
	const char *value;
	struct le *le;
	int err;

	value = sdp_session_lattr_apply(sess->sdp, name, NULL, NULL);
	if (!value)
		return 0;

	for (le = list_head(sdp_session_medial(sess->sdp, true));
	     le; le = le->next) {
		struct sdp_media *media = le->data;

		if (media == transport ||
		    sdp_media_lattr_apply(media, name, NULL, NULL))
			continue;

		err = sdp_media_set_lattr(media, true, name, "%s", value);
		if (err)
			return err;
	}

	sdp_session_del_lattr(sess->sdp, name);
	return 0;
}


static int transport_alloc(struct menc_transport **mtp,
			   struct menc_sess *sess, struct udp_sock *sock,
			   const struct sa *raddr, struct sdp_media *sdpm,
			   bool offerer, menc_transport_recv_h *recvh,
			   menc_transport_estab_h *estabh,
			   menc_transport_close_h *closeh, void *arg)
{
	struct menc_transport *mt;
	const char *local_setup = offerer ? "actpass" : "active";
	const char *remote_setup;
	char tls_id[32];
	int err;

	if (!mtp || !sess || !sock || !sdpm)
		return EINVAL;

	mt = mem_zalloc(sizeof(*mt), transport_destructor);
	if (!mt)
		return ENOMEM;
	if (mtx_init(&mt->lock, mtx_plain) != thrd_success) {
		mem_deref(mt);
		return ENOMEM;
	}
	mt->lock_initialized = true;

	mt->sess = sess;
	mt->sock = mem_ref(sock);
	mt->wire.app_sock = mem_ref(sock);
	mt->wire.is_rtp = true;
	mt->wire_comp = &mt->wire;
	mt->sdpm = mem_ref(sdpm);
	mt->recvh = recvh;
	mt->estabh = estabh;
	mt->closeh = closeh;
	mt->arg = arg;
	mt->offerer = offerer;
	if (raddr)
		sa_cpy(&mt->raddr, raddr);
	remote_setup = sdp_media_session_rattr(sdpm, sess->sdp, "setup");
	if (!offerer && remote_setup) {
		enum setup setup = setup_decode(remote_setup);

		if (setup == SETUP_ACTIVE)
			local_setup = "passive";
		else if (setup == SETUP_PASSIVE || setup == SETUP_ACTPASS)
			local_setup = "active";
		else {
			err = EPROTO;
			goto out;
		}
	}

	err = preserve_session_attr(sess, sdpm, "setup");
	if (!err)
		err = preserve_session_attr(sess, sdpm, "fingerprint");
	if (!err)
		err = sdp_media_set_lattr(sdpm, true, "setup", "%s",
				  local_setup);
	if (!err)
		err = sdp_media_set_lattr(sdpm, true, "fingerprint",
				   "SHA-256 %H",
				   dtls_print_sha256_fingerprint, tls);
	if (offerer || sdp_media_rattr(sdpm, "tls-id")) {
		err |= transport_tls_id(tls_id, sizeof(tls_id));
		if (!err)
			err = sdp_media_set_lattr(sdpm, true, "tls-id",
						  "%s", tls_id);
	}
	if (err)
		goto out;

	if (raddr && sa_isset(raddr, SA_ALL)) {
		err = transport_commit_identity(mt);
		if (err)
			goto out;
		err = transport_start(mt, raddr);
		if (err)
			goto out;
	}

out:
	if (err) {
		mt->notified = true;
		mem_deref(mt);
	}
	else {
		*mtp = mt;
	}

	return err;
}


static int transport_promote(struct menc_transport **mtp,
			     struct menc_media *media,
			     menc_transport_recv_h *recvh,
			     menc_transport_estab_h *estabh,
			     menc_transport_close_h *closeh, void *arg)
{
	struct dtls_srtp *st = (struct dtls_srtp *)media;
	struct menc_transport *mt;
	struct comp *comp;
	const struct sa *peer;
	int err;

	if (!mtp || !st)
		return EINVAL;

	comp = &st->compv[0];
	if (st->transport)
		return EALREADY;
	if (!comp->negotiated && !comp->tls_conn && !comp->dtls_sock)
		return ENOENT;
	if (!st->mux || !comp->negotiated || !comp->tls_conn ||
	    !comp->dtls_sock || !comp->app_sock)
		return EAGAIN;

	peer = dtls_peer(comp->tls_conn);
	if (!peer || !sa_isset(peer, SA_ALL))
		return EAGAIN;

	mt = mem_zalloc(sizeof(*mt), transport_destructor);
	if (!mt)
		return ENOMEM;
	if (mtx_init(&mt->lock, mtx_plain) != thrd_success) {
		mem_deref(mt);
		return ENOMEM;
	}
	mt->lock_initialized = true;

	mt->sess = st->sess;
	mt->sock = mem_ref(comp->app_sock);
	mt->wire_comp = comp;
	mt->sdpm = mem_ref(st->sdpm);
	mt->recvh = recvh;
	mt->estabh = estabh;
	mt->closeh = closeh;
	mt->arg = arg;
	mt->offerer = st->sess->offerer;
	mt->role = st->active ? MENC_DTLS_ROLE_CLIENT
			      : MENC_DTLS_ROLE_SERVER;
	sa_cpy(&mt->raddr, peer);
	err = transport_commit_identity(mt);
	if (err)
		goto out;
	if (!comp->identity || !identity_equal(mt->identity, comp->identity) ||
	    mt->identity->role != mt->role) {
		warning("dtls_srtp: promoted DTLS identity changed\n");
		err = EPROTO;
		goto out;
	}
	/* The legacy media association was authenticated against the SDP view
	 * that established it.  Promotion publishes a separately owned,
	 * immutable transport identity, so bind the live peer certificate to that
	 * committed identity before transferring the connection. */
	if (!verify_cached_fingerprint(mt, comp->tls_conn)) {
		err = EAUTH;
		goto out;
	}

	err = tls_srtp_keyinfo(comp->tls_conn, &mt->suite,
			       mt->client_key, sizeof(mt->client_key),
			       mt->server_key, sizeof(mt->server_key));
	if (err)
		goto out;
	mt->keylen = get_master_keylen(mt->suite);
	if (!mt->keylen) {
		err = EPROTO;
		goto out;
	}

	mt->established = true;
	mt->started = true;
	st->transport = mem_ref(mt);
	list_append(&mt->members, &st->transport_le, st);
	mt->tls_conn = comp->tls_conn;
	comp->tls_conn = NULL;
	mt->dtls_sock = comp->dtls_sock;
	comp->dtls_sock = NULL;
	/* A transport group routes by the selected peer.  This lets an ICE-only
	 * restart move the established association and reject the former path. */
	dtls_set_single(mt->dtls_sock, false);
	dtls_set_handlers(mt->tls_conn, NULL, transport_recv_handler,
			  transport_close_handler, mt);

	*mtp = mt;
	return 0;

out:
	mt->notified = true;
	mem_deref(mt);
	return err;
}


static int transport_start(struct menc_transport *mt, const struct sa *raddr)
{
	int err;

	if (!mt || !raddr)
		return EINVAL;
	if (!sa_isset(raddr, SA_ALL))
		return EDESTADDRREQ;
	if (mt->started) {
		if (mt->established)
			transport_notify(mt, 0);
		return 0;
	}
	if (!mt->identity)
		return EPROTO;

	mt->role = mt->identity->role;
	sa_cpy(&mt->raddr, raddr);

	err = dtls_listen(&mt->dtls_sock, NULL, mt->sock, 2, LAYER_DTLS,
			  transport_conn_handler, mt);
	if (err)
		return err;

	mt->started = true;

	if (mt->role == MENC_DTLS_ROLE_CLIENT) {
		err = dtls_connect(&mt->tls_conn, tls, mt->dtls_sock,
				   &mt->raddr, transport_estab_handler,
				   transport_recv_handler,
				   transport_close_handler, mt);
		if (err) {
			mt->tls_conn = mem_deref(mt->tls_conn);
			mt->dtls_sock = mem_deref(mt->dtls_sock);
			mt->started = false;
			return err;
		}
	}

	return 0;
}


static int transport_send(struct menc_transport *mt, struct mbuf *mb)
{
	struct tls_conn *tc;
	bool established;
	int err;

	if (!mt || !mb)
		return EINVAL;
	mtx_lock(&mt->lock);
	established = mt->established;
	tc = mem_ref(mt->tls_conn);
	mtx_unlock(&mt->lock);
	if (!established || !tc) {
		mem_deref(tc);
		return ENOTCONN;
	}

	err = dtls_send(tc, mb);
	mem_deref(tc);
	return err;
}


static int transport_peer_set(struct menc_transport *mt,
			      const struct sa *peer, struct sa *old_peer)
{
	const struct sa *current;

	if (!mt || (!peer && !old_peer) ||
	    (peer && !sa_isset(peer, SA_ALL)))
		return EINVAL;
	mtx_lock(&mt->lock);
	if (!mt->established || !mt->tls_conn) {
		mtx_unlock(&mt->lock);
		return ENOTCONN;
	}

	current = dtls_peer(mt->tls_conn);
	if (!current || !sa_isset(current, SA_ALL)) {
		mtx_unlock(&mt->lock);
		return ENOTCONN;
	}

	if (old_peer)
		sa_cpy(old_peer, current);
	if (peer && !sa_cmp(current, peer, SA_ALL))
		dtls_set_peer(mt->tls_conn, peer);
	if (peer)
		sa_cpy(&mt->raddr, peer);
	mtx_unlock(&mt->lock);

	return 0;
}


static void transport_detach(struct menc_transport *mt)
{
	void *old_arg = NULL;
	void (*old_deref)(void *) = NULL;

	if (!mt)
		return;

	mtx_lock(&mt->lock);
	mt->notified = true;
	mt->recvh = NULL;
	mt->estabh = NULL;
	mt->closeh = NULL;
	if (mt->binding_arg_owned) {
		old_arg = mt->arg;
		old_deref = mt->binding_arg_deref;
		mt->binding_arg_owned = false;
	}
	mt->arg = NULL;
	mt->binding_arg_ref = NULL;
	mt->binding_arg_deref = NULL;
	mtx_unlock(&mt->lock);
	if (old_deref && old_arg)
		old_deref(old_arg);
}


/* Callback ownership changes are deliberately passive: an already completed
 * handshake is described through state, never replayed synchronously into the
 * new owner.  expected_arg makes restore/detach conditional and prevents a
 * stale runtime from clearing a newer owner's handlers. */
static int transport_rebind(
	struct menc_transport *mt,
	const struct menc_transport_binding *binding, void *expected_arg,
	struct menc_transport_binding *previous,
	struct menc_transport_state *state)
{
	void *new_arg;
	void *old_owned_arg = NULL;
	void (*old_owned_deref)(void *) = NULL;
	bool new_arg_owned = false;

	if (!mt || !binding)
		return EINVAL;
	mtx_lock(&mt->lock);
	if (expected_arg && mt->arg != expected_arg) {
		mtx_unlock(&mt->lock);
		return ESTALE;
	}

	if (previous) {
		previous->recvh = mt->recvh;
		previous->estabh = mt->estabh;
		previous->closeh = mt->closeh;
		previous->arg = mt->arg;
		previous->arg_ref = mt->binding_arg_ref;
		previous->arg_deref = mt->binding_arg_deref;
		/* The returned snapshot remains valid independently of both the
		 * transport's installed reference and the previous owner. */
		if (previous->arg_ref && previous->arg)
			previous->arg = previous->arg_ref(previous->arg);
	}
	if (state) {
		sa_cpy(&state->remote, &mt->raddr);
		state->local_role = mt->role;
		state->remote_set = sa_isset(&mt->raddr, SA_ALL);
		state->started = mt->started;
		state->established = mt->established;
	}

	new_arg = binding->arg;
	if (binding->arg_ref && binding->arg && binding->arg_deref) {
		new_arg = binding->arg_ref(binding->arg);
		new_arg_owned = true;
	}
	if (mt->binding_arg_owned) {
		old_owned_arg = mt->arg;
		old_owned_deref = mt->binding_arg_deref;
	}
	mt->recvh = binding->recvh;
	mt->estabh = binding->estabh;
	mt->closeh = binding->closeh;
	mt->arg = new_arg;
	mt->binding_arg_ref = binding->arg_ref;
	mt->binding_arg_deref = binding->arg_deref;
	mt->binding_arg_owned = new_arg_owned;
	mtx_unlock(&mt->lock);
	if (old_owned_deref && old_owned_arg)
		old_owned_deref(old_owned_arg);
	return 0;
}


static int session_alloc(struct menc_sess **sessp,
			 struct sdp_session *sdp, bool offerer,
			 menc_event_h *eventh, menc_error_h *errorh,
			 void *arg)
{
	struct menc_sess *sess;
	int err;

	if (!sessp || !sdp)
		return EINVAL;

	sess = mem_zalloc(sizeof(*sess), sess_destructor);
	if (!sess)
		return ENOMEM;

	sess->sdp     = mem_ref(sdp);
	sess->offerer = offerer;
	sess->eventh  = eventh;
	sess->errorh  = errorh;
	sess->arg     = arg;

	/* RFC 4145 */
	err = sdp_session_set_lattr(sdp, true, "setup",
				    offerer ? "actpass" : "active");
	if (err)
		goto out;

	/* RFC 4572 */
	err = sdp_session_set_lattr(sdp, true, "fingerprint", "SHA-256 %H",
				    dtls_print_sha256_fingerprint, tls);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(sess);
	else
		*sessp = sess;

	return err;
}


static size_t get_master_keylen(enum srtp_suite suite)
{
	switch (suite) {

	case SRTP_AES_CM_128_HMAC_SHA1_32: return 16+14;
	case SRTP_AES_CM_128_HMAC_SHA1_80: return 16+14;
	case SRTP_AES_128_GCM:             return 16+12;
	case SRTP_AES_256_GCM:             return 32+12;
	default: return 0;
	}
}


static int transport_install_wire_srtp(struct menc_transport *mt)
{
	struct comp *comp;
	const uint8_t *txkey;
	const uint8_t *rxkey;
	int err;

	if (!mt || !mt->wire_comp || !mt->keylen)
		return EPROTO;

	comp = mt->wire_comp;
	if (comp->uh_srtp)
		return 0;

	txkey = mt->role == MENC_DTLS_ROLE_CLIENT
			? mt->client_key : mt->server_key;
	rxkey = mt->role == MENC_DTLS_ROLE_CLIENT
			? mt->server_key : mt->client_key;
	err = srtp_stream_add(&comp->tx, mt->suite, txkey,
			      mt->keylen, true);
	if (!err)
		err = srtp_stream_add(&comp->rx, mt->suite, rxkey,
				      mt->keylen, false);
	if (!err)
		err = srtp_install(comp);
	if (err) {
		comp->uh_srtp = mem_deref(comp->uh_srtp);
		comp->tx = mem_deref(comp->tx);
		comp->rx = mem_deref(comp->rx);
	}

	return err;
}


static int transport_install_member(struct menc_transport *mt,
				    struct dtls_srtp *st, bool notify)
{
	struct comp *comp = &st->compv[0];
	struct menc_sess *sess = NULL;
	struct dtls_srtp *hold = NULL;
	char buf[32] = "";

	if (comp->negotiated) {
		if (!notify)
			return 0;
		goto notify;
	}

	st->active = mt->role == MENC_DTLS_ROLE_CLIENT;
	comp->negotiated = true;
	info("dtls_srtp: ---> group DTLS-SRTP complete (%s/RTP)"
	     " Profile=%s\n", sdp_media_name(st->sdpm),
	     srtp_suite_name(mt->suite));

notify:
	if (notify && !st->secure_notified && st->sess->eventh) {
		if (!re_snprintf(buf, sizeof(buf), "%s,RTP",
				 sdp_media_name(st->sdpm)))
			return ENOMEM;

		hold = mem_ref(st);
		sess = mem_ref((void *)st->sess);
		st->secure_notified = true;
		sess->eventh(MENC_EVENT_SECURE, buf,
			     (struct stream *)st->strm, sess->arg);
		mem_deref(sess);
		mem_deref(hold);
	}

	return 0;
}


static int transport_install_members(struct menc_transport *mt)
{
	struct le *le;
	int err;

	for (le = mt->members.head; le; le = le->next) {
		err = transport_install_member(mt, le->data, false);
		if (err)
			return err;
	}

	while (mem_nrefs(mt->sess) > 1) {
		struct dtls_srtp *st = NULL;

		for (le = mt->members.head; le; le = le->next) {
			struct dtls_srtp *member = le->data;

			if (member->compv[0].negotiated &&
			    !member->secure_notified) {
				st = mem_ref(member);
				break;
			}
		}
		if (!st)
			break;

		err = transport_install_member(mt, st, true);
		mem_deref(st);
		if (err)
			return err;
	}

	return 0;
}


/*
 * Finish all fallible work for a structurally attached replacement group.
 * The active media membership is deliberately left untouched here.
 */
static int transport_prepare_members(struct menc_transport *mt)
{
	struct le *le;
	int err;

	if (!mt)
		return EINVAL;
	if (mt->members_activated)
		return EALREADY;
	if (mt->members_prepared)
		return 0;
	if (!mt->established)
		return EAGAIN;
	if (transport_has_rtp_members(mt) && !mt->keylen) {
		err = tls_srtp_keyinfo(mt->tls_conn, &mt->suite,
				       mt->client_key, sizeof(mt->client_key),
				       mt->server_key, sizeof(mt->server_key));
		if (err)
			return err;
		mt->keylen = get_master_keylen(mt->suite);
		if (!mt->keylen)
			return EPROTO;
	}

	for (le = mt->prepared_members.head; le; le = le->next) {
		struct dtls_srtp *st = le->data;

		if (st->prepared_transport != mt ||
		    (st->prepared_remove && !st->transport) ||
		    (!st->prepared_remove && !st->prepared_mux))
			return EPROTO;
		if (st->prepared_remove)
			continue;
		st->prepared_active = mt->role == MENC_DTLS_ROLE_CLIENT;
		st->prepared_negotiated = true;
		st->prepared_notify = !st->compv[0].negotiated &&
			!st->secure_notified && st->sess->eventh;
		if (st->prepared_notify &&
		    !re_snprintf(st->prepared_event,
				 sizeof(st->prepared_event), "%s,RTP",
				 sdp_media_name(st->sdpm)))
			return ENOMEM;
	}

	err = transport_has_rtp_members(mt)
		      ? transport_install_wire_srtp(mt) : 0;
	if (err)
		return err;

	mt->members_prepared = true;
	return 0;
}


/* Allocation-free and callback-free after transport_prepare_members(). */
static void transport_activate_members(struct menc_transport *mt)
{
	while (mt && mt->members_prepared && mt->prepared_members.head) {
		struct dtls_srtp *st = mt->prepared_members.head->data;

		list_unlink(&st->prepared_transport_le);
		st->prepared_transport = NULL;

		list_unlink(&st->transport_le);
		st->rollback_transport = st->transport;
		st->rollback_active = st->active;
		st->rollback_mux = st->mux;
		st->rollback_negotiated = st->compv[0].negotiated;
		st->rollback_valid = true;
		if (st->prepared_remove) {
			st->transport = NULL;
			st->prepared_remove = false;
			list_append(&mt->removed_members,
				    &st->prepared_transport_le, st);
			continue;
		}
		st->transport = mem_ref(mt);
		st->active = st->prepared_active;
		st->mux = st->prepared_mux;
		st->compv[0].negotiated = st->prepared_negotiated;
		list_append(&mt->members, &st->transport_le, st);
	}
	if (mt && mt->members_prepared) {
		mt->members_prepared = false;
		mt->members_activated = true;
	}
}


/* Allocation-free and callback-free inverse of activation. */
static void transport_rollback_members(struct menc_transport *mt)
{
	struct le *le;
	struct le *next;
	struct menc_transport *hold;

	if (!mt || !mt->members_activated || mt->members_finalizing)
		return;
	hold = mem_ref(mt);

	while (mt->removed_members.head) {
		struct dtls_srtp *st = mt->removed_members.head->data;

		list_unlink(&st->prepared_transport_le);
		st->transport = st->rollback_transport;
		st->rollback_transport = NULL;
		st->active = st->rollback_active;
		st->mux = st->rollback_mux;
		st->compv[0].negotiated = st->rollback_negotiated;
		st->rollback_valid = false;
		list_append(&st->transport->members,
			    &st->transport_le, st);
	}

	for (le = mt->members.head; le; le = next) {
		struct dtls_srtp *st = le->data;
		struct menc_transport *candidate;

		next = le->next;
		if (!st->rollback_valid)
			continue;

		list_unlink(&st->transport_le);
		candidate = st->transport;
		st->transport = st->rollback_transport;
		st->rollback_transport = NULL;
		st->active = st->rollback_active;
		st->mux = st->rollback_mux;
		st->compv[0].negotiated = st->rollback_negotiated;
		st->rollback_valid = false;
		st->prepared_notify = false;
		st->prepared_event[0] = '\0';
		if (st->transport)
			list_append(&st->transport->members,
				    &st->transport_le, st);
		mem_deref(candidate);
	}
	mt->members_activated = false;
	mem_deref(hold);
}


/* Release the rollback graph only after the new route is irrevocably live. */
static void transport_finalize_members(struct menc_transport *mt)
{
	struct le *le;
	struct menc_transport *hold;

	if (!mt || !mt->members_activated || mt->members_finalizing)
		return;
	hold = mem_ref(mt);
	mt->members_finalizing = true;

	while (mt->removed_members.head) {
		struct dtls_srtp *st = mt->removed_members.head->data;
		struct menc_transport *old;

		list_unlink(&st->prepared_transport_le);
		old = st->rollback_transport;
		st->rollback_transport = NULL;
		st->rollback_valid = false;
		mem_deref(old);
	}

	for (;;) {
		struct dtls_srtp *st = NULL;
		struct menc_transport *old;

		for (le = mt->members.head; le; le = le->next) {
			struct dtls_srtp *member = le->data;

			if (member->rollback_valid) {
				st = mem_ref(member);
				break;
			}
		}
		if (!st)
			break;

		old = st->rollback_transport;
		st->rollback_transport = NULL;
		st->rollback_valid = false;
		mem_deref(old);
		mem_deref(st);
	}

	mt->members_activated = false;
	mt->members_finalizing = false;
	mem_deref(hold);
}


/* Publish secure events only after the session generation is current.  Each
 * loop iteration reacquires a member because application code may destroy the
 * session from the event handler. */
static void transport_notify_members(struct menc_transport *mt)
{
	struct menc_transport *hold;
	struct le *le;

	if (!mt)
		return;
	hold = mem_ref(mt);
	for (;;) {
		struct dtls_srtp *st = NULL;
		struct menc_sess *sess;

		for (le = mt->members.head; le; le = le->next) {
			struct dtls_srtp *member = le->data;

			if (member->prepared_notify) {
				st = mem_ref(member);
				break;
			}
		}
		if (!st)
			break;

		sess = mem_ref((void *)st->sess);
		st->prepared_notify = false;
		st->secure_notified = true;
		sess->eventh(MENC_EVENT_SECURE, st->prepared_event,
			     (struct stream *)st->strm, sess->arg);
		st->prepared_event[0] = '\0';
		mem_deref(sess);
		mem_deref(st);
	}
	mem_deref(hold);
}


static void transport_abort_members(struct menc_transport *mt)
{
	if (!mt || mt->members_activated)
		return;

	while (mt->prepared_members.head) {
		struct dtls_srtp *st = mt->prepared_members.head->data;

		list_unlink(&st->prepared_transport_le);
		if (st->prepared_transport == mt)
			st->prepared_transport = NULL;
		st->prepared_remove = false;
		st->prepared_notify = false;
		st->prepared_event[0] = '\0';
	}
	mt->members_prepared = false;
}


static int transport_prepare_member_remove(struct menc_transport *mt,
					   struct menc_media *media)
{
	struct dtls_srtp *st = (struct dtls_srtp *)media;

	if (!mt || !st)
		return EINVAL;
	if (!st->transport)
		return 0;
	if (mt->members_prepared || mt->members_activated)
		return EBUSY;
	if (st->prepared_transport && st->prepared_transport != mt)
		return EBUSY;
	if (!st->prepared_transport) {
		st->prepared_transport = mt;
		st->prepared_remove = true;
		list_append(&mt->prepared_members,
			    &st->prepared_transport_le, st);
	}
	return 0;
}


static int transport_prepare_member_add(struct menc_transport *mt,
					struct menc_media *media, bool mux)
{
	struct dtls_srtp *st = (struct dtls_srtp *)media;
	int err;

	if (!mt || !st)
		return EINVAL;
	if (!sdp_media_has_media(st->sdpm))
		return 0;
	if (!mux)
		return EPROTO;
	err = transport_member_identity_check(mt, st);
	if (err)
		return err;
	if (st->transport == mt)
		return 0;
	if (mt->members_prepared || mt->members_activated)
		return EBUSY;
	if (st->prepared_transport && st->prepared_transport != mt)
		return EBUSY;
	if (!st->prepared_transport) {
		st->prepared_transport = mt;
		st->prepared_remove = false;
		st->prepared_mux = mux;
		list_append(&mt->prepared_members,
			    &st->prepared_transport_le, st);
	}
	return 0;
}


static void dtls_estab_handler(void *arg)
{
	struct comp *comp = arg;
	struct dtls_srtp *ds = comp->ds;
	struct dtls_identity *identity = NULL;
	enum srtp_suite suite;
	uint8_t cli_key[32+12], srv_key[32+12];
	char buf[32] = "";
	size_t keylen;
	int err;

	debug("dtls_srtp: established: cipher=%s\n",
	      tls_cipher_name(comp->tls_conn));

	if (!verify_fingerprint(ds->sess->sdp, ds->sdpm, comp->tls_conn)) {
		warning("dtls_srtp: could not verify remote fingerprint\n");
		if (ds->sess->errorh)
			ds->sess->errorh(EPIPE, ds->sess->arg);
		return;
	}
	err = identity_alloc(&identity, ds->sess, ds->sdpm,
			     ds->sess->offerer);
	if (!err && identity->role != (ds->active ? MENC_DTLS_ROLE_CLIENT
						 : MENC_DTLS_ROLE_SERVER))
		err = EPROTO;
	if (err) {
		warning("dtls_srtp: could not cache negotiated identity (%m)\n",
			err);
		mem_deref(identity);
		if (ds->sess->errorh)
			ds->sess->errorh(err, ds->sess->arg);
		return;
	}
	comp->identity = mem_deref(comp->identity);
	comp->identity = identity;

	err = tls_srtp_keyinfo(comp->tls_conn, &suite,
			       cli_key, sizeof(cli_key),
			       srv_key, sizeof(srv_key));
	if (err) {
		warning("dtls_srtp: could not get SRTP keyinfo (%m)\n", err);
		goto out;
	}

	comp->negotiated = true;

	info("dtls_srtp: ---> DTLS-SRTP complete (%s/%s) Profile=%s\n",
	     sdp_media_name(ds->sdpm),
	     comp->is_rtp ? "RTP" : "RTCP", srtp_suite_name(suite));

	keylen = get_master_keylen(suite);

	err |= srtp_stream_add(&comp->tx, suite,
			       ds->active ? cli_key : srv_key, keylen, true);
	err |= srtp_stream_add(&comp->rx, suite,
			       ds->active ? srv_key : cli_key, keylen, false);
	if (err)
		goto out;

	err |= srtp_install(comp);
	if (err) {
		warning("dtls_srtp: srtp_install: %m\n", err);
		goto out;
	}

	if (ds->sess->eventh) {
		if (re_snprintf(buf, sizeof(buf), "%s,%s",
				sdp_media_name(ds->sdpm),
				comp->is_rtp ? "RTP" : "RTCP")) {
			if (comp->is_rtp)
				ds->secure_notified = true;
			ds->sess->eventh(MENC_EVENT_SECURE, buf,
					 (struct stream *)ds->strm,
					 ds->sess->arg);
		}
		else
			warning("dtls_srtp: failed to print secure"
				" event arguments\n");
	}

 out:
	mem_secclean(cli_key, sizeof(cli_key));
	mem_secclean(srv_key, sizeof(srv_key));
}


static void dtls_close_handler(int err, void *arg)
{
	struct comp *comp = arg;

	info("dtls_srtp: dtls-connection closed (%m)\n", err);

	comp->tls_conn = mem_deref(comp->tls_conn);

	if (!comp->negotiated) {

		if (comp->ds->sess->errorh)
			comp->ds->sess->errorh(err, comp->ds->sess->arg);
	}
}


static void dtls_conn_handler(const struct sa *peer, void *arg)
{
	struct comp *comp = arg;
	int err;

	info("dtls_srtp: %s: incoming DTLS connect from %J\n",
	     sdp_media_name(comp->ds->sdpm), peer);

	/*
	 * Promotion transfers this listener to the shared transport.  Its
	 * connection callback remains the one registered by the legacy media
	 * component, so explicitly refuse a second association here.
	 */
	if (comp->ds->transport) {
		warning("dtls_srtp: promoted transport refuses reaccept\n");
		return;
	}

	if (comp->ds->active) {
		warning("dtls_srtp: conn_handler: role is active\n");
		return;
	}

	if (comp->tls_conn) {
		warning("dtls_srtp: '%s' dtls already accepted (peer = %J)\n",
			sdp_media_name(comp->ds->sdpm),
			dtls_peer(comp->tls_conn));

		if (comp->ds->sess->errorh)
			comp->ds->sess->errorh(EPROTO, comp->ds->sess->arg);

		return;
	}

	err = dtls_accept(&comp->tls_conn, tls, comp->dtls_sock,
			  dtls_estab_handler, NULL, dtls_close_handler, comp);
	if (err) {
		warning("dtls_srtp: dtls_accept failed (%m)\n", err);
		return;
	}
}


static int component_start(struct comp *comp, const struct sa *raddr)
{
	int err = 0;

	debug("dtls_srtp: component start: %s [raddr=%J]\n",
	      comp->is_rtp ? "RTP" : "RTCP", raddr);

	if (!comp->app_sock || comp->negotiated || comp->dtls_sock)
		return 0;

	err = dtls_listen(&comp->dtls_sock, NULL,
			  comp->app_sock, 2, LAYER_DTLS,
			  dtls_conn_handler, comp);
	if (err) {
		warning("dtls_srtp: dtls_listen failed (%m)\n", err);
		return err;
	}

	/* maximum one DTLS connection */
	dtls_set_single(comp->dtls_sock, true);

	if (sa_isset(raddr, SA_ALL)) {

		if (comp->ds->active && !comp->tls_conn) {

			info("dtls_srtp: '%s,%s' dtls connect to %J\n",
			     sdp_media_name(comp->ds->sdpm),
			     comp->is_rtp ? "RTP" : "RTCP",
			     raddr);

			err = dtls_connect(&comp->tls_conn, tls,
					   comp->dtls_sock, raddr,
					   dtls_estab_handler, NULL,
					   dtls_close_handler, comp);
			if (err) {
				warning("dtls_srtp: dtls_connect()"
					" failed (%m)\n", err);
				return err;
			}
		}
	}

	return err;
}


static int media_start(struct dtls_srtp *st, struct sdp_media *sdpm,
		       const struct sa *raddr_rtp,
		       const struct sa *raddr_rtcp)
{
	int err = 0;

	if (st->started)
		return 0;

	info("dtls_srtp: media=%s -- start DTLS %s\n",
	     sdp_media_name(sdpm), st->active ? "client" : "server");

	if (!sdp_media_has_media(sdpm))
		return 0;

	err = component_start(&st->compv[0], raddr_rtp);

	if (!st->mux)
		err |= component_start(&st->compv[1], raddr_rtcp);

	if (err)
		return err;

	st->started = true;

	return 0;
}


static int media_alloc(struct menc_media **mp, struct menc_sess *sess,
		       struct menc_transport *transport,
		       struct rtp_sock *rtp,
		       struct udp_sock *rtpsock, struct udp_sock *rtcpsock,
		       const struct sa *raddr_rtp,
		       const struct sa *raddr_rtcp,
		       struct sdp_media *sdpm, const struct stream *strm)
{
	struct dtls_srtp *st;
	const char *setup, *fingerprint;
	bool mux;
	int err = 0;
	unsigned i;
	(void)rtp;

	if (!mp || !sess)
		return EINVAL;

	st = (struct dtls_srtp *)*mp;
	if (st)
		goto setup;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->sess = sess;
	st->sdpm = mem_ref(sdpm);
	st->strm = strm;
	st->compv[0].app_sock = mem_ref(rtpsock);
	st->compv[1].app_sock = mem_ref(rtcpsock);

	for (i=0; i<2; i++)
		st->compv[i].ds = st;

	st->compv[0].is_rtp = true;
	st->compv[1].is_rtp = false;

	err = sdp_media_set_alt_protos(st->sdpm, 4,
				       "RTP/SAVP",
				       "RTP/SAVPF",
				       "UDP/TLS/RTP/SAVP",
				       "UDP/TLS/RTP/SAVPF");
	if (err)
		goto out;

 out:
	if (err) {
		mem_deref(st);
		return err;
	}
	else
		*mp = (struct menc_media *)st;

 setup:
	mux = (rtpsock == rtcpsock) || (rtcpsock == NULL);

	if (transport) {
		/* A port-zero BUNDLE member has no transport to negotiate. */
		if (!sdp_media_has_media(st->sdpm))
			return 0;
		if (!mux)
			return EPROTO;
		err = transport_member_identity_check(transport, st);
		if (err) {
			warning("dtls_srtp: conflicting BUNDLE transport "
				"attributes\n");
			return err;
		}

		if (st->transport && st->transport != transport) {
			/*
			 * Replacement attachment is structural but provisional.
			 * Keep the old member and its SRTP state fully active until
			 * the session transaction explicitly activates this group.
			 */
			if (st->prepared_transport &&
			    st->prepared_transport != transport)
				return EBUSY;
			if (transport->members_prepared ||
			    transport->members_activated)
				return EBUSY;
			st->prepared_mux = mux;
			st->prepared_remove = false;
			if (!st->prepared_transport) {
				st->prepared_transport = transport;
				list_append(&transport->prepared_members,
					    &st->prepared_transport_le, st);
			}
			return 0;
		}

		if (!st->transport) {
			st->mux = mux;
			st->transport = mem_ref(transport);
			list_append(&transport->members,
				    &st->transport_le, st);
		}

		if (transport->established) {
			struct menc_transport *mt = mem_ref(transport);
			struct menc_sess *hold_sess = mem_ref(sess);
			struct dtls_srtp *hold_st = mem_ref(st);
			bool alive;

			/* A transport may establish first as application-only and gain
			 * an RTP member later in the same negotiated association. Export
			 * the already-negotiated use_srtp keys at that boundary. */
			if (!mt->keylen) {
				err = tls_srtp_keyinfo(
					mt->tls_conn, &mt->suite,
					mt->client_key, sizeof(mt->client_key),
					mt->server_key, sizeof(mt->server_key));
				if (!err)
					mt->keylen = get_master_keylen(mt->suite);
				if (!err && !mt->keylen)
					err = EPROTO;
			}
			if (!err)
				err = transport_install_wire_srtp(mt);
			if (!err)
				err = transport_install_member(mt, hold_st, true);
			alive = mem_nrefs(hold_sess) > 1;
			if (!err && alive && raddr_rtp &&
			    sa_isset(raddr_rtp, SA_ALL))
				err = transport_start(mt, raddr_rtp);

			mem_deref(hold_st);
			mem_deref(hold_sess);
			mem_deref(mt);
			return err;
		}

		if (raddr_rtp && sa_isset(raddr_rtp, SA_ALL)) {
			err = transport_commit_identity(transport);
			if (err)
				return err;
			return transport_start(transport, raddr_rtp);
		}

		return 0;
	}
	st->mux = mux;

	setup = sdp_media_session_rattr(st->sdpm, st->sess->sdp, "setup");
	if (setup) {
		enum setup rsetup = setup_decode(setup);

		st->active = rsetup != SETUP_ACTIVE;

		err = media_start(st, st->sdpm, raddr_rtp, raddr_rtcp);
		if (err)
			return err;
	}

	/* SDP offer/answer on fingerprint attribute */
	fingerprint = sdp_media_session_rattr(st->sdpm, st->sess->sdp,
					      "fingerprint");
	if (fingerprint) {

		struct pl hash;

		err = sdp_fingerprint_decode(fingerprint, &hash, NULL, NULL);
		if (err)
			return err;

		if (0 == pl_strcasecmp(&hash, "SHA-256")) {
			err = sdp_media_set_lattr(st->sdpm, true,
						  "fingerprint", "SHA-256 %H",
						 dtls_print_sha256_fingerprint,
						  tls);
		}
		else {
			info("dtls_srtp: unsupported fingerprint hash `%r'\n",
			     &hash);
			return EPROTO;
		}
	}

	return err;
}


static struct menc dtls_srtp = {
	.id          = "dtls_srtp",
	.sdp_proto   = "UDP/TLS/RTP/SAVPF",
	.wait_secure = true,
	.sessh       = session_alloc,
	.transporth  = transport_alloc,
	.transportpromoteh = transport_promote,
	.transportcommitidentityh = transport_commit_identity,
	.transportstarth = transport_start,
	.transportsendh = transport_send,
	.transportpeerseth = transport_peer_set,
	.transportdetachh = transport_detach,
	.transportrebindh = transport_rebind,
	.transportmembersprepareh = transport_prepare_members,
	.transportmembersactivateh = transport_activate_members,
	.transportmembersrollbackh = transport_rollback_members,
	.transportmembersfinalizeh = transport_finalize_members,
	.transportmembersnotifyh = transport_notify_members,
	.transportmembersaborth = transport_abort_members,
	.transportmemberremoveh = transport_prepare_member_remove,
	.transportmemberaddh = transport_prepare_member_add,
	.mediah      = media_alloc
};


static int module_init(void)
{
	struct list *mencl = baresip_mencl();
	char ec[64] = "prime256v1";
	const char *cn = "dtls@baresip";
	int err;

	err = tls_alloc(&tls, TLS_METHOD_DTLS, NULL, NULL);
	if (err) {
		warning("dtls_srtp: failed to create DTLS context (%m)\n",
			err);
		return err;
	}

	(void)conf_get_str(conf_cur(), "dtls_srtp_use_ec", ec, sizeof(ec));

	info ("dtls_srtp: use %s for elliptic curve cryptography\n", ec);

	err = tls_set_selfsigned_ec(tls, cn, ec);
	if (err) {
		warning("dtls_srtp: failed to self-sign "
			"ec-certificate (%m)\n", err);
		return err;
	}

	tls_set_verify_client_trust_all(tls);

	err = tls_set_srtp(tls, srtp_profiles);
	if (err) {
		warning("dtls_srtp: failed to enable SRTP profile (%m)\n",
			err);
		return err;
	}

	menc_register(mencl, &dtls_srtp);

	debug("DTLS-SRTP ready with profiles %s\n", srtp_profiles);

	return 0;
}


static int module_close(void)
{
	menc_unregister(&dtls_srtp);
	tls = mem_deref(tls);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(dtls_srtp) = {
	"dtls_srtp",
	"menc",
	module_init,
	module_close
};
