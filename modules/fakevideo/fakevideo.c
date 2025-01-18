/**
 * @file fakevideo.c Fake video source and video display
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <re_atomic.h>
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
	struct vidframe *frame;
	thrd_t thread;
	RE_ATOMIC bool run;
	uint64_t ts;
	double fps;
	vidsrc_frame_h *frameh;
	void *arg;
};

struct vidisp_st {
	int dummy;
};


static struct vidsrc *vidsrc;
static struct vidisp *vidisp;


static void process_frame(struct vidsrc_st *st)
{
	st->ts += (uint64_t)(VIDEO_TIMEBASE / st->fps);

	st->frameh(st->frame, st->ts, st->arg);
}


static int read_thread(void *arg)
{
	struct vidsrc_st *st = arg;

	st->ts = tmr_jiffies_usec();

	while (re_atomic_rlx(&st->run)) {

		if (tmr_jiffies_usec() < st->ts) {
			sys_msleep(4);
			continue;
		}

		process_frame(st);
	}

	return 0;
}


static void src_destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	/* Wait for termination of other thread */
	if (re_atomic_rlx(&st->run)) {
		debug("fakevideo: stopping read thread\n");
		re_atomic_rlx_set(&st->run, false);
		thrd_join(st->thread, NULL);
	}

	mem_deref(st->frame);
}


static void disp_destructor(void *arg)
{
	struct vidisp_st *st = arg;
	(void)st;
}


static int src_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		     struct vidsrc_prm *prm,
		     const struct vidsz *size, const char *fmt,
		     const char *dev, vidsrc_frame_h *frameh,
		     vidsrc_packet_h *packeth,
		     vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	unsigned x;
	int err;

	(void)fmt;
	(void)dev;
	(void)packeth;
	(void)errorh;
	(void)vs;

	if (!stp || !prm || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), src_destructor);
	if (!st)
		return ENOMEM;

	st->fps    = prm->fps;
	st->frameh = frameh;
	st->arg    = arg;

	err = vidframe_alloc(&st->frame, prm->fmt, size);
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

	re_atomic_rlx_set(&st->run, true);
	err = thread_create_name(&st->thread, "fakevideo", read_thread, st);
	if (err) {
		re_atomic_rlx_set(&st->run, false);
		goto out;
	}

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
