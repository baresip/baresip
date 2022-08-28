#include "errors.h"
#include <stdio.h>

const char *hi_errstr(int error) {
    switch (error) {
    case 0xA0028003:
        return "ERR_SYS_ILLEGAL_PARAM (0xA0028003): The parameter "
               "configuration is invalid";
    case 0xA0028006:
        return "ERR_SYS_NULL_PTR (0xA0028006): The pointer is null";
    case 0xA0028009:
        return "ERR_SYS_NOT_PERM (0xA0028009): The operation is forbidden";
    case 0xA0028010:
        return "ERR_SYS_NOTREADY (0xA0028010): The system control attributes "
               "are not configured";
    case 0xA0028012:
        return "ERR_SYS_BUSY (0xA0028012): The system is busy";
    case 0xA002800C:
        return "ERR_SYS_NOMEM (0xA002800C): The memory fails to be allocated "
               "due to some causes such as insufficient system memory";

    case 0xA0018003:
        return "ERR_VB_ILLEGAL_PARAM (0xA0018003): The parameter configuration "
               "is invalid";
    case 0xA0018005:
        return "ERR_VB_UNEXIST (0xA0018005): The VB pool does not exist";
    case 0xA0018006:
        return "ERR_VB_NULL_PTR (0xA0018006): The pointer is null";
    case 0xA0018009:
        return "ERR_VB_NOT_PERM (0xA0018009): The operation is forbidden";
    case 0xA001800C:
        return "ERR_VB_NOMEM (0xA001800C): The memory fails to be allocated";
    case 0xA001800D:
        return "ERR_VB_NOBUF (0xA001800D): The buffer fails to be allocated";
    case 0xA0018010:
        return "ERR_VB_NOTREADY (0xA0018010): The system control attributes "
               "are not configured";
    case 0xA0018012:
        return "ERR_VB_BUSY (0xA0018012): The system is busy";
    case 0xA0018040:
        return "ERR_VB_2MPOOLS (0xA0018040): Too many VB pools are created";

    case 0xA01C8006:
        return "ERR_ISP_NULL_PTR (0xA01C8006): The input pointer is null";
    case 0xA01C8003:
        return "ERR_ISP_ILLEGAL_PARAM (0xA01C8003): The input parameter is "
               "invalid";
    case 0xA01C8008:
        return "ERR_ISP_NOT_SUPPORT (0xA01C8008): This function is not "
               "supported by the ISP";
    case 0xA01C8043:
        return "ERR_ISP_SNS_UNREGISTER (0xA01C8043): The sensor is not "
               "registered";
    case 0xA01C8041:
        return "ERR_ISP_MEM_NOT_INIT (0xA01C8041): The external registers are "
               "not initialized";
    case 0xA01C8040:
        return "ERR_ISP_NOT_INIT (0xA01C8040): The ISP is not initialized";
    case 0xA01C8044:
        return "ERR_ISP_INVALID_ADDR (0xA01C8044): The address is invalid";
    case 0xA01C8042:
        return "ERR_ISP_ATTR_NOT_CFG (0xA01C8042): The attribute is not "
               "configured";

    case 0xA0108001:
        return "ERR_VI_INVALID_DEVID (0xA0108001): The VI device ID is "
               "invalid";
    case 0xA0108002:
        return "ERR_VI_INVALID_CHNID (0xA0108002): The VI channel ID is "
               "invalid";
    case 0xA0108003:
        return "ERR_VI_INVALID_PARA (0xA0108003): The VI parameter is invalid";
    case 0xA0108006:
        return "ERR_VI_INVALID_NULL_PTR (0xA0108006): The pointer of the input "
               "parameter is null";
    case 0xA0108007:
        return "ERR_VI_FAILED_NOTCONFIG (0xA0108007): The attributes of the "
               "video device are not set";
    case 0xA0108008:
        return "ERR_VI_NOT_SUPPORT (0xA0108008): The operation is not "
               "supported";
    case 0xA0108009:
        return "ERR_VI_NOT_PERM (0xA0108009): The operation is forbidden";
    case 0xA010800C:
        return "ERR_VI_NOMEM (0xA010800C): The memory fails to be allocated";
    case 0xA010800E:
        return "ERR_VI_BUF_EMPTY (0xA010800E): The VI buffer is empty";
    case 0xA010800F:
        return "ERR_VI_BUF_FULL (0xA010800F): The VI buffer is full";
    case 0xA0108010:
        return "ERR_VI_SYS_NOTREADY (0xA0108010): The VI system is not "
               "initialized";
    case 0xA0108012:
        return "ERR_VI_BUSY (0xA0108012): The VI system is busy";
    case 0xA0108040:
        return "ERR_VI_FAILED_NOTENABLE (0xA0108040): The VI device or VI "
               "channel is not enabled";
    case 0xA0108041:
        return "ERR_VI_FAILED_NOTDISABLE (0xA0108041): The VI device or VI "
               "channel is not disabled";
    case 0xA0108042:
        return "ERR_VI_FAILED_CHNOTDISABLE (0xA0108042): The VI channel is not "
               "disabled";
    case 0xA0108043:
        return "ERR_VI_CFG_TIMEOUT (0xA0108043): The video attribute "
               "configuration times out";
    case 0xA0108044:
        return "ERR_VI_NORM_UNMATCH (0xA0108044): Mismatch occurs";
    case 0xA0108045:
        return "ERR_VI_INVALID_WAYID (0xA0108045): The video channel ID is "
               "invalid";
    case 0xA0108046:
        return "ERR_VI_INVALID_PHYCHNID (0xA0108046): The physical video "
               "channel ID is invalid";
    case 0xA0108047:
        return "ERR_VI_FAILED_NOTBIND (0xA0108047): The video channel is not "
               "bound";
    case 0xA0108048:
        return "ERR_VI_FAILED_BINDED (0xA0108048): The video channel is bound";
    case 0xA0108049:
        return "ERR_VI_DIS_PROCESS_FAIL (0xA0108049): The DIS fails to run";

    case 0xA00F8001:
        return "ERR_VO_INVALID_DEVID (0xA00F8001): The device ID does not fall "
               "within the value range";
    case 0xA00F8002:
        return "ERR_VO_INVALID_CHNID (0xA00F8002): The channel ID does not "
               "fall within the value range";
    case 0xA00F8003:
        return "ERR_VO_ILLEGAL_PARAM (0xA00F8003): The parameter value does "
               "not fall within the value range";
    case 0xA00F8006:
        return "ERR_VO_NULL_PTR (0xA00F8006): The pointer is null";
    case 0xA00F8008:
        return "ERR_VO_NOT_SUPPORT (0xA00F8008): The operation is not "
               "supported";
    case 0xA00F8009:
        return "ERR_VO_NOT_PERMIT (0xA00F8009): The operation is forbidden";
    case 0xA00F800C:
        return "ERR_VO_NO_MEM (0xA00F800C): The memory is insufficient";
    case 0xA00F8010:
        return "ERR_VO_SYS_NOTREADY (0xA00F8010): The system is not "
               "initialized";
    case 0xA00F8012:
        return "ERR_VO_BUSY (0xA00F8012): The resources are unavailable";
    case 0xA00F8040:
        return "ERR_VO_DEV_NOT_CONFIG (0xA00F8040): The VO device is not "
               "configured";
    case 0xA00F8041:
        return "ERR_VO_DEV_NOT_ENABLE (0xA00F8041): The VO device is not "
               "enabled";
    case 0xA00F8042:
        return "ERR_VO_DEV_HAS_ENABLED (0xA00F8042): The VO device has been "
               "enabled";
    case 0xA00F8043:
        return "ERR_VO_DEV_HAS_BINDED (0xA00F8043): The device has been bound";
    case 0xA00F8044:
        return "ERR_VO_DEV_NOT_BINDED (0xA00F8044): The device is not bound";
    case 0xA00F8045:
        return "ERR_VO_VIDEO_NOT_ENABLE (0xA00F8045): The video layer is not "
               "enabled";
    case 0xA00F8046:
        return "ERR_VO_VIDEO_NOT_DISABLE (0xA00F8046): The video layer is not "
               "disabled";
    case 0xA00F8047:
        return "ERR_VO_VIDEO_NOT_CONFIG (0xA00F8047): The video layer is not "
               "configured";
    case 0xA00F8048:
        return "ERR_VO_CHN_NOT_DISABLE (0xA00F8048): The VO channel is not "
               "disabled";
    case 0xA00F8049:
        return "ERR_VO_CHN_NOT_ENABLE (0xA00F8049): No VO channel is enabled";
    case 0xA00F804A:
        return "ERR_VO_CHN_NOT_CONFIG (0xA00F804A): The VO channel is not "
               "configured";
    case 0xA00F804B:
        return "ERR_VO_CHN_NOT_ALLOC (0xA00F804B): No VO channel is allocated";
    case 0xA00F804C:
        return "ERR_VO_INVALID_PATTERN (0xA00F804C): The pattern is invalid";
    case 0xA00F804D:
        return "ERR_VO_INVALID_POSITION (0xA00F804D): The cascade position is "
               "invalid";
    case 0xA00F804E:
        return "ERR_VO_WAIT_TIMEOUT (0xA00F804E): Waiting times out";
    case 0xA00F804F:
        return "ERR_VO_INVALID_VFRAME (0xA00F804F): The video frame is "
               "invalid";
    case 0xA00F8050:
        return "ERR_VO_INVALID_RECT_PARA (0xA00F8050): The rectangle parameter "
               "is invalid";
    case 0xA00F8051:
        return "ERR_VO_SETBEGIN_ALREADY (0xA00F8051): The SETBEGIN MPI has "
               "been configured";
    case 0xA00F8052:
        return "ERR_VO_SETBEGIN_NOTYET (0xA00F8052): The SETBEGIN MPI is not "
               "configured";
    case 0xA00F8053:
        return "ERR_VO_SETEND_ALREADY (0xA00F8053): The SETEND MPI has been "
               "configured";
    case 0xA00F8054:
        return "ERR_VO_SETEND_NOTYET (0xA00F8054): The SETEND MPI is not "
               "configured";
    case 0xA00F8065:
        return "ERR_VO_GFX_NOT_DISABLE (0xA00F8065): The graphics layer is not "
               "disabled";
    case 0xA00F8066:
        return "ERR_VO_GFX_NOT_BIND (0xA00F8066): The graphics layer is not "
               "bound";
    case 0xA00F8067:
        return "ERR_VO_GFX_NOT_UNBIND (0xA00F8067): The graphics layer is not "
               "unbound";
    case 0xA00F8068:
        return "ERR_VO_GFX_INVALID_ID (0xA00F8068): The graphics layer ID does "
               "not fall within the value range";
    case 0xA00F806b:
        return "ERR_VO_CHN_AREA_OVERLAP (0xA00F806b): The VO channel areas "
               "overlap";
    case 0xA00F806d:
        return "ERR_VO_INVALID_LAYERID (0xA00F806d): The video layer ID does "
               "not fall within the value range";
    case 0xA00F806e:
        return "ERR_VO_VIDEO_HAS_BINDED (0xA00F806e): The video layer has been "
               "bound";
    case 0xA00F806f:
        return "ERR_VO_VIDEO_NOT_BINDED (0xA00F806f): The video layer is not "
               "bound";

    case 0xA0078001:
        return "ERR_VPSS_INVALID_DEVID (0xA0078001): The VPSS group ID is "
               "invalid";
    case 0xA0078002:
        return "ERR_VPSS_INVALID_CHNID (0xA0078002): The VPSS channel ID is "
               "invalid";
    case 0xA0078003:
        return "ERR_VPSS_ILLEGAL_PARAM (0xA0078003): The VPSS parameter is "
               "invalid";
    case 0xA0078004:
        return "ERR_VPSS_EXIST (0xA0078004): A VPSS group is created";
    case 0xA0078005:
        return "ERR_VPSS_UNEXIST (0xA0078005): No VPSS group is created";
    case 0xA0078006:
        return "ERR_VPSS_NULL_PTR (0xA0078006): The pointer of the input "
               "parameter is null";
    case 0xA0078008:
        return "ERR_VPSS_NOT_SUPPORT (0xA0078008): The operation is not "
               "supported";
    case 0xA0078009:
        return "ERR_VPSS_NOT_PERM (0xA0078009): The operation is forbidden";
    case 0xA007800C:
        return "ERR_VPSS_NOMEM (0xA007800C): The memory fails to be allocated";
    case 0xA007800D:
        return "ERR_VPSS_NOBUF (0xA007800D): The buffer pool fails to be "
               "allocated";
    case 0xA007800E:
        return "ERR_VPSS_BUF_EMPTY (0xA007800E): The picture queue is empty";
    case 0xA0078010:
        return "ERR_VPSS_NOTREADY (0xA0078010): The VPSS is not initialized";
    case 0xA0078012:
        return "ERR_VPSS_BUSY (0xA0078012): The VPSS is busy";

    case 0xA0088002:
        return "ERR_VENC_INVALID_CHNID (0xA0088002): The channel ID is "
               "invalid";
    case 0xA0088003:
        return "ERR_VENC_ILLEGAL_PARAM (0xA0088003): The parameter is invalid";
    case 0xA0088004:
        return "ERR_VENC_EXIST (0xA0088004): The device, channel or resource "
               "to be created or applied for exists";
    case 0xA0088005:
        return "ERR_VENC_UNEXIST (0xA0088005): The device, channel or resource "
               "to be used or destroyed does not exist";
    case 0xA0088006:
        return "ERR_VENC_NULL_PTR (0xA0088006): The parameter pointer is null";
    case 0xA0088007:
        return "ERR_VENC_NOT_CONFIG (0xA0088007): No parameter is set before "
               "use";
    case 0xA0088008:
        return "ERR_VENC_NOT_SUPPORT (0xA0088008): The parameter or function "
               "is not supported";
    case 0xA0088009:
        return "ERR_VENC_NOT_PERM (0xA0088009): The operation, for example, "
               "modifying static parameters, is forbidden";
    case 0xA008800C:
        return "ERR_VENC_NOMEM (0xA008800C): The memory fails to be allocated "
               "due to some causes such as insufficient system memory";
    case 0xA008800D:
        return "ERR_VENC_NOBUF (0xA008800D): The buffer fails to be allocated "
               "due to some causes such as oversize of the data buffer applied "
               "for";
    case 0xA008800E:
        return "ERR_VENC_BUF_EMPTY (0xA008800E): The buffer is empty";
    case 0xA008800F:
        return "ERR_VENC_BUF_FULL (0xA008800F): The buffer is full";
    case 0xA0088010:
        return "ERR_VENC_SYS_NOTREADY (0xA0088010): The system is not "
               "initialized or the corresponding module is not loaded";
    case 0xA0088012:
        return "ERR_VENC_BUSY (0xA0088012): The VENC system is busy";

    case 0xA0098001:
        return "ERR_VDA_INVALID_DEVID (0xA0098001): The device ID exceeds the "
               "valid range";
    case 0xA0098002:
        return "ERR_VDA_INVALID_CHNID (0xA0098002): The channel ID exceeds the "
               "valid range";
    case 0xA0098003:
        return "ERR_VDA_ILLEGAL_PARAM (0xA0098003): The parameter value "
               "exceeds its valid range";
    case 0xA0098004:
        return "ERR_VDA_EXIST (0xA0098004): The device, channel, or resource "
               "to be created or applied for already exists";
    case 0xA0098005:
        return "ERR_VDA_UNEXIST (0xA0098005): The device, channel, or resource "
               "to be used or destroyed does not exist";
    case 0xA0098006:
        return "ERR_VDA_NULL_PTR (0xA0098006): The pointer is null";
    case 0xA0098007:
        return "ERR_VDA_NOT_CONFIG (0xA0098007): The system or VDA channel is "
               "not configured";
    case 0xA0098008:
        return "ERR_VDA_NOT_SUPPORT (0xA0098008): The parameter or function is "
               "not supported";
    case 0xA0098009:
        return "ERR_VDA_NOT_PERM (0xA0098009): The operation, for example, "
               "attempting to modify the value of a static parameter, is "
               "forbidden";
    case 0xA009800C:
        return "ERR_VDA_NOMEM (0xA009800C): The memory fails to be allocated "
               "due to some causes such as insufficient system memory";
    case 0xA009800D:
        return "ERR_VDA_NOBUF (0xA009800D): The buffer fails to be allocated "
               "due to some causes such as oversize of the data buffer applied "
               "for";
    case 0xA009800E:
        return "ERR_VDA_BUF_EMPTY (0xA009800E): The buffer is empty";
    case 0xA009800F:
        return "ERR_VDA_BUF_FULL (0xA009800F): The buffer is full";
    case 0xA0098010:
        return "ERR_VDA_SYS_NOTREADY (0xA0098010): The system is not "
               "initialized or the corresponding module is not loaded";
    case 0xA0098012:
        return "ERR_VDA_BUSY (0xA0098012): The system is busy";

    case 0xA0038001:
        return "ERR_RGN_INVALID_DEVID (0xA0038001): The device ID exceeds the "
               "valid range";
    case 0xA0038002:
        return "ERR_RGN_INVALID_CHNID (0xA0038002): The channel ID is "
               "incorrect or the region handle is invalid";
    case 0xA0038003:
        return "ERR_RGN_ILLEGAL_PARAM (0xA0038003): The parameter value "
               "exceeds its valid range";
    case 0xA0038004:
        return "ERR_RGN_EXIST (0xA0038004): The device, channel, or resource "
               "to be created already exists";
    case 0xA0038005:
        return "ERR_RGN_UNEXIST (0xA0038005): The device, channel, or resource "
               "to be used or destroyed does not exist";
    case 0xA0038006:
        return "ERR_RGN_NULL_PTR (0xA0038006): The pointer is null";
    case 0xA0038007:
        return "ERR_RGN_NOT_CONFIG (0xA0038007): The module is not configured";
    case 0xA0038008:
        return "ERR_RGN_NOT_SUPPORT (0xA0038008): The parameter or function is "
               "not supported";
    case 0xA0038009:
        return "ERR_RGN_NOT_PERM (0xA0038009): The operation, for example, "
               "attempting to modify the value of a static parameter, is "
               "forbidden";
    case 0xA003800C:
        return "ERR_RGN_NOMEM (0xA003800C): The memory fails to be allocated "
               "due to some causes such as insufficient system memory";
    case 0xA003800D:
        return "ERR_RGN_NOBUF (0xA003800D): The buffer fails to be allocated "
               "due to some causes such as oversize of the data buffer applied "
               "for";
    case 0xA003800E:
        return "ERR_RGN_BUF_EMPTY (0xA003800E): The buffer is empty";
    case 0xA003800F:
        return "ERR_RGN_BUF_FULL (0xA003800F): The buffer is full";
    case 0xA0038010:
        return "ERR_RGN_NOTREADY (0xA0038010): The system is not initialized "
               "or the corresponding module is not loaded";
    case 0xA0038011:
        return "ERR_RGN_BADADDR (0xA0038011): The address is invalid";
    case 0xA0038012:
        return "ERR_RGN_BUSY (0xA0038012): The system is busy";

    case 0xA0158001:
        return "ERR_AI_INVALID_DEVID (0xA0158001): The AI device ID is "
               "invalid";
    case 0xA0158002:
        return "ERR_AI_INVALID_CHNID (0xA0158002): The AI channel ID is "
               "invalid";
    case 0xA0158003:
        return "ERR_AI_ILLEGAL_PARAM (0xA0158003): The settings of the AI "
               "parameters are invalid";
    case 0xA0158005:
        return "ERR_AI_NOT_ENABLED (0xA0158005): The AI device or AI channel "
               "is not enabled";
    case 0xA0158006:
        return "ERR_AI_NULL_PTR (0xA0158006): The input parameter pointer is "
               "null";
    case 0xA0158007:
        return "ERR_AI_NOT_CONFIG (0xA0158007): The attributes of an AI device "
               "are not set";
    case 0xA0158008:
        return "ERR_AI_NOT_SUPPORT (0xA0158008): The operation is not "
               "supported";
    case 0xA0158009:
        return "ERR_AI_NOT_PERM (0xA0158009): The operation is forbidden";
    case 0xA015800C:
        return "ERR_AI_NOMEM (0xA015800C): The memory fails to be allocated";
    case 0xA015800D:
        return "ERR_AI_NOBUF (0xA015800D): The AI buffer is insufficient";
    case 0xA015800E:
        return "ERR_AI_BUF_EMPTY (0xA015800E): The AI buffer is empty";
    case 0xA015800F:
        return "ERR_AI_BUF_FULL (0xA015800F): The AI buffer is full";
    case 0xA0158010:
        return "ERR_AI_SYS_NOTREADY (0xA0158010): The AI system is not "
               "initialized";
    case 0xA0158012:
        return "ERR_AI_BUSY (0xA0158012): The AI system is busy";
    case 0xA0158041:
        return "ERR_AI_VQE_ERR (0xA0158041): A VQE processing error occurs in "
               "the AI channel";

    case 0xA0168001:
        return "ERR_AO_INVALID_DEVID (0xA0168001): The AO device ID is "
               "invalid";
    case 0xA0168002:
        return "ERR_AO_INVALID_CHNID (0xA0168002): The AO channel ID is "
               "invalid";
    case 0xA0168003:
        return "ERR_AO_ILLEGAL_PARAM (0xA0168003): The settings of the AO "
               "parameters are invalid";
    case 0xA0168005:
        return "ERR_AO_NOT_ENABLED (0xA0168005): The AO device or AO channel "
               "is not enabled";
    case 0xA0168006:
        return "ERR_AO_NULL_PTR (0xA0168006): The output parameter pointer is "
               "null";
    case 0xA0168007:
        return "ERR_AO_NOT_CONFIG (0xA0168007): The attributes of an AO device "
               "are not set";
    case 0xA0168008:
        return "ERR_AO_NOT_SUPPORT (0xA0168008): The operation is not "
               "supported";
    case 0xA0168009:
        return "ERR_AO_NOT_PERM (0xA0168009): The operation is forbidden";
    case 0xA016800C:
        return "ERR_AO_NOMEM (0xA016800C): The system memory is insufficient";
    case 0xA016800D:
        return "ERR_AO_NOBUF (0xA016800D): The AO buffer is insufficient";
    case 0xA016800E:
        return "ERR_AO_BUF_EMPTY (0xA016800E): The AO buffer is empty";
    case 0xA016800F:
        return "ERR_AO_BUF_FULL (0xA016800F): The AO buffer is full";
    case 0xA0168010:
        return "ERR_AO_SYS_NOTREADY (0xA0168010): The AO system is not "
               "initialized";
    case 0xA0168012:
        return "ERR_AO_BUSY (0xA0168012): The AO system is busy";
    case 0xA0168041:
        return "ERR_AO_VQE_ERR (0xA0168041): A VQE processing error occurs in "
               "the AO channel";

    case 0xA0178001:
        return "ERR_AENC_INVALID_DEVID (0xA0178001): The AENC device ID is "
               "invalid";
    case 0xA0178002:
        return "ERR_AENC_INVALID_CHNID (0xA0178002): The AENC channel ID is "
               "invalid";
    case 0xA0178003:
        return "ERR_AENC_ILLEGAL_PARAM (0xA0178003): The settings of the AENC "
               "parameters are invalid";
    case 0xA0178004:
        return "ERR_AENC_EXIST (0xA0178004): An AENC channel is created";
    case 0xA0178005:
        return "ERR_AENC_UNEXIST (0xA0178005): An AENC channel is not created";
    case 0xA0178006:
        return "ERR_AENC_NULL_PTR (0xA0178006): The input parameter pointer is "
               "null";
    case 0xA0178007:
        return "ERR_AENC_NOT_CONFIG (0xA0178007): The AENC channel is not "
               "configured";
    case 0xA0178008:
        return "ERR_AENC_NOT_SUPPORT (0xA0178008): The operation is not "
               "supported";
    case 0xA0178009:
        return "ERR_AENC_NOT_PERM (0xA0178009): The operation is forbidden";
    case 0xA017800C:
        return "ERR_AENC_NOMEM (0xA017800C): The system memory is "
               "insufficient";
    case 0xA017800D:
        return "ERR_AENC_NOBUF (0xA017800D): The buffer for AENC channels "
               "fails to be allocated";
    case 0xA017800E:
        return "ERR_AENC_BUF_EMPTY (0xA017800E): The AENC channel buffer is "
               "empty";
    case 0xA017800F:
        return "ERR_AENC_BUF_FULL (0xA017800F): The AENC channel buffer is "
               "full";
    case 0xA0178010:
        return "ERR_AENC_SYS_NOTREADY (0xA0178010): The system is not "
               "initialized";
    case 0xA0178040:
        return "ERR_AENC_ENCODER_ERR (0xA0178040): An AENC data error occurs";
    case 0xA0178041:
        return "ERR_AENC_VQE_ERR (0xA0178041): A VQE processing error occurs "
               "in the AENC channel";

    case 0xA0188001:
        return "ERR_ADEC_INVALID_DEVID (0xA0188001): The ADEC device is "
               "invalid";
    case 0xA0188002:
        return "ERR_ADEC_INVALID_CHNID (0xA0188002): The ADEC channel ID is "
               "invalid";
    case 0xA0188003:
        return "ERR_ADEC_ILLEGAL_PARAM (0xA0188003): The settings of the ADEC "
               "parameters are invalid";
    case 0xA0188004:
        return "ERR_ADEC_EXIST (0xA0188004): An ADEC channel is created";
    case 0xA0188005:
        return "ERR_ADEC_UNEXIST (0xA0188005): An ADEC channel is not created";
    case 0xA0188006:
        return "ERR_ADEC_NULL_PTR (0xA0188006): The input parameter pointer is "
               "null";
    case 0xA0188007:
        return "ERR_ADEC_NOT_CONFIG (0xA0188007): The attributes of an ADEC "
               "channel are not set";
    case 0xA0188008:
        return "ERR_ADEC_NOT_SUPPORT (0xA0188008): The operation is not "
               "supported";
    case 0xA0188009:
        return "ERR_ADEC_NOT_PERM (0xA0188009): The operation is forbidden";
    case 0xA018800C:
        return "ERR_ADEC_NOMEM (0xA018800C): The system memory is "
               "insufficient";
    case 0xA018800D:
        return "ERR_ADEC_NOBUF (0xA018800D): The buffer for ADEC channels "
               "fails to be allocated";
    case 0xA018800E:
        return "ERR_ADEC_BUF_EMPTY (0xA018800E): The ADEC channel buffer is "
               "empty";
    case 0xA018800F:
        return "ERR_ADEC_BUF_FULL (0xA018800F): The ADEC channel buffer is "
               "full";
    case 0xA0188010:
        return "ERR_ADEC_SYS_NOTREADY (0xA0188010): The system is not "
               "initialized";
    case 0xA0188040:
        return "ERR_ADEC_DECODER_ERR (0xA0188040): An ADEC data error occurs";
    case 0xA0188041:
        return "ERR_ADEC_BUF_LACK (0xA0188041): An insufficient buffer occurs "
               "in the ADEC channel";

    case 0xA02D800E:
        return "ERR_VGS_BUF_EMPTY (0xA02D800E): The VGS jobs, tasks, or nodes "
               "are used up";
    case 0xA02D8003:
        return "ERR_VGS_ILLEGAL_PARAM (0xA02D8003): The VGS parameter value is "
               "invalid";
    case 0xA02D8006:
        return "ERR_VGS_NULL_PTR (0xA02D8006): The input parameter pointer is "
               "null";
    case 0xA02D8008:
        return "ERR_VGS_NOT_SUPPORT (0xA02D8008): The operation is not "
               "supported";
    case 0xA02D8009:
        return "ERR_VGS_NOT_PERMITTED (0xA02D8009): The operation is "
               "forbidden";
    case 0xA02D800D:
        return "ERR_VGS_NOBUF (0xA02D800D): The memory fails to be allocated";
    case 0xA02D8010:
        return "ERR_VGS_SYS_NOTREADY (0xA02D8010): The system is not "
               "initialized";

    case 0xA033800D:
        return "ERR_FISHEYE_NOBUF (0xA033800D): The memory fails to be "
               "allocated";
    case 0xA033800E:
        return "ERR_FISHEYE_BUF_EMPTY (0xA033800E): The jobs, tasks, or nodes "
               "of the fisheye subsystem are used up";
    case 0xA0338006:
        return "ERR_FISHEYE_NULL_PTR (0xA0338006): The pointer of the input "
               "parameter is null";
    case 0xA0338003:
        return "ERR_FISHEYE_ILLEGAL_PARAM (0xA0338003): The configuration of "
               "fisheye parameters is invalid";
    case 0xA0338010:
        return "ERR_FISHEYE_SYS_NOTREADY (0xA0338010): The system is not "
               "initialized";
    case 0xA0338008:
        return "ERR_FISHEYE_NOT_SUPPORT (0xA0338008): This operation is not "
               "supported";
    case 0xA0338009:
        return "ERR_FISHEYE_NOT_PERMITTED (0xA0338009): This operation is not "
               "allowed";

    case 0x804D0001:
        return "ERR_CIPHER_NOT_INIT (0x804D0001): The cipher device is not "
               "initialized";
    case 0x804D0002:
        return "ERR_CIPHER_INVALID_HANDLE (0x804D0002): The handle ID is "
               "invalid";
    case 0x804D0003:
        return "ERR_CIPHER_INVALID_POINT (0x804D0003): The pointer is null";
    case 0x804D0004:
        return "ERR_CIPHER_INVALID_PARA (0x804D0004): The parameter is "
               "invalid";
    case 0x804D0005:
        return "ERR_CIPHER_FAILED_INIT (0x804D0005): The cipher module fails "
               "to be initialized";
    case 0x804D0006:
        return "ERR_CIPHER_FAILED_GETHANDLE (0x804D0006): The handle fails to "
               "be obtained";

    case 0xA0648001:
        return "ERR_TDE_DEV_NOT_OPEN (0xA0648001): The TDE device is not "
               "started";
    case 0xA0648002:
        return "ERR_TDE_DEV_OPEN_FAILED (0xA0648002): The TDE device fails to "
               "be started";
    case 0xA0648003:
        return "ERR_TDE_NULL_PTR (0xA0648003): The pointer of the input "
               "parameter is null";
    case 0xA0648004:
        return "ERR_TDE_NO_MEM (0xA0648004): The memory fails to be allocated";
    case 0xA0648005:
        return "ERR_TDE_INVALID_HANDLE (0xA0648005): The task handle is "
               "invalid";
    case 0xA0648006:
        return "ERR_TDE_INVALID_PARA (0xA0648006): The input parameter is "
               "invalid";
    case 0xA0648007:
        return "ERR_TDE_NOT_ALIGNED (0xA0648007): The position, width, height, "
               "or stride of the picture is not aligned as required";
    case 0xA0648008:
        return "ERR_TDE_MINIFICATION (0xA0648008): The multiple of down "
               "scaling exceeds the limitation (the maximum value is 255)";
    case 0xA0648009:
        return "ERR_TDE_CLIP_AREA (0xA0648009): The operation area does not "
               "overlap the clipped area";
    case 0xA064800A:
        return "ERR_TDE_JOB_TIMEOUT (0xA064800A): Waiting times out";
    case 0xA064800B:
        return "ERR_TDE_UNSUPPORTED_OPERATION (0xA064800B): The operation is "
               "not supported";
    case 0xA064800C:
        return "ERR_TDE_QUERY_TIMEOUT (0xA064800C): The specific task is not "
               "complete due to timeout";
    case 0xA064800E:
        return "ERR_TDE_INTERRUPT (0xA064800E): Waiting for task completion is "
               "interrupted";

    case 0xA01D8001:
        return "HI_ERR_IVE_INVALID_DEVID (0xA01D8001): The device ID is "
               "invalid";
    case 0xA01D8002:
        return "HI_ERR_IVE_INVALID_CHNID (0xA01D8002): The channel group ID or "
               "the region handle is invalid";
    case 0xA01D8003:
        return "HI_ERR_IVE_ILLEGAL_PARAM (0xA01D8003): The parameter is "
               "invalid";
    case 0xA01D8004:
        return "HI_ERR_IVE_EXIST (0xA01D8004): The device, channel, or "
               "resource to be created already exists";
    case 0xA01D8005:
        return "HI_ERR_IVE_UNEXIST (0xA01D8005): The device, channel, or "
               "resource to be used or destroyed does not exist";
    case 0xA01D8006:
        return "HI_ERR_IVE_NULL_PTR (0xA01D8006): The pointer is null";
    case 0xA01D8007:
        return "HI_ERR_IVE_NOT_CONFIG (0xA01D8007): The module is not "
               "configured";
    case 0xA01D8008:
        return "HI_ERR_IVE_NOT_SUPPORT (0xA01D8008): The parameter or function "
               "is not supported";
    case 0xA01D8009:
        return "HI_ERR_IVE_NOT_PERM (0xA01D8009): The operation, for example, "
               "modifying the value of a static parameter, is forbidden";
    case 0xA01D800C:
        return "HI_ERR_IVE_NOMEM (0xA01D800C): The memory fails to be "
               "allocated for the reasons such as system memory insufficiency";
    case 0xA01D800D:
        return "HI_ERR_IVE_NOBUF (0xA01D800D): The buffer fails to be "
               "allocated. The reason may be that the requested picture buffer "
               "is too large";
    case 0xA01D800E:
        return "HI_ERR_IVE_BUF_EMPTY (0xA01D800E): There is no picture in the "
               "buffer";
    case 0xA01D800F:
        return "HI_ERR_IVE_BUF_FULL (0xA01D800F): The buffer is full of "
               "pictures";
    case 0xA01D8010:
        return "HI_ERR_IVE_NOTREADY (0xA01D8010): The system is not "
               "initialized or the corresponding module driver is not loaded";
    case 0xA01D8011:
        return "HI_ERR_IVE_BADADDR (0xA01D8011): The address is invalid";
    case 0xA01D8012:
        return "HI_ERR_IVE_BUSY (0xA01D8012): The system is busy";
    case 0xA01D8040:
        return "HI_ERR_IVE_SYS_TIMEOUT (0xA01D8040): The IVE times out";
    case 0xA01D8041:
        return "HI_ERR_IVE_QUERY_TIMEOUT (0xA01D8041): The query times out";
    case 0xA01D8042:
        return "HI_ERR_IVE_OPEN_FILE (0xA01D8042): Opening a file fails";
    case 0xA01D8043:
        return "HI_ERR_IVE_READ_FILE (0xA01D8043): Reading a file fails";
    case 0xA01D8044:
        return "HI_ERR_IVE_WRITE_FILE (0xA01D8044): Writing to a file fails";
    case 0xA0308002:
        return "HI_ERR_ODT_INVALID_CHNID (0xA0308002): The on-die termination "
               "(ODT) channel group ID or the region handle is invalid";
    case 0xA0308004:
        return "HI_ERR_ODT_EXIST (0xA0308004): The device, channel, or "
               "resource to be created already exists";
    case 0xA0308005:
        return "HI_ERR_ODT_UNEXIST (0xA0308005): The device, channel, or "
               "resource to be used or destroyed does not exist";
    case 0xA0308009:
        return "HI_ERR_ODT_NOT_PERM (0xA0308009): The operation, for example, "
               "modifying the value of a static parameter, is forbidden";
    case 0xA0308010:
        return "HI_ERR_ODT_NOTREADY (0xA0308010): The ODT is not initialized";
    case 0xA0308012:
        return "HI_ERR_ODT_BUSY (0xA0308012): The ODT is busy";

    default: {
        static char err_buf[64];
        int len = sprintf(err_buf, "Unknown error code (%X).", error);
        err_buf[len + 1] = 0;
        return err_buf;
    }
    }
}
