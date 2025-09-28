#ifndef H2645_PARSE_H
#define H2645_PARSE_H
#include <stdint.h>

/**
 * split sps and pps
 */
int h264_get_sps_pps(uint8_t *data, int len, uint8_t *sps, int *sps_len, uint8_t *pps, int *pps_len);

/**
 * split sps and pps and sei
 */
int h264_get_sps_pps_sei(uint8_t *data, int len, uint8_t *sps, int *sps_len, uint8_t *pps, int *pps_len, uint8_t *sei, int *sei_len);

/**
 * split vps and sps and pps
 */
int h265_get_vps_sps_pps(uint8_t *data, int len, uint8_t *vps, int *vps_len, uint8_t *sps, int *sps_len, uint8_t *pps, int *pps_len);

/**
 * split vps sps and pps and sei
 */
int h265_get_vps_sps_pps_sei(uint8_t *data, int len, uint8_t *vps, int *vps_len,uint8_t *sps, int *sps_len, uint8_t *pps, int *pps_len, uint8_t *sei, int *sei_len);

int h264_decode_sps_with_width_and_height(uint8_t *buf, int len, int *width, int *height);

int h265_decode_sps_with_width_and_height(uint8_t *buf, int len, int *width, int *height);
#endif
