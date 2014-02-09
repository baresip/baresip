/**
 * @file wincons.c  Windows console input
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <winsock2.h>
#include <re.h>
#include <baresip.h>


/** Local constants */
enum {
	RELEASE_VAL = 250  /**< Key release value in [ms] */
};

struct ui_st {
	struct ui *ui; /* base class */
	struct tmr tmr;
	struct mqueue *mq;
	HANDLE hThread;
	bool run;
	ui_input_h *h;
	void *arg;
};


static struct ui *wincons;


static void destructor(void *arg)
{
	struct ui_st *st = arg;

	st->run = false;
	CloseHandle(st->hThread);

	tmr_cancel(&st->tmr);
	mem_deref(st->mq);
	mem_deref(st->ui);
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


static DWORD WINAPI input_thread(LPVOID arg)
{
	struct ui_st *st = arg;

	HANDLE hstdin = GetStdHandle( STD_INPUT_HANDLE );
	DWORD  mode;

	/* Switch to raw mode */
	GetConsoleMode(hstdin, &mode);
	SetConsoleMode(hstdin, 0);

	while (st->run) {

		char buf[4];
		DWORD i, count = 0;

		ReadConsole(hstdin, buf, sizeof(buf), &count, NULL);

		for (i=0; i<count; i++) {
			int ch = buf[i];

			if (ch == '\r')
				ch = '\n';

			/*
			 * The keys are read from a thread so we have
			 * to send them to the RE main event loop via
			 * a message queue
			 */
			mqueue_push(st->mq, ch, 0);
		}
	}

	/* Restore the console to its previous state */
	SetConsoleMode(hstdin, mode);

	return 0;
}


static void mqueue_handler(int id, void *data, void *arg)
{
	struct ui_st *st = arg;
	(void)data;

	tmr_start(&st->tmr, RELEASE_VAL, timeout, st);
	report_key(st, id);
}


static int ui_alloc(struct ui_st **stp, struct ui_prm *prm,
		    ui_input_h *ih, void *arg)
{
	struct ui_st *st;
	DWORD threadID;
	int err = 0;
	(void)prm;

	if (!stp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->ui  = mem_ref(wincons);
	st->h   = ih;
	st->arg = arg;

	tmr_init(&st->tmr);

	err = mqueue_alloc(&st->mq, mqueue_handler, st);
	if (err)
		goto out;

	st->run = true;
	st->hThread = CreateThread(NULL, 0, input_thread, st, 0, &threadID);
	if (!st->hThread) {
		st->run = false;
		err = ENOMEM;
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
	return ui_register(&wincons, "wincons", ui_alloc, NULL);
}


static int module_close(void)
{
	wincons = mem_deref(wincons);
	return 0;
}


const struct mod_export DECL_EXPORTS(wincons) = {
	"wincons",
	"ui",
	module_init,
	module_close
};
