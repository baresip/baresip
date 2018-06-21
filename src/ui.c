/**
 * @file ui.c  User Interface
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


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
 * @param uis UI Subsystem
 * @param ui  The User-Interface (UI) module to register
 */
void ui_register(struct ui_sub *uis, struct ui *ui)
{
	if (!uis || !ui)
		return;

	list_append(&uis->uil, &ui->le, ui);

	debug("ui: %s\n", ui->name);
}


/**
 * Un-register a User-Interface (UI) module
 *
 * @param ui The User-Interface (UI) module to un-register
 */
void ui_unregister(struct ui *ui)
{
	if (!ui)
		return;

	list_unlink(&ui->le);
}


/**
 * Send an input key to the UI subsystem, with a print function for response
 *
 * @param uis UI Subsystem
 * @param key Input character
 * @param pf  Print function for the response
 */
void ui_input_key(struct ui_sub *uis, char key, struct re_printf *pf)
{
	if (!uis)
		return;

	(void)cmd_process(baresip_commands(), &uis->uictx, key, pf, NULL);
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


/**
 * Send an input pointer-length string to the UI subsystem
 *
 * @param pf  Print function
 * @param pl  Input pointer-length string
 *
 * @return 0 if success, otherwise errorcode
 */
int ui_input_pl(struct re_printf *pf, const struct pl *pl)
{
	struct cmd_ctx *ctx = NULL;
	struct commands *commands = baresip_commands();
	size_t i;
	int err = 0;

	if (!pf || !pl)
		return EINVAL;

	for (i=0; i<pl->l; i++) {
		err |= cmd_process(commands, &ctx, pl->p[i], pf, NULL);
	}

	if (pl->l > 1 && ctx)
		err |= cmd_process(commands, &ctx, '\n', pf, NULL);

	return err;
}


/**
 * Send a long command with arguments to the UI subsystem.
 * The slash prefix is optional.
 *
 * @param pf  Print function for the response
 * @param pl  Long command with or without '/' prefix
 *
 * @return 0 if success, otherwise errorcode
 */
int ui_input_long_command(struct re_printf *pf, const struct pl *pl)
{
	size_t offset;
	int err;

	if (!pl)
		return EINVAL;

	/* strip the prefix, if present */
	if (pl->l > 1 && pl->p[0] == '/')
		offset = 1;
	else
		offset = 0;

	err = cmd_process_long(baresip_commands(),
			       pl->p + offset,
			       pl->l - offset, pf, NULL);

	return err;
}


/**
 * Send output to all modules registered in the UI subsystem
 *
 * @param uis UI Subsystem
 * @param fmt Formatted output string
 */
void ui_output(struct ui_sub *uis, const char *fmt, ...)
{
	char buf[512];
	struct le *le;
	va_list ap;
	int n;

	if (!uis)
		return;

	va_start(ap, fmt);
	n = re_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (n < 0)
		return;

	for (le = uis->uil.head; le; le = le->next) {
		const struct ui *ui = le->data;

		if (ui->outputh)
			ui->outputh(buf);
	}
}


/**
 * Reset the state of the UI subsystem, free resources
 *
 * @param uis  UI Subsystem
 */
void ui_reset(struct ui_sub *uis)
{
	if (!uis)
		return;

	uis->uictx = mem_deref(uis->uictx);
}


/**
 * Check if the UI is in editor mode
 *
 * @param uis  UI Subsystem
 *
 * @return True if editing, otherwise false
 */
bool ui_isediting(const struct ui_sub *uis)
{
	if (!uis)
		return false;

	return uis->uictx != NULL;
}


/**
 * Prompt the user interactively for a password
 *
 * NOTE: This function is blocking and should not be called from
 *       any re_main event handlers.
 *
 * @param passwordp  Pointer to allocated password string
 *
 * @return 0 if success, otherwise errorcode
 */
int ui_password_prompt(char **passwordp)
{
	char pwd[64];
	char *nl;
	int err;

	if (!passwordp)
		return EINVAL;

	/* note: blocking UI call */
	fgets(pwd, sizeof(pwd), stdin);
	pwd[sizeof(pwd) - 1] = '\0';

	nl = strchr(pwd, '\n');
	if (nl == NULL) {
		(void)re_printf("Invalid password (0 - 63 characters"
				" followed by newline)\n");
		return EINVAL;
	}

	*nl = '\0';

	err = str_dup(passwordp, pwd);
	if (err)
		return err;

	return 0;
}
