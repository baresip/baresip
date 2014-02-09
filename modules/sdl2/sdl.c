/**
 * @file sdl2/sdl.c  Simple DirectMedia Layer module for SDL v2.0
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <SDL2/SDL.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


struct vidisp_st {
	struct vidisp *vd;              /**< Inheritance (1st)     */
	SDL_Window *window;             /**< SDL Window            */
	SDL_Renderer *renderer;         /**< SDL Renderer          */
	SDL_Texture *texture;           /**< Texture for pixels    */
	struct vidsz size;              /**< Current size          */
	bool fullscreen;                /**< Fullscreen flag       */
};


static struct vidisp *vid;


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


static void destructor(void *arg)
{
	struct vidisp_st *st = arg;

	sdl_reset(st);

	mem_deref(st->vd);
}


static int alloc(struct vidisp_st **stp, struct vidisp *vd,
		 struct vidisp_prm *prm, const char *dev,
		 vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	int err = 0;

	/* Not used by SDL */
	(void)prm;
	(void)dev;
	(void)resizeh;
	(void)arg;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vd = mem_ref(vd);

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int display(struct vidisp_st *st, const char *title,
		   const struct vidframe *frame)
{
	void *pixels;
	uint8_t *p;
	int pitch, ret;
	unsigned i, h;

	if (!vidsz_cmp(&st->size, &frame->size)) {
		if (st->size.w && st->size.h) {
			info("sdl: reset size: %u x %u ---> %u x %u\n",
			     st->size.w, st->size.h,
			     frame->size.w, frame->size.h);
		}
		sdl_reset(st);
	}

	if (!st->window) {
		Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS;
		char capt[256];

		if (st->fullscreen)
			flags |= SDL_WINDOW_FULLSCREEN;

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
					      flags);
		if (!st->window) {
			warning("sdl: unable to create sdl window: %s\n",
				SDL_GetError());
			return ENODEV;
		}

		st->size = frame->size;

		SDL_RaiseWindow(st->window);
		SDL_SetWindowBordered(st->window, true);
		SDL_ShowWindow(st->window);
	}

	if (!st->renderer) {

		Uint32 flags = 0;

		flags |= SDL_RENDERER_ACCELERATED;
		flags |= SDL_RENDERER_PRESENTVSYNC;

		st->renderer = SDL_CreateRenderer(st->window, -1, flags);
		if (!st->renderer) {
			warning("sdl: unable to create renderer: %s\n",
				SDL_GetError());
			return ENOMEM;
		}
	}

	if (!st->texture) {

		st->texture = SDL_CreateTexture(st->renderer,
						SDL_PIXELFORMAT_IYUV,
						SDL_TEXTUREACCESS_STREAMING,
						frame->size.w, frame->size.h);
		if (!st->texture) {
			warning("sdl: unable to create texture: %s\n",
				SDL_GetError());
			return ENODEV;
		}
	}

	ret = SDL_LockTexture(st->texture, NULL, &pixels, &pitch);
	if (ret != 0) {
		warning("sdl: unable to lock texture (ret=%d)\n", ret);
		return ENODEV;
	}

	p = pixels;
	for (i=0; i<3; i++) {

		const uint8_t *s   = frame->data[i];
		const unsigned stp = frame->linesize[0] / frame->linesize[i];
		const unsigned sz  = frame->size.w / stp;

		for (h = 0; h < frame->size.h; h += stp) {

			memcpy(p, s, sz);

			s += frame->linesize[i];
			p += (pitch / stp);
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

	err = vidisp_register(&vid, "sdl2", alloc, NULL, display, hide);
	if (err)
		return err;

	return 0;
}


static int module_close(void)
{
	vid = mem_deref(vid);

	SDL_VideoQuit();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(sdl2) = {
	"sdl2",
	"vidisp",
	module_init,
	module_close,
};
