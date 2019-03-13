/* @file ptz.h
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#ifndef _ONVIF_PTZ_H_
#define _ONVIF_PTZ_H_

int ptz_GetConfigurations_h (const struct soap_msg *msg,
	struct soap_msg **prtresp);
int ptz_GetNodes_h(const struct soap_msg *msg, struct soap_msg **prtresp);


#endif /* _ONVIF_PTZ_H */

