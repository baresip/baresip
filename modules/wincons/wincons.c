/**
 * @file wincons.c  Windows console input
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <winsock2.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup wincons wincons
 *
 * User-Interface (UI) module for Windows Console
 */


/** Local constants */
enum {
	RELEASE_VAL = 250  /**< Key release value in [ms] */
};

struct ui_st {
	struct tmr tmr;
	struct mqueue *mq;
	HANDLE hThread;
	bool run;
	HANDLE hstdin;
	DWORD  mode;
};


static struct ui_st *wincons;


static void destructor(void *arg)
{
	struct ui_st *st = arg;

	/* Restore the console to its previous state */
	if (st->mode)
		SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), st->mode);

	st->run = false;
	WaitForSingleObject(st->hThread, 5000);
	CloseHandle(st->hThread);

	tmr_cancel(&st->tmr);
	mem_deref(st->mq);
}


static int print_handler(const char *p, size_t size, void *arg)
{
	(void)arg;

	return 1 == fwrite(p, size, 1, stderr) ? 0 : ENOMEM;
}


static void report_key(struct ui_st *ui, char key)
{
	static struct re_printf pf_stderr = {print_handler, NULL};
	(void)ui;

	ui_input_key(baresip_uis(), key, &pf_stderr);
}


static void timeout(void *arg)
{
	struct ui_st *st = arg;

	/* Emulate key-release */
	report_key(st, KEYCODE_REL);
}


static DWORD WINAPI input_thread(LPVOID arg)
{
	struct ui_st *st = arg;

	/* Switch to raw mode */
	SetConsoleMode(st->hstdin, 0);

	while (st->run) {

		INPUT_RECORD buf[4];
		DWORD i, count = 0;

		ReadConsoleInput(st->hstdin, buf, ARRAY_SIZE(buf), &count);

		for (i=0; i<count; i++) {

			if (buf[i].EventType != KEY_EVENT)
				continue;

			if (buf[i].Event.KeyEvent.bKeyDown) {

				int ch = buf[i].Event.KeyEvent.uChar.AsciiChar;

				if (ch == '\r')
					ch = '\n';

				/* Special handling of 'q' (quit) */
				if (ch == 'q')
					st->run = false;

				/*
				 * The keys are read from a thread so we have
				 * to send them to the RE main event loop via
				 * a message queue
				 */
				if (ch)
					mqueue_push(st->mq, ch, NULL);
			}
		}
	}

	return 0;
}


static void mqueue_handler(int id, void *data, void *arg)
{
	struct ui_st *st = arg;
	(void)data;

	tmr_start(&st->tmr, RELEASE_VAL, timeout, st);
	report_key(st, id);
}


static int ui_alloc(struct ui_st **stp)
{
	struct ui_st *st;
	DWORD threadID;
	int err = 0;

	if (!stp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	tmr_init(&st->tmr);

	err = mqueue_alloc(&st->mq, mqueue_handler, st);
	if (err)
		goto out;

	st->hstdin = GetStdHandle(STD_INPUT_HANDLE);

	/* save the current console mode */
	GetConsoleMode(st->hstdin, &st->mode);

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


static int output_handler(const char *str)
{
	return print_handler(str, str_len(str), NULL);
}


static struct ui ui_wincons = {
	.name    = "wincons",
	.outputh = output_handler
};


static int module_init(void)
{
	int err;

	err = ui_alloc(&wincons);
	if (err)
		return err;

	ui_register(baresip_uis(), &ui_wincons);

	return 0;
}


static int module_close(void)
{
	ui_unregister(&ui_wincons);
	wincons = mem_deref(wincons);
	return 0;
}


const struct mod_export DECL_EXPORTS(wincons) = {
	"wincons",
	"ui",
	module_init,
	module_close
};
