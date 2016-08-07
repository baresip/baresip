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

#define ASSERT_DOUBLE_EQ(expected, actual, prec)			\
	if (!test_cmp_double((expected), (actual), (prec))) {		\
		warning("selftest: ASSERT_DOUBLE_EQ: %s:%u:"		\
			" expected=%f, actual=%f\n",			\
			__FILE__, __LINE__,				\
			(double)(expected), (double)(actual));		\
		err = EINVAL;						\
		goto out;						\
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
bool test_cmp_double(double a, double b, double precision);


#ifdef USE_TLS
extern const char test_certificate[];
#endif


/*
 * Mock DNS-Server
 */

struct dns_server {
	struct udp_sock *us;
	struct sa addr;
	struct list rrl;
	bool rotate;
};

int dns_server_alloc(struct dns_server **srvp, bool rotate);
int dns_server_add_a(struct dns_server *srv,
		     const char *name, uint32_t addr);
int dns_server_add_srv(struct dns_server *srv, const char *name,
		       uint16_t pri, uint16_t weight, uint16_t port,
		       const char *target);

/*
 * Mock Audio-source
 */

struct ausrc;

int mock_ausrc_register(struct ausrc **ausrcp);


/* test cases */

int test_cmd(void);
int test_cmd_long(void);
int test_contact(void);
int test_ua_alloc(void);
int test_uag_find_param(void);
int test_ua_register(void);
int test_ua_register_dns(void);
int test_ua_register_auth(void);
int test_ua_register_auth_dns(void);
int test_ua_options(void);
int test_mos(void);
int test_network(void);

int test_call_answer(void);
int test_call_reject(void);
int test_call_af_mismatch(void);
int test_call_answer_hangup_a(void);
int test_call_answer_hangup_b(void);
int test_call_rtp_timeout(void);
int test_call_multiple(void);
int test_call_max(void);
int test_call_dtmf(void);


#ifdef __cplusplus
extern "C" {
#endif

int test_cplusplus(void);

#ifdef __cplusplus
}
#endif
