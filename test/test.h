/**
 * @file test.h  Selftest for Baresip core -- internal API
 *
 * Copyright (C) 2010 Creytiv.com
 */


#define ASSERT_TRUE(cond)					\
	if (!(cond)) {						\
		warning("selftest: ASSERT_TRUE: %s:%u:\n",	\
			__FILE__, __LINE__);			\
		err = EINVAL;					\
		goto out;					\
	}

#define ASSERT_EQ(expected, actual)				\
	if ((expected) != (actual)) {				\
		warning("selftest: ASSERT_EQ: %s:%u:"		\
			" expected=%d, actual=%d\n",		\
			__FILE__, __LINE__,			\
			(int)(expected), (int)(actual));	\
		err = EINVAL;					\
		goto out;					\
	}

#define ASSERT_STREQ(expected, actual)					\
	if (0 != str_cmp((expected), (actual))) {			\
		warning("selftest: ASSERT_STREQ: %s:%u:"		\
			" expected = '%s', actual = '%s'\n",		\
			__FILE__, __LINE__,				\
			(expected), (actual));				\
		err = EBADMSG;						\
		goto out;						\
	}

#define TEST_ERR(err)							\
	if ((err)) {							\
		(void)re_fprintf(stderr, "\n");				\
		warning("TEST_ERR: %s:%u:"			\
			      " (%m)\n",				\
			      __FILE__, __LINE__,			\
			      (err));					\
		goto out;						\
	}


/* helpers */

int re_main_timeout(uint32_t timeout_ms);


#ifdef USE_TLS
extern const char test_certificate[];
#endif


/*
 * SIP Server
 */

struct sip_server {
	struct sip *sip;
	struct sip_lsnr *lsnr;
	bool terminate;

	unsigned n_register_req;
	enum sip_transp tp_last;
};

int sip_server_alloc(struct sip_server **srvp);
int sip_server_uri(struct sip_server *srv, char *uri, size_t sz,
		   enum sip_transp tp);


/* test cases */

int test_cmd(void);
int test_ua_alloc(void);
int test_uag_find_param(void);
int test_ua_register(void);

int test_call_answer(void);
int test_call_reject(void);
int test_call_af_mismatch(void);
int test_call_answer_hangup_a(void);
int test_call_answer_hangup_b(void);


#ifdef __cplusplus
extern "C" {
#endif

int test_cplusplus(void);

#ifdef __cplusplus
}
#endif
