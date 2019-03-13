/* @file soap_str.h
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#ifndef _ONVIF_SOAP_STR_H_
#define _ONVIF_SOAP_STR_H_

/*SOAP.c Strings -------------------------------------------------------------*/
const char *str_uri_schema;
const char *str_pf_schema;
const char *str_uri_device_wsdl;
const char *str_pf_device_wsdl;
const char *str_uri_media_wsdl;
const char *str_pf_media_wsdl;
const char *str_uri_media2_wsdl;
const char *str_pf_media2_wsdl;
const char *str_uri_events_wsdl;
const char *str_pf_events_wsdl;
const char *str_uri_error;
const char *str_pf_error;
const char *str_uri_network_wsdl;
const char *str_pf_network_wsdl;
const char *str_uri_ptz_wsdl;
const char *str_pf_ptz_wsdl;
const char *str_uri_topics;
const char *str_pf_topics;
const char *str_uri_deviceio_wsdl;
const char *str_pf_deviceio_wsdl;
const char *str_uri_wsdl;
const char *str_pf_wsdl;
const char *str_uri_wsdl_soap12;
const char *str_pf_wsdl_soap12;
const char *str_uri_wsdl_http;
const char *str_pf_wsdl_http;
const char *str_uri_encoding;
const char *str_pf_encoding;
const char *str_uri_envelope;
const char *str_pf_envelope;
const char *str_uri_xml_schema;
const char *str_pf_xml_schema;
const char *str_uri_xml_schema_instance;
const char *str_pf_xml_schema_instance;
const char *str_uri_discovery;
const char *str_pf_discovery;
const char *str_uri_xml_soap_addressing;
const char *str_pf_xml_soap_addressing;
const char *str_uri_addressing;
const char *str_pf_addressing;
const char *str_uri_wsn_t1;
const char *str_pf_wsn_t1;
const char *str_uri_wsn_b2;
const char *str_pf_wsn_b2;
const char *str_uri_xop_include;
const char *str_pf_xop_include;

const char *str_uri_topic_expression_concrete;
const char *str_uri_topic_expression_concrete_set;
const char *str_uri_item_filter;

const char *str_uri_passwd_type;

const char *str_http_ctype;
const char *str_xmlprolog;
const char *str_new_ns;
const char *str_envelope;
const char *str_header;
const char *str_body;
const char *str_fault;
const char *str_true;
const char *str_false;
const char *str_enabled;
const char *str_token;
const char *str_uctoken;
const char *str_info;
const char *str_name;
const char *str_config;
const char *str_height;
const char *str_width;
const char *str_ucheight;
const char *str_ucwidth;
const char *str_jpeg;
const char *str_h264;
const char *str_mpeg4;
const char *str_pcmu;
const char *str_ipv4;
const char *str_ipv6;
const char *str_port;
const char *str_ttl;
const char *str_http;
const char *str_https;
const char *str_rtsp ;
const char *str_configurations;
const char *str_configuration;
const char *str_options;
const char *str_items;
const char *str_min;
const char *str_max;
const char *str_extension;
const char *str_totalnumb;
const char *str_uctype;


/*WSD.c Strings --------------------------------------------------------------*/
const char *str_wsd_action;
const char *str_wsd_action_url;
const char *str_wsd_messageid;
const char *str_wsd_relates_to;
const char *str_wsd_reply_to;
const char *str_wsd_addressing_role_anon;
const char *str_wsd_to;
const char *str_wsd_to_value;
const char *str_wsd_appsequence;
const char *str_wsd_instanceid;
const char *str_wsd_messagenumber;
const char *str_wsd_hello;
const char *str_wsd_bye;
const char *str_wsd_probe;
const char *str_wsd_probe_match;
const char *str_wsd_probe_matches;
const char *str_wsd_resolve;
const char *str_wsd_resolve_match;
const char *str_wsd_resolve_matches;
const char *str_wsd_endpointreference;
const char *str_wsd_address;
const char *str_wsd_types;
const char *str_wsd_scopes;
const char *str_wsd_xaddrs;
const char *str_wsd_meadataversion;
const char *str_wsd_discoverymode;
const char *str_wsd_discoverable;
const char *str_wsd_discoverableconf;
const char *str_wsd_nondiscoverable;


/*METHOD STRINGS -------------------------------------------------------------*/
// DEVICE.c
const char *str_method_get_scopes;
const char *str_method_get_scopes_r;
const char *str_method_set_scopes;
const char *str_method_set_scopes_r;
const char *str_method_add_scopes;
const char *str_method_add_scopes_r;
const char *str_method_remove_scopes;
const char *str_method_remove_scopes_r;
const char *str_method_get_device_info;
const char *str_method_get_device_info_r;
const char *str_method_get_services;
const char *str_method_get_services_r;
const char *str_method_get_service_cap;
const char *str_method_get_service_cap_r;
const char *str_method_get_netinterfaces;
const char *str_method_get_netinterfaces_r;
const char *str_method_get_capabilities;
const char *str_method_get_capabilities_r;
const char *str_method_get_systime;
const char *str_method_get_systime_r;
const char *str_method_get_ndg;
const char *str_method_get_ndg_r;
const char *str_method_get_nprotos;
const char *str_method_get_nprotos_r;
const char *str_method_get_wsdlurl;
const char *str_method_get_wsdlurl_r;
const char *str_method_get_hostname;
const char *str_method_get_hostname_r;
const char *str_method_get_users;
const char *str_method_get_users_r;
const char *str_method_systemreboot;
const char *str_method_systemreboot_r;


// WSD.c
const char *str_method_get_discoverymode;
const char *str_method_get_discoverymode_r;
const char *str_method_set_discoverymode;
const char *str_method_set_discoverymode_r;

//MEDIA.c
const char *str_method_get_profiles;
const char *str_method_get_profiles_r;
const char *str_method_get_profile;
const char *str_method_get_profile_r;
const char *str_method_get_vscs;
const char *str_method_get_vscs_r;
const char *str_method_get_vsc;
const char *str_method_get_vsc_r;
const char *str_method_get_vecs;
const char *str_method_get_vecs_r;
const char *str_method_get_vec;
const char *str_method_get_vec_r;
const char *str_method_get_ascs;
const char *str_method_get_ascs_r;
const char *str_method_get_asc;
const char *str_method_get_asc_r;
const char *str_method_get_aecs;
const char *str_method_get_aecs_r;
const char *str_method_get_aec;
const char *str_method_get_aec_r;
const char *str_method_get_aocs;
const char *str_method_get_aocs_r;
const char *str_method_get_aoc;
const char *str_method_get_aoc_r;
const char *str_method_get_adcs;
const char *str_method_get_adcs_r;
const char *str_method_get_adc;
const char *str_method_get_adc_r;
const char *str_method_get_suri;
const char *str_method_get_suri_r;
const char *str_method_get_videosources;
const char *str_method_get_videosources_r;
const char *str_method_get_audiosources;
const char *str_method_get_audiosources_r;
const char *str_method_get_aecos;
const char *str_method_get_aecos_r;
const char *str_method_get_vecos;
const char *str_method_get_vecos_r;
const char *str_method_get_adcos;
const char *str_method_get_adcos_r;
const char *str_method_get_mdconfigs;
const char *str_method_get_mdconfigs_r;
const char *str_method_create_profile;
const char *str_method_create_profile_r;
const char *str_method_delete_profile;
const char *str_method_delete_profile_r;
const char *str_method_add_vsc;
const char *str_method_add_vsc_r;
const char *str_method_add_asc;
const char *str_method_add_asc_r;
const char *str_method_add_vec;
const char *str_method_add_vec_r;
const char *str_method_add_aec;
const char *str_method_add_aec_r;
const char *str_method_add_aoc;
const char *str_method_add_aoc_r;
const char *str_method_add_adc;
const char *str_method_add_adc_r;
const char *str_method_remove_vsc;
const char *str_method_remove_vsc_r;
const char *str_method_remove_asc;
const char *str_method_remove_asc_r;
const char *str_method_remove_vec;
const char *str_method_remove_vec_r;
const char *str_method_remove_aec;
const char *str_method_remove_aec_r;
const char *str_method_remove_aoc;
const char *str_method_remove_aoc_r;
const char *str_method_remove_adc;
const char *str_method_remove_adc_r;
const char *str_method_get_audiooutputs;
const char *str_method_get_audiooutputs_r;
const char *str_method_get_aocos;
const char *str_method_get_aocos_r;
const char *str_method_get_cvec;
const char *str_method_get_cvec_r;
const char *str_method_get_caec;
const char *str_method_get_caec_r;
const char *str_method_get_caoc;
const char *str_method_get_caoc_r;
const char *str_method_get_cadc;
const char *str_method_get_cadc_r;
const char *str_method_get_ggnovei;
const char *str_method_get_ggnovei_r;
const char *str_method_get_cvsc;
const char *str_method_get_cvsc_r;
const char *str_method_get_casc;
const char *str_method_get_casc_r;
const char *str_method_get_vscos;
const char *str_method_get_vscos_r;
const char *str_method_set_videosource;
const char *str_method_set_videosource_r;
const char *str_method_set_audiosource;
const char *str_method_set_audiosource_r;
const char *str_method_set_videoecnoder;
const char *str_method_set_videoencoder_r;
const char *str_method_set_audioecnoder;
const char *str_method_set_audioencoder_r;
const char *str_method_get_ascos;
const char *str_method_get_ascos_r;
const char *str_method_set_audiooutput;
const char *str_method_set_audiooutput_r;

/*PTZ.c Strings --------------------------------------------------------------*/
const char *str_method_get_nodes;
const char *str_method_get_nodes_r;
const char *str_method_get_configurations;
const char *str_method_get_configurations_r;


/*EVENT.c --------------------------------------------------------------------*/
// const char *str_method_get_eventprop = "GetEventProperties";
// const char *str_method_get_eventprop_r = "GetEventPropertiesResponse";


/*SCOPE.c Strings ------------------------------------------------------------*/
const char *str_scope_preemble;
const char *str_scope_scopes;
const char *str_scope_scopedef;
const char *str_scope_scopeitem;
const char *str_scope_fixed;
const char *str_scope_configurable;

const char *str_scope_manufacturer;
const char *str_scope_profstreaming;
const char *str_scope_name;
const char *str_scope_hardware;


/*TYPES Strings --------------------------------------------------------------*/
const char *str_type_nvt;
const char *str_type_dev;


/*DEVICE.c Strings -----------------------------------------------------------*/
const char *str_device_uri;
const char *str_media1_uri;
const char *str_media2_uri;
const char *str_deviceio_uri;
const char *str_event_uri;
const char *str_ptz_uri;

const char *str_device_include_capability;

// GetDeviceInformation - Strings
const char *str_device_hardware;
const char *str_device_model;
const char *str_device_firmware;
const char *str_device_manufacturer;
const char *str_device_serialnumber;

// GetCapablities - Strings
const char *str_device_category;
const char *str_device_cat_device;
const char *str_device_cat_media;
const char *str_device_cat_events;
const char *str_device_cat_imaging;
const char *str_device_cat_analytics;
const char *str_device_cat_ptz;
const char *str_device_cat_io;
const char *str_device_cat_all;

// GetServices - Strings
const char *str_device_service;
const char *str_device_capabilities;
const char *str_device_namespace;
const char *str_device_xaddr;
const char *str_device_ver;
const char *str_device_major;
const char *str_device_minor;
// device
const char *str_device_network;
const char *str_device_netipfilter;
const char *str_device_netzconfig;
const char *str_device_netipv6;
const char *str_device_netdyndns;
const char *str_device_netdot11config;
const char *str_device_netdot1xconfigs;
const char *str_device_nethostnamefdhcp;
const char *str_device_netntp;
const char *str_device_netdhcp6;
const char *str_device_security;
const char *str_device_sectls10;
const char *str_device_sectls11;
const char *str_device_sectls12;
const char *str_device_seconboardkg;
const char *str_device_secaccesspolicyconfig;
const char *str_device_secdefaultaccesspolicy;
const char *str_device_secdot1x;
const char *str_device_secremoteuserhandling;
const char *str_device_secx509token;
const char *str_device_secsamltoken;
const char *str_device_seckerberostoken;
const char *str_device_secusertoken;
const char *str_device_sechttpdigest;
const char *str_device_secreltoken;
const char *str_device_secsupportedeapmethods;
const char *str_device_secsupportedeapmethod;
const char *str_device_secmaxusers;
const char *str_device_secmaxusernamelen;
const char *str_device_secmaxpasswdlen;
const char *str_device_system;
const char *str_device_sysdiscoveryresolve;
const char *str_device_sysdiscoverybye;
const char *str_device_sysremotediscovery;
const char *str_device_syssystembackup;
const char *str_device_syssystemlogging;
const char *str_device_sysfirmwareupgrae;
const char *str_device_syssupportedversion;
const char *str_device_syshttpfirmwareupgrade;
const char *str_device_syshttpsystembackup;
const char *str_device_syshttpsystemlogging;
const char *str_device_syshttpsupportinfo;
const char *str_device_sysstorageconfig;
const char *str_device_sysmaxstorageconfigs;
const char *str_device_sysgeolocationentries;
const char *str_device_sysautogeo;
const char *str_device_sysstoragetypssupported;
const char *str_device_misc;
const char *str_device_miscauxcommands;
// media
const char *str_device_med1snapshoturi;
const char *str_device_med1rotation;
const char *str_device_med1videosourcemode;
const char *str_device_med1osd;
const char *str_device_med1temporaryosdtext;
const char *str_device_med1exicompression;
const char *str_device_med2mask;
const char *str_device_med2sourcemask;
const char *str_device_med1profcap;
const char *str_device_med1maxnumberofprofile;
const char *str_device_med2conigurationsupported;
const char *str_device_med1streamcap;
const char *str_device_med1rtpmcast;
const char *str_device_med1rtptcp;
const char *str_device_med1rtprtsptcp;
const char *str_device_med1nonaggregatecontrol;
const char *str_device_med1nortspstreaming;
const char *str_device_med2rtspwebsocketuri;
const char *str_device_med2autostartmulticast;
// deviceio
const char *str_device_iovideosources;
const char *str_device_iovideooutputs;
const char *str_device_ioaudiosources;
const char *str_device_ioaudiooutputs;
const char *str_device_iorelayoutputs;
const char *str_device_ioserialports;
const char *str_device_iodigitalinputs;
const char *str_device_iodigitalintputoptions;
// events
const char *str_device_eventgetcapaddr;
const char *str_device_eventwssps;
const char *str_device_eventwspps;
const char *str_device_eventwspsmis;
const char *str_device_eventmaxnotificationprodecures;
const char *str_device_eventmaxpullpoints;
const char *str_device_eventpersistentnotificationstorage;
// ptz
const char *str_device_ptzeflip;
const char *str_device_ptzreverse;
const char *str_device_ptzgetcompconfig;
const char *str_device_ptzmovestatus;
const char *str_device_ptzstatuspos;


// GetNetworkInterface - Strings
const char *str_device_config_dhcp;
const char *str_device_netinterfaces;
const char *str_device_hwaddress;
const char *str_device_ipv4;
const char *str_device_config;
const char *str_device_prefixlen;
const char *str_device_fromdhcp;
const char *str_device_dhcp;
const char *str_device_manual;

// GetSysytemDateAndTime
const char *str_sysdate;
const char *str_sysdate_dtt;
const char *str_sysdate_dls;
const char *str_sysdate_timezone;
const char *str_sysdate_utc;
const char *str_sysdate_local;
const char *str_sysdate_time;
const char *str_sysdate_date;
const char *str_sysdate_hour;
const char *str_sysdate_min;
const char *str_sysdate_sec;
const char *str_sysdate_year;
const char *str_sysdate_month;
const char *str_sysdate_day;


// GetNetworkDefaultGateway
const char *str_ndg_ng;


// GetNetworkProtocols
const char *str_nprotos_np;


// GetWsdlUrl
const char *str_gwu_wsdlurl;
const char *str_gwu_onvif_docuurl;


// GetHostname
const char *str_gh_hi;


// GetUsers
const char *str_gu_user;
const char *str_gu_username;
const char *str_gu_userlevel;


// SystemReboot
const char *str_sr_msg;


/*MEDIA.c Strings ------------------------------------------------------------*/
// GetProfiels - Strings
const char *str_profile_fixed;
const char *str_profile_profiles;
const char *str_profile_profile;
const char *str_profile_profiletoken;
const char *str_profile_configtoken;
const char *str_profile_token;
const char *str_profile_name;
const char *str_profile_vsc_token;
const char *str_profile_vs_token;
const char *str_profile_vs_name;
const char *str_profile_ve_token;
const char *str_profile_ve_name;
const char *str_profile_asc_token;
const char *str_profile_as_token;
const char *str_profile_as_name;
const char *str_profile_ae_token;
const char *str_profile_ae_name;
const char *str_profile_aoc_token;
const char *str_profile_ao_token;
const char *str_profile_ao_name;
const char *str_profile_ad_token;
const char *str_profile_ad_name;
const char *str_profile_vsc;
const char *str_profile_asc;
const char *str_profile_vec;
const char *str_profile_aec;
const char *str_profile_aoc;
const char *str_profile_adc;
const char *str_profile_usecount;
const char *str_profile_source_token;
const char *str_profile_output_token;
const char *str_profile_sendprimacy;
const char *str_profile_sp_hds;
const char *str_profile_sp_hdc;
const char *str_profile_sp_hda;
const char *str_profile_outputlevel;
const char *str_profile_bounds;
const char *str_profile_encoding;
const char *str_profile_resolution;
const char *str_profile_quality;
const char *str_profile_ratecontrol;
const char *str_profile_frl;
const char *str_profile_ei;
const char *str_profile_brl;
const char *str_profile_multicast;
const char *str_profile_address;
const char *str_profile_type;
const char *str_profile_autostart;
const char *str_profile_sess_timeout;
const char *str_profile_bitrate;
const char *str_profile_samplerate;
const char *str_profile_configuration;
const char *str_profile_ipv4addr;


// GetStreamUri - Strings
const char *str_streamuri_streamsetup;
const char *str_streamuri_stream;
const char *str_streamuri_transport;
const char *str_streamuri_proto;
const char *str_streamuri_profiletok;

const char *str_streamuri_mediauri;
const char *str_streamuri_uri;
const char *str_streamuri_invalafterconnect;
const char *str_streamuri_invalafterreboot;
const char *str_streamuri_timeout;


// GetVideoSources
const char *str_vsources_vss;
const char *str_vsources_fr;
const char *str_vsources_res;


// GetAudioSources
const char *str_asources_ass;
const char *str_asources_ch;


// GetAudioEncodingConfigurationOptions
const char *str_aecos_encoding;
const char *str_aecos_bitratelist;
const char *str_aecos_sampleratelist;
const char *str_aecos_configtok;


// GetAudioEncodingConfigurationOptions
const char *str_vecos_gfrs;
const char *str_vecos_qualityrange;
const char *str_vecos_resavailable;
const char *str_vecos_frramge;
const char *str_vecos_eirange;
const char *str_vecos_brrange;
const char *str_vecos_configtoken;


// GetAudioDecoderConfigurationOptions
const char *str_adcos_G711DecOptions;
const char *str_adcos_configtok;
const char *str_adcos_bitrate;
const char *str_adcos_srr;


// GetAudioOutputConfigurationOptions
const char *str_aocos_optokensavail;
const char *str_aocos_sendprimacyoptions;
const char *str_aocos_outputlevelrange;


// GetVideoSourceConfigurationOptions
const char *str_vscos_maxprofiles;
const char *str_vscos_boundsrange;
const char *str_vscos_xrange;
const char *str_vscos_yrange;
const char *str_vscos_wrange;
const char *str_vscos_hrange;
const char *str_vscos_vstokensavail;


// GetVideoSourceConfigurationOptions
const char *str_ascos_astokensavail;


/*FAULT.c Strings ------------------------------------------------------------*/
const char *str_fault_code;
const char *str_fault_value;
const char *str_fault_subcode;
const char *str_fault_reason;
const char *str_fault_text;
const char *str_fault_lang;
const char *str_fault_lang_en;

const char *str_fault_noprofile;
const char *str_fault_audionotsupported;
const char *str_fault_audiooutputnotsupported;
const char *str_fault_streamsetupnotsupported;
const char *str_fault_wsdlnotsupported;
const char *str_fault_noconfig;
const char *str_fault_configparamnotset;
const char *str_fault_vsnotexist;
const char *str_fault_asnotexist;
const char *str_fault_scopeempty;
const char *str_fault_toomanyscopes;
const char *str_fault_profileexists;
const char *str_fault_maxprofile;
const char *str_fault_delfixedprofile;
const char *str_fault_delfixedscope;
const char *str_fault_noscope;


/*ONVIF_AUTH.c Strings -------------------------------------------------------*/
const char *str_wss_security;
const char *str_wss_usernametoken;
const char *str_wss_username;
const char *str_wss_password;
const char *str_wss_nonce;
const char *str_wss_created;


/*RTSP DIGEST AUTH -----------------------------------------------------------*/
const char *str_digest_realm;
const char *str_digest_qop;
const char *str_digest_md5sess;

#endif /* _ONVIF_SOAP_STR_H_ */
