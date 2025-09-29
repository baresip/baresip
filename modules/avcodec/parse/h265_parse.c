#include "h2645_util.h"
#include "h2645_parse.h"
#include <re.h>
#include <re_h265.h>
#include <stdlib.h>

int h265_get_vps_sps_pps(uint8_t *data, int len,
	uint8_t *vps,int *vps_len,
	uint8_t *sps, int *sps_len,
	uint8_t *pps, int *pps_len)
{
	uint8_t nalu_t;
	int nalu_len;
	uint8_t *r, *end = data + len;
	*vps_len = 0, *sps_len = 0;
	*pps_len = 0;
	r = (uint8_t *)h264_find_startcode(data, end);
	while (r < end)
	{
		uint8_t *r1;
		while (!*(r++))
		    ;
		r1 = (uint8_t *)h264_find_startcode(r, end);
		struct h265_nal nal;
		h265_nal_decode(&nal,r);
		nalu_t = nal.nal_unit_type;
		nalu_len = (int)(r1 - r);
		if (nalu_t == H265_NAL_VPS_NUT)
		{
		    memcpy(vps, r, nalu_len);
		    *vps_len = nalu_len;
		}
		else if (nalu_t == H265_NAL_SPS_NUT)
		{
		    memcpy(sps, r, nalu_len);
		    *sps_len = nalu_len;
		}
		else if (nalu_t == H265_NAL_PPS_NUT)
		{
		    memcpy(pps, r, nalu_len);
		    *pps_len = nalu_len;
		}
		if (*vps_len > 0 && *sps_len > 0 && *pps_len > 0)
		    break;
		r = r1;
	}
	return (*vps_len > 0 && *sps_len > 0 && *pps_len > 0) ? 0 : -1;
}

int h265_decode_sps_with_width_and_height(uint8_t *buf, int nLen,
	int *width,
	int *height)
{
	int ret = 0;
	struct getbit gb;
	uint8_t* web = NULL;
	uint32_t webSize;
	web = (uint8_t *)malloc(nLen);
	if (!web) {
		ret = ENOMEM;
		goto fail;
	}
	webSize = remove_emulation_bytes(web, nLen, buf, nLen);
	if (webSize == 0) {
		ret = ENOMEM;
		goto fail;
	}
	getbit_init(&gb, web, (webSize) * 8);
	get_bits(&gb, 4); /*sps_video_parameter_set_id*/
	int sps_max_sub_layers_minus1 = get_bits(&gb, 3);
	get_bits(&gb, 1); /*sps_temporal_id_nesting_flag*/
	get_bits(&gb, 2);  /* profile_space*/
	get_bits(&gb, 1);  /* tier_flag*/
	get_bits(&gb, 5);  /* profile_idc*/
	get_bits(&gb, 32); /* profile_compatibility_flags*/
	get_bits(&gb, 48); /* constraint_flags (44~48bits)*/
	get_bits(&gb, 8);  /* level_idc	*/
	if (sps_max_sub_layers_minus1 > 0)
	{
		int sub_layer_profile_present_flag[8] = {0};
		int sub_layer_level_present_flag[8] = {0};
		for (int i = 0; i < sps_max_sub_layers_minus1; i++)
		{
			sub_layer_profile_present_flag[i] = get_bits(&gb, 1);
			sub_layer_level_present_flag[i] = get_bits(&gb, 1);
		}
		if (sps_max_sub_layers_minus1 > 0)
		{
			for (int i = sps_max_sub_layers_minus1; i < 8; i++)
			{
				get_bits(&gb,2); /*reserved_zero_2bits*/
			}
		}
		for (int i = 0; i < sps_max_sub_layers_minus1; i++)
		{
			if (sub_layer_profile_present_flag[i])
			{
				get_bits(&gb,88); /*sub_layer_profile info*/
			}
			if (sub_layer_level_present_flag[i])
			{
				get_bits(&gb,8); /*sub_layer_level_idc*/
			}
		}
	}
	int sps_seq_parameter_set_id;
	get_ue_golomb(&gb, (unsigned *)&sps_seq_parameter_set_id);
	int chroma_format_idc;
	get_ue_golomb(&gb, (unsigned *)&chroma_format_idc);
	if (chroma_format_idc == 3)
	{
		get_bits(&gb,1); /*separate_colour_plane_flag*/
	}
	int pic_width_in_luma_samples;
	get_ue_golomb(&gb, (unsigned *)&pic_width_in_luma_samples);
	int pic_height_in_luma_samples;
	get_ue_golomb(&gb, (unsigned *)&pic_height_in_luma_samples);
	int conformance_window_flag = get_bits(&gb,1);
	int conf_win_left_offset = 0, conf_win_right_offset = 0;
	int conf_win_top_offset = 0, conf_win_bottom_offset = 0;
	if (conformance_window_flag)
	{
		get_ue_golomb(&gb, (unsigned *)&conf_win_left_offset);
		get_ue_golomb(&gb, (unsigned *)&conf_win_right_offset);
		get_ue_golomb(&gb, (unsigned *)&conf_win_top_offset);
		get_ue_golomb(&gb, (unsigned *)&conf_win_bottom_offset);
	}
	int sub_width_c = (chroma_format_idc == 1
		|| chroma_format_idc == 2) ? 2 : 1;
	int sub_height_c = (chroma_format_idc == 1) ? 2 : 1;
	*width = pic_width_in_luma_samples -
	sub_width_c * (conf_win_right_offset +
		conf_win_left_offset);
	*height = pic_height_in_luma_samples -
	sub_height_c * (conf_win_top_offset +
		conf_win_bottom_offset);
fail:
	if (web)
		free(web);
	return ret;
}
