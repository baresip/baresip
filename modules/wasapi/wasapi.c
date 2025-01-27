/**
 * @file wasapi/wasapi.c Windows Audio Session API (WASAPI)
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
#include <baresip.h>

#include "wasapi.h"


static struct auplay *auplay;
static struct ausrc *ausrc;


static int device_thread(void *arg)
{
	HRESULT hr;
	IMMDeviceEnumerator *enumerator = NULL;
	IMMDeviceCollection *devices	= NULL;
	char *dev_id_utf8		= NULL;
	char *name_utf8			= NULL;
	UINT play_dev_count		= 0;
	UINT src_dev_count		= 0;
	int err				= 0;
	(void)arg;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	CHECK_HR(hr, "wasapi/devices: CoInitializeEx failed");

	hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
			      &IID_IMMDeviceEnumerator, (void **)&enumerator);
	CHECK_HR(hr, "wasapi/devices: CoCreateInstance failed");

	hr = IMMDeviceEnumerator_EnumAudioEndpoints(
		enumerator, eRender, DEVICE_STATE_ACTIVE, &devices);
	CHECK_HR(hr, "wasapi/devices: EnumAudioEndpoints failed");

	hr = IMMDeviceCollection_GetCount(devices, &play_dev_count);
	CHECK_HR(hr, "wasapi/devices: GetCount failed");

	for (UINT i = 0; i < play_dev_count; i++) {
		IMMDevice *device     = NULL;
		LPWSTR dev_id	      = NULL;
		IPropertyStore *store = NULL;
		PROPVARIANT name;

		hr = IMMDeviceCollection_Item(devices, i, &device);
		CHECK_HR(hr, "wasapi/devices: Item failed");

		hr = IMMDevice_GetId(device, &dev_id);
		CHECK_HR(hr, "wasapi/devices: GetId failed");

		hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &store);
		CHECK_HR(hr, "wasapi/devices: OpenPropertyStore failed");

		PropVariantInit(&name);
		hr = IPropertyStore_GetValue(store, &PKEY_Device_FriendlyName,
					     &name);
		CHECK_HR(hr, "wasapi/devices: Store GetValue failed");

		err = wasapi_wc_to_utf8(&dev_id_utf8, dev_id);
		if (err)
			goto out;

		err = wasapi_wc_to_utf8(&name_utf8, name.pwszVal);
		if (err)
			goto out;

		info("wasapi/device/play: %s (%s)\n", name_utf8, dev_id_utf8);

		PropVariantClear(&name);
		IPropertyStore_Release(store);
		CoTaskMemFree(dev_id);
		IMMDevice_Release(device);

		err = mediadev_add(&auplay->dev_list, dev_id_utf8);
		if (err)
			goto out;

		dev_id_utf8 = mem_deref(dev_id_utf8);
		name_utf8 = mem_deref(name_utf8);
	}

	if (devices)
		IMMDeviceCollection_Release(devices);

	hr = IMMDeviceEnumerator_EnumAudioEndpoints(
		enumerator, eCapture, DEVICE_STATE_ACTIVE, &devices);
	CHECK_HR(hr, "wasapi/devices: EnumAudioEndpoints failed");

	hr = IMMDeviceCollection_GetCount(devices, &src_dev_count);
	CHECK_HR(hr, "wasapi/devices: GetCount failed");

	for (UINT i = 0; i < src_dev_count; i++) {
		IMMDevice *device     = NULL;
		LPWSTR dev_id	      = NULL;
		IPropertyStore *store = NULL;
		PROPVARIANT name;

		hr = IMMDeviceCollection_Item(devices, i, &device);
		CHECK_HR(hr, "wasapi/devices: Item failed");

		hr = IMMDevice_GetId(device, &dev_id);
		CHECK_HR(hr, "wasapi/devices: GetId failed");

		hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &store);
		CHECK_HR(hr, "wasapi/devices: OpenPropertyStore failed");

		PropVariantInit(&name);
		hr = IPropertyStore_GetValue(store, &PKEY_Device_FriendlyName,
					     &name);
		CHECK_HR(hr, "wasapi/devices: Store GetValue failed");

		err = wasapi_wc_to_utf8(&dev_id_utf8, dev_id);
		if (err)
			goto out;

		err = wasapi_wc_to_utf8(&name_utf8, name.pwszVal);
		if (err)
			goto out;

		info("wasapi/device/src: %s (%s)\n", name_utf8, dev_id_utf8);

		PropVariantClear(&name);
		IPropertyStore_Release(store);
		CoTaskMemFree(dev_id);
		IMMDevice_Release(device);

		err = mediadev_add(&ausrc->dev_list, dev_id_utf8);
		if (err)
			goto out;

		dev_id_utf8 = mem_deref(dev_id_utf8);
		name_utf8 = mem_deref(name_utf8);
	}

	info("wasapi: output devices: %d, input devices: %d\n", play_dev_count,
	     src_dev_count);

out:
	dev_id_utf8 = mem_deref(dev_id_utf8);
	name_utf8 = mem_deref(name_utf8);

	if (devices)
		IMMDeviceCollection_Release(devices);
	if (enumerator)
		IMMDeviceEnumerator_Release(enumerator);
	CoUninitialize();

	return err;
}


static int wasapi_init(void)
{
	int err;
	thrd_t thread;

	err = ausrc_register(&ausrc, baresip_ausrcl(), "wasapi",
			     wasapi_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(), "wasapi",
			       wasapi_play_alloc);
	if (err)
		return err;

	err = thread_create_name(&thread, "wasapi_devices", device_thread,
				 NULL);
	thrd_join(thread, &err);

	return err;
}


static int wasapi_close(void)
{
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(wasapi) = {
	"wasapi", "sound", wasapi_init, wasapi_close};
