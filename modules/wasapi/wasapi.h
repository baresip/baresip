/**
 * @file wasapi/wasapi.h Windows Audio Session API (WASAPI)
 *
 * Copyright (C) 2024 Sebastian Reimers
 * Copyright (C) 2024 AGFEO GmbH & Co. KG
 */

#define REF_PER_MS 10000LL
#define CHECK_HR(hr, msg)                                                     \
	if (FAILED((hr))) {                                                   \
		warning("%s: 0x%08x\n", (msg), (hr));                         \
		err = ENODATA;                                                \
		goto out;                                                     \
	}

#ifndef __MINGW32__
static const CLSID CLSID_MMDeviceEnumerator = {
	0xbcde0395,
	0xe52f,
	0x467c,
	{0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};

static const IID IID_IMMDeviceEnumerator = {
	/* MIDL_INTERFACE("A95664D2-9614-4F35-A746-DE8DB63617E6") */
	0xa95664d2,
	0x9614,
	0x4f35,
	{0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};

static const IID IID_IAudioClient = {
	/* MIDL_INTERFACE("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2") */
	0x1cb9ad4c,
	0xdbfa,
	0x4c32,
	{0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};

static const IID IID_IAudioRenderClient = {
	/* MIDL_INTERFACE("F294ACFC-3146-4483-A7BF-ADDCA7C260E2") */
	0xf294acfc,
	0x3146,
	0x4483,
	{0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}};

static const IID IID_IAudioCaptureClient = {
	//MIDL_INTERFACE("C8ADBD64-E71E-48a0-A4DE-185C395CD317")
	0xc8adbd64,
	0xe71e,
	0x48a0,
	{0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17}};
#endif

int wasapi_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		      struct auplay_prm *prm, const char *device,
		      auplay_write_h *wh, void *arg);
int wasapi_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int wasapi_wc_to_utf8(char **dst, LPCWSTR src);
int wasapi_wc_from_utf8(LPWSTR *dst, const struct pl *src);
