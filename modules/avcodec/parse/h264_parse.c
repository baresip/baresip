#include "h2645_parse.h"
#include "h2645_util.h"
#include <re.h>
#include <stdlib.h>
#include <string.h>

int h264_get_sps_pps(const uint8_t *data, size_t len,
	uint8_t *sps, size_t *sps_len,
	uint8_t *pps, size_t *pps_len)
{
	uint8_t nalu_t;
	int nalu_len;
	const uint8_t *r, *end = data + len;
	*sps_len = 0;
	*pps_len = 0;
	r = (uint8_t *)h264_find_startcode(data, end);
	while (r < end)
	{
		uint8_t *r1;
		while (!*(r++))
		    ;
		r1 = (uint8_t *)h264_find_startcode(r, end);
		nalu_t = r[0] & 0x1f;
		nalu_len = (int)(r1 - r);
		if (nalu_t == H264_NALU_SPS)
		{
			if (MAX_SPS < nalu_len) {
				continue;
			}
		    memcpy(sps, r, nalu_len);
		    *sps_len = nalu_len;
		}
		else if (nalu_t == H264_NALU_PPS)
		{
			if (MAX_PPS < nalu_len) {
				continue;
			}
		    memcpy(pps, r, nalu_len);
		    *pps_len = nalu_len;
		}
		if ((*sps_len > 0 && *pps_len > 0))
		    break;
		r = r1;
	}
	return (*sps_len > 0 && *pps_len > 0) ? 0 : -1;
}

int h264_decode_sps_with_width_and_height(const uint8_t *buf, size_t len,
	int *width, int *height) {
	int ret = 0;
	struct h264_sps sps;
	uint8_t* web = NULL;
	uint32_t webSize;
	web = (uint8_t *)mem_alloc(len,NULL);
	if (!web)
		goto fail;
	webSize = remove_emulation_bytes(web, (uint32_t)len,
		buf, (uint32_t)len);
	if (webSize == 0) {
		ret = ENOMEM;
		goto fail;
	}
	ret = h264_sps_decode(&sps, web+1, webSize-1);
	if (ret) {
		return ret;
	}
	h264_sps_resolution(&sps, (unsigned *)width, (unsigned *)height);

fail:
	mem_deref(web);
	return ret;
}

