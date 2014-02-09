/**
 * @file ui.c  User Interface
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/** User Interface */
struct ui {
	struct le le;
	const char *name;
	struct ui_st *st;      /* only one instance */
	ui_output_h *outputh;
	struct cmd_ctx *ctx;
};

static struct list uil;  /**< List of UIs (struct ui) */
static struct config_input input_cfg;


static void ui_handler(char key, struct re_printf *pf, void *arg)
{
	struct ui *ui = arg;

	(void)cmd_process(ui ? &ui->ctx : NULL, key, pf);
}


static void destructor(void *arg)
{
	struct ui *ui = arg;

	list_unlink(&ui->le);
	mem_deref(ui->st);
	mem_deref(ui->ctx);
}


static int stdout_handler(const char *p, size_t size, void *arg)
{
	(void)arg;

	if (1 != fwrite(p, size, 1, stdout))
		return ENOMEM;

	return 0;
}


/**
 * Register a new User-Interface (UI) module
 *
 * @param uip    Pointer to allocated UI module
 * @param name   Name of the UI module
 * @param alloch UI allocation handler
 * @param outh   UI output handler
 *
 * @return 0 if success, otherwise errorcode
 */
int ui_register(struct ui **uip, const char *name,
		ui_alloc_h *alloch, ui_output_h *outh)
{
	struct ui *ui;
	int err = 0;

	if (!uip)
		return EINVAL;

	ui = mem_zalloc(sizeof(*ui), destructor);
	if (!ui)
		return ENOMEM;

	list_append(&uil, &ui->le, ui);

	ui->name    = name;
	ui->outputh = outh;

	if (alloch) {
		struct ui_prm prm;

		prm.device = input_cfg.device;
		prm.port   = input_cfg.port;

		err = alloch(&ui->st, &prm, ui_handler, ui);
		if (err) {
			warning("ui: register: module '%s' failed (%m)\n",
				ui->name, err);
		}
	}

	if (err)
		mem_deref(ui);
	else
		*uip = ui;

	return err;
}


/**
 * Send input to the UI subsystem
 *
 * @param key Input character
 */
void ui_input(char key)
{
	struct re_printf pf;

	pf.vph = stdout_handler;
	pf.arg = NULL;

	ui_handler(key, &pf, list_ledata(uil.head));
}


/**
 * Send an input string to the UI subsystem
 *
 * @param str Input string
 */
void ui_input_str(const char *str)
{
	struct re_printf pf;
	struct pl pl;

	if (!str)
		return;

	pf.vph = stdout_handler;
	pf.arg = NULL;

	pl_set_str(&pl, str);

	(void)ui_input_pl(&pf, &pl);
}


int ui_input_pl(struct re_printf *pf, const struct pl *pl)
{
	struct cmd_ctx *ctx = NULL;
	size_t i;
	int err = 0;

	if (!pf || !pl)
		return EINVAL;

	for (i=0; i<pl->l; i++) {
		err |= cmd_process(&ctx, pl->p[i], pf);
	}

	if (pl->l > 1 && ctx)
		err |= cmd_process(&ctx, '\n', pf);

	return err;
}


/**
 * Send output to all modules registered in the UI subsystem
 *
 * @param str Output string
 */
void ui_output(const char *str)
{
	struct le *le;

	for (le = uil.head; le; le = le->next) {
		const struct ui *ui = le->data;

		if (ui->outputh)
			ui->outputh(ui->st, str);
	}
}


void ui_init(const struct config_input *cfg)
{
	if (!cfg)
		return;

	input_cfg = *cfg;
}
