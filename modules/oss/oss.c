/**
 * @file oss.c  Open Sound System (OSS) driver
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#if defined(NETBSD) || defined(OPENBSD)
#include <soundcard.h>
#elif defined (LINUX)
#include <linux/soundcard.h>
#else
#include <sys/soundcard.h>
#endif
#ifdef SOLARIS
#include <sys/filio.h>
#endif


/**
 * @defgroup oss oss
 *
 * Open Sound System (OSS) audio driver module
 *
 *
 * References:
 *
 *    http://www.4front-tech.com/linux.html
 */


struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	pthread_t thread;
	bool run;
	int fd;
	int16_t *sampv;
	size_t sampc;
	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
};

struct auplay_st {
	const struct auplay *ap;      /* inheritance */
	pthread_t thread;
	bool run;
	int fd;
	int16_t *sampv;
	size_t sampc;
	auplay_write_h *wh;
	void *arg;
};


static struct ausrc *ausrc;
static struct auplay *auplay;
static char oss_dev[64] = "/dev/dsp";


/*
 * Automatically calculate the fragment size depending on sampling rate
 * and number of channels. More entries can be added to the table below.
 *
 * NOTE. Powermac 8200 and linux 2.4.18 gives:
 *       SNDCTL_DSP_SETFRAGMENT: Invalid argument
 */
static int set_fragment(int fd, uint32_t sampc)
{
	static const struct {
		uint16_t max;
		uint16_t size;
	} fragv[] = {
		{10, 7},  /* 10 x 2^7 = 1280 =  4 x 320 */
		{15, 7},  /* 15 x 2^7 = 1920 =  6 x 320 */
		{20, 7},  /* 20 x 2^7 = 2560 =  8 x 320 */
		{25, 7},  /* 25 x 2^7 = 3200 = 10 x 320 */
		{15, 8},  /* 15 x 2^8 = 3840 = 12 x 320 */
		{20, 8},  /* 20 x 2^8 = 5120 = 16 x 320 */
		{25, 8}   /* 25 x 2^8 = 6400 = 20 x 320 */
	};
	size_t i;
	const uint32_t buf_size = 2 * sampc;

	for (i=0; i<ARRAY_SIZE(fragv); i++) {
		const uint16_t frag_max  = fragv[i].max;
		const uint16_t frag_size = fragv[i].size;
		const uint32_t fragment_size = frag_max * (1<<frag_size);

		if (0 == (fragment_size%buf_size)) {
			int fragment = (frag_max<<16) | frag_size;

			if (0 == ioctl(fd, SNDCTL_DSP_SETFRAGMENT,
				       &fragment)) {
				return 0;
			}
		}
	}

	return ENODEV;
}


static int oss_reset(int fd, uint32_t srate, uint8_t ch, int sampc,
		     int nonblock)
{
	int format    = AFMT_S16_NE; /* native endian */
	int speed     = srate;
	int channels  = ch;
	int blocksize = 0;
	int err;

	err = set_fragment(fd, sampc);
	if (err)
		return err;

	if (0 != ioctl(fd, FIONBIO, &nonblock))
		return errno;
	if (0 != ioctl(fd, SNDCTL_DSP_SETFMT, &format))
		return errno;
	if (0 != ioctl(fd, SNDCTL_DSP_CHANNELS, &channels))
		return errno;
	if (2 == channels) {
		int stereo = 1;
		if (0 != ioctl(fd, SNDCTL_DSP_STEREO, &stereo))
			return errno;
	}
	if (0 != ioctl(fd, SNDCTL_DSP_SPEED, &speed))
		return errno;

	(void)ioctl(fd, SNDCTL_DSP_GETBLKSIZE, &blocksize);

	info("oss: init: %d Hz %d ch, blocksize=%d\n",
	     speed, channels, blocksize);

	return 0;
}


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (-1 != st->fd) {
		(void)close(st->fd);
	}

	mem_deref(st->sampv);
}


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (-1 != st->fd) {
		(void)close(st->fd);
	}

	mem_deref(st->sampv);
}


static void *record_thread(void *arg)
{
	struct ausrc_st *st = arg;
	int n;

	while (st->run) {

		n = read(st->fd, st->sampv, st->sampc*2);
		if (n <= 0)
			continue;

		st->rh(st->sampv, n/2, st->arg);
	}

	return NULL;
}


static void *play_thread(void *arg)
{
	struct auplay_st *st = arg;
	int n;

	while (st->run) {

		st->wh(st->sampv, st->sampc, st->arg);

		n = write(st->fd, st->sampv, st->sampc*2);
		if (n < 0) {
			warning("oss: write: %m\n", errno);
			break;
		}
	}

	return NULL;
}


static int src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		     struct media_ctx **ctx,
		     struct ausrc_prm *prm, const char *device,
		     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;

	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm || prm->fmt != AUFMT_S16LE || !rh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->fd   = -1;
	st->rh   = rh;
	st->errh = errh;
	st->arg  = arg;

	if (!device)
		device = oss_dev;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->sampv = mem_alloc(2 * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	st->fd = open(device, O_RDONLY);
	if (st->fd < 0) {
		err = errno;
		goto out;
	}

	err = oss_reset(st->fd, prm->srate, prm->ch, st->sampc, 0);
	if (err)
		goto out;

	st->as = as;

	st->run = true;
	err = pthread_create(&st->thread, NULL, record_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int play_alloc(struct auplay_st **stp, const struct auplay *ap,
		      struct auplay_prm *prm, const char *device,
		      auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err;

	if (!stp || !ap || !prm || prm->fmt != AUFMT_S16LE || !wh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->fd  = -1;
	st->wh  = wh;
	st->arg = arg;

	if (!device)
		device = oss_dev;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->sampv = mem_alloc(st->sampc * 2, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	st->fd = open(device, O_WRONLY);
	if (st->fd < 0) {
		err = errno;
		goto out;
	}

	err = oss_reset(st->fd, prm->srate, prm->ch, st->sampc, 0);
	if (err)
		goto out;

	st->ap = ap;

	st->run = true;
	err = pthread_create(&st->thread, NULL, play_thread, st);
	if (err) {
		st->run = false;
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

	err  = ausrc_register(&ausrc, baresip_ausrcl(), "oss", src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(), "oss", play_alloc);

	return err;
}


static int module_close(void)
{
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(oss) = {
	"oss",
	"audio",
	module_init,
	module_close,
};
