/**
 * @file sdl/sdl.c  SDL - Simple DirectMedia Layer v1.2
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <SDL/SDL.h>
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "sdl.h"


/**
 * @defgroup sdl sdl
 *
 * Video display using Simple DirectMedia Layer (SDL)
 */


/** Local constants */
enum {
	KEY_RELEASE_VAL = 250  /**< Key release value in [ms] */
};

struct vidisp_st {
	const struct vidisp *vd;  /* inheritance */
};

/** Global SDL data */
static struct {
	struct tmr tmr;
	SDL_Surface *screen;            /**< SDL Surface           */
	SDL_Overlay *bmp;               /**< SDL YUV Overlay       */
	struct vidsz size;              /**< Current size          */
	vidisp_resize_h *resizeh;       /**< Screen resize handler */
	void *arg;                      /**< Handler argument      */
	bool fullscreen;
	bool open;
} sdl;


static struct vidisp *vid;


static void event_handler(void *arg);


static void sdl_reset(void)
{
	if (sdl.bmp) {
		SDL_FreeYUVOverlay(sdl.bmp);
		sdl.bmp = NULL;
	}

	if (sdl.screen) {
		SDL_FreeSurface(sdl.screen);
		sdl.screen = NULL;
	}
}


static void handle_resize(int w, int h)
{
	struct vidsz size;

	size.w = w;
	size.h = h;

	/* notify app */
	if (sdl.resizeh)
		sdl.resizeh(&size, sdl.arg);
}


static void timeout(void *arg)
{
	(void)arg;

	tmr_start(&sdl.tmr, 1, event_handler, NULL);

	/* Emulate key-release */
	ui_input(KEYCODE_REL);
}


static void event_handler(void *arg)
{
	SDL_Event event;
	char ch;

	(void)arg;

	tmr_start(&sdl.tmr, 100, event_handler, NULL);

	while (SDL_PollEvent(&event)) {

		switch (event.type) {

		case SDL_KEYDOWN:

			switch (event.key.keysym.sym) {

			case SDLK_ESCAPE:
				if (!sdl.fullscreen)
					break;

				sdl.fullscreen = false;
				sdl_reset();
				break;

			case SDLK_f:
				if (sdl.fullscreen)
					break;

				sdl.fullscreen = true;
				sdl_reset();
				break;

			default:
				ch = event.key.keysym.unicode & 0x7f;

				/* Relay key-press to UI subsystem */
				if (isprint(ch)) {
					tmr_start(&sdl.tmr, KEY_RELEASE_VAL,
						  timeout, NULL);
					ui_input(ch);
				}
				break;
			}

			break;

		case SDL_VIDEORESIZE:
			handle_resize(event.resize.w, event.resize.h);
			break;

		case SDL_QUIT:
			ui_input('q');
			break;

		default:
			break;
		}
	}
}


static int sdl_open(void)
{
	if (sdl.open)
		return 0;

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		warning("sdl: unable to init SDL: %s\n", SDL_GetError());
		return ENODEV;
	}

	SDL_EnableUNICODE(1);

	tmr_start(&sdl.tmr, 100, event_handler, NULL);
	sdl.open = true;

	return 0;
}


static void sdl_close(void)
{
	tmr_cancel(&sdl.tmr);
	sdl_reset();

	if (sdl.open) {
		SDL_Quit();
		sdl.open = false;
	}
}


static void destructor(void *arg)
{
	struct vidisp_st *st = arg;
	(void)st;

	sdl_close();
}


static int alloc(struct vidisp_st **stp, const struct vidisp *vd,
		 struct vidisp_prm *prm, const char *dev,
		 vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	int err;

	/* Not used by SDL */
	(void)prm;
	(void)dev;

	if (sdl.open)
		return EBUSY;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vd = vd;

	sdl.resizeh = resizeh;
	sdl.arg     = arg;

	err = sdl_open();

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


/**
 * Display a video frame
 *
 * @param st    Video display state
 * @param title Window title
 * @param frame Video frame
 *
 * @return 0 if success, otherwise errorcode
 *
 * @note: On Darwin, this must be called from the main thread
 */
static int display(struct vidisp_st *st, const char *title,
		   const struct vidframe *frame)
{
	SDL_Rect rect;

	if (!st || !sdl.open)
		return EINVAL;

	if (!vidsz_cmp(&sdl.size, &frame->size)) {
		if (sdl.size.w && sdl.size.h) {
			info("sdl: reset size %u x %u  --->  %u x %u\n",
			     sdl.size.w, sdl.size.h,
			     frame->size.w, frame->size.h);
		}
		sdl_reset();
	}

	if (!sdl.screen) {
		int flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL;
		char capt[256];

		if (sdl.fullscreen)
			flags |= SDL_FULLSCREEN;
		else if (sdl.resizeh)
			flags |= SDL_RESIZABLE;

		if (title) {
			re_snprintf(capt, sizeof(capt), "%s - %u x %u",
				    title, frame->size.w, frame->size.h);
		}
		else {
			re_snprintf(capt, sizeof(capt), "%u x %u",
				    frame->size.w, frame->size.h);
		}

		SDL_WM_SetCaption(capt, capt);

		sdl.screen = SDL_SetVideoMode(frame->size.w, frame->size.h,
					      0, flags);
		if (!sdl.screen) {
			warning("sdl: unable to get video screen: %s\n",
				SDL_GetError());
			return ENODEV;
		}

		sdl.size = frame->size;
	}

	if (!sdl.bmp) {
		sdl.bmp = SDL_CreateYUVOverlay(frame->size.w, frame->size.h,
					       SDL_YV12_OVERLAY, sdl.screen);
		if (!sdl.bmp) {
			warning("sdl: unable to create overlay: %s\n",
				SDL_GetError());
			return ENODEV;
		}
	}

	SDL_LockYUVOverlay(sdl.bmp);
	picture_copy(sdl.bmp->pixels, sdl.bmp->pitches, frame);
	SDL_UnlockYUVOverlay(sdl.bmp);

	rect.x = 0;
	rect.y = 0;
	rect.w = sdl.size.w;
	rect.h = sdl.size.h;

	SDL_DisplayYUVOverlay(sdl.bmp, &rect);

	return 0;
}


static int module_init(void)
{
	return vidisp_register(&vid, "sdl", alloc, NULL, display, NULL);
}


static int module_close(void)
{
	vid = mem_deref(vid);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(sdl) = {
	"sdl",
	"vidisp",
	module_init,
	module_close,
};
