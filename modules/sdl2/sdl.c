/**
 * @file sdl2/sdl.c  Simple DirectMedia Layer module for SDL v2.0
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <SDL2/SDL.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup sdl2 sdl2
 *
 * Video display using Simple DirectMedia Layer version 2 (SDL2)
 */


struct vidisp_st {
	const struct vidisp *vd;        /**< Inheritance (1st)     */
	SDL_Window *window;             /**< SDL Window            */
	SDL_Renderer *renderer;         /**< SDL Renderer          */
	SDL_Texture *texture;           /**< Texture for pixels    */
	struct vidsz size;              /**< Current size          */
	enum vidfmt fmt;                /**< Current pixel format  */
	bool fullscreen;                /**< Fullscreen flag       */
	struct tmr tmr;
	Uint32 flags;
};


static struct vidisp *vid;


static void event_handler(void *arg);


static uint32_t match_fmt(enum vidfmt fmt)
{
	switch (fmt) {

	case VID_FMT_YUV420P:	return SDL_PIXELFORMAT_IYUV;
#if SDL_VERSION_ATLEAST(2, 0, 4)
	case VID_FMT_NV12:	return SDL_PIXELFORMAT_NV12;
#endif
	case VID_FMT_RGB32:     return SDL_PIXELFORMAT_ARGB8888;
	default:		return SDL_PIXELFORMAT_UNKNOWN;
	}
}


static uint32_t chroma_step(enum vidfmt fmt)
{
	switch (fmt) {

	case VID_FMT_YUV420P:	return 2;
	case VID_FMT_NV12:	return 1;
	case VID_FMT_RGB32:     return 0;
	default:		return 0;
	}
}


static void sdl_reset(struct vidisp_st *st)
{
	if (st->texture) {
		/*SDL_DestroyTexture(st->texture);*/
		st->texture = NULL;
	}

	if (st->renderer) {
		/*SDL_DestroyRenderer(st->renderer);*/
		st->renderer = NULL;
	}

	if (st->window) {
		SDL_DestroyWindow(st->window);
		st->window = NULL;
	}
}


static void event_handler(void *arg)
{
	struct vidisp_st *st = arg;
	SDL_Event event;

	tmr_start(&st->tmr, 100, event_handler, st);

	/* NOTE: events must be checked from main thread */
	while (SDL_PollEvent(&event)) {

		if (event.type == SDL_KEYDOWN) {

			switch (event.key.keysym.sym) {

			case SDLK_f:
				/* press key 'f' to toggle fullscreen */
				st->fullscreen = !st->fullscreen;
				info("sdl: %sable fullscreen mode\n",
				     st->fullscreen ? "en" : "dis");

				if (st->fullscreen)
					st->flags |=
						SDL_WINDOW_FULLSCREEN_DESKTOP;
				else
					st->flags &=
						~SDL_WINDOW_FULLSCREEN_DESKTOP;

				SDL_SetWindowFullscreen(st->window, st->flags);
				break;

			default:
				break;
			}
		}
	}
}


static void destructor(void *arg)
{
	struct vidisp_st *st = arg;

	tmr_cancel(&st->tmr);
	sdl_reset(st);
}


static int alloc(struct vidisp_st **stp, const struct vidisp *vd,
		 struct vidisp_prm *prm, const char *dev,
		 vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	int err = 0;

	/* Not used by SDL */
	(void)dev;
	(void)resizeh;
	(void)arg;

	if (!stp || !vd)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vd = vd;
	st->fullscreen = prm ? prm->fullscreen : false;

	tmr_start(&st->tmr, 100, event_handler, st);

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int display(struct vidisp_st *st, const char *title,
		   const struct vidframe *frame, uint64_t timestamp)
{
	void *pixels;
	uint8_t *d;
	int dpitch, ret;
	unsigned i, h;
	uint32_t format;
	(void)timestamp;

	if (!st || !frame)
		return EINVAL;

	format = match_fmt(frame->fmt);
	if (format == SDL_PIXELFORMAT_UNKNOWN) {
		warning("sdl2: pixel format not supported (%s)\n",
			vidfmt_name(frame->fmt));
		return ENOTSUP;
	}

	if (!vidsz_cmp(&st->size, &frame->size) || frame->fmt != st->fmt) {
		if (st->size.w && st->size.h) {
			info("sdl: reset size:"
			     " %s %u x %u ---> %s %u x %u\n",
			     vidfmt_name(st->fmt), st->size.w, st->size.h,
			     vidfmt_name(frame->fmt),
			     frame->size.w, frame->size.h);
		}
		sdl_reset(st);
	}

	if (!st->window) {
		char capt[256];

		st->flags = SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS;

		if (st->fullscreen)
			st->flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

		if (title) {
			re_snprintf(capt, sizeof(capt), "%s - %u x %u",
				    title, frame->size.w, frame->size.h);
		}
		else {
			re_snprintf(capt, sizeof(capt), "%u x %u",
				    frame->size.w, frame->size.h);
		}

		st->window = SDL_CreateWindow(capt,
					      SDL_WINDOWPOS_CENTERED,
					      SDL_WINDOWPOS_CENTERED,
					      frame->size.w, frame->size.h,
					      st->flags);
		if (!st->window) {
			warning("sdl: unable to create sdl window: %s\n",
				SDL_GetError());
			return ENODEV;
		}

		st->size = frame->size;
		st->fmt = frame->fmt;

		SDL_RaiseWindow(st->window);
		SDL_SetWindowBordered(st->window, true);
		SDL_ShowWindow(st->window);
	}

	if (!st->renderer) {

		SDL_RendererInfo rend_info;
		Uint32 flags = 0;

		flags |= SDL_RENDERER_ACCELERATED;
		flags |= SDL_RENDERER_PRESENTVSYNC;

		st->renderer = SDL_CreateRenderer(st->window, -1, flags);
		if (!st->renderer) {
			warning("sdl: unable to create renderer: %s\n",
				SDL_GetError());
			return ENOMEM;
		}

		if (!SDL_GetRendererInfo(st->renderer, &rend_info)) {
			info("sdl: created renderer '%s'\n", rend_info.name);
		}
	}

	if (!st->texture) {

		st->texture = SDL_CreateTexture(st->renderer,
						format,
						SDL_TEXTUREACCESS_STREAMING,
						frame->size.w, frame->size.h);
		if (!st->texture) {
			warning("sdl: unable to create texture: %s\n",
				SDL_GetError());
			return ENODEV;
		}
	}

	ret = SDL_LockTexture(st->texture, NULL, &pixels, &dpitch);
	if (ret != 0) {
		warning("sdl: unable to lock texture (ret=%d)\n", ret);
		return ENODEV;
	}

	d = pixels;
	for (i=0; i<3; i++) {

		const uint8_t *s = frame->data[i];
		unsigned sz, dsz, hstep, wstep;

		if (!frame->data[i] || !frame->linesize[i])
			break;

		hstep = i==0 ? 1 : 2;
		wstep = i==0 ? 1 : chroma_step(frame->fmt);

		dsz = dpitch / wstep;
		sz  = min(frame->linesize[i], dsz);

		for (h = 0; h < frame->size.h; h += hstep) {

			memcpy(d, s, sz);

			s += frame->linesize[i];
			d += dsz;
		}
	}

	SDL_UnlockTexture(st->texture);

	/* Blit the sprite onto the screen */
	SDL_RenderCopy(st->renderer, st->texture, NULL, NULL);

	/* Update the screen! */
	SDL_RenderPresent(st->renderer);

	return 0;
}


static void hide(struct vidisp_st *st)
{
	if (!st || !st->window)
		return;

	SDL_HideWindow(st->window);
}


static int module_init(void)
{
	int err;

	if (SDL_VideoInit(NULL) < 0) {
		warning("sdl2: unable to init Video: %s\n",
			SDL_GetError());
		return ENODEV;
	}

	err = vidisp_register(&vid, baresip_vidispl(),
			      "sdl2", alloc, NULL, display, hide);
	if (err)
		return err;

	return 0;
}


static int module_close(void)
{
	vid = mem_deref(vid);

	SDL_Quit();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(sdl2) = {
	"sdl2",
	"vidisp",
	module_init,
	module_close,
};
