/**
 * @file sdl/sdl.c  Simple DirectMedia Layer module for SDL v2.0
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <SDL2/SDL.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup sdl sdl
 *
 * Video display using Simple DirectMedia Layer version 2 (SDL2)
 */


struct vidisp_st {
	SDL_Window *window;             /**< SDL Window            */
	SDL_Renderer *renderer;         /**< SDL Renderer          */
	SDL_Texture *texture;           /**< Texture for pixels    */
	struct vidsz size;              /**< Current size          */
	enum vidfmt fmt;                /**< Current pixel format  */
	bool fullscreen;                /**< Fullscreen flag       */

	struct mqueue *mq;
	Uint32 flags;
	bool quit;
	int inited;
};


static struct vidisp *vid = NULL;


static uint32_t match_fmt(enum vidfmt fmt)
{
	switch (fmt) {

	case VID_FMT_YUV420P:	return SDL_PIXELFORMAT_IYUV;
	case VID_FMT_YUYV422:   return SDL_PIXELFORMAT_YUY2;
	case VID_FMT_UYVY422:   return SDL_PIXELFORMAT_UYVY;
	case VID_FMT_NV12:	return SDL_PIXELFORMAT_NV12;
	case VID_FMT_NV21:	return SDL_PIXELFORMAT_NV21;
	case VID_FMT_RGB32:     return SDL_PIXELFORMAT_ARGB8888;
	default:		return SDL_PIXELFORMAT_UNKNOWN;
	}
}


static uint32_t chroma_step(enum vidfmt fmt)
{
	switch (fmt) {

	case VID_FMT_YUV420P:	return 2;
	case VID_FMT_NV12:	return 1;
	case VID_FMT_NV21:	return 1;
	case VID_FMT_RGB32:     return 0;
	default:		return 0;
	}
}


static void sdl_reset(struct vidisp_st *st)
{
	if (st->texture) {
		SDL_DestroyTexture(st->texture);
		st->texture = NULL;
	}

	if (st->renderer) {
		SDL_DestroyRenderer(st->renderer);
		st->renderer = NULL;
	}

	if (st->window) {
		SDL_DestroyWindow(st->window);
		st->window = NULL;
	}

	if (!st->inited) {
		info(".. SDL_Quit\n");
		SDL_Quit();
	}
}


static void destructor(void *arg)
{
	struct vidisp_st *st = arg;

	sdl_reset(st);

	/* needed to close the window */
	SDL_PumpEvents();

	mem_deref(st->mq);
}


static void mqueue_handler(int id, void *data, void *arg)
{
	(void)data;
	(void)arg;

	ui_input_key(baresip_uis(), id, NULL);
}


/* NOTE: should be called from the main thread */
static int alloc(struct vidisp_st **stp, const struct vidisp *vd,
		 struct vidisp_prm *prm, const char *dev,
		 vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	int err;

	/* Not used by SDL */
	(void)dev;
	(void)resizeh;
	(void)arg;

	if (!stp || !vd)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->fullscreen = prm ? prm->fullscreen : false;

	err = mqueue_alloc(&st->mq, mqueue_handler, st);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int write_header(struct vidisp_st *st, const char *title,
			const struct vidsz *size, uint32_t format)
{
	if (SDL_WasInit(SDL_INIT_VIDEO)) {
		warning("SDL video subsystem was already inited,"
			" you could have multiple SDL outputs."
			" This may cause unknown behaviour.\n");
		st->inited = 1;
	}

	/* initialization */
	if (!st->inited){

		if (SDL_Init(SDL_INIT_VIDEO) != 0) {
			warning("Unable to initialize SDL: %s\n",
				SDL_GetError());
			return ENOTSUP;
		}
	}

	if (!st->window) {
		char capt[256];

		st->flags  = SDL_WINDOW_HIDDEN;
		st->flags |= SDL_WINDOW_RESIZABLE;

		if (st->fullscreen)
			st->flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

		if (title) {
			re_snprintf(capt, sizeof(capt), "%s - %u x %u",
				    title, size->w, size->h);
		}
		else {
			re_snprintf(capt, sizeof(capt), "%u x %u",
				    size->w, size->h);
		}

		if (SDL_CreateWindowAndRenderer(size->w, size->h, st->flags,
						&st->window,
						&st->renderer) != 0) {

			warning("Couldn't create window and renderer: %s\n",
				SDL_GetError());
			return ENOTSUP;
		}

		SDL_SetWindowTitle(st->window, capt);
		SDL_SetWindowPosition(st->window,
				      SDL_WINDOWPOS_CENTERED,
				      SDL_WINDOWPOS_CENTERED);
		SDL_ShowWindow(st->window);

		st->size = *size;
	}

	if (!st->texture) {

		st->texture = SDL_CreateTexture(st->renderer,
						format,
						SDL_TEXTUREACCESS_STREAMING,
						size->w, size->h);
		if (!st->texture) {
			warning("sdl: unable to create texture: %s\n",
				SDL_GetError());
			return ENODEV;
		}

	}

	st->inited = 1;

	return 0;
}


static void poll_events(struct vidisp_st *st)
{
	SDL_Event event;

	if (!SDL_PollEvent(&event))
		return;

	switch (event.type) {

	case SDL_KEYDOWN:

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

		case SDLK_q:
			mqueue_push(st->mq, 'q', NULL);
			break;

		default:
			break;
		}

		break;

	case SDL_QUIT:
		info(".. QUIT\n");
		st->quit = 1;
		break;

	default:
		break;
	}
}


static int display(struct vidisp_st *st, const char *title,
		   const struct vidframe *frame, uint64_t timestamp)
{
	void *pixels;
	uint8_t *d;
	int dpitch, ret;
	unsigned i, h;
	uint32_t format;
	int err;

	(void)timestamp;

	if (!st || !frame)
		return EINVAL;

	if (st->quit)
		return ENODEV;

	format = match_fmt(frame->fmt);
	if (format == SDL_PIXELFORMAT_UNKNOWN) {
		warning("sdl: pixel format not supported (%s)\n",
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

		err = write_header(st, title, &frame->size, format);
		if (err)
			return err;

		st->fmt = frame->fmt;
	}

	/* NOTE: poll events first */
	poll_events(st);

	if (st->quit) {
		sdl_reset(st);
		return ENODEV;
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

		if (wstep == 0)
			continue;

		dsz = dpitch / wstep;
		sz  = min(frame->linesize[i], dsz);

		for (h = 0; h < frame->size.h; h += hstep) {

			memcpy(d, s, sz);

			s += frame->linesize[i];
			d += dsz;
		}
	}

	SDL_UnlockTexture(st->texture);

	/* Clear screen (avoid artifacts) */
	SDL_RenderClear(st->renderer);

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

	if (SDL_Init(0) != 0) {
		warning("sdl: unable to init SDL: %s\n", SDL_GetError());
		return ENODEV;
	}

	if (SDL_VideoInit(NULL) != 0) {
		warning("sdl: unable to init Video: %s\n", SDL_GetError());
		return ENODEV;
	}

	err = vidisp_register(&vid, baresip_vidispl(),
			      "sdl", alloc, NULL, display, hide);
	if (err)
		return err;

	return 0;
}


static int module_close(void)
{
	if (vid) {
		vid = mem_deref(vid);
		SDL_VideoQuit();
	}

	SDL_Quit();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(sdl) = {
	"sdl",
	"vidisp",
	module_init,
	module_close,
};
