/**
 * @file test.h  Selftest for Baresip core -- internal API
 *
 * Copyright (C) 2010 Alfred E. Heggestad
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
		warning("selftest: ASSERT_EQ: %s:%u: %s():"	\
			" expected=%d(0x%x), actual=%d(0x%x)\n",\
			__FILE__, __LINE__, __func__,		\
			(expected), (expected),			\
			(actual), (actual));			\
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

#define ASSERT_PLEQ(expected, actual)					\
	if (0 != pl_cmp((expected), (actual))) {			\
		warning("selftest: ASSERT_PLEQ: %s:%u:"			\
			" expected = '%r', actual = '%r'\n",		\
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

#define TEST_ERR_TXT(err,txt)						\
	if ((err)) {							\
		(void)re_fprintf(stderr, "\n");				\
		warning("TEST_ERR: %s:%u: %s"				\
			      " (%m)\n",				\
			      __FILE__, __LINE__,			\
			      (txt), (err));				\
		goto out;						\
	}

#define TEST_MEMCMP(expected, expn, actual, actn)			\
	if (expn != actn ||						\
	    0 != memcmp((expected), (actual), (expn))) {		\
		(void)re_fprintf(stderr, "\n");				\
		warning("TEST_MEMCMP: %s:%u:"				\
			" %s(): failed\n",				\
			__FILE__, __LINE__, __func__);			\
		test_hexdump_dual(stderr,				\
				  expected, expn,			\
				  actual, actn);			\
		err = EINVAL;						\
		goto out;						\
	}

#define TEST_STRCMP(expected, expn, actual, actn)			\
	if (expn != actn ||						\
	    0 != memcmp((expected), (actual), (expn))) {		\
		(void)re_fprintf(stderr, "\n");				\
		warning("TEST_STRCMP: %s:%u:"				\
			" failed\n",					\
			__FILE__, __LINE__);				\
		(void)re_fprintf(stderr,				\
				 "expected string: (%zu bytes)\n"	\
				 "\"%b\"\n",				\
				 (size_t)(expn),			\
				 (expected), (size_t)(expn));		\
		(void)re_fprintf(stderr,				\
				 "actual string: (%zu bytes)\n"		\
				 "\"%b\"\n",				\
				 (size_t)(actn),			\
				 (actual), (size_t)(actn));		\
		err = EINVAL;						\
		goto out;						\
	}


/* helpers */

int re_main_timeout(uint32_t timeout_ms);
bool test_cmp_double(double a, double b, double precision);
void test_hexdump_dual(FILE *f,
		       const void *ep, size_t elen,
		       const void *ap, size_t alen);


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
int dns_server_add_aaaa(struct dns_server *srv, const char *name,
			const uint8_t *addr);
int dns_server_add_srv(struct dns_server *srv, const char *name,
		       uint16_t pri, uint16_t weight, uint16_t port,
		       const char *target);


/*
 * Mock Audio-player
 */

struct auplay;

typedef void (mock_sample_h)(struct auframe *af, const char *dev, void *arg);

void mock_aucodec_register(void);
void mock_aucodec_unregister(void);
int mock_auplay_register(struct auplay **auplayp, struct list *auplayl,
			 mock_sample_h *sampleh, void *arg);


/*
 * Mock Media NAT-traversal
 */

void mock_mnat_register(struct list *mnatl);
void mock_mnat_unregister(void);


/*
 * Mock Video-codec
 */

void mock_vidcodec_register(void);
void mock_vidcodec_unregister(void);


/*
 * Mock Video-display
 */

struct vidisp;
struct vidframe;

typedef void (mock_vidisp_h)(const struct vidframe *frame, uint64_t timestamp,
			     const char *title, void *arg);

int mock_vidisp_register(struct vidisp **vidispp,
			 mock_vidisp_h *disph, void *arg);


/* test cases */

int test_account(void);
int test_account_uri_complete(void);
int test_aulevel(void);
int test_call_answer(void);
int test_call_answer_hangup_a(void);
int test_call_answer_hangup_b(void);
int test_call_aulevel(void);
int test_call_custom_headers(void);
int test_call_dtmf(void);
int test_call_format_float(void);
int test_call_max(void);
int test_call_mediaenc(void);
int test_call_medianat(void);
int test_call_multiple(void);
int test_call_progress(void);
int test_call_reject(void);
int test_call_rtcp(void);
int test_call_rtp_timeout(void);
int test_call_tcp(void);
int test_call_deny_udp(void);
int test_call_transfer(void);
int test_call_transfer_fail(void);
int test_call_attended_transfer(void);
int test_call_video(void);
int test_call_change_videodir(void);
int test_call_webrtc(void);
int test_call_bundle(void);
int test_call_ipv6ll(void);
int test_call_100rel_audio(void);
int test_call_100rel_video(void);
int test_call_hold_resume(void);
int test_call_srtp_tx_rekey(void);
int test_cmd(void);
int test_cmd_long(void);
int test_contact(void);
int test_event(void);
int test_jbuf(void);
int test_jbuf_adaptive(void);
int test_jbuf_adaptive_video(void);
int test_message(void);
int test_network(void);
int test_play(void);
int test_stunuri(void);
int test_ua_alloc(void);
int test_ua_options(void);
int test_ua_refer(void);
int test_ua_register(void);
int test_ua_register_auth(void);
int test_ua_register_auth_dns(void);
int test_ua_register_dns(void);
int test_uag_find_param(void);
int test_video(void);
int test_clean_number(void);
int test_clean_number_only_numeric(void);
