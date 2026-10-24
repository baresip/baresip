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

	/*
	 * 3. Odd-length value: "XX:XX:..:X" has f.l = 3k + 1.  The decode
	 * loop writes ceil(f.l / 3) = k + 1 bytes, while the old guard only
	 * required floor((f.l + 1) / 3) = k bytes.  With a buffer of the
	 * size the old guard accepted, this must be rejected instead of
	 * writing one byte past the end.
	 *
	 * 4. The reported input: "0:1:" (f.l = 4) with a one-byte buffer.
	 */
	{
		uint8_t buf32[32];
		uint8_t buf1[1];

		encode_fingerprint(attr + 8, 32, data, false);
		attr[8 + 95] = ':';
		attr[8 + 96] = 'A';
		attr[8 + 97] = '\0';
		sz = sizeof(buf32);   /* the size the old guard accepted */
		err = sdp_fingerprint_decode(attr, &hash, buf32, &sz);
		ASSERT_EQ(EOVERFLOW, err);

		sz = sizeof(buf1);
		err = sdp_fingerprint_decode("SHA-256 0:1:", &hash, buf1, &sz);
		ASSERT_EQ(EOVERFLOW, err);
	}

	/* 5. with a large enough buffer the odd-length value decodes, and the
	 *    reported size matches the number of bytes written */
	sz = sizeof(md);
	memset(md, 0xff, sizeof(md));
	err = sdp_fingerprint_decode(attr, &hash, md, &sz);
	ASSERT_EQ(0, err);
	ASSERT_EQ(33, sz);
	TEST_MEMCMP(data, 32, md, 32);
	ASSERT_EQ(0xa0, md[32]);   /* "A" followed by end of value */

 out:
	return err;
}
