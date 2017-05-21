/**
 * @file quicktime.c  Quicktime video-source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <pthread.h>
#include <re.h>
#include <QuickTime/QuickTimeComponents.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <baresip.h>


/* this module is deprecated, in favour of qtcapture or avcapture */


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */
	pthread_t thread;
	pthread_mutex_t mutex;
	struct vidsz sz;
	SeqGrabComponent seq_grab;
	SGDataUPP upp;
	SGChannel ch;
	struct mbuf *buf;
	struct SwsContext *sws;
	vidsrc_frame_h *frameh;
	void *arg;
	bool run;
};


static struct vidsrc *vidsrc;


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->seq_grab) {

		pthread_mutex_lock(&st->mutex);
		SGStop(st->seq_grab);
		pthread_mutex_unlock(&st->mutex);

		if (st->run) {
			pthread_join(st->thread, NULL);
		}

		if (st->upp) {
			DisposeSGDataUPP(st->upp);
		}

		if (st->ch) {
			SGDisposeChannel(st->seq_grab, st->ch);
		}

		CloseComponent(st->seq_grab);
	}

	if (st->sws)
		sws_freeContext(st->sws);

	mem_deref(st->buf);
}


static OSErr frame_handler(SGChannel c, Ptr p, long len, long *offset,
			   long chRefCon, TimeValue timeval, short writeType,
			   long refCon)
{
	struct vidsrc_st *st = (struct vidsrc_st *)refCon;
	ImageDescriptionHandle imageDesc;
	AVPicture pict_src, pict_dst;
	struct vidframe vidframe;
	ComponentResult result;
	int i, ret;
	int new_len;
	(void)c;
	(void)p;
	(void)len;
	(void)offset;
	(void)chRefCon;
	(void)timeval;
	(void)writeType;
	(void)refCon;

	if (!st->buf) {

		imageDesc = (ImageDescriptionHandle)NewHandle(0);
		if (!imageDesc)
			return noErr;

		result = SGGetChannelSampleDescription(c,(Handle)imageDesc);
		if (result != noErr) {
			warning("quicktime: GetChanSampDesc: %d\n", result);
			DisposeHandle((Handle)imageDesc);
			return noErr;
		}

		st->sz.w = (*imageDesc)->width;
		st->sz.h = (*imageDesc)->height;

		/* buffer size after scaling */
		new_len = avpicture_get_size(PIX_FMT_YUV420P,
					     st->sz.w, st->sz.h);

#if 1
		re_fprintf(stderr, "got frame len=%u (%ux%u) [%s] depth=%u\n",
			   len, st->sz.w, st->sz.h,
			   (*imageDesc)->name, (*imageDesc)->depth);
#endif

		DisposeHandle((Handle)imageDesc);

		st->buf = mbuf_alloc(new_len);
		if (!st->buf)
			return noErr;
	}

	if (!st->sws) {
		st->sws = sws_getContext(st->sz.w, st->sz.h, PIX_FMT_YUYV422,
					 st->sz.w, st->sz.h, PIX_FMT_YUV420P,
					 SWS_BICUBIC, NULL, NULL, NULL);
		if (!st->sws)
			return noErr;
	}

	avpicture_fill(&pict_src, (uint8_t *)p, PIX_FMT_YUYV422,
		       st->sz.w, st->sz.h);

	avpicture_fill(&pict_dst, st->buf->buf, PIX_FMT_YUV420P,
		       st->sz.w, st->sz.h);

	ret = sws_scale(st->sws,
			pict_src.data, pict_src.linesize, 0, st->sz.h,
			pict_dst.data, pict_dst.linesize);

	if (ret <= 0) {
		re_fprintf(stderr, "scale: sws_scale: returned %d\n", ret);
		return noErr;
	}

	for (i=0; i<4; i++) {
		vidframe.data[i]     = pict_dst.data[i];
		vidframe.linesize[i] = pict_dst.linesize[i];
	}

	vidframe.size  = st->sz;
	vidframe.valid = true;

	st->frameh(&vidframe, st->arg);

	return noErr;
}


static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;
	ComponentResult result;

	for (;;) {
		pthread_mutex_lock(&st->mutex);
		result = SGIdle(st->seq_grab);
		pthread_mutex_unlock(&st->mutex);

		if (result != noErr)
			break;

		usleep(10000);
	}

	return NULL;
}


static int alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		 struct media_ctx **ctx,
		 struct vidsrc_prm *prm, const char *fmt,
		 const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	ComponentResult result;
	Rect rect;
	struct vidsrc_st *st;
	int err;

	(void)ctx;
	(void)fmt;
	(void)dev;
	(void)errorh;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs     = vs;
	st->frameh = frameh;
	st->arg    = arg;

	err = pthread_mutex_init(&st->mutex, NULL);
	if (err) {
		re_fprintf(stderr, "mutex error: %s\n", strerror(err));
		goto out;
	}

	st->seq_grab = OpenDefaultComponent(SeqGrabComponentType, 0);
	if (!st->seq_grab) {
		re_fprintf(stderr, "Unable to open component\n");
		err = ENODEV;
		goto out;
	}

	result = SGInitialize(st->seq_grab);
	if (result != noErr) {
		re_fprintf(stderr, "Unable to initialize sequence grabber\n");
		err = ENODEV;
		goto out;
	}

	result = SGSetGWorld(st->seq_grab, NULL, NULL);
	if (result != noErr) {
		re_fprintf(stderr, "Unable to set gworld\n");
		err = ENODEV;
		goto out;
	}

	result = SGSetDataRef(st->seq_grab, 0, 0, seqGrabDontMakeMovie);
	if (result != noErr) {
		re_fprintf(stderr, "Unable to set data ref\n");
		err = ENODEV;
		goto out;
	}

	result = SGNewChannel(st->seq_grab, VideoMediaType, &st->ch);
	if (result != noErr) {
		re_fprintf(stderr, "Unable to allocate channel (result=%d)\n",
			   result);
		err = ENOMEM;
		goto out;
	}

	/* XXX: check flags */
	result = SGSetChannelUsage(st->ch,
				   seqGrabRecord |
				   seqGrabLowLatencyCapture);
	if (result != noErr) {
		re_fprintf(stderr, "Unable to set channel usage\n");
		err = ENODEV;
		goto out;
	}

	rect.top    = 0;
	rect.left   = 0;
	rect.bottom = prm->size.h;
	rect.right  = prm->size.w;

	result = SGSetChannelBounds(st->ch, &rect);
	if (result != noErr) {
		re_fprintf(stderr, "Unable to set channel bounds\n");
		err = ENODEV;
		goto out;
	}

	st->upp = NewSGDataUPP(frame_handler);
	if (!st->upp) {
		re_fprintf(stderr, "Unable to allocate data upp\n");
		err = ENOMEM;
		goto out;
	}

	result = SGSetDataProc(st->seq_grab, st->upp, (long)st);
	if (result != noErr) {
		re_fprintf(stderr, "Unable to sg callback\n");
		err = ENODEV;
		goto out;
	}

	result = SGStartRecord(st->seq_grab);
	if (result != noErr) {
		re_fprintf(stderr, "Unable to start record: %d\n", result);
		err = ENODEV;
		goto out;
	}

	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		re_fprintf(stderr, "thread error: %s\n", strerror(err));
		goto out;
	}

	st->run = true;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;
	return err;
}


static int qt_init(void)
{
	return vidsrc_register(&vidsrc, baresip_vidsrcl(),
			       "quicktime", alloc, NULL);
}


static int qt_close(void)
{
	vidsrc = mem_deref(vidsrc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(quicktime) = {
	"quicktime",
	"videosrc",
	qt_init,
	qt_close
};
