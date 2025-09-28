#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "h2645_util.h"
#include "bs.h"

int h265_get_vps_sps_pps(uint8_t *data, int len, uint8_t *vps, int *vps_len, uint8_t *sps, int *sps_len, uint8_t *pps, int *pps_len)
{
    uint8_t nalu_t;
    int nalu_len;
    uint8_t *r, *end = data + len;
    *vps_len = 0, *sps_len = 0;
    *pps_len = 0;

    r = avc_find_startcode(data, end);

    while (r < end)
    {
        uint8_t *r1;

        while (!*(r++))
            ;
        r1 = avc_find_startcode(r, end);
        nalu_t = (r[0] >> 1) & 0x3F;
        nalu_len = (int)(r1 - r);

        if (nalu_t == 32)
        {
            memcpy(vps, r, nalu_len);
            *vps_len = nalu_len;
        }
        else if (nalu_t == 33)
        {
            memcpy(sps, r, nalu_len);
            *sps_len = nalu_len;
        }
        else if (nalu_t == 34)
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

int h265_get_vps_sps_pps_sei(uint8_t *data, int len, uint8_t *vps, int *vps_len, uint8_t *sps, int *sps_len, uint8_t *pps, int *pps_len, uint8_t *sei, int *sei_len)
{
    uint8_t nalu_t;
    int nalu_len;
    uint8_t *r, *end = data + len;
    *vps_len = 0, *sps_len = 0;
    *pps_len = 0;

    r = avc_find_startcode(data, end);

    while (r < end)
    {
        uint8_t *r1;

        while (!*(r++))
            ;
        r1 = avc_find_startcode(r, end);
        nalu_t = (r[0] >> 1) & 0x3F;
        nalu_len = (int)(r1 - r);

        if (nalu_t == 32)
        {
            memcpy(vps, r, nalu_len);
            *vps_len = nalu_len;
        }
        else if (nalu_t == 33)
        {
            memcpy(sps, r, nalu_len);
            *sps_len = nalu_len;
        }
        else if (nalu_t == 34)
        {
            memcpy(pps, r, nalu_len);
            *pps_len = nalu_len;
        }
        else if (nalu_t == 35)
        {
            memcpy(sei, r, nalu_len);
            *sei_len = nalu_len;
        }

        if (*vps_len > 0 && *sps_len > 0 && *pps_len > 0 && *sei_len > 0)
            break;

        r = r1;
    }

    return (*vps_len > 0 && *sps_len > 0 && *pps_len > 0 && *sei_len > 0) ? 0 : -1;
}

void print_profile_tier_level(uint8_t *buf, int *StartBit, int maxNumSubLayersMinus1)
{
    u(2, buf, StartBit); // general_profile_space
    u(1, buf, StartBit); // general_tier_flag
    u(5, buf, StartBit); // general_profile_idc
    for (int j = 0; j < 32; j++)
    {
        u(1, buf, StartBit); // general_profile_compatibility_flag[j]
    }
    u(1, buf, StartBit);  // general_progressive_source_flag
    u(1, buf, StartBit);  // general_interlaced_source_flag
    u(1, buf, StartBit);  // general_non_packed_constraint_flag
    u(1, buf, StartBit);  // general_frame_only_constraint_flag
    u(44, buf, StartBit); // skipping 44 reserved bits
    u(8, buf, StartBit);  // general_level_idc
    int sub_layer_profile_present_flag[8];
    int sub_layer_level_present_flag[8];
    for (int i = 0; i < maxNumSubLayersMinus1; i++)
    {
        sub_layer_profile_present_flag[i] = u(1, buf, StartBit);
        sub_layer_level_present_flag[i] = u(1, buf, StartBit);
    }

    if (maxNumSubLayersMinus1 > 0)
    {
        for (int i = maxNumSubLayersMinus1; i < 8; i++)
        {
            u(2, buf, StartBit); // reserved_zero_2bits[i]
        }
    }

    for (int i = 0; i < maxNumSubLayersMinus1; i++)
    {
        if (sub_layer_profile_present_flag[i])
        {
            u(2, buf, StartBit); // sub_layer_profile_space[i]
            u(1, buf, StartBit); // sub_layer_tier_flag[i]
            u(5, buf, StartBit); // sub_layer_profile_idc[i]
            for (int j = 0; j < 32; j++)
            {
                u(1, buf, StartBit); // sub_layer_profile_compatibility_flag[i][j]
                // fprintf(stdout, "\tsub_layer_profile_compatibility_flag[%d][%d]=%d\n", i, j, read_bit(pnal_buffer));
            }
            u(1, buf, StartBit);  // sub_layer_progressive_source_flag[i]
            u(1, buf, StartBit);  // sub_layer_interlaced_source_flag[i]
            u(1, buf, StartBit);  // sub_layer_non_packed_constraint_flag[i]
            u(1, buf, StartBit);  // sub_layer_frame_only_constraint_flag[i]
            u(44, buf, StartBit); // skipping 44 reserved bits
        }
        if (sub_layer_level_present_flag[i])
        {
            u(8, buf, StartBit); // sub_layer_level_idc[i]
        }
    }
}

int h265_decode_sps_with_width_and_height(uint8_t *buf, int nLen, int *width, int *height)
{
    de_emulation_prevention(buf, &nLen);
    uint8_t *nalData = buf;
    int len = nLen;
    int bitIndex = 0;
    int sps_video_parameter_set_id = u(4, nalData, &bitIndex);
    int sps_max_sub_layers_minus1 = u(3, nalData, &bitIndex);
    int sps_temporal_id_nesting_flag = u(1, nalData, &bitIndex);

    // -------- profile_tier_level --------
    // profile_space(2) + tier_flag(1) + profile_idc(5) + profile_compatibility_flag(32)
    // + progressive_source_flag(1) + interlaced_source_flag(1) + non_packed_constraint_flag(1) + frame_only_constraint_flag(1)
    // + reserved_zero_44bits + level_idc(8)
    u(2, nalData, &bitIndex); // profile_space
    u(1, nalData, &bitIndex); // tier_flag
    u(5, nalData, &bitIndex); // profile_idc
    u(32, nalData, &bitIndex); // profile_compatibility_flags
    u(48, nalData, &bitIndex); // constraint_flags (44~48bits)
    u(8, nalData, &bitIndex);  // level_idc

    if (sps_max_sub_layers_minus1 > 0) {
        // sub_layer_profile_present_flag[ i ] + sub_layer_level_present_flag[ i ]
        int sub_layer_profile_present_flag[8] = {0};
        int sub_layer_level_present_flag[8] = {0};
        for (int i = 0; i < sps_max_sub_layers_minus1; i++) {
            sub_layer_profile_present_flag[i] = u(1, nalData, &bitIndex);
            sub_layer_level_present_flag[i]   = u(1, nalData, &bitIndex);
        }
        if (sps_max_sub_layers_minus1 > 0) {
            for (int i = sps_max_sub_layers_minus1; i < 8; i++) {
                u(2, nalData, &bitIndex); // reserved_zero_2bits
            }
        }
        for (int i = 0; i < sps_max_sub_layers_minus1; i++) {
            if (sub_layer_profile_present_flag[i]) {
                u(88, nalData, &bitIndex); // sub_layer_profile info
            }
            if (sub_layer_level_present_flag[i]) {
                u(8, nalData, &bitIndex); // sub_layer_level_idc
            }
        }
    }

    int sps_seq_parameter_set_id = ue(nalData, len, &bitIndex);
    int chroma_format_idc = ue(nalData, len, &bitIndex);
    if (chroma_format_idc == 3) {
        u(1, nalData, &bitIndex); // separate_colour_plane_flag
    }

    int pic_width_in_luma_samples  = ue(nalData, len, &bitIndex);
    int pic_height_in_luma_samples = ue(nalData, len, &bitIndex);

    int conformance_window_flag = u(1, nalData, &bitIndex);
    int conf_win_left_offset = 0, conf_win_right_offset = 0;
    int conf_win_top_offset = 0, conf_win_bottom_offset = 0;
    if (conformance_window_flag) {
        conf_win_left_offset   = ue(nalData, len, &bitIndex);
        conf_win_right_offset  = ue(nalData, len, &bitIndex);
        conf_win_top_offset    = ue(nalData, len, &bitIndex);
        conf_win_bottom_offset = ue(nalData, len, &bitIndex);
    }

    int sub_width_c  = (chroma_format_idc == 1 || chroma_format_idc == 2) ? 2 : 1;
    int sub_height_c = (chroma_format_idc == 1) ? 2 : 1;

    *width  = pic_width_in_luma_samples  - sub_width_c  * (conf_win_right_offset + conf_win_left_offset);
    *height = pic_height_in_luma_samples - sub_height_c * (conf_win_top_offset   + conf_win_bottom_offset);

    return 0;
}