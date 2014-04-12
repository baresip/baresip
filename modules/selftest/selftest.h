/**
 * @file selftest.h  Selftest for Baresip core -- internal API
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


/* helpers */

int re_main_timeout(uint32_t timeout);

struct sip_server {
	struct sa laddr;
	struct udp_sock *us;
	struct sip *sip;

	bool got_register_req;
	bool terminate;
};

int sip_server_create(struct sip_server **srvp);


/* test cases */

int test_cmd(void);
int test_ua_alloc(void);
int test_uag_find_param(void);
int test_ua_register(void);
