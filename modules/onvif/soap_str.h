/* @file soap_str.h
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#ifndef _ONVIF_SOAP_STR_H_
#define _ONVIF_SOAP_STR_H_

/*SOAP.c Strings ------------------------------------------------------------*/
extern const char str_uri_schema[];
extern const char str_pf_schema[];
extern const char str_uri_device_wsdl[];
extern const char str_pf_device_wsdl[];
extern const char str_uri_media_wsdl[];
extern const char str_pf_media_wsdl[];
extern const char str_uri_events_wsdl[];
extern const char str_pf_events_wsdl[];
extern const char str_uri_error[];
extern const char str_pf_error[];
extern const char str_uri_network_wsdl[];
extern const char str_pf_network_wsdl[];
extern const char str_uri_ptz_wsdl[];
extern const char str_pf_ptz_wsdl[];
extern const char str_uri_topics[];
extern const char str_pf_topics[];
extern const char str_uri_deviceio_wsdl[];
extern const char str_pf_deviceio_wsdl[];
extern const char str_uri_wsdl[];
extern const char str_pf_wsdl[];
extern const char str_uri_wsdl_soap12[];
extern const char str_pf_wsdl_soap12[];
extern const char str_uri_wsdl_http[];
extern const char str_pf_wsdl_http[];
extern const char str_uri_encoding[];
extern const char str_pf_encoding[];
extern const char str_uri_envelope[];
extern const char str_pf_envelope[];
extern const char str_uri_xml_schema[];
extern const char str_pf_xml_schema[];
extern const char str_uri_xml_schema_instance[];
extern const char str_pf_xml_schema_instance[];
extern const char str_uri_discovery[];
extern const char str_pf_discovery[];
extern const char str_uri_xml_soap_addressing[];
extern const char str_pf_xml_soap_addressing[];
extern const char str_uri_addressing[];
extern const char str_pf_addressing[];
extern const char str_uri_wsn_t1[];
extern const char str_pf_wsn_t1[];
extern const char str_uri_wsn_b2[];
extern const char str_pf_wsn_b2[];
extern const char str_uri_xop_include[];
extern const char str_pf_xop_include[];

extern const char str_uri_topic_expression_concrete[];
extern const char str_uri_topic_expression_concrete_set[];
extern const char str_uri_item_filter[];

extern const char str_uri_passwd_type[];

extern const char str_http_ctype[];
extern const char str_xmlprolog[];
extern const char str_new_ns[];
extern const char str_envelope[];
extern const char str_header[];
extern const char str_body[];
extern const char str_fault[];
extern const char str_true[];
extern const char str_false[];
extern const char str_enabled[];
extern const char str_token[];
extern const char str_uctoken[];
extern const char str_info[];
extern const char str_name[];
extern const char str_config[];
extern const char str_height[];
extern const char str_width[];
extern const char str_ucheight[];
extern const char str_ucwidth[];
extern const char str_jpeg[];
extern const char str_h264[];
extern const char str_mpeg4[];
extern const char str_pcmu[];
extern const char str_ipv4[];
extern const char str_ipv6[];
extern const char str_port[];
extern const char str_ttl[];
extern const char str_http[];
extern const char str_https[];
extern const char str_rtsp [];
extern const char str_configurations[];
extern const char str_configuration[];
extern const char str_options[];
extern const char str_items[];
extern const char str_min[];
extern const char str_max[];
extern const char str_extension[];
extern const char str_totalnumb[];
extern const char str_uctype[];


/*WSD.c Strings -------------------------------------------------------------*/
extern const char str_wsd_action[];
extern const char str_wsd_action_url[];
extern const char str_wsd_messageid[];
extern const char str_wsd_relates_to[];
extern const char str_wsd_reply_to[];
extern const char str_wsd_addressing_role_anon[];
extern const char str_wsd_to[];
extern const char str_wsd_to_value[];
extern const char str_wsd_appsequence[];
extern const char str_wsd_instanceid[];
extern const char str_wsd_messagenumber[];
extern const char str_wsd_hello[];
extern const char str_wsd_bye[];
extern const char str_wsd_probe[];
extern const char str_wsd_probe_match[];
extern const char str_wsd_probe_matches[];
extern const char str_wsd_resolve[];
extern const char str_wsd_resolve_match[];
extern const char str_wsd_resolve_matches[];
extern const char str_wsd_endpointreference[];
extern const char str_wsd_address[];
extern const char str_wsd_types[];
extern const char str_wsd_scopes[];
extern const char str_wsd_xaddrs[];
extern const char str_wsd_meadataversion[];
extern const char str_wsd_discoverymode[];
extern const char str_wsd_discoverable[];
extern const char str_wsd_discoverableconf[];
extern const char str_wsd_nondiscoverable[];


/*
 * for the reponse create a new member in childs which states a response
 * child (bool)
 * if true ... encode it as <METHOD>Response
 */

/*METHOD STRINGS ------------------------------------------------------------*/
/* DEVICE.c */
extern const char str_method_get_scopes[];
extern const char str_method_get_scopes_r[];
extern const char str_method_set_scopes[];
extern const char str_method_set_scopes_r[];
extern const char str_method_add_scopes[];
extern const char str_method_add_scopes_r[];
extern const char str_method_remove_scopes[];
extern const char str_method_remove_scopes_r[];
extern const char str_method_get_device_info[];
extern const char str_method_get_device_info_r[];
extern const char str_method_get_services[];
extern const char str_method_get_services_r[];
extern const char str_method_get_service_cap[];
extern const char str_method_get_service_cap_r[];
extern const char str_method_get_netinterfaces[];
extern const char str_method_get_netinterfaces_r[];
extern const char str_method_get_capabilities[];
extern const char str_method_get_capabilities_r[];
extern const char str_method_get_systime[];
extern const char str_method_get_systime_r[];
extern const char str_method_get_ndg[];
extern const char str_method_get_ndg_r[];
extern const char str_method_get_nprotos[];
extern const char str_method_get_nprotos_r[];
extern const char str_method_get_wsdlurl[];
extern const char str_method_get_wsdlurl_r[];
extern const char str_method_get_hostname[];
extern const char str_method_get_hostname_r[];
extern const char str_method_get_users[];
extern const char str_method_get_users_r[];
extern const char str_method_systemreboot[];
extern const char str_method_systemreboot_r[];


// WSD.c
extern const char str_method_get_discoverymode[];
extern const char str_method_get_discoverymode_r[];
extern const char str_method_set_discoverymode[];
extern const char str_method_set_discoverymode_r[];

//MEDIA.c
extern const char str_method_get_profiles[];
extern const char str_method_get_profiles_r[];
extern const char str_method_get_profile[];
extern const char str_method_get_profile_r[];
extern const char str_method_get_vscs[];
extern const char str_method_get_vscs_r[];
extern const char str_method_get_vsc[];
extern const char str_method_get_vsc_r[];
extern const char str_method_get_vecs[];
extern const char str_method_get_vecs_r[];
extern const char str_method_get_vec[];
extern const char str_method_get_vec_r[];
extern const char str_method_get_ascs[];
extern const char str_method_get_ascs_r[];
extern const char str_method_get_asc[];
extern const char str_method_get_asc_r[];
extern const char str_method_get_aecs[];
extern const char str_method_get_aecs_r[];
extern const char str_method_get_aec[];
extern const char str_method_get_aec_r[];
extern const char str_method_get_aocs[];
extern const char str_method_get_aocs_r[];
extern const char str_method_get_aoc[];
extern const char str_method_get_aoc_r[];
extern const char str_method_get_adcs[];
extern const char str_method_get_adcs_r[];
extern const char str_method_get_adc[];
extern const char str_method_get_adc_r[];
extern const char str_method_get_suri[];
extern const char str_method_get_suri_r[];
extern const char str_method_get_videosources[];
extern const char str_method_get_videosources_r[];
extern const char str_method_get_audiosources[];
extern const char str_method_get_audiosources_r[];
extern const char str_method_get_aecos[];
extern const char str_method_get_aecos_r[];
extern const char str_method_get_vecos[];
extern const char str_method_get_vecos_r[];
extern const char str_method_get_adcos[];
extern const char str_method_get_adcos_r[];
extern const char str_method_get_mdconfigs[];
extern const char str_method_get_mdconfigs_r[];
extern const char str_method_create_profile[];
extern const char str_method_create_profile_r[];
extern const char str_method_delete_profile[];
extern const char str_method_delete_profile_r[];
extern const char str_method_add_vsc[];
extern const char str_method_add_vsc_r[];
extern const char str_method_add_asc[];
extern const char str_method_add_asc_r[];
extern const char str_method_add_vec[];
extern const char str_method_add_vec_r[];
extern const char str_method_add_aec[];
extern const char str_method_add_aec_r[];
extern const char str_method_add_aoc[];
extern const char str_method_add_aoc_r[];
extern const char str_method_add_adc[];
extern const char str_method_add_adc_r[];
extern const char str_method_remove_vsc[];
extern const char str_method_remove_vsc_r[];
extern const char str_method_remove_asc[];
extern const char str_method_remove_asc_r[];
extern const char str_method_remove_vec[];
extern const char str_method_remove_vec_r[];
extern const char str_method_remove_aec[];
extern const char str_method_remove_aec_r[];
extern const char str_method_remove_aoc[];
extern const char str_method_remove_aoc_r[];
extern const char str_method_remove_adc[];
extern const char str_method_remove_adc_r[];
extern const char str_method_get_audiooutputs[];
extern const char str_method_get_audiooutputs_r[];
extern const char str_method_get_aocos[];
extern const char str_method_get_aocos_r[];
extern const char str_method_get_cvec[];
extern const char str_method_get_cvec_r[];
extern const char str_method_get_caec[];
extern const char str_method_get_caec_r[];
extern const char str_method_get_ggnovei[];
extern const char str_method_get_ggnovei_r[];
extern const char str_method_get_cvsc[];
extern const char str_method_get_cvsc_r[];
extern const char str_method_get_casc[];
extern const char str_method_get_casc_r[];
extern const char str_method_get_caoc[];
extern const char str_method_get_caoc_r[];
extern const char str_method_get_cadc[];
extern const char str_method_get_cadc_r[];
extern const char str_method_get_vscos[];
extern const char str_method_get_vscos_r[];
extern const char str_method_set_videosource[];
extern const char str_method_set_videosource_r[];
extern const char str_method_set_audiosource[];
extern const char str_method_set_audiosource_r[];
extern const char str_method_set_videoecnoder[];
extern const char str_method_set_videoencoder_r[];
extern const char str_method_get_ascos[];
extern const char str_method_get_ascos_r[];
extern const char str_method_set_audioecnoder[];
extern const char str_method_set_audioencoder_r[];
extern const char str_method_set_audiooutput[];
extern const char str_method_set_audiooutput_r[];


/*PTZ.c Strings -------------------------------------------------------------*/
extern const char str_method_get_nodes[];
extern const char str_method_get_nodes_r[];
extern const char str_method_get_configurations[];
extern const char str_method_get_configurations_r[];


/*EVENT.c -------------------------------------------------------------------*/
// const char str_method_get_eventprop[];
// const char str_method_get_eventprop_r[];


/*SCOPE.c Strings -----------------------------------------------------------*/
extern const char str_scope_preemble[];
extern const char str_scope_scopes[];
extern const char str_scope_scopedef[];
extern const char str_scope_scopeitem[];
extern const char str_scope_fixed[];
extern const char str_scope_configurable[];

extern const char str_scope_manufacturer[];
extern const char str_scope_profstreaming[];
extern const char str_scope_name[];
extern const char str_scope_hardware[];


/*TYPES Strings -------------------------------------------------------------*/
extern const char str_type_nvt[];
extern const char str_type_dev[];


/*DEVICE.c Strings ----------------------------------------------------------*/
extern const char str_device_uri[];
extern const char str_media1_uri[];
extern const char str_deviceio_uri[];
extern const char str_event_uri[];
extern const char str_ptz_uri[];

extern const char str_device_include_capability[];

// GetDeviceInformation - Strings
extern const char str_device_hardware[];
extern const char str_device_model[];
extern const char str_device_firmware[];
extern const char str_device_manufacturer[];
extern const char str_device_serialnumber[];

// GetCapablities - Strings
extern const char str_device_category[];
extern const char str_device_cat_device[];
extern const char str_device_cat_media[];
extern const char str_device_cat_events[];
extern const char str_device_cat_imaging[];
extern const char str_device_cat_analytics[];
extern const char str_device_cat_ptz[];
extern const char str_device_cat_io[];
extern const char str_device_cat_all[];

// GetServices - Strings
extern const char str_device_service[];
extern const char str_device_capabilities[];
extern const char str_device_namespace[];
extern const char str_device_xaddr[];
extern const char str_device_ver[];
extern const char str_device_major[];
extern const char str_device_minor[];
// device
extern const char str_device_network[];
extern const char str_device_netipfilter[];
extern const char str_device_netzconfig[];
extern const char str_device_netipv6[];
extern const char str_device_netdyndns[];
extern const char str_device_netdot11config[];
extern const char str_device_netdot1xconfigs[];
extern const char str_device_nethostnamefdhcp[];
extern const char str_device_netntp[];
extern const char str_device_netdhcp6[];
extern const char str_device_security[];
extern const char str_device_sectls10[];
extern const char str_device_sectls11[];
extern const char str_device_sectls12[];
extern const char str_device_seconboardkg [];
extern const char str_device_secaccesspolicyconfig[];
extern const char str_device_secdefaultaccesspolicy[];
extern const char str_device_secdot1x[];
extern const char str_device_secremoteuserhandling[];
extern const char str_device_secx509token[];
extern const char str_device_secsamltoken[];
extern const char str_device_seckerberostoken[];
extern const char str_device_secusertoken[];
extern const char str_device_sechttpdigest[];
extern const char str_device_secreltoken[];
extern const char str_device_secsupportedeapmethods[];
extern const char str_device_secsupportedeapmethod[];
extern const char str_device_secmaxusers[];
extern const char str_device_secmaxusernamelen[];
extern const char str_device_secmaxpasswdlen[];
extern const char str_device_system[];
extern const char str_device_sysdiscoveryresolve[];
extern const char str_device_sysdiscoverybye[];
extern const char str_device_sysremotediscovery[];
extern const char str_device_syssystembackup[];
extern const char str_device_syssystemlogging[];
extern const char str_device_sysfirmwareupgrae[];
extern const char str_device_syssupportedversion[];
extern const char str_device_syshttpfirmwareupgrade[];
extern const char str_device_syshttpsystembackup[];
extern const char str_device_syshttpsystemlogging[];
extern const char str_device_syshttpsupportinfo[];
extern const char str_device_sysstorageconfig[];
extern const char str_device_sysmaxstorageconfigs[];
extern const char str_device_sysgeolocationentries[];
extern const char str_device_sysautogeo[];
extern const char str_device_sysstoragetypssupported[];
extern const char str_device_misc[];
extern const char str_device_miscauxcommands[];
// media
extern const char str_device_med1snapshoturi[];
extern const char str_device_med1rotation[];
extern const char str_device_med1videosourcemode[];
extern const char str_device_med1osd[];
extern const char str_device_med1temporaryosdtext[];
extern const char str_device_med1exicompression[];
extern const char str_device_med2mask[];
extern const char str_device_med2sourcemask[];
extern const char str_device_med1profcap[];
extern const char str_device_med1maxnumberofprofile[];
extern const char str_device_med2conigurationsupported[];
extern const char str_device_med1streamcap[];
extern const char str_device_med1rtpmcast[];
extern const char str_device_med1rtptcp[];
extern const char str_device_med1rtprtsptcp[];
extern const char str_device_med1nonaggregatecontrol[];
extern const char str_device_med1nortspstreaming[];
extern const char str_device_med2rtspwebsocketuri[];
extern const char str_device_med2autostartmulticast[];
// deviceio
extern const char str_device_iovideosources[];
extern const char str_device_iovideooutputs[];
extern const char str_device_ioaudiosources[];
extern const char str_device_ioaudiooutputs[];
extern const char str_device_iorelayoutputs[];
extern const char str_device_ioserialports[];
extern const char str_device_iodigitalinputs[];
extern const char str_device_iodigitalintputoptions[];
// events
extern const char str_device_eventgetcapaddr[];
extern const char str_device_eventwssps[];
extern const char str_device_eventwspps[];
extern const char str_device_eventwspsmis[];
extern const char str_device_eventmaxnotificationprodecures[];
extern const char str_device_eventmaxpullpoints[];
extern const char str_device_eventpersistentnotificationstorage[];
// ptz
extern const char str_device_ptzeflip[];
extern const char str_device_ptzreverse[];
extern const char str_device_ptzgetcompconfig[];
extern const char str_device_ptzmovestatus[];
extern const char str_device_ptzstatuspos[];


// GetNetworkInterface - Strings
extern const char str_device_config_dhcp[];
extern const char str_device_netinterfaces[];
extern const char str_device_hwaddress[];
extern const char str_device_ipv4[];
extern const char str_device_config[];
extern const char str_device_prefixlen[];
extern const char str_device_fromdhcp[];
extern const char str_device_dhcp[];
extern const char str_device_manual[];


// GetSysytemDateAndTime
extern const char str_sysdate[];
extern const char str_sysdate_dtt[];
extern const char str_sysdate_dls[];
extern const char str_sysdate_timezone[];
extern const char str_sysdate_utc[];
extern const char str_sysdate_local[];
extern const char str_sysdate_time[];
extern const char str_sysdate_date[];
extern const char str_sysdate_hour[];
extern const char str_sysdate_min[];
extern const char str_sysdate_sec[];
extern const char str_sysdate_year[];
extern const char str_sysdate_month[];
extern const char str_sysdate_day[];


// GetNetworkDefaultGateway
extern const char str_ndg_ng[];


// GetNetworkProtocols
extern const char str_nprotos_np[];


// GetWsdlUrl
extern const char str_gwu_wsdlurl[];
extern const char str_gwu_onvif_docuurl[];


// GetHostname
extern const char str_gh_hi[];


// GetUsers
extern const char str_gu_user[];
extern const char str_gu_username[];
extern const char str_gu_userlevel[];


// SystemReboot
extern const char str_sr_msg[];


/*MEDIA.c Strings -----------------------------------------------------------*/
// GetProfiels - Strings
extern const char str_profile_fixed[];
extern const char str_profile_profiles[];
extern const char str_profile_profile[];
extern const char str_profile_profiletoken[];
extern const char str_profile_configtoken[];
extern const char str_profile_token[];
extern const char str_profile_name[];
extern const char str_profile_vsc_token[];
extern const char str_profile_vs_token[];
extern const char str_profile_vs_name[];
extern const char str_profile_ve_token[];
extern const char str_profile_ve_name[];
extern const char str_profile_asc_token[];
extern const char str_profile_as_token[];
extern const char str_profile_as_name[];
extern const char str_profile_ae_token[];
extern const char str_profile_ae_name[];
extern const char str_profile_aoc_token[];
extern const char str_profile_ao_token[];
extern const char str_profile_ao_name[];
extern const char str_profile_ad_token[];
extern const char str_profile_ad_name[];
extern const char str_profile_vsc[];
extern const char str_profile_asc[];
extern const char str_profile_vec[];
extern const char str_profile_aec[];
extern const char str_profile_aoc[];
extern const char str_profile_adc[];
extern const char str_profile_usecount[];
extern const char str_profile_source_token[];
extern const char str_profile_output_token[];
extern const char str_profile_sendprimacy[];
extern const char str_profile_sp_hds[];
extern const char str_profile_sp_hdc[];
extern const char str_profile_sp_hda[];
extern const char str_profile_outputlevel[];
extern const char str_profile_bounds[];
extern const char str_profile_encoding[];
extern const char str_profile_resolution[];
extern const char str_profile_quality[];
extern const char str_profile_ratecontrol[];
extern const char str_profile_frl[];
extern const char str_profile_ei[];
extern const char str_profile_brl[];
extern const char str_profile_multicast[];
extern const char str_profile_address[];
extern const char str_profile_type[];
extern const char str_profile_autostart[];
extern const char str_profile_sess_timeout[];
extern const char str_profile_bitrate[];
extern const char str_profile_samplerate[];
extern const char str_profile_configuration[];
extern const char str_profile_ipv4addr[];


// GetStreamUri - Strings
extern const char str_streamuri_streamsetup[];
extern const char str_streamuri_stream[];
extern const char str_streamuri_transport[];
extern const char str_streamuri_proto[];
extern const char str_streamuri_profiletok[];

extern const char str_streamuri_mediauri[];
extern const char str_streamuri_uri[];
extern const char str_streamuri_invalafterconnect[];
extern const char str_streamuri_invalafterreboot[];
extern const char str_streamuri_timeout[];


// GetVideoSources
extern const char str_vsources_vss[];
extern const char str_vsources_fr[];
extern const char str_vsources_res[];


// GetAudioSources
extern const char str_asources_ass[];
extern const char str_asources_ch[];


// GetAudioEncodingConfigurationOptions
extern const char str_aecos_encoding[];
extern const char str_aecos_bitratelist[];
extern const char str_aecos_sampleratelist[];
extern const char str_aecos_configtok[];


// GetVideoEncodingConfigurationOptions
extern const char str_vecos_gfrs[];
extern const char str_vecos_qualityrange[];
extern const char str_vecos_resavailable[];
extern const char str_vecos_frramge[];
extern const char str_vecos_eirange[];
extern const char str_vecos_brrange[];
extern const char str_vecos_configtoken[];


// GetAudioDecoderConfigurationOptions
extern const char str_adcos_G711DecOptions[];
extern const char str_adcos_configtok[];
extern const char str_adcos_bitrate[];
extern const char str_adcos_srr[];


// GetAudioOutputConfigurationOptions
extern const char str_aocos_optokensavail[];
extern const char str_aocos_sendprimacyoptions[];
extern const char str_aocos_outputlevelrange[];


// GetVideoSourceConfigurationOptions
extern const char str_vscos_maxprofiles[];
extern const char str_vscos_boundsrange[];
extern const char str_vscos_xrange[];
extern const char str_vscos_yrange[];
extern const char str_vscos_wrange[];
extern const char str_vscos_hrange[];
extern const char str_vscos_vstokensavail[];


// GetVideoSourceConfigurationOptions
extern const char str_ascos_astokensavail[];


/*FAULT.c Strings -----------------------------------------------------------*/
extern const char str_fault_code[];
extern const char str_fault_value[];
extern const char str_fault_subcode[];
extern const char str_fault_reason[];
extern const char str_fault_text[];
extern const char str_fault_lang[];
extern const char str_fault_lang_en[];

extern const char str_fault_noprofile[];
extern const char str_fault_audionotsupported[];
extern const char str_fault_audiooutputnotsupported[];
extern const char str_fault_streamsetupnotsupported[];
extern const char str_fault_wsdlnotsupported[];
extern const char str_fault_noconfig[];
extern const char str_fault_configparamnotset[];
extern const char str_fault_vsnotexist[];
extern const char str_fault_asnotexist[];
extern const char str_fault_scopeempty[];
extern const char str_fault_toomanyscopes[];
extern const char str_fault_profileexists[];
extern const char str_fault_maxprofile[];
extern const char str_fault_delfixedprofile[];
extern const char str_fault_delfixedscope[];
extern const char str_fault_noscope[];

/*ONVIF_AUTH.c Strings ------------------------------------------------------*/
extern const char str_wss_security[];
extern const char str_wss_usernametoken[];
extern const char str_wss_username[];
extern const char str_wss_password[];
extern const char str_wss_nonce[];
extern const char str_wss_created[];


/*RTSP DIGEST AUTH ----------------------------------------------------------*/
extern const char str_digest_realm[];
extern const char str_digest_qop[];
extern const char str_digest_md5sess[];

#endif /* _ONVIF_SOAP_STR_H_ */
