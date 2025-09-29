#include "h2645_parse.h"
#include "h2645_util.h"
#include <re.h>
#include <stdlib.h>

int h264_get_sps_pps(uint8_t *data, int len,
	uint8_t *sps, int *sps_len,
	uint8_t *pps, int *pps_len)
{
	uint8_t nalu_t;
	int nalu_len;
	uint8_t *r, *end = data + len;
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
		if (nalu_t == 7)
		{
		    memcpy(sps, r, nalu_len);
		    *sps_len = nalu_len;
		}
		else if (nalu_t == 8)
		{
		    memcpy(pps, r, nalu_len);
		    *pps_len = nalu_len;
		}
		if ((*sps_len > 0 && *pps_len > 0))
		    break;
		r = r1;
	}
	return (*sps_len > 0 && *pps_len > 0) ? 0 : -1;
}

int h264_get_sps_pps_sei(uint8_t *data, int len,
	uint8_t *sps, int *sps_len,
	uint8_t *pps, int *pps_len,
	uint8_t *sei, int *sei_len)
{
	uint8_t nalu_t;
	int nalu_len;
	uint8_t *r, *end = data + len;
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
		if (nalu_t == 7)
		{
		    memcpy(sps, r, nalu_len);
		    *sps_len = nalu_len;
		}
		else if (nalu_t == 8)
		{
		    memcpy(pps, r, nalu_len);
		    *pps_len = nalu_len;
		}
		else if (nalu_t == 6)
		{
		    memcpy(sei, r, nalu_len);
		    *sei_len = nalu_len;
		}
		if (*sps_len > 0 && *pps_len > 0 && *sei_len > 0)
		    break;
		r = r1;
	}
	return (*sps_len > 0 && *pps_len > 0 && *sei_len > 0) ? 0 : -1;
}

int h264_decode_sps_with_width_and_height(uint8_t *buf, int len,
	int *width, int *height) {
	struct h264_sps sps;
	uint8_t* web = NULL;
	uint32_t webSize;
	web = (uint8_t *)malloc(len);
	if (!web)
		goto fail;
	webSize = remove_emulation_bytes(web, len, buf, len);
	int ret = h264_sps_decode(&sps, web+1, webSize-1);
	if (ret) {
		return ret;
	}
	h264_sps_resolution(&sps, (unsigned *)width, (unsigned *)height);

fail:
	if (web)
		free(web);
	return ret;
}

