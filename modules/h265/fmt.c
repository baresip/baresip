
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "h265.h"


/*
1.1.4 NAL Unit Header

   HEVC maintains the NAL unit concept of H.264 with modifications.
   HEVC uses a two-byte NAL unit header, as shown in Figure 1.  The
   payload of a NAL unit refers to the NAL unit excluding the NAL unit
   header.

                     +---------------+---------------+
                     |0|1|2|3|4|5|6|7|0|1|2|3|4|5|6|7|
                     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                     |F|   Type    |  LayerId  | TID |
                     +-------------+-----------------+

              Figure 1 The structure of HEVC NAL unit header
*/


void h265_nal_encode(uint8_t buf[2], unsigned nal_unit_type,
		    unsigned nuh_temporal_id_plus1)
{
	if (!buf)
		return;

	buf[0] = (nal_unit_type & 0x3f) << 1;
	buf[1] = nuh_temporal_id_plus1 & 0x07;
}


int h265_nal_encode_mbuf(struct mbuf *mb, const struct h265_nal *nal)
{
	uint8_t buf[2];

	h265_nal_encode(buf, nal->nal_unit_type, nal->nuh_temporal_id_plus1);

	return mbuf_write_mem(mb, buf, sizeof(buf));
}


int h265_nal_decode(struct h265_nal *nal, const uint8_t *p)
{
	bool forbidden_zero_bit;
	unsigned nuh_layer_id;

	if (!nal || !p)
		return EINVAL;

	forbidden_zero_bit         = p[0] >> 7;
	nal->nal_unit_type         = (p[0] >> 1) & 0x3f;
	nuh_layer_id               = (p[0]&1)<<5 | p[1] >> 3;
	nal->nuh_temporal_id_plus1 = p[1] & 0x07;

	if (forbidden_zero_bit) {
		re_fprintf(stderr, "?!?!?!?! FORBIDDEN !!! ?!?!?!*\n");
		return EBADMSG;
	}
	if (nuh_layer_id != 0) {
		re_fprintf(stderr, "h265_nal_decode: LayerId MUST be zero\n");
		return EBADMSG;
	}

	return 0;
}


void h265_nal_print(const struct h265_nal *nal)
{
	re_printf("type=%u(%s), TID=%u\n",
		  nal->nal_unit_type,
		  h265_nalunit_name(nal->nal_unit_type),
		  nal->nuh_temporal_id_plus1);
}


static const uint8_t sc3[3] = {0, 0, 1};
static const uint8_t sc4[4] = {0, 0, 0, 1};


void h265_skip_startcode(uint8_t **p, size_t *n)
{
	if (*n < 4)
		return;

	if (0 == memcmp(*p, sc4, 4)) {
		(*p) += 4;
		*n -= 4;
	}
	else if (0 == memcmp(*p, sc3, 3)) {
		(*p) += 3;
		*n -= 3;
	}
}


bool h265_have_startcode(const uint8_t *p, size_t len)
{
	if (len >= 4 && 0 == memcmp(p, sc4, 4)) return true;
	if (len >= 3 && 0 == memcmp(p, sc3, 3)) return true;

	return false;
}


bool h265_is_keyframe(enum h265_naltype type)
{
	/* between 16 and 21 (inclusive) */
	switch (type) {

	case H265_NAL_BLA_W_LP:
	case H265_NAL_BLA_W_RADL:
	case H265_NAL_BLA_N_LP:
	case H265_NAL_IDR_W_RADL:
	case H265_NAL_IDR_N_LP:
	case H265_NAL_CRA_NUT:
		return true;
	default:
		return false;
	}
}


const char *h265_nalunit_name(enum h265_naltype type)
{
	switch (type) {

	/* VCL class */
	case H265_NAL_TRAIL_N:         return "TRAIL_N";
	case H265_NAL_TRAIL_R:         return "TRAIL_R";

	case H265_NAL_RASL_N:          return "RASL_N";
	case H265_NAL_RASL_R:          return "RASL_R";

	case H265_NAL_BLA_W_LP:        return "BLA_W_LP";
	case H265_NAL_BLA_W_RADL:      return "BLA_W_RADL";
	case H265_NAL_BLA_N_LP:        return "BLA_N_LP";
	case H265_NAL_IDR_W_RADL:      return "IDR_W_RADL";
	case H265_NAL_IDR_N_LP:        return "IDR_N_LP";
	case H265_NAL_CRA_NUT:         return "CRA_NUT";

	/* non-VCL class */
	case H265_NAL_VPS_NUT:         return "VPS_NUT";
	case H265_NAL_SPS_NUT:         return "SPS_NUT";
	case H265_NAL_PPS_NUT:         return "PPS_NUT";
	case H265_NAL_PREFIX_SEI_NUT:  return "PREFIX_SEI_NUT";
	case H265_NAL_SUFFIX_SEI_NUT:  return "SUFFIX_SEI_NUT";

	/* draft-ietf-payload-rtp-h265 */
	case H265_NAL_AP:              return "H265_NAL_AP";
	case H265_NAL_FU:              return "H265_NAL_FU";
	}

	return "???";
}
