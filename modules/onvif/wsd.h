/* @file wsd.h
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */

#ifndef _WS_DISCOVERY_H_
#define _WS_DISCOVERY_H_


#define SOAP_BC_IP4 "239.255.255.250"
#define SOAP_BC_IP6 "FF02::C"
#define SOAP_BC_PORT 3702
#define DEFAULT_ONVIF_PORT 8080

int wsd_init(void);
int wsd_deinit(void);
int wsd_probe(const struct soap_msg *msg, struct soap_msg **presponse);
int wsd_resolve(const struct soap_msg *msg, struct soap_msg **presponse);
int wsd_GetDiscoverable(const struct soap_msg *msg,
	struct soap_msg **presponse);
int wsd_SetDiscoverable(const struct soap_msg *msg,
	struct soap_msg **presponse);

#endif /* _WS_DISCOVERY_H_ */

