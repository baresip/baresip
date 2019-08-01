/**
 * @file mock/mock_vidsrc.c Mock video source
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "../test.h"


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */

	struct vidframe *frame;
	struct tmr tmr;
	uint64_t timestamp;
	double fps;
	vidsrc_frame_h *frameh;
	void *arg;
};


static void tmr_handler(void *arg)
{
	struct vidsrc_st *st = arg;

	tmr_start(&st->tmr, 1000/st->fps, tmr_handler, st);

	if (st->frameh)
		st->frameh(st->frame, st->timestamp, st->arg);

	st->timestamp += VIDEO_TIMEBASE / st->fps;
}


static void vidsrc_destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	tmr_cancel(&st->tmr);
	mem_deref(st->frame);
}


static int mock_vidsrc_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
			     struct media_ctx **ctx, struct vidsrc_prm *prm,
			     const struct vidsz *size, const char *fmt,
			     const char *dev, vidsrc_frame_h *frameh,
			     vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err = 0;
	(void)ctx;
	(void)fmt;
	(void)dev;
	(void)errorh;

	if (!stp || !prm || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), vidsrc_destructor);
	if (!st)
		return ENOMEM;

	st->vs     = vs;
	st->fps    = prm->fps;
	st->frameh = frameh;
	st->arg    = arg;

	err = vidframe_alloc(&st->frame, prm->fmt, size);
	if (err)
		goto out;

	tmr_start(&st->tmr, 0, tmr_handler, st);

	info("mock_vidsrc: new instance with size %u x %u (%.2f fps)\n",
	     size->w, size->h, prm->fps);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


int mock_vidsrc_register(struct vidsrc **vidsrcp)
{
	return vidsrc_register(vidsrcp, baresip_vidsrcl(), "mock-vidsrc",
			       mock_vidsrc_alloc, NULL);
}
