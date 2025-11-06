/**
 * @file test/parse.c  Baresip selftest -- parse
 *
 * Copyright (C) 2025 Christian Spielberger - c.spielberger@commend.com
 */

#include <re.h>
#include <baresip.h>
#include "test.h"


#define DEBUG_MODULE "parse"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


static int mbuf_vph(const char *p, size_t size, void *arg)
{
	struct mbuf *mb = arg;
	return mbuf_printf(mb, "%b", p, size);
}


int test_call_cmd_prm_decode(void)
{
	int err = 0;
	struct mbuf *mb = mbuf_alloc(32);
	if (!mb)
		return ENOMEM;

	struct re_printf pf = {mbuf_vph, mb};

	const struct {
		const char *prm;
		struct call_cmd_prm cp;
		int err;
		struct pl log;
	} testv[] = {
		{
			.prm = "audio=sendonly video=inactive callid=123",
			.cp = {
				.adir = SDP_SENDONLY,
				.vdir = SDP_INACTIVE,
				.callid = PL("123"),
			},
		},
		{
			.prm = "video=recvonly callid=234 audio=sendonly",
			.cp = {
				.adir = SDP_SENDONLY,
				.vdir = SDP_RECVONLY,
				.callid = PL("234"),
			},
		},
		{
			.prm = "video=recvonly callid=234 audio=fail",
			.err = EINVAL,
			.log = PL("unknown audio/video direction 'fail'\n"),
		},
		{
			.prm = "video=sendonly",
			.cp = {
				.adir = SDP_SENDRECV,
				.vdir = SDP_SENDONLY,
			},
		},
		{
			.prm = "video=fail2",
			.err = EINVAL,
			.log = PL("unknown audio/video direction 'fail2'\n"),
		},
		{
			.prm = "recvonly",
			.cp = {
				.adir = SDP_RECVONLY,
				.vdir = SDP_RECVONLY,
			},
		},
		{
			.prm = "callid2",
			.cp = {
				.callid = PL("callid2"),
				.adir = SDP_SENDRECV,
				.vdir = SDP_SENDRECV,
			},
		},
		{
			.prm = "recvonly 345",
			.cp = {
				.adir = SDP_RECVONLY,
				.vdir = SDP_RECVONLY,
				.callid = PL("345"),
			},
		},
		{
			.prm = "fail3 456",
			.err = EINVAL,
			.log = PL("unknown audio/video direction 'fail3'\n"),
		},
		{
			.prm = "video=inactive callid=234 audio=inactive",
			.err = EINVAL,
			.log = PL("both media directions inactive\n"),
		},
	};

	struct call_cmd_prm *cp = NULL;
	for (uint32_t i=0; i<RE_ARRAY_SIZE(testv); i++) {
		mbuf_rewind(mb);

		info("test %u: %s\n", i, testv[i].prm);
		cp = mem_deref(cp);
		err = call_cmd_prm_decode(&cp, testv[i].prm, &pf);
		ASSERT_EQ(testv[i].err, err);

		/* err set means negative test, no further checks */
		if (err) {
			struct pl log;
			mbuf_set_pos(mb, 0);
			pl_set_mbuf(&log, mb);
			err = 0;
			ASSERT_PLEQ(&testv[i].log, &log);
			continue;
		}

		ASSERT_EQ(testv[i].cp.adir, cp->adir);
		ASSERT_EQ(testv[i].cp.vdir, cp->vdir);
		ASSERT_PLEQ(&testv[i].cp.callid, &cp->callid);
		/* on success print nothing */
		ASSERT_EQ(0, mbuf_get_left(mb));
	}
out:
	mem_deref(cp);
	mem_deref(mb);
	return err;
}
