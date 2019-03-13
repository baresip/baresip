/**
 * @file deviceio.h
 *
 * For more information see:
 * https://www.onvif.org/specs/srv/io/ONVIF-DeviceIo-Service-Spec.pdf
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#ifndef _ONVIF_DEVICEIO_H_
#define _ONVIF_DEVICEIO_H_

struct soap_msg;

int deviceio_GetVideoSources_h(const struct soap_msg *msg,
	struct soap_msg **prtresp);
int deviceio_GetAudioSources_h(const struct soap_msg *msg,
	struct soap_msg **prtresp);
int deviceio_GetAudioOutputs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp);

#endif /* _ONVIF_DEVICEIO_H_ */

