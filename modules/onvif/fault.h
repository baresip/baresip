/* @file fault.h
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */

#ifndef _ONVIF_FAULT_H_
#define _ONVIF_FAULT_H_

struct soap_msg;

enum fault_code {
	FC_VersionMismatch,
	FC_MustUnderstand,
	FC_DataEncodingUnknown,
	FC_Sender,
	FC_Receiver,
};

enum fault_subcode {
	FS_None,
	FS_WellFormed,
	FS_TagMismatch,
	FS_Tag,
	FS_Namespace,
	FS_MissingAttr,
	FS_ProhibAttr,
	FS_InvalidArgs,
	FS_InvalidArgVal,
	FS_UnknownAction,
	FS_OperationProhibited,
	FS_NotAuthorized,
	FS_ActionNotSupported,
	FS_Action,
	FS_OutofMemory,
	FS_CriticalError,
	FS_NoProfile,
	FS_NoSuchService,
	FS_AudioNotSupported,
	FS_AudioOutputNotSupported,
	FS_InvalidStreamSetup,
	FS_NoConfig,
	FS_ConfigModify,
	FS_NoVideoSource,
	FS_EmptyScope,
	FS_TooManyScopes,
	FS_ProfilExists,
	FS_MaxNVTProfiles,
	FS_DeletionOfFixedProfile,
	FS_FixedScope,
	FS_NoScope,

	FS_MAX,
};

struct soap_fault {
	bool is_set;
	enum fault_code c;
	enum fault_subcode sc;
	enum fault_subcode sc2;
	const char *r;
};

void fault_clear(struct soap_fault *sf);
void fault_set(struct soap_fault *sf, enum fault_code c, enum fault_subcode sc,
	enum fault_subcode sc2, const char *reason);
int fault_create(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *sf);


#endif /* _ONVIF_FAULT_H */

