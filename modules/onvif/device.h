/* @file device.h
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */

#ifndef _ONVIF_DEVICE_H_
#define _ONVIF_DEVICE_H_

#define UUID_TB_SIZE 36 + 1
#define REBOOTDELAY 3

#define CAP_MAX_XADDR 64

struct soap_fault;

enum servicetype{
	S_ALL,
	S_DEVICE,
	S_MEDIA1,
	S_EVENT,
	S_PTZ,
	S_IO,

	S_MAX,
};

struct capabilities {
	char xaddr [CAP_MAX_XADDR];
	union {
		struct {
			struct {
				bool ipfilter      : 1;
				bool zconfig       : 1;
				bool ipv6          : 1;
				bool dyndns        : 1;
				bool dot11config   : 1;
				bool hostnamefdhcp : 1;
				bool dhcp6         : 1;
				uint8_t ntp;
				uint8_t dot1xconfigs;
			} network;
			struct {
				bool discoveryresolve     : 1;
				bool discoverybye         : 1;
				bool remotediscovery      : 1;
				bool systembackup         : 1;
				bool systemlogging        : 1;
				bool firmwareupgrae       : 1;
				bool httpfirmwareupgrade  : 1;
				bool httpsystembackup     : 1;
				bool httpsystemlogging    : 1;
				bool httpsupportinfo      : 1;
				bool storageconfig        : 1;
				uint8_t maxstorageconfigs;
				uint8_t geolocationentries;
				char * autogeo;
				char * storagetypssupported;
			} system;
			struct {
				bool tls10               : 1;
				bool tls11               : 1;
				bool tls12               : 1;
				bool onboardkg           : 1;
				bool accesspolicyconfig  : 1;
				bool defaultaccesspolicy : 1;
				bool dot1x               : 1;
				bool remoteuserhandling  : 1;
				bool x509token           : 1;
				bool samltoken           : 1;
				bool kerberostoken       : 1;
				bool usertoken           : 1;
				bool httpdigest          : 1;
				bool reltoken            : 1;
				char *supportedeapmethods;
				uint8_t maxusers;
				uint8_t maxusernamelen;
				uint8_t maxpasswdlen;
			} security;

			struct {
				char *auxcommands;
			} misc;
		} device;

		struct {
			bool wssps                           : 1;
			bool wspps                           : 1;
			bool wspsmis                         : 1;
			bool persistentnotificationstorage   : 1;
			uint8_t maxnotificationprodecures;
			uint8_t maxpullpoints;
		} events;

		struct {
			bool snapshoturi         : 1;
			bool rotation            : 1;
			bool videosourcemode     : 1;
			bool osd                 : 1;
			bool temporaryosdtext    : 1;
			bool exicompression      : 1;
			bool rtpmcast            : 1;
			bool rtptcp              : 1;
			bool rtprtsptcp          : 1;
			bool nonaggregatecontrol : 1;
			bool nortspstreaming     : 1;
			uint8_t maxnumberofprofile;
		} media1;

		struct {
			uint8_t videosources;
			uint8_t videooutputs;
			uint8_t audiosources;
			uint8_t audiooutputs;
			uint8_t relayoutputs;
			uint8_t serialports;
			uint8_t digitalinputs;
			uint8_t digitalintputoptions;
		}io;

		struct {
			bool eflip           : 1;
			bool reverse         : 1;
			bool getcompconfig   : 1;
			bool movestatus      : 1;
			bool statuspos       : 1;
		} ptz;
	} cap;
};

// bytes
struct service {
	struct le le;

	const char *namespace;
	struct capabilities *c;
	enum servicetype type;
	uint8_t vmajor;
	uint8_t vminor;
};


int services_init(void);
void services_deinit(void);

int generate_timebased_uuid(char *uuid, size_t len);

int device_GetServices_h(const struct soap_msg *msg,
			 struct soap_msg **ptrresp);
int device_GetDeviceInfo_h(const struct soap_msg *msg,
			   struct soap_msg **prtresp);
int device_GetCapabilities_h(const struct soap_msg *msg,
			     struct soap_msg **ptrresp, struct soap_fault *f);
int device_GetNWI_h(const struct soap_msg *msg,
		    struct soap_msg **prtresp);
int device_GetSystemDateAndTime_h(const struct soap_msg *msg,
				  struct soap_msg **prtresp);
int device_GetNetworkDefaultGateway_h(const struct soap_msg *msg,
				      struct soap_msg **prtresp);
int device_GetNetworkProtocols_h(const struct soap_msg *msg,
				 struct soap_msg **prtresp);
int device_GetServiceCapabilities_h(const struct soap_msg *msg,
				    struct soap_msg **prtresp);
int ptz_GetConfigurations_h(const struct soap_msg *msg,
			    struct soap_msg **prtresp);
int device_GetWsdlUrl_h(const struct soap_msg *msg,
			struct soap_msg **prtresp);
int device_GetHostname_h(const struct soap_msg *msg,
			 struct soap_msg **prtresp);
int device_SystemReboot_h(const struct soap_msg *msg,
			  struct soap_msg **prtresp);

#endif /* _ONVIF_DEVICE_H */

