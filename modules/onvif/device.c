/**
 * @file device.c
 *
 * For more information see:
 * https://www.onvif.org/ver10/device/wsdl/devicemgmt.wsdl
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ifaddrs.h>
#include <time.h>
#include <netpacket/packet.h>
#ifdef HAVE_SIGNAL
#include <signal.h>
#endif

#include <re.h>
#include <baresip.h>

#include "soap.h"
#include "onvif_auth.h"
#include "media.h"
#include "soap_str.h"
#include "wsd.h"
#include "device.h"
#include "rtspd.h"
#include "fault.h"

#define DEBUG_MODULE "onvif_device"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


enum {
	MAC_LEN =     18,
	MAC_LEN_SHORT=13,
};

static struct tmr shutdown_timer;
struct list services_l;


/**
 * shut down the user agent via a SIGTERM signal to the main loop.
 */
static void shutdown_timer_h(void *arg)
{
	(void)arg;

  #ifdef HAVE_SIGNAL
	raise(SIGTERM);
  #endif
}


static void service_destructor(void *arg)
{
	struct service *s = (struct service *)arg;

	s->c = mem_deref(s->c);
	list_unlink(&s->le);
}


/**
 *  remove all services from the service list
 */
void services_deinit(void)
{
	list_flush(&services_l);
}


/**
 * initialise all services and the capabilities
 * mem_zalloc allocates memory and set everything to 0
 * just pick non 0 fields and set the value accordingly
 *
 * @return          0 if success, error otherwise
 *
 */
int services_init(void)
{
	int err = 0;
	struct service *s;
	const struct sa *laddr;

	laddr =  net_laddr_af(baresip_network(), AF_INET);
	if (!laddr) {
		warning("onvif: %s Could not get local IP address.", __func__);
		return EINVAL;
	}

	/* DEVICE SERVICE ALLOC */
	s = NULL;
	s = mem_zalloc(sizeof(struct service), service_destructor);
	if (!s)
		return ENOMEM;

	list_append(&services_l, &s->le, s);

	s->c = mem_zalloc(sizeof(struct capabilities), NULL);
	if (!s->c)
		return ENOMEM;

	s->namespace = str_uri_device_wsdl;
	s->type = S_DEVICE;
	s->vmajor = 2;
	s->vminor = 2;
	if (-1 == re_snprintf(s->c->xaddr, CAP_MAX_XADDR , "http://%j:%u%s",
		laddr, DEFAULT_ONVIF_PORT, str_device_uri))
		return EINVAL;

	s->c->cap.device.security.tls10 = true;
	s->c->cap.device.security.tls11 = true;
	s->c->cap.device.security.tls12 = true;
	s->c->cap.device.security.onboardkg = true;
	s->c->cap.device.security.usertoken = true;
	/* s->c->cap.device.security.httpdigest = true; */
	s->c->cap.device.security.supportedeapmethods = "";
	s->c->cap.device.security.maxusers = 5;
	s->c->cap.device.security.maxusernamelen = MAXUSERLEN;
	s->c->cap.device.security.maxpasswdlen = MAXPASSWDLEN;

	s->c->cap.device.system.discoveryresolve = true;
	s->c->cap.device.system.discoverybye = true;
	s->c->cap.device.system.httpsystemlogging = true;
	s->c->cap.device.system.httpsystembackup = true;
	s->c->cap.device.system.httpsystemlogging = true;
	s->c->cap.device.system.autogeo = "";
	s->c->cap.device.system.storagetypssupported = "";

	s->c->cap.device.misc.auxcommands = "";

	/* MEDIA1 SERVICE ALLOC */
	s = NULL;
	s = mem_zalloc(sizeof(struct service), service_destructor);
	if (!s)
		return ENOMEM;

	list_append(&services_l, &s->le, s);

	s->c = mem_zalloc(sizeof(struct capabilities), NULL);
	if (!s->c)
		return ENOMEM;

	s->namespace = str_uri_media_wsdl;
	s->type = S_MEDIA1;
	s->vmajor = 2;
	s->vminor = 2;
	if (-1 == re_snprintf(s->c->xaddr, CAP_MAX_XADDR , "http://%j:%u%s",
		laddr, DEFAULT_ONVIF_PORT, str_media1_uri))
		return EINVAL;

	s->c->cap.media1.snapshoturi = true;
	s->c->cap.media1.maxnumberofprofile = MAXMEDIAPROFILE;
	s->c->cap.media1.rtprtsptcp = true;

	/* EVENT SERVICE ALLOC */
	s = NULL;
	s = mem_zalloc(sizeof(struct service), service_destructor);
	if (!s)
		return ENOMEM;

	list_append(&services_l, &s->le, s);

	s->c = mem_zalloc(sizeof(struct capabilities), NULL);
	if (!s->c)
		return ENOMEM;

	s->namespace = str_uri_events_wsdl;
	s->type = S_EVENT;
	s->vmajor = 2;
	s->vminor = 2;
	if (-1 == re_snprintf(s->c->xaddr, CAP_MAX_XADDR , "http://%j:%u%s",
		laddr, DEFAULT_ONVIF_PORT, str_event_uri))
		return EINVAL;

	s->c->cap.events.wssps = 1;
	s->c->cap.events.wspps = 1;

	/* PTZ SERVICE ALLOC */
	s = NULL;
	s = mem_zalloc(sizeof(struct service), service_destructor);
	if (!s)
		return ENOMEM;

	list_append(&services_l, &s->le, s);

	s->c = mem_zalloc(sizeof(struct capabilities), NULL);
	if (!s->c)
		return ENOMEM;

	s->namespace = str_uri_ptz_wsdl;
	s->type = S_PTZ;
	s->vmajor = 2;
	s->vminor = 2;
	if (-1 == re_snprintf(s->c->xaddr, CAP_MAX_XADDR , "http://%j:%u%s",
		laddr, DEFAULT_ONVIF_PORT, str_ptz_uri))
		return EINVAL;

	/* DEVICE-IO SERVICE ALLOC */
	s = NULL;
	s = mem_zalloc(sizeof(struct service), service_destructor);
	if (!s)
		return ENOMEM;

	list_append(&services_l, &s->le, s);

	s->c = mem_zalloc(sizeof(struct capabilities), NULL);
	if (!s->c)
		return ENOMEM;

	s->namespace = str_uri_deviceio_wsdl;
	s->type = S_IO;
	s->vmajor = 2;
	s->vminor = 2;
	if (-1 == re_snprintf(s->c->xaddr, CAP_MAX_XADDR , "http://%j:%u%s",
		laddr, DEFAULT_ONVIF_PORT, str_deviceio_uri))
		return EINVAL;

	s->c->cap.io.videosources = 1;
	s->c->cap.io.audiosources = 1;
	s->c->cap.io.audiooutputs = 1;

	return err;
}


static bool device_cmp_service_ns(struct le *le, void *arg)
{
	struct pl *ns_uri = (struct pl *)arg;
	struct service *s = le->data;

	return (0 == pl_strcmp(ns_uri, s->namespace));
}


static bool device_cmp_service_type(struct le *le, void *arg)
{
	enum servicetype *type = (enum servicetype *)arg;
	struct service *s = le->data;

	return (s->type == *type);
}


/**
 * find and get the mac address of the used network interface of baresip
 *
 * @param addr      location to save the mac address in integer representation
 *
 * @return          0 if success, error otherwise
 *
 */
static int get_macaddr(uint64_t *addr)
{
	struct ifaddrs *ifa, *ifaddr;
	const char *bs_ifname = conf_config()->net.ifname;
	struct sockaddr_ll *s;

	if (!addr)
		return EINVAL;

	if (getifaddrs(&ifaddr))
		return errno;

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (0 == str_cmp(ifa->ifa_name, bs_ifname) &&
		    ifa->ifa_addr->sa_family == AF_PACKET) {
			s = (struct sockaddr_ll *) ((void *)ifa->ifa_addr);
			memcpy(addr, s->sll_addr, 8);
			break;
		}
	}

	freeifaddrs(ifaddr);
	return 0;
}


/**
 * write the mac address as string with and without double points
 *
 * @param mac       buffer to return the mac address as string
 * @param len       max length of the buffer mac
 * @param dp        defines the output format of the mac address
 *
 * @return          number of written chars if success, -1 otherwise
 *
 */
static int get_mac_addr_fmt(char *mac, size_t len, bool dp, const char c)
{
	int err;
	uint64_t addr;
	const unsigned char *ptr;

	if (!mac)
		return -1;

	err = get_macaddr(&addr);
	if (err)
		return -1;

	ptr = (unsigned char *)&addr;
	if (dp)
		return re_snprintf(mac, len,
				   "%02X%c%02X%c%02X%c%02X%c%02X%c%02X",
				    *ptr,    c,
				   *(ptr+1), c,
				   *(ptr+2), c,
				   *(ptr+3), c,
				   *(ptr+4), c,
				   *(ptr+5)   );
	else
		return re_snprintf(mac, len, "%02x%02x%02x%02x%02x%02x",
				   *ptr, *(ptr+1), *(ptr+2), *(ptr+3),
				   *(ptr+4), *(ptr+5));

	return 0;
}

/**
 * Generate a time-based uuid.
 *
 * @param uuid      pointer to string location
 * @param len       max length of the string
 *
 * @return          0 if success, error otherwise
 * 0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                          time_low                             |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |       time_mid                |         time_hi_and_version   |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |clk_seq_hi_res |  clk_seq_low  |         node (0-1)            |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                         node (2-5)                            |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *  60 bit timestamp, version = 5
 *
 *  eg.:
 *  uuid: 3d8a44c2-0e00-e6b3-0c49-d47da47cffeb
 */
static uint16_t clk_seq = 0;
int generate_timebased_uuid(char *uuid, size_t len)
{
	int err;
	char mac[MAC_LEN_SHORT];
	uint64_t timestamp = tmr_jiffies();
	uint32_t time_low = timestamp & 0xffffffff;
	uint16_t time_mid = timestamp >> 32;
	uint16_t time_hi = timestamp >> 48;
	uint8_t clk_seq_hi_res, clk_seq_low;

	if (!uuid)
		return EINVAL;

	time_hi |= 0x1 >> 12;
	if (!clk_seq)
		clk_seq = rand_u16();
	else
		++clk_seq;

	clk_seq_low = clk_seq;
	clk_seq_hi_res = clk_seq >> 8;
	err = get_mac_addr_fmt(mac, MAC_LEN_SHORT, false, ':');
	if (err != (MAC_LEN_SHORT - 1))
		return EINVAL;

	err = re_snprintf(uuid, len, "%08x-%04x-%04x-%02x%02x-%s",
		time_low, time_mid, time_hi, clk_seq_hi_res, clk_seq_low, mac);

	if (err != ((int)len - 1)) {
		return EINVAL;
	}

	return 0;
}


/**
 * check if the method should include the capabilities.
 *
 * @param msg       request message
 * @param ic        includecapability boolean
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int device_includCapability(const struct soap_msg *msg, bool *ic)
{
	struct soap_child *c;

	if (!msg || !ic)
		return EINVAL;

	c = soap_child_has_child(soap_child_has_child(
		msg->envelope, NULL, str_body), NULL, str_method_get_services);
	c = soap_child_has_child(c, NULL, str_device_include_capability);
	if (!c)
		return EINVAL;

	*ic = (0 == pl_strcmp(&c->value, "true"));
	return 0;
}


/**
 * add the device capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_capabilities_device(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	const char *val;
	struct soap_child *devicec, *tmpc, *netc, *sysc, *secc, *extc, *mmc,
			  *ioc;

	if (!root || !s || s->type != S_DEVICE)
		return EINVAL;

	devicec = soap_add_child(root->msg, root, str_pf_schema,
		str_device_cat_device);
	tmpc = soap_add_child(root->msg, devicec, str_pf_schema,
			      str_device_xaddr);
	err |= soap_set_value_fmt(tmpc, "%s", s->c->xaddr);

	/* NETWORK */
	netc = soap_add_child(root->msg, devicec, str_pf_schema,
		str_device_network);
	val = s->c->cap.device.network.ipfilter ? str_true : str_false;
	tmpc = soap_add_child(root->msg, netc, str_pf_schema,
		str_device_netipfilter);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.network.zconfig ? str_true : str_false;
	tmpc = soap_add_child(root->msg, netc, str_pf_schema,
		str_device_netzconfig);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.network.ipv6 ? str_true : str_false;
	tmpc = soap_add_child(root->msg, netc, str_pf_schema,
		str_device_netipv6);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.network.dyndns ? str_true : str_false;
	tmpc = soap_add_child(root->msg, netc, str_pf_schema,
		str_device_netdyndns);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	extc = soap_add_child(root->msg, netc, str_pf_schema, str_extension);
	val = s->c->cap.device.network.dot11config ? str_true : str_false;
	tmpc = soap_add_child(root->msg, extc, str_pf_schema,
		str_device_netdot11config);
	err |= soap_set_value_fmt(tmpc, "%s", val);

	/* SYSTEM */
	sysc = soap_add_child(root->msg, devicec, str_pf_schema,
		str_device_system);
	val = s->c->cap.device.system.discoveryresolve ? str_true : str_false;
	tmpc = soap_add_child(root->msg, sysc, str_pf_schema,
		str_device_sysdiscoveryresolve);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.system.discoverybye ? str_true : str_false;
	tmpc = soap_add_child(root->msg, sysc, str_pf_schema,
		str_device_sysdiscoverybye);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.system.remotediscovery ? str_true : str_false;
	tmpc = soap_add_child(root->msg, sysc, str_pf_schema,
		str_device_sysremotediscovery);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.system.systembackup ? str_true : str_false;
	tmpc = soap_add_child(root->msg, sysc, str_pf_schema,
		str_device_syssystembackup);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.system.systemlogging ? str_true : str_false;
	tmpc = soap_add_child(root->msg, sysc, str_pf_schema,
		str_device_syssystemlogging);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.system.firmwareupgrae ? str_true : str_false;
	tmpc = soap_add_child(root->msg, sysc, str_pf_schema,
		str_device_sysfirmwareupgrae);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	tmpc = soap_add_child(root->msg, sysc, str_pf_schema,
		str_device_syssupportedversion);
	mmc = soap_add_child(root->msg, tmpc, str_pf_schema, str_device_major);
	err |= soap_set_value_fmt(mmc, "%d", s->vmajor);
	mmc = soap_add_child(root->msg, tmpc, str_pf_schema, str_device_minor);
	err |= soap_set_value_fmt(mmc, "%d", s->vminor);
	extc = soap_add_child(root->msg, sysc, str_pf_schema, str_extension);
	val = s->c->cap.device.system.httpfirmwareupgrade ?
		str_true : str_false;
	tmpc = soap_add_child(root->msg, extc, str_pf_schema,
		str_device_syshttpfirmwareupgrade);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.system.httpsystembackup ? str_true : str_false;
	tmpc = soap_add_child(root->msg, extc, str_pf_schema,
		str_device_syshttpsystembackup);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.system.httpsystemlogging ? str_true : str_false;
	tmpc = soap_add_child(root->msg, extc, str_pf_schema,
		str_device_syshttpsystemlogging);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.system.httpsupportinfo ? str_true : str_false;
	tmpc = soap_add_child(root->msg, extc, str_pf_schema,
		str_device_syshttpsupportinfo);
	err |= soap_set_value_fmt(tmpc, "%s", val);

	/* IO dummy (optional but fails if not included, let it empty) */
	ioc = soap_add_child(root->msg, devicec, str_pf_schema, "IO");
	tmpc = soap_add_child(root->msg, ioc, str_pf_schema,
			      "InputConnectors");
	err |= soap_set_value_fmt(tmpc, "%d", 0);
	tmpc = soap_add_child(root->msg, ioc, str_pf_schema, "RelayOutputs");
	err |= soap_set_value_fmt(tmpc, "%d", 0);
	extc = soap_add_child(root->msg, ioc, str_pf_schema, "Extension");
	tmpc = soap_add_child(root->msg, extc, str_pf_schema, "Auxiliary");
	err |= soap_set_value_fmt(tmpc, "%s", str_false);
	tmpc = soap_add_child(root->msg, extc, str_pf_schema,
			      "AuxiliaryCommands");
	tmpc = soap_add_child(root->msg, extc, str_pf_schema, "Extension");

	/* SECURITY */
	secc = soap_add_child(root->msg, devicec, str_pf_schema,
		str_device_security);
	val = s->c->cap.device.security.tls11 ? str_true : str_false;
	tmpc = soap_add_child(root->msg, secc, str_pf_schema,
		str_device_sectls11);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.security.tls12 ? str_true : str_false;
	tmpc = soap_add_child(root->msg, secc, str_pf_schema,
		str_device_sectls12);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.security.onboardkg ? str_true : str_false;
	tmpc = soap_add_child(root->msg, secc, str_pf_schema,
		str_device_seconboardkg);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.security.accesspolicyconfig ?
		str_true : str_false;
	tmpc = soap_add_child(root->msg, secc, str_pf_schema,
		str_device_secaccesspolicyconfig);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.security.x509token ? str_true : str_false;
	tmpc = soap_add_child(root->msg, secc, str_pf_schema,
		str_device_secx509token);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.security.samltoken ? str_true : str_false;
	tmpc = soap_add_child(root->msg, secc, str_pf_schema,
		str_device_secsamltoken);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.security.kerberostoken ? str_true : str_false;
	tmpc = soap_add_child(root->msg, secc, str_pf_schema,
		str_device_seckerberostoken);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.device.security.reltoken ? str_true : str_false;
	tmpc = soap_add_child(root->msg, secc, str_pf_schema,
		str_device_secreltoken);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	extc = soap_add_child(root->msg, secc, str_pf_schema, str_extension);
	val = s->c->cap.device.security.tls10 ? str_true : str_false;
	tmpc = soap_add_child(root->msg, extc, str_pf_schema,
		str_device_sectls10);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	extc = soap_add_child(root->msg, extc, str_pf_schema, str_extension);
	val = s->c->cap.device.security.dot1x ? str_true : str_false;
	tmpc = soap_add_child(root->msg, extc, str_pf_schema,
		str_device_secdot1x);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	/* tmpc = soap_add_child(root->msg, extc, str_pf_schema, */
	/*     str_device_secsupportedeapmethod); */
	/* err |= soap_set_value_fmt(tmpc, "%s", */
	/*     s->c->cap.device.security.supportedeapmethods); */
	val = s->c->cap.device.security.remoteuserhandling ?
		str_true : str_false;
	tmpc = soap_add_child(root->msg, extc, str_pf_schema,
		str_device_secremoteuserhandling);
	err |= soap_set_value_fmt(tmpc, "%s", val);

	return err;
}


/**
 * add the event capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_capabilities_events(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	const char *val;
	struct soap_child *eventc, *tmpc;

	if (!root || !s || s->type != S_EVENT)
		return EINVAL;

	eventc = soap_add_child(root->msg, root, str_pf_schema,
		str_device_cat_events);
	tmpc = soap_add_child(root->msg, eventc, str_pf_schema,
			      str_device_xaddr);
	err |= soap_set_value_fmt(tmpc, "%s", s->c->xaddr);
	val = s->c->cap.events.wssps ? str_true : str_false;
	tmpc = soap_add_child(root->msg, eventc, str_pf_schema,
		str_device_eventwssps);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.events.wspps ? str_true : str_false;
	tmpc = soap_add_child(root->msg, eventc, str_pf_schema,
		str_device_eventwspps);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.events.wspsmis ? str_true : str_false;
	tmpc = soap_add_child(root->msg, eventc, str_pf_schema,
		str_device_eventwspsmis);
	err |= soap_set_value_fmt(tmpc, "%s", val);

	return err;
}


/**
 * add the media capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_capabilities_media(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	const char *val;
	struct soap_child *mediac, *tmpc, *scc, *pcc, *extc;

	if (!root || !s || s->type != S_MEDIA1)
		return EINVAL;

	mediac = soap_add_child(root->msg, root, str_pf_schema,
		str_device_cat_media);
	tmpc = soap_add_child(root->msg, mediac, str_pf_schema,
			      str_device_xaddr);
	err |= soap_set_value_fmt(tmpc, "%s", s->c->xaddr);
	scc = soap_add_child(root->msg, mediac, str_pf_schema,
		str_device_med1streamcap);
	val = s->c->cap.media1.rtpmcast ? str_true : str_false;
	tmpc = soap_add_child(root->msg, scc, str_pf_schema,
		str_device_med1rtpmcast);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.media1.rtptcp ? str_true : str_false;
	tmpc = soap_add_child(root->msg, scc, str_pf_schema,
		str_device_med1rtptcp);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	val = s->c->cap.media1.rtprtsptcp ? str_true : str_false;
	tmpc = soap_add_child(root->msg, scc, str_pf_schema,
		str_device_med1rtprtsptcp);
	err |= soap_set_value_fmt(tmpc, "%s", val);
	extc = soap_add_child(root->msg, mediac, str_pf_schema, str_extension);
	pcc = soap_add_child(root->msg, extc,
		str_pf_schema, str_device_med1profcap);
	tmpc = soap_add_child(root->msg, pcc, str_pf_schema,
		str_device_med1maxnumberofprofile);
	err |= soap_set_value_fmt(tmpc, "%d",
				  s->c->cap.media1.maxnumberofprofile);

	return err;
}


/**
 * add the ptz capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_capabilities_ptz(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	struct soap_child *ptzc, *tmpc;

	if (!root || !s || s->type != S_PTZ)
		return EINVAL;

	ptzc = soap_add_child(root->msg, root, str_pf_schema,
		str_device_cat_ptz);
	tmpc = soap_add_child(root->msg, ptzc, str_pf_schema,
			      str_device_xaddr);
	err |= soap_set_value_fmt(tmpc, "%s", s->c->xaddr);

	return err;
}


/**
 * add the device-io capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_capabilities_deviceio(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	struct soap_child *ioc, *tmpc, *extc;

	if (!root || !s || s->type != S_IO)
		return EINVAL;

	extc = soap_add_child(root->msg, root, str_pf_schema, str_extension);
	ioc = soap_add_child(root->msg, extc, str_pf_schema,
			     str_device_cat_io);
	tmpc = soap_add_child(root->msg, ioc, str_pf_schema,
		str_device_xaddr);
	err |= soap_set_value_fmt(tmpc, "%s", s->c->xaddr);
	tmpc = soap_add_child(root->msg, ioc, str_pf_schema,
		str_device_iovideosources);
	err |= soap_set_value_fmt(tmpc, "%d", s->c->cap.io.videosources);
	tmpc = soap_add_child(root->msg, ioc, str_pf_schema,
		str_device_iovideooutputs);
	err |= soap_set_value_fmt(tmpc, "%d", s->c->cap.io.videooutputs);
	tmpc = soap_add_child(root->msg, ioc, str_pf_schema,
		str_device_ioaudiosources);
	err |= soap_set_value_fmt(tmpc, "%d", s->c->cap.io.audiosources);
	tmpc = soap_add_child(root->msg, ioc, str_pf_schema,
		str_device_ioaudiooutputs);
	err |= soap_set_value_fmt(tmpc, "%d", s->c->cap.io.audiooutputs);
	tmpc = soap_add_child(root->msg, ioc, str_pf_schema,
		str_device_iorelayoutputs);
	err |= soap_set_value_fmt(tmpc, "%d", s->c->cap.io.relayoutputs);

	return err;
}


/**
 * add the device service capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_servicecap_device(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	struct soap_child *capc, *nwc, *secc, *sysc, *miscc;
	const char *val;

	if (!root || !s || s->type != S_DEVICE)
		return EINVAL;

	capc = soap_add_child(root->msg, root,
		str_pf_device_wsdl, str_device_capabilities);

	/* NETWORK */
	nwc = soap_add_child(root->msg, capc,
		str_pf_device_wsdl, str_device_network);
	val = s->c->cap.device.network.ipfilter ? str_true : str_false;
	err |= soap_add_parameter_str(nwc, NULL,
		str_device_netipfilter, strlen(str_device_netipfilter),
		val, strlen(val));
	val = s->c->cap.device.network.zconfig ? str_true : str_false;
	err |= soap_add_parameter_str(nwc, NULL,
		str_device_netzconfig, strlen(str_device_netzconfig),
		val, strlen(val));
	val = s->c->cap.device.network.ipv6 ? str_true : str_false;
	err |= soap_add_parameter_str(nwc, NULL,
		str_device_netipv6, strlen(str_device_netipv6),
		val, strlen(val));
	val = s->c->cap.device.network.dyndns ? str_true : str_false;
	err |= soap_add_parameter_str(nwc, NULL,
		str_device_netdyndns, strlen(str_device_netdyndns),
		val, strlen(val));
	val = s->c->cap.device.network.dot11config ? str_true : str_false;
	err |= soap_add_parameter_str(nwc, NULL,
		str_device_netdot11config, strlen(str_device_netdot11config),
		val, strlen(val));
	err |= soap_add_parameter_uint(nwc, NULL,
		str_device_netdot1xconfigs, strlen(str_device_netdot1xconfigs),
		s->c->cap.device.network.dot1xconfigs);
	val = s->c->cap.device.network.hostnamefdhcp ? str_true : str_false;
	err |= soap_add_parameter_str(nwc, NULL,
		str_device_nethostnamefdhcp,
		strlen(str_device_nethostnamefdhcp), val, strlen(val));
	err |= soap_add_parameter_uint(nwc, NULL,
		str_device_netntp, strlen(str_device_netntp),
		s->c->cap.device.network.ntp);
	val = s->c->cap.device.network.dhcp6 ? str_true : str_false;
	err |= soap_add_parameter_str(nwc, NULL,
		str_device_netdhcp6, strlen(str_device_netdhcp6),
		val, strlen(val));

	/* SECURITY */
	secc = soap_add_child(root->msg, capc,
		str_pf_device_wsdl, str_device_security);
	val = s->c->cap.device.security.tls10 ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_sectls10, strlen(str_device_sectls10),
		val, strlen(val));
	val = s->c->cap.device.security.tls11 ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_sectls11, strlen(str_device_sectls11),
		val, strlen(val));
	val = s->c->cap.device.security.tls12 ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_sectls12, strlen(str_device_sectls12),
		val, strlen(val));
	val = s->c->cap.device.security.onboardkg ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_seconboardkg, strlen(str_device_seconboardkg),
		val, strlen(val));
	val = s->c->cap.device.security.accesspolicyconfig ?
		str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_secaccesspolicyconfig,
		strlen(str_device_secaccesspolicyconfig), val, strlen(val));
	val = s->c->cap.device.security.defaultaccesspolicy ?
		str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_secdefaultaccesspolicy,
		strlen(str_device_secdefaultaccesspolicy), val, strlen(val));
	val = s->c->cap.device.security.dot1x ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_secdot1x, strlen(str_device_secdot1x),
		val, strlen(val));
	val = s->c->cap.device.security.remoteuserhandling ?
		str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
				      str_device_secremoteuserhandling,
		strlen(str_device_secremoteuserhandling), val, strlen(val));
	val = s->c->cap.device.security.x509token ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_secx509token, strlen(str_device_secx509token),
		val, strlen(val));
	val = s->c->cap.device.security.samltoken ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_secsamltoken, strlen(str_device_secsamltoken),
		val, strlen(val));
	val = s->c->cap.device.security.kerberostoken ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_seckerberostoken,
		strlen(str_device_seckerberostoken),
		val, strlen(val));
	val = s->c->cap.device.security.usertoken ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_secusertoken, strlen(str_device_secusertoken),
		val, strlen(val));
	val = s->c->cap.device.security.httpdigest ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_sechttpdigest, strlen(str_device_sechttpdigest),
		val, strlen(val));
	val = s->c->cap.device.security.reltoken ? str_true : str_false;
	err |= soap_add_parameter_str(secc, NULL,
		str_device_secreltoken, strlen(str_device_secreltoken),
		val, strlen(val));
	err |= soap_add_parameter_str(secc, NULL,
				      str_device_secsupportedeapmethods,
		strlen(str_device_secsupportedeapmethods),
		s->c->cap.device.security.supportedeapmethods,
		strlen(s->c->cap.device.security.supportedeapmethods));
	err |= soap_add_parameter_uint(secc, NULL,
		str_device_secmaxusers, strlen(str_device_secmaxusers),
		s->c->cap.device.security.maxusers);
	err |= soap_add_parameter_uint(secc, NULL,
		str_device_secmaxusernamelen,
		strlen(str_device_secmaxusernamelen),
		s->c->cap.device.security.maxusernamelen);
	err |= soap_add_parameter_uint(secc, NULL,
		str_device_secmaxpasswdlen, strlen(str_device_secmaxpasswdlen),
		s->c->cap.device.security.maxpasswdlen);

	/* SYSTEM */
	sysc = soap_add_child(root->msg, capc,
		str_pf_device_wsdl, str_device_system);
	val = s->c->cap.device.system.discoveryresolve ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_sysdiscoveryresolve,
		strlen(str_device_sysdiscoveryresolve),
		val, strlen(val));
	val = s->c->cap.device.system.discoverybye ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_sysdiscoverybye, strlen(str_device_sysdiscoverybye),
		val, strlen(val));
	val = s->c->cap.device.system.remotediscovery ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_sysremotediscovery,
		strlen(str_device_sysremotediscovery), val, strlen(val));
	val = s->c->cap.device.system.systembackup ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_syssystembackup, strlen(str_device_syssystembackup),
		val, strlen(val));
	val = s->c->cap.device.system.systemlogging ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_syssystemlogging,
		strlen(str_device_syssystemlogging),
		val, strlen(val));
	val = s->c->cap.device.system.firmwareupgrae ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_sysfirmwareupgrae,
		strlen(str_device_sysfirmwareupgrae),
		val, strlen(val));
	val = s->c->cap.device.system.httpfirmwareupgrade ?
		str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
				      str_device_syshttpfirmwareupgrade,
		strlen(str_device_syshttpfirmwareupgrade), val, strlen(val));
	val = s->c->cap.device.system.httpsystembackup ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_syshttpsystembackup,
		strlen(str_device_syshttpsystembackup),
		val, strlen(val));
	val = s->c->cap.device.system.httpsystemlogging ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_syshttpsystemlogging,
		strlen(str_device_syshttpsystemlogging),
		val, strlen(val));
	val = s->c->cap.device.system.httpsupportinfo ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_syshttpsupportinfo,
		strlen(str_device_syshttpsupportinfo),
		val, strlen(val));
	val = s->c->cap.device.system.storageconfig ? str_true : str_false;
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_sysstorageconfig,
		strlen(str_device_sysstorageconfig),
		val, strlen(val));
	err |= soap_add_parameter_uint(sysc, NULL,
				       str_device_sysgeolocationentries,
		strlen(str_device_sysgeolocationentries),
		s->c->cap.device.system.geolocationentries);
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_sysautogeo, strlen(str_device_sysautogeo),
		s->c->cap.device.system.autogeo,
		strlen(s->c->cap.device.system.autogeo));
	err |= soap_add_parameter_str(sysc, NULL,
		str_device_sysstoragetypssupported,
		strlen(str_device_sysstoragetypssupported),
		s->c->cap.device.system.storagetypssupported,
		strlen(s->c->cap.device.system.storagetypssupported));

	/* MISC */
	miscc = soap_add_child(root->msg, capc,
		str_pf_device_wsdl, str_device_misc);
	err |= soap_add_parameter_str(miscc, NULL,
		str_device_miscauxcommands, strlen(str_device_miscauxcommands),
		s->c->cap.device.misc.auxcommands,
		strlen(s->c->cap.device.misc.auxcommands));

	return err;
}


/**
 * add the media1 service capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_servicecap_media1(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	struct soap_child *capc, *pcc, *scc;
	const char *val;

	if (!root || !s || s->type != S_MEDIA1)
		return EINVAL;

	capc = soap_add_child(root->msg, root,
		str_pf_media_wsdl, str_device_capabilities);

	val = s->c->cap.media1.snapshoturi ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL,
		str_device_med1snapshoturi, strlen(str_device_med1snapshoturi),
		val, strlen(val));
	val = s->c->cap.media1.rotation ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL,
		str_device_med1rotation, strlen(str_device_med1rotation),
		val, strlen(val));
	val = s->c->cap.media1.videosourcemode ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL,
		str_device_med1videosourcemode,
		strlen(str_device_med1videosourcemode),
		val, strlen(val));
	val = s->c->cap.media1.osd ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL,
		str_device_med1osd, strlen(str_device_med1osd),
		val, strlen(val));
	val = s->c->cap.media1.temporaryosdtext ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL,
		str_device_med1temporaryosdtext,
		strlen(str_device_med1temporaryosdtext),
		val, strlen(val));
	val = s->c->cap.media1.exicompression ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL,
		str_device_med1exicompression,
		strlen(str_device_med1exicompression),
		val, strlen(val));

	pcc = soap_add_child(root->msg, capc,
		str_pf_media_wsdl, str_device_med1profcap);
	err |= soap_add_parameter_uint(pcc, NULL,
				       str_device_med1maxnumberofprofile,
		strlen(str_device_med1maxnumberofprofile),
		s->c->cap.media1.maxnumberofprofile);
	scc = soap_add_child(root->msg, capc,
		str_pf_media_wsdl, str_device_med1streamcap);
	val = s->c->cap.media1.rtpmcast ? str_true : str_false;
	err |= soap_add_parameter_str(scc, NULL,
		str_device_med1rtpmcast, strlen(str_device_med1rtpmcast),
		val, strlen(val));
	val = s->c->cap.media1.rtptcp ? str_true : str_false;
	err |= soap_add_parameter_str(scc, NULL,
		str_device_med1rtptcp, strlen(str_device_med1rtptcp),
		val, strlen(val));
	val = s->c->cap.media1.rtprtsptcp ? str_true : str_false;
	err |= soap_add_parameter_str(scc, NULL,
		str_device_med1rtprtsptcp, strlen(str_device_med1rtprtsptcp),
		val, strlen(val));
	val = s->c->cap.media1.nonaggregatecontrol ? str_true : str_false;
	err |= soap_add_parameter_str(scc, NULL,
				      str_device_med1nonaggregatecontrol,
		strlen(str_device_med1nonaggregatecontrol), val, strlen(val));
	val = s->c->cap.media1.nortspstreaming ? str_true : str_false;
	err |= soap_add_parameter_str(scc, NULL,
		str_device_med1nortspstreaming,
		strlen(str_device_med1nortspstreaming),
		val, strlen(val));

	return err;
}


/**
 * add the event service capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_servicecap_event(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	struct soap_child *capc, *h, *actionc;
	const char *val;

	if (!root || !s || s->type != S_EVENT)
		return EINVAL;

	h = soap_child_has_child(root->msg->envelope, NULL, str_header);
	actionc = soap_add_child(root->msg, h, str_pf_addressing,
				 str_wsd_action);
	err |= soap_set_value_fmt(actionc, "%s", str_device_eventgetcapaddr);

	capc = soap_add_child(root->msg, root,
		str_pf_events_wsdl, str_device_capabilities);

	val = s->c->cap.events.wssps ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL, str_device_eventwssps,
		strlen(str_device_eventwssps), val, strlen(val));
	val = s->c->cap.events.wspps ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL, str_device_eventwspps,
		strlen(str_device_eventwspps), val, strlen(val));
	val = s->c->cap.events.wspsmis ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL, str_device_eventwspsmis,
		strlen(str_device_eventwspsmis), val, strlen(val));
	err |= soap_add_parameter_uint(capc, NULL,
		str_device_eventmaxnotificationprodecures,
		strlen(str_device_eventmaxnotificationprodecures),
		s->c->cap.events.maxnotificationprodecures);
	err |= soap_add_parameter_uint(capc, NULL,
		str_device_eventmaxpullpoints,
		strlen(str_device_eventmaxpullpoints),
		s->c->cap.events.maxpullpoints);
	val = s->c->cap.events.persistentnotificationstorage ?
		str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL,
		str_device_eventpersistentnotificationstorage,
		strlen(str_device_eventpersistentnotificationstorage),
		val, strlen(val));

	return err;
}


/**
 * add the ptz service capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_servicecap_ptz(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	struct soap_child *capc;
	const char *val;

	if (!root || !s || s->type != S_PTZ)
		return EINVAL;

	capc = soap_add_child(root->msg, root,
		str_pf_ptz_wsdl, str_device_capabilities);

	val = s->c->cap.ptz.eflip ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL, str_device_ptzeflip,
		strlen(str_device_ptzeflip), val, strlen(val));
	val = s->c->cap.ptz.reverse ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL, str_device_ptzreverse,
		strlen(str_device_ptzreverse), val, strlen(val));
	val = s->c->cap.ptz.getcompconfig ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL, str_device_ptzgetcompconfig,
		strlen(str_device_ptzgetcompconfig), val, strlen(val));
	val = s->c->cap.ptz.movestatus ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL, str_device_ptzmovestatus,
		strlen(str_device_ptzmovestatus), val, strlen(val));
	val = s->c->cap.ptz.statuspos ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL, str_device_ptzstatuspos,
		strlen(str_device_ptzstatuspos), val, strlen(val));

	return err;
}


/**
 * add the device-io service capabilities to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_servicecap_deviceio(struct soap_child *root,
	struct service *s)
{
	int err = 0;
	struct soap_child *capc;
	const char *val;

	if (!root || !s || s->type != S_IO)
		return EINVAL;

	capc = soap_add_child(root->msg, root,
		str_pf_deviceio_wsdl, str_device_capabilities);

	err |= soap_add_parameter_uint(capc, NULL, str_device_iovideosources,
		strlen(str_device_iovideosources), s->c->cap.io.videosources);
	err |= soap_add_parameter_uint(capc, NULL, str_device_iovideooutputs,
		strlen(str_device_iovideooutputs), s->c->cap.io.videooutputs);
	err |= soap_add_parameter_uint(capc, NULL, str_device_ioaudiosources,
		strlen(str_device_ioaudiosources), s->c->cap.io.audiosources);
	err |= soap_add_parameter_uint(capc, NULL, str_device_ioaudiooutputs,
		strlen(str_device_ioaudiooutputs), s->c->cap.io.audiooutputs);
	err |= soap_add_parameter_uint(capc, NULL, str_device_iorelayoutputs,
		strlen(str_device_iorelayoutputs), s->c->cap.io.relayoutputs);
	err |= soap_add_parameter_uint(capc, NULL, str_device_ioserialports,
		strlen(str_device_ioserialports), s->c->cap.io.serialports);
	err |= soap_add_parameter_uint(capc, NULL, str_device_iodigitalinputs,
		strlen(str_device_iodigitalinputs),
		s->c->cap.io.digitalinputs);
	val = s->c->cap.io.digitalintputoptions ? str_true : str_false;
	err |= soap_add_parameter_str(capc, NULL,
		str_device_iodigitalintputoptions,
		strlen(str_device_iodigitalintputoptions), val, strlen(val));

	return err;
}


/**
 * add a service child to @root
 *
 * @param root          root element to add service
 * @param s             service to add
 * @param include_cap   capabilities requested
 *
 * @return          0 if success, errorcode otherwise
 */
static int device_add_service(struct soap_child *root, struct service *s,
	bool include_cap)
{
	int err = 0;
	struct soap_child *sc, *capc, *tmpc, *verc, *mmc;
	struct soap_namespace *ns = NULL;

	if (!root || !s)
		return EINVAL;

	ns = soap_msg_has_ns_uri(root->msg, s->namespace);
	if (!ns)
		return EINVAL;

	sc = soap_add_child(root->msg, root, str_pf_device_wsdl,
			    str_device_service);
	tmpc = soap_add_child(root->msg, sc, str_pf_device_wsdl,
			      str_device_namespace);
	err |= soap_set_value_fmt(tmpc, "%s", s->namespace);
	tmpc = soap_add_child(root->msg, sc, str_pf_device_wsdl,
			      str_device_xaddr);
	err |= soap_set_value_fmt(tmpc, "%s", s->c->xaddr);

	if (include_cap) {
		capc = soap_add_child(root->msg, sc, str_pf_device_wsdl,
			str_device_capabilities);
		switch (s->type) {
			case S_DEVICE:
				err |= device_add_servicecap_device(capc, s);
				break;

			case S_MEDIA1:
				err |= device_add_servicecap_media1(capc, s);
				break;

			case S_EVENT:
				err |= device_add_servicecap_event(capc, s);
				break;

			case S_IO:
				err |= device_add_servicecap_deviceio(capc, s);
				break;

			case S_PTZ:
				err |= device_add_servicecap_ptz(capc, s);
				break;

			default:
				return ENOTSUP;
		}
	}

	verc = soap_add_child(root->msg, sc, str_pf_device_wsdl,
			      str_device_ver);
	mmc = soap_add_child(root->msg, verc, str_pf_schema, str_device_major);
	err |= soap_set_value_fmt(mmc, "%d", s->vmajor);
	mmc = soap_add_child(root->msg, verc, str_pf_schema, str_device_minor);
	err |= soap_set_value_fmt(mmc, "%d", s->vminor);

	return err;
}


/**
 * handle GetServices requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetServices_h (const struct soap_msg *msg,
			  struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gsrc;
	struct le *le;
	struct service *s;
	bool include_cap = false;


	if (!msg || !prtresp)
		return EINVAL;

	err = device_includCapability(msg, &include_cap);
	if (err)
		goto out;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_events_wsdl, str_uri_events_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_deviceio_wsdl, str_uri_deviceio_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_ptz_wsdl, str_uri_ptz_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_addressing, str_uri_addressing) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_header);
	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gsrc = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_get_services_r);

	le = services_l.head;
	while (le) {
		s = le->data;
		err |= device_add_service(gsrc, s, include_cap);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetServiceCapabilities requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetServiceCapabilities_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gsrc;
	struct soap_child *gscc;
	struct soap_namespace *ns;
	struct le *le;
	struct service *s;


	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gscc = soap_child_has_child(b, NULL, str_method_get_service_cap);
	le = list_apply(&services_l, true, device_cmp_service_ns,
			&gscc->ns->uri);
	if (!le)
		return EINVAL;

	s = le->data;
	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_events_wsdl, str_uri_events_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_ptz_wsdl, str_uri_ptz_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_deviceio_wsdl, str_uri_deviceio_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_addressing, str_uri_addressing) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	ns = soap_msg_has_ns_uri(resp, s->namespace);

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_header);
	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gsrc = soap_add_child(resp, b, ns->prefix.p,
		str_method_get_service_cap_r);

	switch (s->type) {
		case S_DEVICE:
			err |= device_add_servicecap_device(gsrc, s);
			break;

		case S_MEDIA1:
			err |= device_add_servicecap_media1(gsrc, s);
			break;

		case S_EVENT:
			err |= device_add_servicecap_event(gsrc, s);
			break;

		case S_IO:
			err |= device_add_servicecap_deviceio(gsrc, s);
			break;

		case S_PTZ:
			err |= device_add_servicecap_ptz(gsrc, s);
			break;

		default:
			err = ENOTSUP;
			goto out;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetCapabilities requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetCapabilities_h (const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gcrc, *caprc;
	struct soap_child *gcc, *capc;
	struct le *le;
	struct service *s;
	enum servicetype t;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gcc = soap_child_has_child(b, NULL, str_method_get_capabilities);
	capc = soap_child_has_child(gcc, NULL, str_device_category);
	if (capc) {
		if (0 == pl_strcmp(&capc->value, str_device_cat_all)) {
			t = S_ALL;
		}
		else if (0 == pl_strcmp(&capc->value, str_device_cat_device)) {
			t = S_DEVICE;
		}
		else if (0 == pl_strcmp(&capc->value, str_device_cat_media)) {
			t = S_MEDIA1;
		}
		else if (0 == pl_strcmp(&capc->value, str_device_cat_events)) {
			t = S_EVENT;
		}
		else if (0 == pl_strcmp(&capc->value, str_device_cat_ptz)) {
			t = S_PTZ;
		}
		else {
			fault_set(f, FC_Receiver, FS_ActionNotSupported,
				  FS_NoSuchService,
				  str_fault_wsdlnotsupported);
			return EINVAL;
		}

	}
	else {
		t = S_ALL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gcrc = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_get_capabilities_r);
	caprc = soap_add_child(resp, gcrc,
		str_pf_device_wsdl, str_device_capabilities);

	switch (t) {
	case S_ALL:
		t = S_DEVICE;
		le = list_apply(&services_l, true, device_cmp_service_type,
				&t);
		if (!le) {
			err = EINVAL;
			goto out;
		}
		s = le->data;
		err |= device_add_capabilities_device(caprc, s);

		t = S_EVENT;
		le = list_apply(&services_l, true, device_cmp_service_type,
				&t);
		if (!le) {
			err = EINVAL;
			goto out;
		}
		s = le->data;
		err |= device_add_capabilities_events(caprc, s);

		t = S_MEDIA1;
		le = list_apply(&services_l, true, device_cmp_service_type,
				&t);
		if (!le) {
			err = EINVAL;
			goto out;
		}
		s = le->data;
		err |= device_add_capabilities_media(caprc, s);

		t = S_PTZ;
		le = list_apply(&services_l, true, device_cmp_service_type,
				&t);
		if (!le) {
			err = EINVAL;
			goto out;
		}
		s = le->data;
		err |= device_add_capabilities_ptz(caprc, s);

		t = S_IO;
		le = list_apply(&services_l, true, device_cmp_service_type,
				&t);
		if (!le) {
			err = EINVAL;
			goto out;
		}
		s = le->data;
		err |= device_add_capabilities_deviceio(caprc, s);
		break;


	case S_DEVICE:
		le = list_apply(&services_l, true, device_cmp_service_type,
				&t);
		if (!le) {
			err = EINVAL;
			goto out;
		}
		s = le->data;
		err |= device_add_capabilities_device(caprc, s);
		break;

	case S_EVENT:
		le = list_apply(&services_l, true, device_cmp_service_type,
				&t);
		if (!le) {
			err = EINVAL;
			goto out;
		}
		s = le->data;
		err |= device_add_capabilities_events(caprc, s);
		break;

	case S_MEDIA1:
		le = list_apply(&services_l, true, device_cmp_service_type,
				&t);
		if (!le) {
			err = EINVAL;
			goto out;
		}
		s = le->data;
		err |= device_add_capabilities_media(caprc, s);
		break;

	case S_PTZ:
		le = list_apply(&services_l, true, device_cmp_service_type,
				&t);
		if (!le) {
			err = EINVAL;
			goto out;
		}
		s = le->data;
		err |= device_add_capabilities_ptz(caprc, s);
		break;


	default:
		err = ENOTSUP;
		goto out;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetNetworkInterface requests
 *
 * TODO 1: check serial number of device
 * DONE 1: will be written into the config file at startup
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetDeviceInfo_h (const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gdir, *c;
	struct pl value;
	const char *device_info;
	char *tmp;
	char mac[MAC_LEN];

	if (!msg || !prtresp)
		return EINVAL;


	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gdir = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_get_device_info_r);

	device_info = strchr(str_device_manufacturer, '_') + 1;
	c = soap_add_child(resp, gdir, str_pf_device_wsdl, device_info);
	if (0 == conf_get(conf_cur(), str_device_manufacturer, &value)) {
		err |= soap_set_value_fmt(c, "%r", &value);
		tmp = strchr(c->str_value, '-');
		while (tmp) {
			*tmp = ' ';
			tmp = strchr(c->str_value, '-');
		}
	}

	device_info = strchr(str_device_model, '_') + 1;
	c = soap_add_child(resp, gdir, str_pf_device_wsdl, device_info);
	if (0 == conf_get(conf_cur(), str_device_model, &value))
		err |= soap_set_value_fmt(c, "%r", &value);

	device_info = strchr(str_device_firmware, '_') + 1;
	c = soap_add_child(resp, gdir, str_pf_device_wsdl, device_info);
	if (0 == conf_get(conf_cur(), str_device_firmware, &value))
		err |= soap_set_value_fmt(c, "%r", &value);

	device_info = strchr(str_device_serialnumber, '_') + 1;
	c = soap_add_child(resp, gdir, str_pf_device_wsdl, device_info);
	if (0 == conf_get(conf_cur(), str_device_serialnumber, &value))
		err |= soap_set_value_fmt(c, "%r", &value);

	device_info = strchr(str_device_hardware, '_') + 1;
	c = soap_add_child(resp, gdir, str_pf_device_wsdl, device_info);
	if ((MAC_LEN - 1) != get_mac_addr_fmt(mac, MAC_LEN, true, '-')) {
		err = EINVAL;
		goto out;
	}

	err |= soap_set_value_fmt(c, "%s", mac);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAutioEncoderConfiguration requests
 *
 * @param dtc       DateAndTime Child to add the information
 * @param tm        time information to add
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int device_add_systime(struct soap_child *dtc, const struct tm *tm)
{
	int err = 0;
	struct soap_child *tmp, *timec, *datec;

	timec = soap_add_child(dtc->msg, dtc, str_pf_schema, str_sysdate_time);

	datec = soap_add_child(dtc->msg, dtc, str_pf_schema, str_sysdate_date);

	tmp = soap_add_child(dtc->msg, timec, str_pf_schema, str_sysdate_hour);
	err |= soap_set_value_fmt(tmp, "%u", tm->tm_hour);
	tmp = soap_add_child(dtc->msg, timec, str_pf_schema, str_sysdate_min);
	err |= soap_set_value_fmt(tmp, "%u", tm->tm_min);
	tmp = soap_add_child(dtc->msg, timec, str_pf_schema, str_sysdate_sec);
	err |= soap_set_value_fmt(tmp, "%u", tm->tm_sec);

	tmp = soap_add_child(dtc->msg, datec, str_pf_schema, str_sysdate_year);
	err |= soap_set_value_fmt(tmp, "%u", tm->tm_year + 1900);
	tmp = soap_add_child(dtc->msg, datec, str_pf_schema,
			     str_sysdate_month);
	err |= soap_set_value_fmt(tmp, "%u", tm->tm_mon + 1);
	tmp = soap_add_child(dtc->msg, datec, str_pf_schema, str_sysdate_day);
	err |= soap_set_value_fmt(tmp, "%u", tm->tm_mday);

	return err;
}


/**
 * add a network protocol to the soap message defiend by @proto, @port and
 * @enabled
 *
 * @param gnpr          GetNetworkProtocols response child
 * @param proto         protocol string {'HTTP', 'HTTPS', 'RTSP'}
 * @param port          port number of the protocol
 * @param enabled       is this protocol enabled
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int device_add_networkprotocol(struct soap_child *gnpr,
	const char *proto, int port, bool enabled)
{
	int err = 0;
	struct soap_child *npc, *tmp;

	if (!gnpr || !proto || (port < 0))
		return EINVAL;

	npc = soap_add_child(gnpr->msg, gnpr, str_pf_device_wsdl,
			     str_nprotos_np);

	LAST_CHILD(npc);
	tmp = soap_add_child(gnpr->msg, npc, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmp, "%s", proto);

	tmp = soap_add_child(gnpr->msg, npc, str_pf_schema, str_enabled);
	err |= soap_set_value_fmt(tmp, "%s", enabled ? str_true : str_false);

	tmp = soap_add_child(gnpr->msg, npc, str_pf_schema, str_port);
	err |= soap_set_value_fmt(tmp, "%d", port);

	return err;
}


/**
 * handle GetNetworkInterface requests
 *
 * TODO 1: search for the subnetmask length
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetNWI_h (const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *c, *cc, *netic, *addrc, *ipc, *confc;

	const char *bs_ifname = conf_config()->net.ifname;
	bool isipv4 = net_af_enabled(baresip_network() , AF_INET);
	bool isdhcpenabled;
	char mac[MAC_LEN];

	if (!msg || !prtresp)
		return EINVAL;

	err |= conf_get_bool(conf_cur(),
		str_device_config_dhcp, &isdhcpenabled);
	err |= soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	c = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_get_netinterfaces_r);
	netic = soap_add_child(resp, c, str_pf_device_wsdl,
		str_device_netinterfaces);

	err |= soap_add_parameter_str(netic, NULL, str_token,
				      strlen(str_token), bs_ifname,
				      strlen(bs_ifname));
	c = soap_add_child(resp, netic, str_pf_schema, str_enabled);
	err |= soap_set_value_fmt(c, "%s", str_true);

	c = soap_add_child(resp, netic, str_pf_schema, str_info);
	cc = soap_add_child(resp, c, str_pf_schema, str_name);
	err |= soap_set_value_fmt(cc, "%s", bs_ifname);
	cc = soap_add_child(resp, c, str_pf_schema, str_device_hwaddress);

	if ((MAC_LEN - 1) != get_mac_addr_fmt(mac, MAC_LEN, true, '-')) {
		err = EINVAL;
		goto out;
	}

	err |= soap_set_value_fmt(cc, "%s", mac);

	if (isipv4) {
		ipc = soap_add_child(resp, netic, str_pf_schema,
				     str_device_ipv4);
		cc = soap_add_child(resp, ipc, str_pf_schema, str_enabled);
		err |= soap_set_value_fmt(cc, "%s", str_true);

		confc = soap_add_child(resp, ipc, str_pf_schema,
				       str_device_config);

		if (isdhcpenabled)
			addrc = soap_add_child(resp,
				confc, str_pf_schema, str_device_fromdhcp);
		else
			addrc = soap_add_child(resp,
				confc, str_pf_schema, str_device_manual);

		cc = soap_add_child(resp, addrc, str_pf_schema,
				    str_wsd_address);
		err |= soap_set_value_fmt(cc, "%j",
			net_laddr_af(baresip_network(), AF_INET));

		cc = soap_add_child(resp, addrc, str_pf_schema,
				    str_device_prefixlen);
		err |= soap_set_value_fmt(cc, "%d", 24); /* TODO 1 */

		cc = soap_add_child(resp, confc, str_pf_schema,
				    str_device_dhcp);
		err |= soap_set_value_fmt(cc, "%s",
			isdhcpenabled ? str_true : str_false);
	}
	else {
		return ENOTSUP;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetSystemDateAndTime requests
 * TODO 1: CHECK FOR NTP
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetSystemDateAndTime_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{

	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *sdatc, *tmp;
	struct tm *timeinfo;
	time_t rawtime;
	bool daylightsavingtime;

	if (!msg || !prtresp)
		return EINVAL;

	time(&rawtime);
	timeinfo = gmtime(&rawtime);

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	sdatc = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_get_systime_r);

	sdatc = soap_add_child(resp, sdatc, str_pf_device_wsdl, str_sysdate);

	tmp = soap_add_child(resp, sdatc, str_pf_schema, str_sysdate_dtt);
	/* TODO 1 */
	err |= soap_set_value_fmt(tmp, "%s", "Manual");

	timeinfo = localtime(&rawtime);
	if (timeinfo->tm_isdst > 0)
		daylightsavingtime = true;
	else
		daylightsavingtime = false;

	tmp = soap_add_child(resp, sdatc, str_pf_schema, str_sysdate_dls);
	err |= soap_set_value_fmt(tmp, "%s",
		daylightsavingtime ? str_true : str_false);

	timeinfo = gmtime(&rawtime);
	tmp = soap_add_child(resp, sdatc, str_pf_schema,
		str_sysdate_utc);
	err |= device_add_systime(tmp, timeinfo);

	timeinfo = localtime(&rawtime);
	tmp = soap_add_child(resp, sdatc, str_pf_schema,
		str_sysdate_local);
	err |= device_add_systime(tmp, timeinfo);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetSystemDateAndTime requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetNetworkDefaultGateway_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{

	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *ngc;
	struct sa gw;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	ngc = soap_add_child(resp, b, str_pf_device_wsdl,
			     str_method_get_ndg_r);
	ngc = soap_add_child(resp, ngc, str_pf_device_wsdl, str_ndg_ng);
	ngc = soap_add_child(resp, ngc, str_pf_schema, str_profile_ipv4addr);
	err |= net_default_gateway_get(AF_INET, &gw);
	err |= soap_set_value_fmt(ngc, "%j", &gw);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetNetworkProtocols requests
 * TODO 1: Implement the support for HTTPS
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetNetworkProtocols_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gnpc;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gnpc = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_get_nprotos_r);

	err |= device_add_networkprotocol(gnpc, "HTTP", DEFAULT_ONVIF_PORT,
					  true);
	/* TODO 1 */
	err |= device_add_networkprotocol(gnpc, "HTTPS", 0, false);
	err |= device_add_networkprotocol(gnpc, "RTSP", DEFAULT_RTSP_PORT,
					  true);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetWsdlUrl requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetWsdlUrl_h(const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gwur;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gwur = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_get_wsdlurl_r);
	gwur = soap_add_child(resp, gwur, str_pf_device_wsdl, str_gwu_wsdlurl);
	err |= soap_set_value_fmt(gwur, "%s", str_gwu_onvif_docuurl);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetHostname requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_GetHostname_h(const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	char hostname[256];
	struct soap_msg *resp;
	struct soap_child *b, *ghr, *tmpc;

	if (!msg || !prtresp)
		return EINVAL;

	if (-1 == net_hostname(hostname, sizeof(hostname)))
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	ghr = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_get_hostname_r);
	ghr = soap_add_child(resp, ghr, str_pf_device_wsdl, str_gh_hi);
	tmpc = soap_add_child(resp, ghr, str_pf_schema, str_device_fromdhcp);
	err |= soap_set_value_fmt(tmpc, "%s", str_false);
	tmpc = soap_add_child(resp, ghr, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmpc, "%s", hostname);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle SystemReboot requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int device_SystemReboot_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *srrc, *msgc;

	(void)msg;

	tmr_init(&shutdown_timer);
	tmr_start(&shutdown_timer, REBOOTDELAY * 1000, shutdown_timer_h, NULL);

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_device_wsdl, str_uri_device_wsdl)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	srrc = soap_add_child(resp, b, str_pf_device_wsdl,
		str_method_systemreboot_r);
	msgc = soap_add_child(resp, srrc, str_pf_device_wsdl, str_sr_msg);

	err |= soap_set_value_fmt(msgc, "System Reboots in %d s\n",
				  REBOOTDELAY);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;

}


