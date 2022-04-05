/**
 * @file device.c
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "soap.h"
#include "fault.h"
#include "soap_str.h"

#define DEBUG_MODULE "onvif_fault"
#define DEBUG_LEVEL 6
#include <re_dbg.h>

static const char *fc_str[] = {
	"VersionMismatch",
	"MustUnderstand",
	"DataEncodingUnknown",
	"Sender",
	"Receiver",
};

static const char *fs_str[] = {
	"",
	"WellFormed",
	"TagMismatch",
	"Tag",
	"Namespace",
	"MissingAttr",
	"ProhibAttr",
	"InvalidArgs",
	"InvalidArgVal",
	"UnknownAction",
	"OperationProhibited",
	"NotAuthorized",
	"ActionNotSupported",
	"Action",
	"OutofMemory",
	"CriticalError",
	"NoProfile",
	"NoSuchService",
	"AudioNotSupported",
	"AudioOutputNotSupported",
	"InvalidStreamSetup",
	"NoConfig",
	"ConfigModify",
	"NoVideoSource",
	"EmptyScope",
	"TooManyScopes",
	"ProfileExists",
	"MaxNVTProfiles",
	"DeletionOfFixedProfile",
	"FixedScope",
	"NoScope",
};


void fault_clear(struct soap_fault *sf)
{
	sf->is_set = false;
}

void fault_set(struct soap_fault *sf, enum fault_code c, enum fault_subcode sc,
	enum fault_subcode sc2, const char *reason)
{
	sf->is_set = true;
	sf->c = c;
	sf->sc = sc;
	sf->sc2 = sc2;
	sf->r = reason;
}

int fault_create(const struct soap_msg *msg, struct soap_msg **ptrresp,
	struct soap_fault *sf)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *f, *c, *val, *sc, *sc2, *r, *txt, *b;

	if (!sf->is_set)
		return 0;

	if (!msg || !ptrresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_error, str_uri_error) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	f = soap_add_child(resp, b, str_pf_envelope, str_fault);
	c = soap_add_child(resp, f, str_pf_envelope, str_fault_code);
	val = soap_add_child(resp, c, str_pf_envelope, str_fault_value);
	err |= soap_set_value_fmt(val, "%s:%s", str_pf_envelope,
				  fc_str[sf->c]);

	sc = soap_add_child(resp, c, str_pf_envelope, str_fault_subcode);
	val = soap_add_child(resp, sc, str_pf_envelope, str_fault_value);
	err |= soap_set_value_fmt(val, "%s:%s", str_pf_error, fs_str[sf->sc]);
	if (sf->sc2 != FS_MAX) {
		sc2 = soap_add_child(resp, sc, str_pf_envelope,
				     str_fault_subcode);
		val = soap_add_child(resp, sc2, str_pf_envelope,
				     str_fault_value);
		err |= soap_set_value_fmt(val, "%s:%s", str_pf_error,
					  fs_str[sf->sc2]);
	}

	r = soap_add_child(resp, f, str_pf_envelope, str_fault_reason);
	txt = soap_add_child(resp, r, str_pf_envelope, str_fault_text);
	err |= soap_add_parameter_str(txt, NULL,
		str_fault_lang, strlen(str_fault_lang),
		str_fault_lang_en, strlen(str_fault_lang_en));
	err |= soap_set_value_fmt(txt, "%s", sf->r);

  out:
	if (err)
		mem_deref(resp);
	else
		*ptrresp = resp;

	return err;
}
