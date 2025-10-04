#ifndef H2645_PARSE_H
#define H2645_PARSE_H
#include <stdint.h>
#include <stddef.h>

#define MAX_VPS 256
#define MAX_SPS 256
#define MAX_PPS 256

/**
 * split sps and pps
 */
int h264_get_sps_pps(const uint8_t *data, size_t len,
			uint8_t *sps, size_t *sps_len,
			uint8_t *pps, size_t *pps_len);

/**
 * split vps and sps and pps
 */
int h265_get_vps_sps_pps(const uint8_t *data, size_t len,
			uint8_t *vps, size_t *vps_len,
			uint8_t *sps, size_t *sps_len,
			uint8_t *pps, size_t *pps_len);

int h264_decode_sps_with_width_and_height(const uint8_t *buf, size_t len,
			int *width, int *height);

int h265_decode_sps_with_width_and_height(const uint8_t *buf,
			size_t len, int *width, int *height);
#endif
