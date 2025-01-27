/**
 * @file wasapi/util.c Windows Audio Session API (WASAPI)
 *
 * Copyright (C) 2024 Sebastian Reimers
 * Copyright (C) 2024 AGFEO GmbH & Co. KG
 */
 #ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
 #endif
 #define CINTERFACE
 #define COBJMACROS
 #define CONST_VTABLE
 #include <initguid.h>
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <audiosessiontypes.h>
 #include <audiopolicy.h>
 #include <functiondiscoverykeys_devpkey.h>

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <re_atomic.h>

#include "wasapi.h"


int wasapi_wc_to_utf8(char **dst, LPCWSTR src)
{
	int bufsz =
		WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);

	if (!bufsz || !dst)
		return EINVAL;

	char *buf = mem_zalloc(bufsz, NULL);
	if (!buf)
		return ENOMEM;

	WideCharToMultiByte(CP_UTF8, 0, src, -1, buf, bufsz, NULL, NULL);

	*dst = buf;

	return 0;
}


int wasapi_wc_from_utf8(LPWSTR *dst, const struct pl *src)
{
	if (!src || !dst)
		return EINVAL;

	int wclen =
		MultiByteToWideChar(CP_UTF8, 0, src->p, (int)src->l, NULL, 0);

	if (wclen <= 0)
		return EMSGSIZE;

	LPWSTR buf = mem_zalloc(sizeof(wchar_t) * (wclen + 1), NULL);
	if (!buf)
		return ENOMEM;

	MultiByteToWideChar(CP_UTF8, 0, src->p, (int)src->l, buf, wclen);

	*dst = buf;

	return 0;
}
