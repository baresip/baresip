/**
 * @file test/sdp.c  Baresip selftest -- SDP fingerprint decoding
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"

#define DEBUG_MODULE "sdp"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


/* Encode @n bytes as "XX:XX:.." (hex, optionally lowercase) into @buf */
static void encode_fingerprint(char *buf, size_t n, const uint8_t *data,
			       bool lower)
{
	static const char up[] = "0123456789ABCDEF";
	static const char lo[] = "0123456789abcdef";
	const char *hex = lower ? lo : up;
	size_t i;

	for (i = 0; i < n; i++) {
		if (i > 0)
			*buf++ = ':';
		*buf++ = hex[data[i] >> 4];
		*buf++ = hex[data[i] & 0x0f];
	}
	*buf = '\0';
}


int test_sdp_fingerprint_decode(void)
{
	int err = 0;
	struct pl hash;
	uint8_t data[33], md[33];
	char attr[256];
	size_t sz;
	int i;

	for (i = 0; i < (int)sizeof(data); i++)
		data[i] = (uint8_t)(i * 7 + 3);

	/* 1. valid 32-byte SHA-256 fingerprint, uppercase hex */
	strcpy(attr, "SHA-256 ");
	encode_fingerprint(attr + 8, 32, data, false);
	sz = sizeof(md);
	memset(md, 0xff, sizeof(md));
	err = sdp_fingerprint_decode(attr, &hash, md, &sz);
	ASSERT_EQ(0, err);
	ASSERT_EQ(32, sz);
	ASSERT_TRUE(0 == pl_strcasecmp(&hash, "SHA-256"));
	TEST_MEMCMP(data, 32, md, 32);

	/* 2. the same value with lowercase hex */
	encode_fingerprint(attr + 8, 32, data, true);
	sz = sizeof(md);
	err = sdp_fingerprint_decode(attr, &hash, md, &sz);
	ASSERT_EQ(0, err);
	ASSERT_EQ(32, sz);
	TEST_MEMCMP(data, 32, md, 32);

	/* 3. strict format: a valid value uses pairs of hex digits separated
	 *    by single colons; trailing colons, odd digit counts, empty groups
	 *    and colons in place of a hex digit are malformed */
	{
		static const uint8_t exp4[4] = {0x11, 0x22, 0x33, 0x44};

		sz = sizeof(md);
		err = sdp_fingerprint_decode("SHA-256 11:22:33:44",
					     &hash, md, &sz);
		ASSERT_EQ(0, err);
		ASSERT_EQ(4, sz);
		TEST_MEMCMP(exp4, 4, md, 4);

		err = sdp_fingerprint_decode("SHA-256 11:22:33:44:",
					     &hash, md, &sz);
		ASSERT_EQ(EBADMSG, err);

		err = sdp_fingerprint_decode("SHA-256 11:22:3",
					     &hash, md, &sz);
		ASSERT_EQ(EBADMSG, err);

		err = sdp_fingerprint_decode("SHA-256 1:2:3",
					     &hash, md, &sz);
		ASSERT_EQ(EBADMSG, err);

		err = sdp_fingerprint_decode("SHA-256 11:22::33",
					     &hash, md, &sz);
		ASSERT_EQ(EBADMSG, err);

		/* the reported input */
		err = sdp_fingerprint_decode("SHA-256 0:1:", &hash, md, &sz);
		ASSERT_EQ(EBADMSG, err);
	}

	/* 4. a valid 33-byte fingerprint reports EOVERFLOW for a 32-byte
	 *    output buffer, and decodes with a 33-byte buffer */
	encode_fingerprint(attr + 8, 33, data, false);
	sz = 32;
	err = sdp_fingerprint_decode(attr, &hash, md, &sz);
	ASSERT_EQ(EOVERFLOW, err);

	sz = sizeof(md);
	memset(md, 0xff, sizeof(md));
	err = sdp_fingerprint_decode(attr, &hash, md, &sz);
	ASSERT_EQ(0, err);
	ASSERT_EQ(33, sz);
	TEST_MEMCMP(data, 33, md, 33);
 out:
	return err;
}
