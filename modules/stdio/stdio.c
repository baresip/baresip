/**
 * @file stdio.c Standard Input/Output UI module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <re.h>
#include <baresip.h>


/** Local constants */
enum {
	RELEASE_VAL = 250  /**< Key release value in [ms] */
};

struct ui_st {
	struct ui *ui; /* base class */
	struct tmr tmr;
	struct termios term;
	bool term_set;
	ui_input_h *h;
	void *arg;
};


/* We only allow one instance */
static struct ui_st *_ui;
static struct ui *stdio;


static void ui_destructor(void *arg)
{
	struct ui_st *st = arg;

	fd_close(STDIN_FILENO);

	if (st->term_set)
		tcsetattr(STDIN_FILENO, TCSANOW, &st->term);

	tmr_cancel(&st->tmr);
	mem_deref(st->ui);

	_ui = NULL;
}


static int print_handler(const char *p, size_t size, void *arg)
{
	(void)arg;

	return 1 == fwrite(p, size, 1, stderr) ? 0 : ENOMEM;
}


static void report_key(struct ui_st *ui, char key)
{
	struct re_printf pf;

	pf.vph = print_handler;

	if (ui->h)
		ui->h(key, &pf, ui->arg);
}


static void timeout(void *arg)
{
	struct ui_st *st = arg;

	/* Emulate key-release */
	report_key(st, 0x00);
}


static void ui_fd_handler(int flags, void *arg)
{
	struct ui_st *st = arg;
	char key;
	(void)flags;

	if (1 != read(STDIN_FILENO, &key, 1)) {
		return;
	}

	tmr_start(&st->tmr, RELEASE_VAL, timeout, st);
	report_key(st, key);
}


static int term_setup(struct ui_st *st)
{
	struct termios now;

	if (tcgetattr(STDIN_FILENO, &st->term) < 0)
		return errno;

	now = st->term;

	now.c_lflag |= ISIG;
	now.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);

	/* required on Solaris */
	now.c_cc[VMIN] = 1;
	now.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &now) < 0)
		return errno;

	st->term_set = true;

	return 0;
}


static int ui_alloc(struct ui_st **stp, struct ui_prm *prm,
		    ui_input_h *ih, void *arg)
{
	struct ui_st *st;
	int err;

	(void)prm;

	if (!stp)
		return EINVAL;

	if (_ui) {
		*stp = mem_ref(_ui);
		return 0;
	}

	st = mem_zalloc(sizeof(*st), ui_destructor);
	if (!st)
		return ENOMEM;

	st->ui = mem_ref(stdio);
	tmr_init(&st->tmr);

	err = fd_listen(STDIN_FILENO, FD_READ, ui_fd_handler, st);
	if (err)
		goto out;

	err = term_setup(st);
	if (err) {
		info("stdio: could not setup terminal: %m\n", err);
		err = 0;
	}

	st->h   = ih;
	st->arg = arg;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = _ui = st;

	return err;
}


static int module_init(void)
{
	return ui_register(&stdio, "stdio", ui_alloc, NULL);
}


static int module_close(void)
{
	stdio = mem_deref(stdio);
	return 0;
}


const struct mod_export DECL_EXPORTS(stdio) = {
	"stdio",
	"ui",
	module_init,
	module_close
};
