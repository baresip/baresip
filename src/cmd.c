/**
 * @file src/cmd.c  Command Interface
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <ctype.h>
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


enum {
	REL = 0x00,
	ESC = 0x1b,
	DEL = 0x7f,
};


struct cmds {
	struct le le;
	const struct cmd *cmdv;
	size_t cmdc;
};

struct cmd_ctx {
	struct mbuf *mb;
	const struct cmd *cmd;
};


static struct list cmdl;           /**< List of command blocks (struct cmds) */


static void destructor(void *arg)
{
	struct cmds *cmds = arg;

	list_unlink(&cmds->le);
}


static void ctx_destructor(void *arg)
{
	struct cmd_ctx *ctx = arg;

	mem_deref(ctx->mb);
}


static int ctx_alloc(struct cmd_ctx **ctxp, const struct cmd *cmd)
{
	struct cmd_ctx *ctx;

	ctx = mem_zalloc(sizeof(*ctx), ctx_destructor);
	if (!ctx)
		return ENOMEM;

	ctx->mb = mbuf_alloc(32);
	if (!ctx->mb) {
		mem_deref(ctx);
		return ENOMEM;
	}

	ctx->cmd = cmd;

	*ctxp = ctx;

	return 0;
}


static struct cmds *cmds_find(const struct cmd *cmdv)
{
	struct le *le;

	if (!cmdv)
		return NULL;

	for (le = cmdl.head; le; le = le->next) {
		struct cmds *cmds = le->data;

		if (cmds->cmdv == cmdv)
			return cmds;
	}

	return NULL;
}


static const struct cmd *cmd_find_by_key(char key)
{
	struct le *le;

	for (le = cmdl.tail; le; le = le->prev) {

		struct cmds *cmds = le->data;
		size_t i;

		for (i=0; i<cmds->cmdc; i++) {

			const struct cmd *cmd = &cmds->cmdv[i];

			if (cmd->key == key && cmd->h)
				return cmd;
		}
	}

	return NULL;
}


static const char *cmd_name(char *buf, size_t sz, const struct cmd *cmd)
{
	switch (cmd->key) {

	case ' ':   return "SPACE";
	case '\n':  return "ENTER";
	case ESC:   return "ESC";
	}

	buf[0] = cmd->key;
	buf[1] = '\0';

	if (cmd->flags & CMD_PRM)
		strncat(buf, " ..", sz-1);

	return buf;
}


static int editor_input(struct mbuf *mb, char key,
			struct re_printf *pf, bool *del)
{
	int err = 0;

	switch (key) {

	case ESC:
		*del = true;
		return re_hprintf(pf, "\nCancel\n");

	case REL:
		break;

	case '\n':
		*del = true;
		return re_hprintf(pf, "\n");

	case '\b':
	case DEL:
		if (mb->pos > 0)
			mb->pos = mb->end = (mb->pos - 1);
		break;

	default:
		err = mbuf_write_u8(mb, key);
		break;
	}

	err |= re_hprintf(pf, "\r> %32b", mb->buf, mb->end);

	return err;
}


static int cmd_report(const struct cmd *cmd, struct re_printf *pf,
		      struct mbuf *mb, bool compl)
{
	struct cmd_arg arg;
	int err;

	mb->pos = 0;
	err = mbuf_strdup(mb, &arg.prm, mb->end);
	if (err)
		return err;

	arg.key      = cmd->key;
	arg.complete = compl;

	err = cmd->h(pf, &arg);

	mem_deref(arg.prm);

	return err;
}


static int cmd_process_edit(struct cmd_ctx **ctxp, char key,
			    struct re_printf *pf)
{
	struct cmd_ctx *ctx;
	bool compl = (key == '\n'), del = false;
	int err;

	if (!ctxp)
		return EINVAL;

	ctx = *ctxp;

	err = editor_input(ctx->mb, key, pf, &del);
	if (err)
		return err;

	if (compl || ctx->cmd->flags & CMD_PROG)
		err = cmd_report(ctx->cmd, pf, ctx->mb, compl);

	if (del)
		*ctxp = mem_deref(*ctxp);

	return err;
}


/**
 * Register commands
 *
 * @param cmdv Array of commands
 * @param cmdc Number of commands
 *
 * @return 0 if success, otherwise errorcode
 */
int cmd_register(const struct cmd *cmdv, size_t cmdc)
{
	struct cmds *cmds;

	if (!cmdv || !cmdc)
		return EINVAL;

	cmds = cmds_find(cmdv);
	if (cmds)
		return EALREADY;

	cmds = mem_zalloc(sizeof(*cmds), destructor);
	if (!cmds)
		return ENOMEM;

	cmds->cmdv = cmdv;
	cmds->cmdc = cmdc;

	list_append(&cmdl, &cmds->le, cmds);

	return 0;
}


/**
 * Unregister commands
 *
 * @param cmdv Array of commands
 */
void cmd_unregister(const struct cmd *cmdv)
{
	mem_deref(cmds_find(cmdv));
}


/**
 * Process input characters to the command system
 *
 * @param ctxp Pointer to context for editor (optional)
 * @param key  Input character
 * @param pf   Print function
 *
 * @return 0 if success, otherwise errorcode
 */
int cmd_process(struct cmd_ctx **ctxp, char key, struct re_printf *pf)
{
	const struct cmd *cmd;

	/* are we in edit-mode? */
	if (ctxp && *ctxp) {

		if (key == REL)
			return 0;

		return cmd_process_edit(ctxp, key, pf);
	}

	cmd = cmd_find_by_key(key);
	if (cmd) {
		struct cmd_arg arg;

		/* check for parameters */
		if (cmd->flags & CMD_PRM) {

			if (ctxp) {
				int err = ctx_alloc(ctxp, cmd);
				if (err)
					return err;
			}

			return cmd_process_edit(ctxp,
						isdigit(key) ? key : 0,
						pf);
		}

		arg.key      = key;
		arg.prm      = NULL;
		arg.complete = true;

		return cmd->h(pf, &arg);
	}

	if (key == REL)
		return 0;

	return cmd_print(pf, NULL);
}


/**
 * Print a list of available commands
 *
 * @param pf     Print function
 * @param unused Unused variable
 *
 * @return 0 if success, otherwise errorcode
 */
int cmd_print(struct re_printf *pf, void *unused)
{
	size_t width = 5;
	char fmt[32], buf[8];
	int err = 0;
	int key;

	(void)unused;

	if (!pf)
		return EINVAL;

	(void)re_snprintf(fmt, sizeof(fmt), " %%-%zus   %%s\n", width);

	err |= re_hprintf(pf, "--- Help ---\n");

	/* print in alphabetical order */
	for (key = 1; key <= 0x80; key++) {

		const struct cmd *cmd = cmd_find_by_key(key);
		if (!cmd || !str_isset(cmd->desc))
			continue;

		err |= re_hprintf(pf, fmt, cmd_name(buf, sizeof(buf), cmd),
				  cmd->desc);

	}

	err |= re_hprintf(pf, "\n");

	return err;
}
