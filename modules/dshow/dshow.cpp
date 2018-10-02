/**
 * @file dshow.cpp Windows DirectShow video-source
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2010 Dusan Stevanovic
 */

#include <stdio.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <commctrl.h>
#include <dshow.h>

#pragma comment(lib, "strmiids.lib")


/**
 * @defgroup dshow dshow
 *
 * Windows DirectShow video-source
 *
 *
 * References:
 *
 *    http://www.alsa-project.org/main/index.php/Main_Page
 */


#ifndef __ISampleGrabberCB_INTERFACE_DEFINED__
#define __ISampleGrabberCB_INTERFACE_DEFINED__

/* interface ISampleGrabberCB */
/* [unique][helpstring][local][uuid][object] */

EXTERN_C const IID IID_ISampleGrabberCB;

MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85")
ISampleGrabberCB : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE SampleCB(
		double SampleTime,
		IMediaSample *pSample) = 0;

	virtual HRESULT STDMETHODCALLTYPE BufferCB(
		double SampleTime,
		BYTE *pBuffer,
		long BufferLen) = 0;
};

#endif 	/* __ISampleGrabberCB_INTERFACE_DEFINED__ */


#ifndef __ISampleGrabber_INTERFACE_DEFINED__
#define __ISampleGrabber_INTERFACE_DEFINED__

/* interface ISampleGrabber */
/* [unique][helpstring][local][uuid][object] */

EXTERN_C const IID IID_ISampleGrabber;

MIDL_INTERFACE("6B652FFF-11FE-4fce-92AD-0266B5D7C78F")
ISampleGrabber : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE SetOneShot(
		BOOL OneShot) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetMediaType(
		const AM_MEDIA_TYPE *pType) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(
		AM_MEDIA_TYPE *pType) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(
		BOOL BufferThem) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(
		/* [out][in] */ long *pBufferSize,
		/* [out] */ long *pBuffer) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(
		/* [retval][out] */ IMediaSample **ppSample) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetCallback(
		ISampleGrabberCB *pCallback,
		long WhichMethodToCallback) = 0;
};

DEFINE_GUID(CLSID_SampleGrabber, 0xc1f400a0, 0x3f08, 0x11d3,
	0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37);

#endif 	/* __ISampleGrabber_INTERFACE_DEFINED__ */


class Grabber;

struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */

	ICaptureGraphBuilder2 *capture;
	IBaseFilter *grabber_filter;
	IBaseFilter *dev_filter;
	ISampleGrabber *grabber;
	IMoniker *dev_moniker;
	IGraphBuilder *graph;
	IMediaControl *mc;

	Grabber *grab;

	struct vidsz size;
	vidsrc_frame_h *frameh;
	void *arg;
};


class Grabber : public ISampleGrabberCB {
public:
	Grabber(struct vidsrc_st *st) : src(st) { }

	STDMETHOD(QueryInterface)(REFIID InterfaceIdentifier,
				  VOID** ppvObject) throw()
	{
		if (InterfaceIdentifier == IID_ISampleGrabberCB) {
			*ppvObject = (ISampleGrabberCB**) this;
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	STDMETHOD_(ULONG, AddRef)() throw()
	{
		return 2;
	}

	STDMETHOD_(ULONG, Release)() throw()
	{
		return 1;
	}

	STDMETHOD(BufferCB) (double sample_time, BYTE *buf, long buf_len)
	{
		int i, j = 0, k;
		int buf_len_RGB32 = src->size.h*src->size.w;
		uint32_t tmp_pix_RGB32;
		uint32_t *buf_RGB32;
		struct vidframe vidframe;
		uint64_t timestamp = sample_time * VIDEO_TIMEBASE;

		vidframe_init_buf(&vidframe, VID_FMT_RGB32, &src->size, buf);

		//By default in Dshow, RGB32 image orientation is bottom-up
		buf_RGB32 = (uint32_t *)buf;
		for (i = buf_len_RGB32-1 ; i > buf_len_RGB32/2; i-=1) {
			k = src->size.w*(j/src->size.w) + i%(src->size.w);
			tmp_pix_RGB32 = buf_RGB32[k];
			buf_RGB32[k] = buf_RGB32[i];
			buf_RGB32[i] = tmp_pix_RGB32;
			++j;
		}

		if (src->frameh)
			src->frameh(&vidframe, timestamp, src->arg);

		return S_OK;
	}

	STDMETHOD(SampleCB) (double sample_time, IMediaSample *samp)
	{
		return S_OK;
	}

private:
	struct vidsrc_st *src;
};


static struct vidsrc *vsrc;


static int enum_devices(struct vidsrc_st *st, const char *name,
			struct list *dev_list)
{
	ICreateDevEnum *dev_enum;
	IEnumMoniker *enum_mon;
	IMoniker *mon;
	ULONG fetched;
	HRESULT res;
	int id = 0;
	int err = 0;

	res = CoCreateInstance(CLSID_SystemDeviceEnum, NULL,
			       CLSCTX_INPROC_SERVER,
			       IID_ICreateDevEnum, (void**)&dev_enum);
	if (res != NOERROR)
		return ENOENT;

	res = dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
					      &enum_mon, 0);
	if (res != NOERROR)
		return ENOENT;

	enum_mon->Reset();
	while (enum_mon->Next(1, &mon, &fetched) == S_OK) {

		IPropertyBag *bag;
		VARIANT var;
		char dev_name[256];
		int len = 0;

		res = mon->BindToStorage(0, 0, IID_IPropertyBag,
					 (void **)&bag);
		if (!SUCCEEDED(res))
			continue;

		var.vt = VT_BSTR;
		res = bag->Read(L"FriendlyName", &var, NULL);
		if (NOERROR != res)
			continue;

		len = WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1,
					  dev_name, sizeof(dev_name),
					  NULL, NULL);

		SysFreeString(var.bstrVal);
		bag->Release();

		if (len > 0) {
			if (st) {
				if (!str_isset(name) ||
				    !str_casecmp(dev_name, name)) {
					info("dshow: got device '%s' id=%d\n",
					     name, id);
					st->dev_moniker = mon;

					return 0;
				}
			}
			else {
				err = mediadev_add(dev_list, dev_name);
				if (err) {
					return err;
				}
			}
		}

		mon->Release();
		++id;
	}

	return err;

}


static int set_available_devices(struct list* dev_list)
{
	return enum_devices(NULL, NULL, dev_list);
}


static int get_device(struct vidsrc_st *st, const char *name)
{
	int err = 0;
	bool found = false;

	if (!st)
		return EINVAL;

	err = enum_devices(st, name, NULL);

	if (err)
		return err;

	if (st->dev_moniker)
		found = true;

	return found? 0 : ENOENT;

}


static int add_sample_grabber(struct vidsrc_st *st)
{
	AM_MEDIA_TYPE mt;
	HRESULT hr;

	hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
			      IID_IBaseFilter, (void**) &st->grabber_filter);
	if (FAILED(hr))
		return ENOMEM;

	hr = st->graph->AddFilter(st->grabber_filter, L"Sample Grabber");
	if (FAILED(hr))
		return ENOMEM;

	hr = st->grabber_filter->QueryInterface(IID_ISampleGrabber,
						(void**)&st->grabber);
	if (FAILED(hr))
		return ENODEV;

	hr = st->grabber->SetCallback(st->grab, 1);
	if (FAILED(hr))
		return ENOSYS;

	memset(&mt, 0, sizeof(mt));
	mt.majortype = MEDIATYPE_Video;
	mt.subtype = MEDIASUBTYPE_RGB32;  /* XXX: try YUV420P */
	hr = st->grabber->SetMediaType(&mt);
	if (FAILED(hr))
		return ENODEV;

	st->grabber->SetOneShot(FALSE);
	st->grabber->SetBufferSamples(FALSE);

	return 0;
}


static AM_MEDIA_TYPE *free_mt(AM_MEDIA_TYPE *mt)
{
	if (!mt)
		return NULL;

	if (mt->cbFormat) {
		CoTaskMemFree((PVOID)mt->pbFormat);
	}
	if (mt->pUnk != NULL) {
		mt->pUnk->Release();
		mt->pUnk = NULL;
	}

	CoTaskMemFree((PVOID)mt);

	return NULL;
}


static int config_pin(struct vidsrc_st *st, IPin *pin)
{
	AM_MEDIA_TYPE *mt;
	AM_MEDIA_TYPE *best_mt = NULL;
	IEnumMediaTypes *media_enum = NULL;
	IAMStreamConfig *stream_conf = NULL;
	VIDEOINFOHEADER *vih;
	HRESULT hr;
	int h = st->size.h;
	int w = st->size.w;
	int rh, rw;
	int wh, rwrh;
	int best_match = 0;
	int err = 0;

	if (!pin || !st)
		return EINVAL;

	hr = pin->EnumMediaTypes(&media_enum);
	if (FAILED(hr))
		return ENODATA;

	while ((hr = media_enum->Next(1, &mt, NULL)) == S_OK) {
		if (mt->formattype != FORMAT_VideoInfo)
			continue;

		vih = (VIDEOINFOHEADER *) mt->pbFormat;
		rw = vih->bmiHeader.biWidth;
		rh = vih->bmiHeader.biHeight;

		wh = w * h;
		rwrh = rw * rh;
		if (wh == rwrh) {
			best_mt = free_mt(best_mt);
			break;
		}
		else {
			int diff = abs(rwrh - wh);

			if (best_match != 0 && diff >= best_match)
				mt = free_mt(mt);
			else {
				best_match = diff;
				free_mt(best_mt);
				best_mt = mt;
			}
		}
	}
	if (hr != S_OK)
		mt = free_mt(mt);

	if (mt == NULL && best_mt == NULL) {
		err = ENODATA;
		goto out;
	}
	if (mt == NULL)
		mt = best_mt;

	hr = pin->QueryInterface(IID_IAMStreamConfig,
				 (void **) &stream_conf);
	if (FAILED(hr)) {
		err = EINVAL;
		goto out;
	}

	vih = (VIDEOINFOHEADER *) mt->pbFormat;
	hr = stream_conf->SetFormat(mt);
	mt = free_mt(mt);
	if (FAILED(hr)) {
		err = ERANGE;
		goto out;
	}

	hr = stream_conf->GetFormat(&mt);
	if (FAILED(hr)) {
		err = EINVAL;
		goto out;
	}
	if (mt->formattype != FORMAT_VideoInfo) {
		err = EINVAL;
		goto out;
	}

	vih = (VIDEOINFOHEADER *)mt->pbFormat;
	rw = vih->bmiHeader.biWidth;
	rh = vih->bmiHeader.biHeight;

	if (w != rw || h != rh) {
		warning("dshow: config_pin: picture size missmatch: "
			      "wanted %d x %d, got %d x %d\n",
			      w, h, rw, rh);
	}
	st->size.w = rw;
	st->size.h = rh;

 out:
	if (media_enum)
		media_enum->Release();
	if (stream_conf)
		stream_conf->Release();
	free_mt(mt);

	return err;
}


static void destructor(void *arg)
{
	struct vidsrc_st *st = (struct vidsrc_st *)arg;

	if (st->mc) {
		st->mc->Stop();
		st->mc->Release();
	}

	if (st->grabber) {
		st->grabber->SetCallback(NULL, 1);
		st->grabber->Release();
	}
	if (st->grabber_filter)
		st->grabber_filter->Release();
	if (st->dev_moniker)
		st->dev_moniker->Release();
	if (st->dev_filter)
		st->dev_filter->Release();
	if (st->capture) {
		st->capture->RenderStream(&PIN_CATEGORY_CAPTURE,
					  &MEDIATYPE_Video,
					  NULL, NULL, NULL);
		st->capture->Release();
	}
	if (st->graph)
		st->graph->Release();

	delete st->grab;
}


static int alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		 struct media_ctx **ctx, struct vidsrc_prm *prm,
		 const struct vidsz *size,
		 const char *fmt, const char *dev,
		 vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	IEnumPins *pin_enum = NULL;
	IPin *pin = NULL;
	HRESULT hr;
	int err;
	(void)ctx;
	(void)errorh;

	if (!stp || !vs || !prm || !size)
		return EINVAL;

	st = (struct vidsrc_st *) mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	err = get_device(st, dev);
	if (err)
		goto out;

	st->vs = vs;

	st->size   = *size;
	st->frameh = frameh;
	st->arg    = arg;

	st->grab = new Grabber(st);
	if (!st->grab) {
		err = ENOMEM;
		goto out;
	}

	hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC,
                              IID_IGraphBuilder, (void **) &st->graph);
	if (FAILED(hr)) {
		warning("dshow: alloc: IID_IGraphBuilder failed: %ld\n", hr);
		err = ENODEV;
		goto out;
	}

	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2 , NULL,
			      CLSCTX_INPROC, IID_ICaptureGraphBuilder2,
			      (void **) &st->capture);
	if (FAILED(hr)) {
		warning("dshow: alloc: IID_ICaptureGraphBuilder2: %ld\n", hr);
		err = ENODEV;
		goto out;
	}

	hr = st->capture->SetFiltergraph(st->graph);
	if (FAILED(hr)) {
		warning("dshow: alloc: SetFiltergraph failed: %ld\n", hr);
		err = ENODEV;
		goto out;
	}

	hr = st->dev_moniker->BindToObject(NULL, NULL, IID_IBaseFilter,
				    (void **) &st->dev_filter);
	if (FAILED(hr)) {
		warning("dshow: alloc: bind to base filter failed: %ld\n", hr);
		err = ENODEV;
		goto out;
	}

	hr = st->graph->AddFilter(st->dev_filter, L"Video Capture");
	if (FAILED(hr)) {
		warning("dshow: alloc: VideoCapture failed: %ld\n", hr);
		err = ENODEV;
		goto out;
	}

	hr = st->dev_filter->EnumPins(&pin_enum);
	if (pin_enum) {
		pin_enum->Reset();
		hr = pin_enum->Next(1, &pin, NULL);
	}

	add_sample_grabber(st);
	err = config_pin(st, pin);
	pin->Release();
	if (err)
		goto out;

	hr = st->capture->RenderStream(&PIN_CATEGORY_CAPTURE,
				       &MEDIATYPE_Video,
				       st->dev_filter,
				       NULL, st->grabber_filter);
	if (FAILED(hr)) {
		warning("dshow: alloc: RenderStream failed\n");
		err = ENODEV;
		goto out;
	}

	hr = st->graph->QueryInterface(IID_IMediaControl,
				       (void **) &st->mc);
	if (FAILED(hr)) {
		warning("dshow: alloc: IMediaControl failed\n");
		err = ENODEV;
		goto out;
	}

	hr = st->mc->Run();
	if (FAILED(hr)) {
		warning("dshow: alloc: Run failed\n");
		err = ENODEV;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int module_init(void)
{
	int err;
	if (CoInitialize(NULL) != S_OK)
		return ENODATA;

	err = vidsrc_register(&vsrc, baresip_vidsrcl(),
			       "dshow", alloc, NULL);
	if (err)
		return err;

	list_init(&vsrc->dev_list);
	err = set_available_devices(&vsrc->dev_list);

	return err;
}


static int module_close(void)
{
	vsrc = (struct vidsrc *) mem_deref(vsrc);
	CoUninitialize();

	return 0;
}


extern "C" const struct mod_export DECL_EXPORTS(dshow) = {
	"dshow",
	"vidsrc",
	module_init,
	module_close
};
