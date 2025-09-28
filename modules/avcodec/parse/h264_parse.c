#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "h2645_util.h"
#include "bs.h"
#include "h2645_parse.h"

// static void get_profile(int profile_idc, char* profile_str){
//     switch(profile_idc){
//         case 66:
//             strcpy(profile_str, "Baseline");
//             break;
//         case 77:
//             strcpy(profile_str, "Main");
//             break;
//         case 88:
//             strcpy(profile_str, "Extended");
//             break;
//         case 100:
//             strcpy(profile_str, "High(FRExt)");
//             break;
//         case 110:
//             strcpy(profile_str, "High10(FRExt)");
//             break;
//         case 122:
//             strcpy(profile_str, "High4:2:2(FRExt)");
//             break;
//         case 144:
//             strcpy(profile_str, "High4:4:4(FRExt)");
//             break;
//         default:
//             strcpy(profile_str, "Unknown");
//     }
// }

int h264_decode_sps_with_width_and_height(uint8_t *buf, int nLen, int *width, int *height)
{
    int StartBit = 0;
    de_emulation_prevention(buf, &nLen);

    u(1, buf, &StartBit);
    u(2, buf, &StartBit);
    int nal_unit_type = u(5, buf, &StartBit);
    if (nal_unit_type == 7)
    {
        int profile_idc = u(8, buf, &StartBit);
        u(1, buf, &StartBit);
        u(1, buf, &StartBit);
        u(1, buf, &StartBit);
        u(1, buf, &StartBit);
        u(4, buf, &StartBit);
        u(8, buf, &StartBit);

        ue(buf, nLen, &StartBit);

        if (profile_idc == 100 || profile_idc == 110 ||
            profile_idc == 122 || profile_idc == 144)
        {
            int chroma_format_idc = ue(buf, nLen, &StartBit);
            if (chroma_format_idc == 3)
                u(1, buf, &StartBit);
            ue(buf, nLen, &StartBit);
            ue(buf, nLen, &StartBit);
            u(1, buf, &StartBit);
            int seq_scaling_matrix_present_flag = u(1, buf, &StartBit);

            if (seq_scaling_matrix_present_flag)
            {
                for (int i = 0; i < 8; i++)
                {
                    u(1, buf, &StartBit);
                }
            }
        }
        ue(buf, nLen, &StartBit);
        int pic_order_cnt_type = ue(buf, nLen, &StartBit);
        if (pic_order_cnt_type == 0)
            ue(buf, nLen, &StartBit);
        else if (pic_order_cnt_type == 1)
        {
            u(1, buf, &StartBit);
            se(buf, nLen, &StartBit);
            se(buf, nLen, &StartBit);
            int num_ref_frames_in_pic_order_cnt_cycle = ue(buf, nLen, &StartBit);

            int *offset_for_ref_frame = (int *)malloc(num_ref_frames_in_pic_order_cnt_cycle * sizeof(int));
            for (int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++)
                offset_for_ref_frame[i] = se(buf, nLen, &StartBit);
            free(offset_for_ref_frame);
        }
        ue(buf, nLen, &StartBit);
        u(1, buf, &StartBit);
        int pic_width_in_mbs_minus1 = ue(buf, nLen, &StartBit);
        int pic_height_in_map_units_minus1 = ue(buf, nLen, &StartBit);

        *width = (pic_width_in_mbs_minus1 + 1) * 16;
        *height = (pic_height_in_map_units_minus1 + 1) * 16;
        // Get the width and height to terminate the parsing
        return 0;
    }
    else
    {
        return 1;
    }
}

int h264_get_sps_pps(uint8_t *data, int len, uint8_t *sps, int *sps_len, uint8_t *pps, int *pps_len)
{
    uint8_t nalu_t;
    int nalu_len;
    uint8_t *r, *end = data + len;
    *sps_len = 0;
    *pps_len = 0;

    r = avc_find_startcode(data, end);

    while (r < end)
    {
        uint8_t *r1;

        while (!*(r++))
            ;
        r1 = avc_find_startcode(r, end);
        nalu_t = r[0] & 0x1F;
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

int h264_get_sps_pps_sei(uint8_t *data, int len, uint8_t *sps, int *sps_len, uint8_t *pps, int *pps_len, uint8_t *sei, int *sei_len)
{
    uint8_t nalu_t;
    int nalu_len;
    uint8_t *r, *end = data + len;
    *sps_len = 0;
    *pps_len = 0;

    r = avc_find_startcode(data, end);

    while (r < end)
    {
        uint8_t *r1;

        while (!*(r++))
            ;
        r1 = avc_find_startcode(r, end);
        nalu_t = r[0] & 0x1F;
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
