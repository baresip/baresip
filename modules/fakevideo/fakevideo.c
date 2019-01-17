/**
 * @file fakevideo.c Fake video source and video display
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <unistd.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup fakevideo fakevideo
 *
 * Fake video source and display module
 *
 * This module can be used to generate fake video input frames, and to
 * send output video frames to a fake non-existant display.
 *
 * Example config:
 \verbatim
  video_source    fakevideo,nil
  video_display   fakevideo,nil
 \endverbatim
 */


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */
	struct vidframe *frame;
#ifdef HAVE_PTHREAD
	pthread_t thread;
	bool run;
#else
	struct tmr tmr;
#endif
	uint64_t ts;
	double fps;
	vidsrc_frame_h *frameh;
	void *arg;
};

struct vidisp_st {
	const struct vidisp *vd;  /* inheritance */
};


static struct vidsrc *vidsrc;
static struct vidisp *vidisp;


static void process_frame(struct vidsrc_st *st)
{
	st->ts += (VIDEO_TIMEBASE / st->fps);

	st->frameh(st->frame, st->ts, st->arg);
}


#ifdef HAVE_PTHREAD
static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;

	st->ts = tmr_jiffies_usec();

	while (st->run) {

		if (tmr_jiffies_usec() < st->ts) {
			sys_msleep(4);
			continue;
		}

		process_frame(st);
	}

	return NULL;
}
#else
static void tmr_handler(void *arg)
{
	struct vidsrc_st *st = arg;
	const uint64_t now = tmr_jiffies_usec();

	tmr_start(&st->tmr, 4, tmr_handler, st);

	if (!st->ts)
		st->ts = now;

	if (now >= st->ts) {
		process_frame(st);
	}
}
#endif


static void src_destructor(void *arg)
{
	struct vidsrc_st *st = arg;

#ifdef HAVE_PTHREAD
	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}
#else
	tmr_cancel(&st->tmr);
#endif

	mem_deref(st->frame);
}


static void disp_destructor(void *arg)
{
	struct vidisp_st *st = arg;
	(void)st;
}


static int src_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		     struct media_ctx **ctx, struct vidsrc_prm *prm,
		     const struct vidsz *size, const char *fmt,
		     const char *dev, vidsrc_frame_h *frameh,
		     vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	unsigned x;
	int err;

	(void)ctx;
	(void)fmt;
	(void)dev;
	(void)errorh;

	if (!stp || !prm || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), src_destructor);
	if (!st)
		return ENOMEM;

	st->vs     = vs;
	st->fps    = prm->fps;
	st->frameh = frameh;
	st->arg    = arg;

	err = vidframe_alloc(&st->frame, VID_FMT_YUV420P, size);
	if (err)
		goto out;

	/* Pattern of three vertical bars in RGB */
	for (x=0; x<size->w; x++) {

		uint8_t r=0, g=0, b=0;

		if (x < size->w/3)
			r = 255;
		else if (x < size->w*2/3)
			g = 255;
		else
			b = 255;

		vidframe_draw_vline(st->frame, x, 0, size->h, r, g, b);
	}

#ifdef HAVE_PTHREAD
	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}
#else
	tmr_start(&st->tmr, 1, tmr_handler, st);
#endif

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int disp_alloc(struct vidisp_st **stp, const struct vidisp *vd,
		      struct vidisp_prm *prm, const char *dev,
		      vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	(void)prm;
	(void)dev;
	(void)resizeh;
	(void)arg;

	if (!stp || !vd)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), disp_destructor);
	if (!st)
		return ENOMEM;

	st->vd = vd;

	*stp = st;

	return 0;
}


static int display(struct vidisp_st *st, const char *title,
		   const struct vidframe *frame, uint64_t timestamp)
{
	(void)st;
	(void)title;
	(void)frame;
	(void)timestamp;

	return 0;
}


static int module_init(void)
{
	int err = 0;
	err |= vidsrc_register(&vidsrc, baresip_vidsrcl(),
			       "fakevideo", src_alloc, NULL);
	err |= vidisp_register(&vidisp, baresip_vidispl(),
			       "fakevideo", disp_alloc, NULL,
			       display, NULL);
	return err;
}


static int module_close(void)
{
	vidsrc = mem_deref(vidsrc);
	vidisp = mem_deref(vidisp);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(fakevideo) = {
	"fakevideo",
	"fakevideo",
	module_init,
	module_close
};
