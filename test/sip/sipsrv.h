/**
 * @file sip/sipsrv.h Mock SIP server -- interface
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */


struct auth;


/*
 * SIP Server
 */

struct sip_server {
	struct sip *sip;
	struct sip_lsnr *lsnr;
	bool auth_enabled;
	bool terminate;
	unsigned instance;

	unsigned n_register_req;
	enum sip_transp tp_last;

	uint64_t secret;
	struct hash *ht_dom;
	struct hash *ht_aor;

	sip_exit_h *exith;
	void *arg;
};

int sip_server_alloc(struct sip_server **srvp,
		     sip_exit_h *exith, void *arg);
int sip_server_uri(struct sip_server *srv, char *uri, size_t sz,
		   enum sip_transp tp);


/*
 * AoR
 */

struct aor {
	struct le he;
	struct list locl;
	char *uri;
};

int aor_create(struct sip_server *srv, struct aor **aorp,
	       const struct uri *uri);
int aor_find(struct sip_server *srv, struct aor **aorp,
	     const struct uri *uri);


/*
 * Auth
 */

struct auth {
	const struct sip_server *srv;
	char realm[256];
	bool stale;
};

int auth_print(struct re_printf *pf, const struct auth *auth);
int auth_chk_nonce(struct sip_server *srv,
		   const struct pl *nonce, uint32_t expires);
int auth_set_realm(struct auth *auth, const char *realm);


/*
 * Domain
 */

struct domain {
	struct le he;
	struct hash *ht_usr;
	char *name;
};


int domain_add(struct sip_server *srv, const char *name);
int domain_find(struct sip_server *srv, const struct uri *uri);
int domain_auth(struct sip_server *srv,
		const struct uri *uri, bool user_match,
		const struct sip_msg *msg, enum sip_hdrid hdrid,
		struct auth *auth);
struct domain *domain_lookup(struct sip_server *srv, const char *name);


/*
 * Location
 */

struct location {
	struct le le;
	struct sa src;
	struct uri duri;
	char *uri;
	char *callid;
	struct loctmp *tmp;
	uint64_t expires;
	uint32_t cseq;
	double q;
	bool rm;
};

int  location_update(struct list *locl, const struct sip_msg *msg,
		     const struct sip_addr *contact, uint32_t expires);
void location_commit(struct list *locl);
void location_rollback(struct list *locl);


/*
 * User
 */

struct user;

int user_add(struct hash *ht, const char *username, const char *password,
	     const char *realm);
struct user *user_find(struct hash *ht, const struct pl *name);
const uint8_t *user_ha1(const struct user *usr);
