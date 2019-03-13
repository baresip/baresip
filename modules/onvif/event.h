/* @file event.h
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#ifndef _ONVIF_EVENT_H_
#define _ONVIF_EVENT_H_

int event_GetEventProperties_h(const struct soap_msg *msg,
	struct soap_msg **prtresp);

#endif /* _ONVIF_EVENT_H */