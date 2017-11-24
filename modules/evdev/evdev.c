/**
 * @file evdev.c Input event device UI module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <re.h>
#include <baresip.h>
#include "print.h"


/**
 * @defgroup evdev evdev
 *
 * User-Interface (UI) module using the Linux input subsystem.
 *
 * The following options can be configured:
 *
 \verbatim
  evdev_device     /dev/input/event0         # Name of the input device to use
 \endverbatim
 */


struct ui_st {
	int fd;
};


static struct ui_st *evdev;
static char evdev_device[64] = "/dev/input/event0";


static void evdev_close(struct ui_st *st)
{
	if (st->fd < 0)
		return;

	fd_close(st->fd);
	(void)close(st->fd);
	st->fd = -1;
}


static void evdev_destructor(void *arg)
{
	struct ui_st *st = arg;

	evdev_close(st);
}


static int code2ascii(uint16_t modifier, uint16_t code)
{
	switch (code) {

	case KEY_0:          return '0';
	case KEY_1:          return '1';
	case KEY_2:          return '2';
	case KEY_3:          return KEY_LEFTSHIFT==modifier ? '#' : '3';
	case KEY_4:          return '4';
	case KEY_5:          return '5';
	case KEY_6:          return '6';
	case KEY_7:          return '7';
	case KEY_8:          return '8';
	case KEY_9:          return '9';
	case KEY_BACKSPACE:  return '\b';
	case KEY_ENTER:      return '\n';
	case KEY_ESC:        return 0x1b;
	case KEY_KPASTERISK: return '*';
#ifdef KEY_NUMERIC_0
	case KEY_NUMERIC_0:  return '0';
#endif
#ifdef KEY_NUMERIC_1
	case KEY_NUMERIC_1:  return '1';
#endif
#ifdef KEY_NUMERIC_2
	case KEY_NUMERIC_2:  return '2';
#endif
#ifdef KEY_NUMERIC_3
	case KEY_NUMERIC_3:  return '3';
#endif
#ifdef KEY_NUMERIC_4
	case KEY_NUMERIC_4:  return '4';
#endif
#ifdef KEY_NUMERIC_5
	case KEY_NUMERIC_5:  return '5';
#endif
#ifdef KEY_NUMERIC_6
	case KEY_NUMERIC_6:  return '6';
#endif
#ifdef KEY_NUMERIC_7
	case KEY_NUMERIC_7:  return '7';
#endif
#ifdef KEY_NUMERIC_8
	case KEY_NUMERIC_8:  return '8';
#endif
#ifdef KEY_NUMERIC_9
	case KEY_NUMERIC_9:  return '9';
#endif
#ifdef KEY_NUMERIC_STAR
	case KEY_NUMERIC_STAR: return '*';
#endif
#ifdef KEY_NUMERIC_POUND
	case KEY_NUMERIC_POUND: return '#';
#endif
#ifdef KEY_KP0
	case KEY_KP0:        return '0';
#endif
#ifdef KEY_KP1
	case KEY_KP1:        return '1';
#endif
#ifdef KEY_KP2
	case KEY_KP2:        return '2';
#endif
#ifdef KEY_KP3
	case KEY_KP3:        return '3';
#endif
#ifdef KEY_KP4
	case KEY_KP4:        return '4';
#endif
#ifdef KEY_KP5
	case KEY_KP5:        return '5';
#endif
#ifdef KEY_KP6
	case KEY_KP6:        return '6';
#endif
#ifdef KEY_KP7
	case KEY_KP7:        return '7';
#endif
#ifdef KEY_KP8
	case KEY_KP8:        return '8';
#endif
#ifdef KEY_KP9
	case KEY_KP9:        return '9';
#endif
#ifdef KEY_KPDOT
	case KEY_KPDOT:      return 0x1b;
#endif
#ifdef KEY_KPENTER
	case KEY_KPENTER:    return '\n';
#endif
	default:             return -1;
	}
}


static int stderr_handler(const char *p, size_t sz, void *arg)
{
	(void)arg;

	if (write(STDERR_FILENO, p, sz) < 0)
		return errno;

	return 0;
}


static void reportkey(struct ui_st *st, int ascii)
{
	static struct re_printf pf_stderr = {stderr_handler, NULL};
	(void)st;

	ui_input_key(baresip_uis(), ascii, &pf_stderr);
}


static void evdev_fd_handler(int flags, void *arg)
{
	struct ui_st *st = arg;
	struct input_event evv[64]; /* the events (up to 64 at once) */
	uint16_t modifier = 0;
	size_t n;
	int i;

	/* This might happen if you unplug a USB device */
	if (flags & FD_EXCEPT) {
		warning("evdev: fd handler: FD_EXCEPT - device unplugged?\n");
		evdev_close(st);
		return;
	}

	n = read(st->fd, evv, sizeof(evv));

	if (n < (int) sizeof(struct input_event)) {
		warning("evdev: event: short read (%m)\n", errno);
		return;
	}

	for (i = 0; i < (int) (n / sizeof(struct input_event)); i++) {
		const struct input_event *ev = &evv[i];

		if (EV_KEY != ev->type)
			continue;

		if (KEY_LEFTSHIFT == ev->code) {
			modifier = KEY_LEFTSHIFT;
			continue;
		}

		if (1 == ev->value) {
			const int ascii = code2ascii(modifier, ev->code);
			if (-1 == ascii) {
				warning("evdev: unhandled key code %u\n",
					ev->code);
			}
			else
				reportkey(st, ascii);
			modifier = 0;
		}
		else if (0 == ev->value) {
			reportkey(st, KEYCODE_REL);
		}
	}
}


static int evdev_alloc(struct ui_st **stp, const char *dev)
{
	struct ui_st *st;
	int err = 0;

	if (!stp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), evdev_destructor);
	if (!st)
		return ENOMEM;

	st->fd = open(dev, O_RDWR);
	if (st->fd < 0) {
		err = errno;
		warning("evdev: failed to open device '%s' (%m)\n", dev, err);
		goto out;
	}

#if 0
	/* grab the event device to prevent it from propagating
	   its events to the regular keyboard driver            */
	if (-1 == ioctl(st->fd, EVIOCGRAB, (void *)1)) {
		warning("evdev: ioctl EVIOCGRAB on %s (%m)\n", dev, errno);
	}
#endif

	print_name(st->fd);
	print_events(st->fd);
	print_keys(st->fd);
	print_leds(st->fd);

	err = fd_listen(st->fd, FD_READ, evdev_fd_handler, st);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int buzz(const struct ui_st *st, int value)
{
	struct input_event ev;
	ssize_t n;

	ev.type  = EV_SND;
	ev.code  = SND_BELL;
	ev.value = value;

	n = write(st->fd, &ev, sizeof(ev));
	if (n < 0) {
		warning("evdev: output: write fd=%d (%m)\n", st->fd, errno);
	}

	return errno;
}


static int evdev_output(const char *str)
{
	struct ui_st *st = evdev;
	int err = 0;

	if (!st || !str)
		return EINVAL;

	while (*str) {
		switch (*str++) {

		case '\a':
			err |= buzz(st, 1);
			break;

		default:
			err |= buzz(st, 0);
			break;
		}
	}

	return err;
}


static struct ui ui_evdev = {
	.name = "evdev",
	.outputh = evdev_output
};


static int module_init(void)
{
	int err;

	conf_get_str(conf_cur(), "evdev_device",
		     evdev_device, sizeof(evdev_device));

	err = evdev_alloc(&evdev, evdev_device);
	if (err)
		return err;

	ui_register(baresip_uis(), &ui_evdev);

	return 0;
}


static int module_close(void)
{
	ui_unregister(&ui_evdev);
	evdev = mem_deref(evdev);
	return 0;
}


const struct mod_export DECL_EXPORTS(evdev) = {
	"evdev",
	"ui",
	module_init,
	module_close
};
