/**
 * @file aupipe.c  Unix domain socket driver
 *
 * Copyright (C) 2020 Orion Labs, Inc.
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>


/**
 * @defgroup aupipe aupipe
 *
 * Unix domain socket audio driver module
 */


struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	pthread_t thread;
	bool run;
	int fd;
	const char *pipe;
	int16_t *sampv;
	size_t sampc;
	uint32_t ptime;
	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
};

struct auplay_st {
	const struct auplay *ap;      /* inheritance */
	pthread_t thread;
	bool run;
	int fd;
	const char *pipe;
	int16_t *sampv;
	size_t sampc;
	uint32_t ptime;
	auplay_write_h *wh;
	void *arg;
};


static struct ausrc *ausrc;
static struct auplay *auplay;


static void auplay_destructor(void *arg) {
	struct auplay_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (-1 != st->fd) {
		(void)close(st->fd);
		unlink(st->pipe);
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
		unlink(st->pipe);
	}

	mem_deref(st->sampv);
}


static void *record_thread(void *arg) {
	struct ausrc_st *st = arg;
	int fd = -1;
	uint64_t clock;

	clock = tmr_jiffies() + st->ptime;
	while (st->run) {
		bool restart = false;
		bool silence = false;
		uint64_t now;
		int n;

		if (fd == -1) {
			info("aupipe_record: waiting for connection\n");
			fd = accept(st->fd, NULL, NULL);
			if (fd == -1 && errno == EAGAIN) {
				silence = true;
			}
			else if (fd == -1) {
				error_msg(
				"aupipe_record: accept failed: %d (%m)\n",
				errno, errno);
				silence = true;
			}
			else {
				struct timeval tv = { 0,
					st->ptime * 1000 / 4 };

				if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
						&tv, sizeof(tv)) == -1) {
					error_msg(
						"setsockopt failed: %d (%m)\n",
						errno, errno);
					restart = true;
					silence = true;
				}
			}
		}

		if (!silence) {
			n = read(fd, st->sampv, st->sampc*2);

			if (n == 0) {
				error_msg("aupipe_record: eof\n");
				restart = true;
				silence = true;
			}
			else if (n == -1 && errno == EAGAIN) {
				silence = true;
			}
			else if (n == -1) {
				error_msg(
				"aupipe_record: read failed: %d (%m)\n",
				errno, errno);
				restart = true;
				silence = true;
			}
			else if ((size_t)n < st->sampc*2) {
				error_msg("aupipe_record: partial read: %d\n",
					n);
			}
			else {
			/* debug("aupipe_record: read %d bytes\n", n); */
			}
		}

		if (silence) {
			memset(st->sampv, 0, st->sampc*2);
		}

		now = tmr_jiffies();
		if (clock < now) {
			warning("aupipe_record: skipped %lldms\n",
				now - clock);
			clock = now;
		}
		else {
			sys_msleep(clock - now);
		}

		st->rh(st->sampv, st->sampc, st->arg);

		if (restart) {
			close(fd);
			fd = -1;
		}

		clock += st->ptime;
	}

	return NULL;
}


static void *play_thread(void *arg) {
	struct auplay_st *st = arg;
	uint64_t clock;
	int fd = -1;

	clock = tmr_jiffies() + st->ptime;
	while (st->run) {
		bool silence = false;
		uint64_t now;
		int n;

		if (fd == -1) {
			info("aupipe_play: waiting for connection\n");
			fd = accept(st->fd, NULL, NULL);
			if (fd == -1 && errno == EAGAIN) {
				silence = true;
			}
			else if (fd == -1) {
				error_msg("aupipe_play: accept: %d (%m)\n",
					errno, errno);
				silence = true;
			}
		}

		st->wh(st->sampv, st->sampc, st->arg);

		if (!silence) {
			n = write(fd, st->sampv, st->sampc*2);
			if (n < 0) {
				warning("aupipe_play: write failed: %d %m\n",
					errno, errno);
				close(fd);
				fd = -1;
			}
			else if ((size_t)n < st->sampc*2) {
				warning("aupipe_play: partial write: %d\n", n);
			}
			else {
			/* debug("aupipe_play: write %d bytes\n", n); */
			}
		}

		now = tmr_jiffies();
		if (clock < now) {
			warning("aupipe_play: skipped %lldms\n", now - clock);
			clock = now;
		}
		else {
			sys_msleep(clock - now);
		}

		clock += st->ptime;
	}

	return NULL;
}


static int aupipe_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		struct media_ctx **ctx,
		struct ausrc_prm *prm, const char *device,
		ausrc_read_h *rh, ausrc_error_h *errh, void *arg) {
	struct sockaddr_un addr = { 0 };
	struct ausrc_st *st;
	struct timeval tv;
	int err;

	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm || prm->fmt != AUFMT_S16LE || !rh)
		return EINVAL;

	if (device == NULL || device[0] == '\0') {
		error_msg("device required\n");
		return EINVAL;
	}

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (st->fd == -1) {
		error_msg("create socket failed\n");
		err = errno;
		goto out;
	}

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, device, sizeof(addr.sun_path) - 1);
	if (bind(st->fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		error_msg("bind failed\n");
		err = errno;
		goto out;
	}

	if (listen(st->fd, 1) == -1) {
		error_msg("listen failed\n");
		err = errno;
		goto out;
	}

	/*
	 * Setting the recieve timeout to ptime/2 so we can keep handling reads
	 * and writes while waiting for a pipe connection.
	 */
	tv.tv_sec = 0;
	tv.tv_usec = st->ptime * 1000 / 2;
	if (setsockopt(st->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))
			== -1) {
		error_msg("setsockopt failed: %d (%m)\n", errno, errno);
		err = errno;
		goto out;
	}

	st->pipe = device;
	st->ptime = prm->ptime;
	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	st->sampv = mem_alloc(st->sampc * 2, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	st->rh = rh;
	st->errh = errh;
	st->arg = arg;
	st->as = as;
	st->run = true;
	err = pthread_create(&st->thread, NULL, record_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("aupipe: recording started (%s)\n", st->pipe);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int aupipe_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		struct auplay_prm *prm, const char *device,
		auplay_write_h *wh, void *arg) {
	struct sockaddr_un addr = { 0 };
	struct auplay_st *st;
	int err;

	if (!stp || !ap || !prm || prm->fmt != AUFMT_S16LE || !wh)
		return EINVAL;

	if (device == NULL || device[0] == '\0') {
		error_msg("device required\n");
		return EINVAL;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (st->fd == -1) {
		error_msg("create socket failed\n");
		err = errno;
		goto out;
	}

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, device, sizeof(addr.sun_path) - 1);
	if (bind(st->fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		error_msg("bind failed\n");
		err = errno;
		goto out;
	}

	if (listen(st->fd, 1) == -1) {
		error_msg("listen failed\n");
		err = errno;
		goto out;
	}

	st->pipe = device;
	st->ptime = prm->ptime;
	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	st->sampv = mem_alloc(st->sampc * 2, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	st->wh = wh;
	st->arg = arg;
	st->ap = ap;
	st->run = true;
	err = pthread_create(&st->thread, NULL, play_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("aupipe: playback started (%s)\n", st->pipe);

 out:
	if (err) {
		if (st->fd != -1) {
			close(st->fd);
		}
		mem_deref(st);
	}
	else {
		*stp = st;
	}

	return err;
}


static int aupipe_init(void) {
	int err;

	debug("aupipe init\n");

	err = ausrc_register(&ausrc, baresip_ausrcl(),
		"aupipe", aupipe_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
		"aupipe", aupipe_play_alloc);

	return err;
}


static int aupipe_close(void) {
	debug("aupipe close\n");

	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aupipe) = {
	"aupipe",
	"audio",
	aupipe_init,
	aupipe_close,
};
