/* @file soap_str.c
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#include "soap_str.h"

/*SOAP.c Strings -------------------------------------------------------------*/
const char *str_uri_schema = "http://www.onvif.org/ver10/schema";
const char *str_pf_schema = "tt";
const char *str_uri_device_wsdl = "http://www.onvif.org/ver10/device/wsdl";
const char *str_pf_device_wsdl = "tds";
const char *str_uri_media_wsdl = "http://www.onvif.org/ver10/media/wsdl";
const char *str_pf_media_wsdl = "trt";
const char *str_uri_media2_wsdl = "http://www.onvif.org/ver20/media/wsdl";
const char *str_pf_media2_wsdl = "tr2";
const char *str_uri_events_wsdl = "http://www.onvif.org/ver10/events/wsdl";
const char *str_pf_events_wsdl = "tev";
const char *str_uri_error = "http://www.onvif.org/ver10/error";
const char *str_pf_error = "ter";
const char *str_uri_network_wsdl = "http://www.onvif.org/ver10/network/wsdl";
const char *str_pf_network_wsdl = "dn";
const char *str_uri_ptz_wsdl = "http://www.onvif.org/ver20/ptz/wsdl";
const char *str_pf_ptz_wsdl = "tptz";
const char *str_uri_topics = "http://www.onvif.org/ver10/topics";
const char *str_pf_topics = "tns1";
const char *str_uri_deviceio_wsdl = "http://www.onvif.org/ver10/deviceIO/wsdl";
const char *str_pf_deviceio_wsdl = "tmd";
const char *str_uri_wsdl = "http://schemas.xmlsoap.org/wsdl/";
const char *str_pf_wsdl = "wsdl";
const char *str_uri_wsdl_soap12 = "http://schemas.xmlsoap.org/wsdl/soap12/";
const char *str_pf_wsdl_soap12 = "wsoap12";
const char *str_uri_wsdl_http = "http://schemas.xmlsoap.org/wsdl/http/";
const char *str_pf_wsdl_http = "http";
const char *str_uri_encoding = "http://www.w3.org/2003/05/soap-encoding";
const char *str_pf_encoding = "soapenc";
const char *str_uri_envelope = "http://www.w3.org/2003/05/soap-envelope";
const char *str_pf_envelope = "soapenv";
const char *str_uri_xml_schema = "http://www.w3.org/2001/XMLSchema";
const char *str_pf_xml_schema = "xs";
const char *str_uri_xml_schema_instance =
	"http://www.w3.org/2001/XMLSchema-instance";
const char *str_pf_xml_schema_instance = "xsi";
const char *str_uri_discovery =
	"http://schemas.xmlsoap.org/ws/2005/04/discovery";
const char *str_pf_discovery = "d";
const char *str_uri_xml_soap_addressing =
	"http://schemas.xmlsoap.org/ws/2004/08/addressing";
const char *str_pf_xml_soap_addressing = "wsadis";
const char *str_uri_addressing = "http://www.w3.org/2005/08/addressing";
const char *str_pf_addressing = "wsa";
const char *str_uri_wsn_t1 = "http://docs.oasis-open.org/wsn/t-1";
const char *str_pf_wsn_t1 = "wstop";
const char *str_uri_wsn_b2 = "http://docs.oasis-open.org/wsn/b-2";
const char *str_pf_wsn_b2 = "wsnt";
const char *str_uri_xop_include = "http://www.w3.org/2004/08/xop/include";
const char *str_pf_xop_include = "xop";

const char *str_uri_topic_expression_concrete =
	"http://docs.oasis-open.org/wsn/t-1/TopicExpression/Concrete";
const char *str_uri_topic_expression_concrete_set =
	"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet";
const char *str_uri_item_filter =
	"http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter";

const char *str_uri_passwd_type = "http://docs.oasis-open.org/wss/2004/01/"
	"oasis-200401-wss-username-token-profile-1.0#PasswordDigest";

const char *str_http_ctype = "application/soap+xml;charset=UTF-8";
const char *str_xmlprolog = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
const char *str_new_ns = "xmlns";
const char *str_envelope = "Envelope";
const char *str_header = "Header";
const char *str_body = "Body";
const char *str_fault = "Fault";
const char *str_true = "true";
const char *str_false = "false";
const char *str_enabled = "Enabled";
const char *str_token = "token";
const char *str_uctoken = "Token";
const char *str_info = "Info";
const char *str_name = "Name";
const char *str_config = "Config";
const char *str_height = "height";
const char *str_width = "width";
const char *str_ucheight = "Height";
const char *str_ucwidth = "Width";
const char *str_jpeg = "JPEG";
const char *str_h264 = "H264";
const char *str_mpeg4 = "MPEG4";
const char *str_pcmu = "G711";
const char *str_ipv4 = "IPv4";
const char *str_ipv6 = "IPv6";
const char *str_port = "Port";
const char *str_ttl = "TTL";
const char *str_http = "HTTP";
const char *str_https = "HTTPS";
const char *str_rtsp  = "RTSP";
const char *str_configurations = "Configurations";
const char *str_configuration = "Configuration";
const char *str_options = "Options";
const char *str_items = "Items";
const char *str_min = "Min";
const char *str_max = "Max";
const char *str_extension = "Extension";
const char *str_totalnumb = "TotalNumber";
const char *str_uctype = "Type";


/*WSD.c Strings --------------------------------------------------------------*/
const char *str_wsd_action = "Action";
const char *str_wsd_action_url =
	"http://docs.oasis-open.org/ws-dd/ns/discovery/2009/01";
const char *str_wsd_messageid = "MessageID";
const char *str_wsd_relates_to = "RelatesTo";
const char *str_wsd_reply_to = "ReplyTo";
const char *str_wsd_addressing_role_anon =
	"http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous";
const char *str_wsd_to = "To";
const char *str_wsd_to_value = "urn:docs-oasis-org:ws-dd:ns:discovery:2009:01";
const char *str_wsd_appsequence = "AppSequence";
const char *str_wsd_instanceid = "InstanceId";
const char *str_wsd_messagenumber = "MessageNumber";
const char *str_wsd_hello = "Hello";
const char *str_wsd_bye = "Bye";
const char *str_wsd_probe = "Probe";
const char *str_wsd_probe_match = "ProbeMatch";
const char *str_wsd_probe_matches = "ProbeMatches";
const char *str_wsd_resolve = "Resolve";
const char *str_wsd_resolve_match = "ResolveMatch";
const char *str_wsd_resolve_matches = "ResolveMatches";
const char *str_wsd_endpointreference = "EndpointReference";
const char *str_wsd_address = "Address";
const char *str_wsd_types = "Types";
const char *str_wsd_scopes = "Scopes";
const char *str_wsd_xaddrs = "XAddrs";
const char *str_wsd_meadataversion = "MetadataVersion";
const char *str_wsd_discoverymode = "DiscoveryMode";
const char *str_wsd_discoverable = "Discoverable";
const char *str_wsd_discoverableconf = "onvif_DiscoveryEnabled";
const char *str_wsd_nondiscoverable = "NonDiscoverable";


// for the reponse create a new member in childs which states a response child (bool)
// if true ... encode it as <METHOD>Response
/*METHOD STRINGS -------------------------------------------------------------*/
// DEVICE.c
const char *str_method_get_scopes = "GetScopes";
const char *str_method_get_scopes_r = "GetScopesResponse";
const char *str_method_set_scopes = "SetScopes";
const char *str_method_set_scopes_r = "SetScopesResponse";
const char *str_method_add_scopes = "AddScopes";
const char *str_method_add_scopes_r = "AddScopesResponse";
const char *str_method_remove_scopes = "RemoveScopes";
const char *str_method_remove_scopes_r = "RemoveScopesResponse";
const char *str_method_get_device_info = "GetDeviceInformation";
const char *str_method_get_device_info_r =
	"GetDeviceInformationResponse";
const char *str_method_get_services = "GetServices";
const char *str_method_get_services_r = "GetServicesResponse";
const char *str_method_get_service_cap = "GetServiceCapabilities";
const char *str_method_get_service_cap_r = "GetServiceCapabilitiesResponse";
const char *str_method_get_netinterfaces = "GetNetworkInterfaces";
const char *str_method_get_netinterfaces_r =
	"GetNetworkInterfacesResponse";
const char *str_method_get_capabilities = "GetCapabilities";
const char *str_method_get_capabilities_r = "GetCapabilitiesResponse";
const char *str_method_get_systime = "GetSystemDateAndTime";
const char *str_method_get_systime_r = "GetSystemDateAndTimeResponse";
const char *str_method_get_ndg = "GetNetworkDefaultGateway";
const char *str_method_get_ndg_r = "GetNetworkDefaultGatewayResponse";
const char *str_method_get_nprotos = "GetNetworkProtocols";
const char *str_method_get_nprotos_r = "GetNetworkProtocolsResponse";
const char *str_method_get_wsdlurl = "GetWsdlUrl";
const char *str_method_get_wsdlurl_r = "GetWsdlUrlResponse";
const char *str_method_get_hostname = "GetHostname";
const char *str_method_get_hostname_r = "GetHostnameResponse";
const char *str_method_get_users = "GetUsers";
const char *str_method_get_users_r = "GetUsersResponse";
const char *str_method_systemreboot = "SystemReboot";
const char *str_method_systemreboot_r = "SystemRebootResponse";


// WSD.c
const char *str_method_get_discoverymode = "GetDiscoveryMode";
const char *str_method_get_discoverymode_r = "GetDiscoveryModeResponse";
const char *str_method_set_discoverymode = "SetDiscoveryMode";
const char *str_method_set_discoverymode_r = "SetDiscoveryModeResponse";

//MEDIA.c
const char *str_method_get_profiles = "GetProfiles";
const char *str_method_get_profiles_r = "GetProfilesResponse";
const char *str_method_get_profile = "GetProfile";
const char *str_method_get_profile_r = "GetProfileResponse";
const char *str_method_get_vscs = "GetVideoSourceConfigurations";
const char *str_method_get_vscs_r = "GetVideoSourceConfigurationsResponse";
const char *str_method_get_vsc = "GetVideoSourceConfiguration";
const char *str_method_get_vsc_r = "GetVideoSourceConfigurationResponse";
const char *str_method_get_vecs = "GetVideoEncoderConfigurations";
const char *str_method_get_vecs_r = "GetVideoEncoderConfigurationsResponse";
const char *str_method_get_vec = "GetVideoEncoderConfiguration";
const char *str_method_get_vec_r = "GetVideoEncoderConfigurationResponse";
const char *str_method_get_ascs = "GetAudioSourceConfigurations";
const char *str_method_get_ascs_r = "GetAudioSourceConfigurationsResponse";
const char *str_method_get_asc = "GetAudioSourceConfiguration";
const char *str_method_get_asc_r = "GetAudioSourceConfigurationResponse";
const char *str_method_get_aecs = "GetAudioEncoderConfigurations";
const char *str_method_get_aecs_r = "GetAudioEncoderConfigurationsResponse";
const char *str_method_get_aec = "GetAudioEncoderConfiguration";
const char *str_method_get_aec_r = "GetAudioEncoderConfigurationResponse";
const char *str_method_get_aocs = "GetAudioOutputConfigurations";
const char *str_method_get_aocs_r = "GetAudioOutputConfigurationsResponse";
const char *str_method_get_aoc = "GetAudioOutputConfiguration";
const char *str_method_get_aoc_r = "GetAudioOutputConfigurationResponse";
const char *str_method_get_adcs = "GetAudioDecoderConfigurations";
const char *str_method_get_adcs_r = "GetAudioDecoderConfigurationsResponse";
const char *str_method_get_adc = "GetAudioDecoderConfiguration";
const char *str_method_get_adc_r = "GetAudioDecoderConfigurationResponse";
const char *str_method_get_suri = "GetStreamUri";
const char *str_method_get_suri_r = "GetStreamUriResponse";
const char *str_method_get_videosources = "GetVideoSources";
const char *str_method_get_videosources_r = "GetVideoSourcesResponse";
const char *str_method_get_audiosources = "GetAudioSources";
const char *str_method_get_audiosources_r = "GetAudioSourcesResponse";
const char *str_method_get_aecos = "GetAudioEncoderConfigurationOptions";
const char *str_method_get_aecos_r =
	"GetAudioEncoderConfigurationOptionsResponse";
const char *str_method_get_vecos = "GetVideoEncoderConfigurationOptions";
const char *str_method_get_vecos_r =
	"GetVideoEncoderConfigurationOptionsResponse";
const char *str_method_get_adcos = "GetAudioDecoderConfigurationOptions";
const char *str_method_get_adcos_r =
	"GetAudioDecoderConfigurationOptionsResponse";
const char *str_method_get_mdconfigs = "GetMetadataConfigurations";
const char *str_method_get_mdconfigs_r = "GetMetadataConfigurationsResponse";
const char *str_method_create_profile = "CreateProfile";
const char *str_method_create_profile_r = "CreateProfileResponse";
const char *str_method_delete_profile = "DeleteProfile";
const char *str_method_delete_profile_r = "DeleteProfileResponse";
const char *str_method_add_vsc = "AddVideoSourceConfiguration";
const char *str_method_add_vsc_r = "AddVideoSourceConfigurationResponse";
const char *str_method_add_asc = "AddAudioSourceConfiguration";
const char *str_method_add_asc_r = "AddAudioSourceConfigurationResponse";
const char *str_method_add_vec = "AddVideoEncoderConfiguration";
const char *str_method_add_vec_r = "AddVideoEncoderConfigurationResponse";
const char *str_method_add_aec = "AddAudioEncoderConfiguration";
const char *str_method_add_aec_r = "AddAudioEncoderConfigurationResponse";
const char *str_method_add_aoc = "AddAudioOutputConfiguration";
const char *str_method_add_aoc_r = "AddAudioOutputConfigurationResponse";
const char *str_method_add_adc = "AddAudioDecoderConfiguration";
const char *str_method_add_adc_r = "AddAudioDecoderConfigurationResponse";
const char *str_method_remove_vsc = "RemoveVideoSourceConfiguration";
const char *str_method_remove_vsc_r = "RemoveVideoSourceConfigurationResponse";
const char *str_method_remove_asc = "RemoveAudioSourceConfiguration";
const char *str_method_remove_asc_r = "RemoveAudioSourceConfigurationResponse";
const char *str_method_remove_vec = "RemoveVideoEncoderConfiguration";
const char *str_method_remove_vec_r = "RemoveVideoEncoderConfigurationResponse";
const char *str_method_remove_aec = "RemoveAudioEncoderConfiguration";
const char *str_method_remove_aec_r = "RemoveAudioEncoderConfigurationResponse";
const char *str_method_remove_aoc = "RemoveAudioOutputConfiguration";
const char *str_method_remove_aoc_r = "RemoveAudioOutputConfigurationResponse";
const char *str_method_remove_adc = "RemoveAudioDecoderConfiguration";
const char *str_method_remove_adc_r = "RemoveAudioDecoderConfigurationResponse";
const char *str_method_get_audiooutputs = "GetAudioOutputs";
const char *str_method_get_audiooutputs_r = "GetAudioOutputsResponse";
const char *str_method_get_aocos = "GetAudioOutputConfigurationOptions";
const char *str_method_get_aocos_r =
	"GetAudioOutputConfigurationOptionsResponse";
const char *str_method_get_cvec = "GetCompatibleVideoEncoderConfigurations";
const char *str_method_get_cvec_r =
	"GetCompatibleVideoEncoderConfigurationsResponse";
const char *str_method_get_caec = "GetCompatibleAudioEncoderConfigurations";
const char *str_method_get_caec_r =
	"GetCompatibleAudioEncoderConfigurationsResponse";
const char *str_method_get_ggnovei =
	"GetGuaranteedNumberOfVideoEncoderInstances";
const char *str_method_get_ggnovei_r =
	"GetGuaranteedNumberOfVideoEncoderInstancesResponse";
const char *str_method_get_cvsc = "GetCompatibleVideoSourceConfigurations";
const char *str_method_get_cvsc_r =
	"GetCompatibleVideoSourceConfigurationsResponse";
const char *str_method_get_casc = "GetCompatibleAudioSourceConfigurations";
const char *str_method_get_casc_r =
	"GetCompatibleAudioSourceConfigurationsResponse";
const char *str_method_get_caoc = "GetCompatibleAudioOutputConfigurations";
const char *str_method_get_caoc_r =
	"GetCompatibleAudioOutputConfigurationsResponse";
const char *str_method_get_cadc = "GetCompatibleAudioDecoderConfigurations";
const char *str_method_get_cadc_r =
	"GetCompatibleAudioDecoderConfigurationsResponse";
const char *str_method_get_vscos = "GetVideoSourceConfigurationOptions";
const char *str_method_get_vscos_r =
	"GetVideoSourceConfigurationOptionsResponse";
const char *str_method_set_videosource = "SetVideoSourceConfiguration";
const char *str_method_set_videosource_r =
	"SetVideoSourceConfigurationResponse";
const char *str_method_set_audiosource = "SetAudioSourceConfiguration";
const char *str_method_set_audiosource_r =
	"SetAudioSourceConfigurationResponse";
const char *str_method_set_videoecnoder = "SetVideoEncoderConfiguration";
const char *str_method_set_videoencoder_r =
	"SetVideoEncoderConfigurationResponse";
const char *str_method_get_ascos = "GetAudioSourceConfigurationOptions";
const char *str_method_get_ascos_r =
	"GetAudioSourceConfigurationOptionsResponse";
const char *str_method_set_audioecnoder = "SetAudioEncoderConfiguration";
const char *str_method_set_audioencoder_r =
	"SetAudioEncoderConfigurationResponse";
const char *str_method_set_audiooutput = "SetAudioOutputConfiguration";
const char *str_method_set_audiooutput_r =
	"SetAudioOutputConfigurationResponse";


/*PTZ.c Strings --------------------------------------------------------------*/
const char *str_method_get_nodes = "GetNodes";
const char *str_method_get_nodes_r = "GetNodesResponse";
const char *str_method_get_configurations = "GetConfigurations";
const char *str_method_get_configurations_r = "GetConfigurationsResponse";


/*EVENT.c --------------------------------------------------------------------*/
// const char *str_method_get_eventprop = "GetEventProperties";
// const char *str_method_get_eventprop_r = "GetEventPropertiesResponse";


/*SCOPE.c Strings ------------------------------------------------------------*/
const char *str_scope_preemble = "onvif://www.onvif.org/";
const char *str_scope_scopes = "Scopes";
const char *str_scope_scopedef = "ScopeDef";
const char *str_scope_scopeitem = "ScopeItem";
const char *str_scope_fixed = "Fixed";
const char *str_scope_configurable = "Configurable";

const char *str_scope_manufacturer = "onvif_s_manufacturer";
const char *str_scope_profstreaming = "onvif_s_Profile";
const char *str_scope_name = "onvif_s_name";
const char *str_scope_hardware = "onvif_s_hardware";


/*TYPES Strings --------------------------------------------------------------*/
const char *str_type_nvt = "NetworkVideoTransmitter";
const char *str_type_dev = "Device";


/*DEVICE.c Strings -----------------------------------------------------------*/
const char *str_device_uri = "/onvif/device_service";
const char *str_media1_uri = "/onvif/media_service";
const char *str_media2_uri = "/onvif/media2_service";
const char *str_deviceio_uri = "/onvif/deviceio_service";
const char *str_event_uri = "/onvif/event_service";
const char *str_ptz_uri = "/onvif/ptz_service";

const char *str_device_include_capability = "IncludeCapability";

// GetDeviceInformation - Strings
const char *str_device_hardware = "onvif_HardwareId";
const char *str_device_model = "onvif_Model";
const char *str_device_firmware = "onvif_FirmwareVersion";
const char *str_device_manufacturer = "onvif_Manufacturer";
const char *str_device_serialnumber = "onvif_SerialNumber";

// GetCapablities - Strings
const char *str_device_category = "Category";
const char *str_device_cat_device = "Device";
const char *str_device_cat_media = "Media";
const char *str_device_cat_events = "Events";
const char *str_device_cat_imaging = "Imaging";
const char *str_device_cat_analytics = "Analytics";
const char *str_device_cat_ptz = "PTZ";
const char *str_device_cat_io = "DeviceIO";
const char *str_device_cat_all = "All";

// GetServices - Strings
const char *str_device_service = "Service";
const char *str_device_capabilities = "Capabilities";
const char *str_device_namespace = "Namespace";
const char *str_device_xaddr = "XAddr";
const char *str_device_ver = "Version";
const char *str_device_major = "Major";
const char *str_device_minor = "Minor";
// device
const char *str_device_network = "Network";
const char *str_device_netipfilter = "IPFilter";
const char *str_device_netzconfig = "ZeroConfiguration";
const char *str_device_netipv6 = "IPVersion6";
const char *str_device_netdyndns = "DynDNS";
const char *str_device_netdot11config = "Dot11Configuration";
const char *str_device_netdot1xconfigs = "Dot1XConfigurations";
const char *str_device_nethostnamefdhcp = "HostnameFromDHCP";
const char *str_device_netntp = "NTP";
const char *str_device_netdhcp6 = "DHCPv6";
const char *str_device_security = "Security";
const char *str_device_sectls10 = "TLS1.0";
const char *str_device_sectls11 = "TLS1.1";
const char *str_device_sectls12 = "TLS1.2";
const char *str_device_seconboardkg  = "OnboardKeyGeneration";
const char *str_device_secaccesspolicyconfig = "AccessPolicyConfig";
const char *str_device_secdefaultaccesspolicy = "DefaultAccessPolicy";
const char *str_device_secdot1x = "Dot1X";
const char *str_device_secremoteuserhandling = "RemoteUserHandling";
const char *str_device_secx509token = "X.509Token";
const char *str_device_secsamltoken = "SAMLToken";
const char *str_device_seckerberostoken = "KerberosToken";
const char *str_device_secusertoken = "UsernameToken";
const char *str_device_sechttpdigest = "HttpDigest";
const char *str_device_secreltoken = "RELToken";
const char *str_device_secsupportedeapmethods = "SupportedEAPMethods";
const char *str_device_secsupportedeapmethod = "SupportedEAPMethod";
const char *str_device_secmaxusers = "MaxUsers";
const char *str_device_secmaxusernamelen = "MaxUserNameLength";
const char *str_device_secmaxpasswdlen = "MaxPasswordLength";
const char *str_device_system = "System";
const char *str_device_sysdiscoveryresolve = "DiscoveryResolve";
const char *str_device_sysdiscoverybye = "DiscoveryBye";
const char *str_device_sysremotediscovery = "RemoteDiscovery";
const char *str_device_syssystembackup = "SystemBackup";
const char *str_device_syssystemlogging = "SystemLogging";
const char *str_device_sysfirmwareupgrae = "FirmwareUpgrade";
const char *str_device_syssupportedversion = "SupportedVersions";
const char *str_device_syshttpfirmwareupgrade = "HttpFirmwareUpgrade";
const char *str_device_syshttpsystembackup = "HttpSystemBackup";
const char *str_device_syshttpsystemlogging = "HttpSystemLogging";
const char *str_device_syshttpsupportinfo = "HttpSupportInformation";
const char *str_device_sysstorageconfig = "StorageConfiguration";
const char *str_device_sysmaxstorageconfigs = "MaxStorageConfigurations";
const char *str_device_sysgeolocationentries = "GeoLocationEntries";
const char *str_device_sysautogeo = "AutoGeo";
const char *str_device_sysstoragetypssupported = "StorageTypesSupported";
const char *str_device_misc = "Misc";
const char *str_device_miscauxcommands = "AuxilaryCommands";
// media
const char *str_device_med1snapshoturi = "SnapshotUri";
const char *str_device_med1rotation = "Rotation";
const char *str_device_med1videosourcemode = "VideoSourceMode";
const char *str_device_med1osd = "OSD";
const char *str_device_med1temporaryosdtext = "TemporaryOSDText";
const char *str_device_med1exicompression = "EXICompression";
const char *str_device_med2mask = "Mask";
const char *str_device_med2sourcemask = "SourceMask";
const char *str_device_med1profcap = "ProfileCapabilities";
const char *str_device_med1maxnumberofprofile = "MaximumNumberOfProfiles";
const char *str_device_med2conigurationsupported = "ConfigurationsSupported";
const char *str_device_med1streamcap = "StreamingCapabilities";
const char *str_device_med1rtpmcast = "RTPMulticast";
const char *str_device_med1rtptcp = "RTP_TCP";
const char *str_device_med1rtprtsptcp = "RTP_RTSP_TCP";
const char *str_device_med1nonaggregatecontrol = "NonAggregateControl";
const char *str_device_med1nortspstreaming = "NoRTSPStreaming";
const char *str_device_med2rtspwebsocketuri = "RTSPWebSocketUri";
const char *str_device_med2autostartmulticast = "AutoStartMulticast";
// deviceio
const char *str_device_iovideosources = "VideoSources";
const char *str_device_iovideooutputs = "VideoOutputs";
const char *str_device_ioaudiosources = "AudioSources";
const char *str_device_ioaudiooutputs = "AudioOutputs";
const char *str_device_iorelayoutputs = "RelayOutputs";
const char *str_device_ioserialports = "SerialPorts";
const char *str_device_iodigitalinputs = "DigitalInputs";
const char *str_device_iodigitalintputoptions = "DigitalInputOptions";
// events
const char *str_device_eventgetcapaddr =
	"http://www.onvif.org/ver10/events/"
	"wsdl/EventPortType/GetServiceCapabilities";
const char *str_device_eventwssps =
	"WSSubscriptionPolicySupport";
const char *str_device_eventwspps = "WSPullPointSupport";
const char *str_device_eventwspsmis =
	"WSPausableSubscriptionManagerInterfaceSupport";
const char *str_device_eventmaxnotificationprodecures =
	"MaxNotificationProducers";
const char *str_device_eventmaxpullpoints = "MaxPullPoints";
const char *str_device_eventpersistentnotificationstorage =
	"persistentnotificationstorage";
// ptz
const char *str_device_ptzeflip = "EFlip";
const char *str_device_ptzreverse = "Reverse";
const char *str_device_ptzgetcompconfig = "GetCompatibleConfigurations";
const char *str_device_ptzmovestatus = "MoveStatus";
const char *str_device_ptzstatuspos = "StatusPosition";


// GetNetworkInterface - Strings
const char *str_device_config_dhcp = "onvif_DhcpEnabled";
const char *str_device_netinterfaces = "NetworkInterfaces";
const char *str_device_hwaddress = "HwAddress";
const char *str_device_ipv4 = "IPv4";
const char *str_device_config = "Config";
const char *str_device_prefixlen = "PrefixLength";
const char *str_device_fromdhcp = "FromDHCP";
const char *str_device_dhcp = "DHCP";
const char *str_device_manual = "Manual";


// GetSysytemDateAndTime
const char *str_sysdate = "SystemDateAndTime";
const char *str_sysdate_dtt = "DateTimeType";
const char *str_sysdate_dls = "DaylightSavings";
const char *str_sysdate_timezone = "TimeZone";
const char *str_sysdate_utc = "UTCDateTime";
const char *str_sysdate_local = "LocalDateTime";
const char *str_sysdate_time = "Time";
const char *str_sysdate_date = "Date";
const char *str_sysdate_hour = "Hour";
const char *str_sysdate_min = "Minute";
const char *str_sysdate_sec = "Second";
const char *str_sysdate_year = "Year";
const char *str_sysdate_month = "Month";
const char *str_sysdate_day = "Day";


// GetNetworkDefaultGateway
const char *str_ndg_ng = "NetworkGateway";


// GetNetworkProtocols
const char *str_nprotos_np = "NetworkProtocols";


// GetWsdlUrl
const char *str_gwu_wsdlurl = "WsdlUrl";
const char *str_gwu_onvif_docuurl =
	"http://www.onvif.org/Documents/Specifications.aspx";


// GetHostname
const char *str_gh_hi = "HostnameInformation";


// GetUsers
const char *str_gu_user = "User";
const char *str_gu_username = "Username";
const char *str_gu_userlevel = "UserLevel";


// SystemReboot
const char *str_sr_msg = "Message";


/*MEDIA.c Strings ------------------------------------------------------------*/
// GetProfiels - Strings
const char *str_profile_fixed = "fixed";
const char *str_profile_profiles = "Profiles";
const char *str_profile_profile = "Profile";
const char *str_profile_profiletoken = "ProfileToken";
const char *str_profile_configtoken = "ConfigurationToken";
const char *str_profile_token = "p0";
const char *str_profile_name = "standard_profile";
const char *str_profile_vsc_token = "vs_config0";
const char *str_profile_vs_token = "vs0";
const char *str_profile_vs_name = "standard_video_source0";
const char *str_profile_ve_token = "ve_config0";
const char *str_profile_ve_name = "standard_video_encoder0";
const char *str_profile_asc_token = "as_config0";
const char *str_profile_as_token = "as0";
const char *str_profile_as_name = "standard_audio_source0";
const char *str_profile_ae_token = "ae_config0";
const char *str_profile_ae_name = "standard_audio_encoder0";
const char *str_profile_aoc_token = "ao_config0";
const char *str_profile_ao_token = "ao0";
const char *str_profile_ao_name = "standard_audio_ouput0";
const char *str_profile_ad_token = "ad_config0";
const char *str_profile_ad_name = "standard_audio_decoder0";
const char *str_profile_vsc = "VideoSourceConfiguration";
const char *str_profile_asc = "AudioSourceConfiguration";
const char *str_profile_vec = "VideoEncoderConfiguration";
const char *str_profile_aec = "AudioEncoderConfiguration";
const char *str_profile_aoc = "AudioOutputConfiguration";
const char *str_profile_adc = "AudioDecoderConfiguration";
const char *str_profile_usecount = "UseCount";
const char *str_profile_source_token = "SourceToken";
const char *str_profile_output_token = "OutputToken";
const char *str_profile_sendprimacy = "SendPrimacy";
const char *str_profile_sp_hds = "www.onvif.org/ver20/HalfDuplex/Server";
const char *str_profile_sp_hdc = "www.onvif.org/ver20/HalfDuplex/Clien";
const char *str_profile_sp_hda = "www.onvif.org/ver20/HalfDuplex/Auto";
const char *str_profile_outputlevel = "OutputLevel";
const char *str_profile_bounds = "Bounds";
const char *str_profile_encoding = "Encoding";
const char *str_profile_resolution = "Resolution";
const char *str_profile_quality = "Quality";
const char *str_profile_ratecontrol = "RateControl";
const char *str_profile_frl = "FrameRateLimit";
const char *str_profile_ei = "EncodingInterval";
const char *str_profile_brl = "BitrateLimit";
const char *str_profile_multicast = "Multicast";
const char *str_profile_address = "Address";
const char *str_profile_type = "Type";
const char *str_profile_autostart = "AutoStart";
const char *str_profile_sess_timeout = "SessionTimeout";
const char *str_profile_bitrate = "Bitrate";
const char *str_profile_samplerate = "SampleRate";
const char *str_profile_configuration = "Configruation";
const char *str_profile_ipv4addr = "IPv4Address";


// GetStreamUri - Strings
const char *str_streamuri_streamsetup = "StreamSetup";
const char *str_streamuri_stream = "Stream";
const char *str_streamuri_transport = "Transport";
const char *str_streamuri_proto = "Protocol";
const char *str_streamuri_profiletok = "ProfileToken";

const char *str_streamuri_mediauri = "MediaUri";
const char *str_streamuri_uri = "Uri";
const char *str_streamuri_invalafterconnect = "InvalidAfterConnect";
const char *str_streamuri_invalafterreboot = "InvalidAfterReboot";
const char *str_streamuri_timeout = "Timeout";


// GetVideoSources
const char *str_vsources_vss = "VideoSources";
const char *str_vsources_fr = "Framerate";
const char *str_vsources_res = "Resolution";


// GetAudioSources
const char *str_asources_ass = "AudioSources";
const char *str_asources_ch = "Channels";


// GetAudioEncodingConfigurationOptions
const char *str_aecos_encoding = "Encoding";
const char *str_aecos_bitratelist = "BitrateList";
const char *str_aecos_sampleratelist = "SampleRateList";
const char *str_aecos_configtok = "ConfigurationToken";


// GetVideoEncodingConfigurationOptions
const char *str_vecos_gfrs = "GuaranteedFrameRateSupported";
const char *str_vecos_qualityrange = "QualityRange";
const char *str_vecos_resavailable = "ResolutionsAvailable";
const char *str_vecos_frramge = "FrameRateRange";
const char *str_vecos_eirange = "EncodingIntervalRange";
const char *str_vecos_brrange = "BitrateRange";
const char *str_vecos_configtoken = "ConfigurationToken";


// GetAudioDecoderConfigurationOptions
const char *str_adcos_G711DecOptions = "G711DecOptions";
const char *str_adcos_configtok = "ConfigurationToken";
const char *str_adcos_bitrate = "Bitrate";
const char *str_adcos_srr = "SampleRateRange";


// GetAudioOutputConfigurationOptions
const char *str_aocos_optokensavail = "OutputTokensAvailable";
const char *str_aocos_sendprimacyoptions = "SendPrimacyOptions";
const char *str_aocos_outputlevelrange = "OutputLevelRange";


// GetVideoSourceConfigurationOptions
const char *str_vscos_maxprofiles = "MaximumNumberOfProfiles";
const char *str_vscos_boundsrange = "BoundsRange";
const char *str_vscos_xrange = "XRange";
const char *str_vscos_yrange = "YRange";
const char *str_vscos_wrange = "WidthRange";
const char *str_vscos_hrange = "HeightRange";
const char *str_vscos_vstokensavail = "VideoSourceTokensAvailable";


// GetVideoSourceConfigurationOptions
const char *str_ascos_astokensavail = "InputTokensAvailable";


/*FAULT.c Strings ------------------------------------------------------------*/
const char *str_fault_code = "Code";
const char *str_fault_value = "Value";
const char *str_fault_subcode = "Subcode";
const char *str_fault_reason = "Reason";
const char *str_fault_text = "Text";
const char *str_fault_lang = "xml:lang";
const char *str_fault_lang_en = "en";

const char *str_fault_noprofile = "Requested profile token does not exist";
const char *str_fault_audionotsupported = "The device does not support audio";
const char *str_fault_audiooutputnotsupported =
	"Audio Outputs are not supported";
const char *str_fault_streamsetupnotsupported =
	"Specification of StreamType of Transport part is not supported";
const char *str_fault_wsdlnotsupported = "Requested WSDL service not supported";
const char *str_fault_noconfig = "Requested config token does not exist";
const char *str_fault_configparamnotset =
	"Configuration parameter are not possible to set";
const char *str_fault_vsnotexist = "The requested Videosource does not exist";
const char *str_fault_asnotexist = "The requested Audiosource does not exist";
const char *str_fault_scopeempty = "Scope list is empty";
const char *str_fault_toomanyscopes =
	"The Requested scope list exceeds the supported number of scopes";
const char *str_fault_profileexists =
	"A profile with the given ProfileToken already exists";
const char *str_fault_maxprofile =
	"The max number of supported profiles by the device has been reached";
const char *str_fault_delfixedprofile = "The fixed Profile cannot be deleted";
const char *str_fault_delfixedscope =
	"Trying to Remove a fixed scope parameter, command rejected";
const char *str_fault_noscope = "Trying to Remove scope which does not exist";

/*ONVIF_AUTH.c Strings -------------------------------------------------------*/
const char *str_wss_security = "Security";
const char *str_wss_usernametoken = "UsernameToken";
const char *str_wss_username = "Username";
const char *str_wss_password = "Password";
const char *str_wss_nonce = "Nonce";
const char *str_wss_created = "Created";


/*RTSP DIGEST AUTH -----------------------------------------------------------*/
const char *str_digest_realm = "stream";
const char *str_digest_qop = "auth";
const char *str_digest_md5sess = "MD5-Sess";
