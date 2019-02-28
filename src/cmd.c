/**
 * @file src/cmd.c  Command Interface
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <ctype.h>
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


enum {
	KEYCODE_DEL = 0x7f,
	LONG_PREFIX = '/'
};


struct cmds {
	struct le le;
	const struct cmd *cmdv;
	size_t cmdc;
};

struct cmd_ctx {
	struct mbuf *mb;
	const struct cmd *cmd;
	bool is_long;
};

struct commands {
	struct list cmdl;        /**< List of command blocks (struct cmds) */
};


static int cmd_print_all(struct re_printf *pf,
			 const struct commands *commands,
			 bool print_long, bool print_short,
			 const char *match, size_t match_len);


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


static void commands_destructor(void *data)
{
	struct commands *commands = data;

	list_flush(&commands->cmdl);
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


/**
 * Find a command block
 *
 * @param commands Commands container
 * @param cmdv     Command vector
 *
 * @return Command block if found, otherwise NULL
 */
struct cmds *cmds_find(const struct commands *commands,
		       const struct cmd *cmdv)
{
	struct le *le;

	if (!commands || !cmdv)
		return NULL;

	for (le = commands->cmdl.head; le; le = le->next) {
		struct cmds *cmds = le->data;

		if (cmds->cmdv == cmdv)
			return cmds;
	}

	return NULL;
}


static const struct cmd *cmd_find_by_key(const struct commands *commands,
					 char key)
{
	struct le *le;

	if (!commands)
		return NULL;

	for (le = commands->cmdl.tail; le; le = le->prev) {

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
	case KEYCODE_ESC:   return "ESC";
	}

	buf[0] = cmd->key;
	buf[1] = '\0';

	if (cmd->flags & CMD_PRM)
		strncat(buf, " ..", sz-1);

	return buf;
}


static size_t get_match_long(const struct commands *commands,
			     const struct cmd **cmdp,
			     const char *str, size_t len)
{
	struct le *le;
	size_t nmatch = 0;

	if (!commands)
		return 0;

	for (le = commands->cmdl.head; le; le = le->next) {

		struct cmds *cmds = le->data;
		size_t i;

		for (i=0; i<cmds->cmdc; i++) {

			const struct cmd *cmd = &cmds->cmdv[i];

			if (!str_isset(cmd->name))
				continue;

			if (str_len(cmd->name) >= len &&
			    0 == memcmp(cmd->name, str, len)) {

				++nmatch;
				*cmdp = cmd;
			}
		}
	}

	return nmatch;
}


static int editor_input(struct commands *commands, struct mbuf *mb, char key,
			struct re_printf *pf, bool *del, bool is_long)
{
	int err = 0;

	switch (key) {

	case KEYCODE_ESC:
		*del = true;
		return re_hprintf(pf, "\nCancel\n");

	case KEYCODE_NONE:
	case KEYCODE_REL:
		break;

	case '\n':
		*del = true;
		return re_hprintf(pf, "\n");

	case '\b':
	case KEYCODE_DEL:
		if (mb->pos > 0) {
			err |= re_hprintf(pf, "\b ");
			mb->pos = mb->end = (mb->pos - 1);
		}
		break;

	case '\t':
		if (is_long) {
			const struct cmd *cmd = NULL;
			size_t n;

			err = re_hprintf(pf,
					 "TAB completion for \"%b\":\n",
					 mb->buf, mb->end);
			if (err)
				return err;

			/* Find all long commands that matches the N
			 * first characters of the input string.
			 *
			 * If the number of matches is exactly one,
			 * we can regard it as TAB completion.
			 */

			err = cmd_print_all(pf, commands, true, false,
					    (char *)mb->buf, mb->end);
			if (err)
				return err;

			n = get_match_long(commands, &cmd,
					   (char *)mb->buf, mb->end);
			if (n == 1 && cmd) {

				mb->pos = 0;
				mbuf_write_str(mb, cmd->name);
			}
			else if (n == 0) {
				err = re_hprintf(pf, "(none)\n");
			}
		}
		else {
			err = mbuf_write_u8(mb, key);
		}
		break;

	default:
		err = mbuf_write_u8(mb, key);
		break;
	}

	if (is_long) {
		err |= re_hprintf(pf, "\r%c%b", LONG_PREFIX,
				  mb->buf, mb->end);
	}
	else
		err |= re_hprintf(pf, "\r> %32b", mb->buf, mb->end);

	return err;
}


static int cmd_report(const struct cmd *cmd, struct re_printf *pf,
		      struct mbuf *mb, void *data)
{
	struct cmd_arg arg;
	int err;

	memset(&arg, 0, sizeof(arg));

	mb->pos = 0;
	err = mbuf_strdup(mb, &arg.prm, mb->end);
	if (err)
		return err;

	arg.key      = cmd->key;
	arg.data     = data;

	err = cmd->h(pf, &arg);

	mem_deref(arg.prm);

	return err;
}


/**
 * Process long commands
 *
 * @param commands Commands container
 * @param str      Input string
 * @param len      Length of input string
 * @param pf_resp  Print function for response
 * @param data     Application data
 *
 * @return 0 if success, otherwise errorcode
 */
int cmd_process_long(struct commands *commands, const char *str, size_t len,
		     struct re_printf *pf_resp, void *data)
{
	struct cmd_arg arg;
	const struct cmd *cmd_long;
	char *name = NULL, *prm = NULL;
	struct pl pl_name, pl_prm;
	int err;

	if (!str || !len)
		return EINVAL;

	memset(&arg, 0, sizeof(arg));

	err = re_regex(str, len, "[^ ]+[ ]*[~]*", &pl_name, NULL, &pl_prm);
	if (err) {
		return err;
	}

	err = pl_strdup(&name, &pl_name);
	if (pl_isset(&pl_prm))
		err |= pl_strdup(&prm, &pl_prm);
	if (err)
		goto out;

	cmd_long = cmd_find_long(commands, name);
	if (cmd_long) {

		arg.key      = LONG_PREFIX;
		arg.prm      = prm;
		arg.data     = data;

		if (cmd_long->h)
			err = cmd_long->h(pf_resp, &arg);
	}
	else {
		(void)re_hprintf(pf_resp, "command not found (%s)\n", name);
		err = ENOTSUP;
	}

 out:
	mem_deref(name);
	mem_deref(prm);

	return err;
}


static int cmd_process_edit(struct commands *commands,
			    struct cmd_ctx **ctxp, char key,
			    struct re_printf *pf, void *data)
{
	struct cmd_ctx *ctx;
	bool compl = (key == '\n'), del = false;
	int err;

	if (!ctxp)
		return EINVAL;

	ctx = *ctxp;

	err = editor_input(commands, ctx->mb, key, pf, &del, ctx->is_long);
	if (err)
		return err;

	if (ctx->is_long) {

		if (compl) {

			err = cmd_process_long(commands,
					       (char *)ctx->mb->buf,
					       ctx->mb->end,
					       pf, data);
		}
	}
	else {
		if (compl)
			err = cmd_report(ctx->cmd, pf, ctx->mb, data);
	}

	if (del)
		*ctxp = mem_deref(*ctxp);

	return err;
}


/**
 * Register commands
 *
 * @param commands Commands container
 * @param cmdv     Array of commands
 * @param cmdc     Number of commands
 *
 * @return 0 if success, otherwise errorcode
 */
int cmd_register(struct commands *commands,
		 const struct cmd *cmdv, size_t cmdc)
{
	struct cmds *cmds;
	size_t i;

	if (!commands || !cmdv || !cmdc)
		return EINVAL;

	cmds = cmds_find(commands, cmdv);
	if (cmds)
		return EALREADY;

	/* verify that command is not registered */
	for (i=0; i<cmdc; i++) {
		const struct cmd *cmd = &cmdv[i];

		if (cmd->key) {
			const struct cmd *x = cmd_find_by_key(commands,
							      cmd->key);
			if (x) {
				warning("short command '%c' already"
					" registered as \"%s\"\n",
					x->key, x->desc);
				return EALREADY;
			}
		}

		if (cmd->key == LONG_PREFIX) {
			warning("cmd: cannot register command with"
				" short key '%c'\n", cmd->key);
			return EINVAL;
		}

		if (str_isset(cmd->name) &&
		    cmd_find_long(commands, cmd->name)) {
			warning("cmd: long command '%s' already registered\n",
				cmd->name);
			return EINVAL;
		}
	}

	cmds = mem_zalloc(sizeof(*cmds), destructor);
	if (!cmds)
		return ENOMEM;

	cmds->cmdv = cmdv;
	cmds->cmdc = cmdc;

	list_append(&commands->cmdl, &cmds->le, cmds);

	return 0;
}


/**
 * Unregister commands
 *
 * @param commands Commands container
 * @param cmdv     Array of commands
 */
void cmd_unregister(struct commands *commands, const struct cmd *cmdv)
{
	mem_deref(cmds_find(commands, cmdv));
}


/**
 * Find a long command
 *
 * @param commands Commands container
 * @param name     Name of command, excluding prefix
 *
 * @return Command if found, NULL if not found
 */
const struct cmd *cmd_find_long(const struct commands *commands,
				const char *name)
{
	struct le *le;

	if (!commands || !name)
		return NULL;

	for (le = commands->cmdl.tail; le; le = le->prev) {

		struct cmds *cmds = le->data;
		size_t i;

		for (i=0; i<cmds->cmdc; i++) {

			const struct cmd *cmd = &cmds->cmdv[i];

			if (0 == str_casecmp(name, cmd->name) && cmd->h)
				return cmd;
		}
	}

	return NULL;
}


/**
 * Process input characters to the command system
 *
 * @param commands Commands container
 * @param ctxp     Pointer to context for editor (optional)
 * @param key      Input character
 * @param pf       Print function
 * @param data     Application data
 *
 * @return 0 if success, otherwise errorcode
 */
int cmd_process(struct commands *commands, struct cmd_ctx **ctxp, char key,
		struct re_printf *pf, void *data)
{
	const struct cmd *cmd;

	if (!commands)
		return EINVAL;

	if (key == KEYCODE_NONE) {
		warning("cmd: process: illegal keycode NONE\n");
		return EINVAL;
	}

	/* are we in edit-mode? */
	if (ctxp && *ctxp) {

		if (key == KEYCODE_REL)
			return 0;

		return cmd_process_edit(commands, ctxp, key, pf, data);
	}

	cmd = cmd_find_by_key(commands, key);
	if (cmd) {
		struct cmd_arg arg;

		/* check for parameters */
		if (cmd->flags & CMD_PRM) {

			int err = 0;

			if (ctxp) {
				err = ctx_alloc(ctxp, cmd);
				if (err)
					return err;
			}

			key = isdigit(key) ? key : KEYCODE_REL;

			return cmd_process_edit(commands, ctxp, key, pf, data);
		}

		arg.key      = key;
		arg.prm      = NULL;
		arg.data     = data;

		return cmd->h(pf, &arg);
	}
	else if (key == LONG_PREFIX) {

		int err;

		err = re_hprintf(pf, "%c", LONG_PREFIX);
		if (err)
			return err;

		if (!ctxp) {
			warning("cmd: ctxp is required\n");
			return EINVAL;
		}

		err = ctx_alloc(ctxp, cmd);
		if (err)
			return err;

		(*ctxp)->is_long = true;

		return 0;
	}
	else if (key == '\t') {
		return cmd_print_all(pf, commands, false, true, NULL, 0);
	}

	if (key == KEYCODE_REL)
		return 0;

	return cmd_print(pf, commands);
}


struct cmd_sort {
	struct le le;
	const struct cmd *cmd;
};


static bool sort_handler(struct le *le1, struct le *le2, void *arg)
{
	struct cmd_sort *cs1 = le1->data;
	struct cmd_sort *cs2 = le2->data;
	const struct cmd *cmd1 = cs1->cmd;
	const struct cmd *cmd2 = cs2->cmd;
	bool print_long  = *(bool *)arg;

	if (print_long) {
		return str_casecmp(cs2->cmd->name ? cs2->cmd->name : "",
				   cs1->cmd->name ? cs1->cmd->name : "") >= 0;
	}
	else {
		return tolower(cmd2->key) >= tolower(cmd1->key);
	}
}


static int cmd_print_all(struct re_printf *pf,
			 const struct commands *commands,
			 bool print_long, bool print_short,
			 const char *match, size_t match_len)
{
	struct list sortedl = LIST_INIT;
	struct le *le;
	size_t width_long = 1;
	size_t width_short = 5;
	char fmt[64];
	char buf[16];
	int err = 0;

	if (!commands)
		return EINVAL;

	for (le = commands->cmdl.head; le; le = le->next) {

		struct cmds *cmds = le->data;
		size_t i;

		for (i=0; i<cmds->cmdc; i++) {

			const struct cmd *cmd = &cmds->cmdv[i];
			struct cmd_sort *cs;

			if (match && match_len) {

				if (str_len(cmd->name) >= match_len &&
				    0 == memcmp(cmd->name, match, match_len)) {
					/* Match */
				}
				else {
					continue;
				}
			}

			if (!str_isset(cmd->desc))
				continue;

			if (print_short && !print_long) {

				if (cmd->key == KEYCODE_NONE)
					continue;
			}

			cs = mem_zalloc(sizeof(*cs), NULL);
			if (!cs) {
				err = ENOMEM;
				goto out;
			}
			cs->cmd = cmd;

			list_append(&sortedl, &cs->le, cs);

			width_long = max(width_long, 1+str_len(cmd->name)+3);
		}
	}

	list_sort(&sortedl, sort_handler, &print_long);

	if (re_snprintf(fmt, sizeof(fmt),
			"  %%-%zus    %%-%zus    %%s\n",
			width_long, width_short) < 0) {
		err = ENOMEM;
		goto out;
	}

	for (le = sortedl.head; le; le = le->next) {
		struct cmd_sort *cs = le->data;
		const struct cmd *cmd = cs->cmd;
		char namep[64] = "";

		if (print_long && str_isset(cmd->name)) {
			re_snprintf(namep, sizeof(namep), "%c%s%s",
				    LONG_PREFIX, cmd->name,
				    (cmd->flags & CMD_PRM) ? " .." : "");
		}

		err |= re_hprintf(pf, fmt,
				  namep,
				  (print_short && cmd->key)
				    ? cmd_name(buf, sizeof(buf), cmd)
				    : "",
				  cmd->desc);
	}

	err |= re_hprintf(pf, "\n");

 out:
	list_flush(&sortedl);
	return err;
}


/**
 * Print a list of available commands
 *
 * @param pf       Print function
 * @param commands Commands container
 *
 * @return 0 if success, otherwise errorcode
 */
int cmd_print(struct re_printf *pf, const struct commands *commands)
{
	int err = 0;

	if (!pf)
		return EINVAL;

	err |= re_hprintf(pf, "--- Help ---\n");
	err |= cmd_print_all(pf, commands, true, true, NULL, 0);
	err |= re_hprintf(pf, "\n");

	return err;
}


/**
 * Initialize the commands subsystem.
 *
 * @param commandsp  Pointer to allocated commands
 *
 * @return 0 if success, otherwise errorcode
 */
int cmd_init(struct commands **commandsp)
{
	struct commands *commands;

	if (!commandsp)
		return EINVAL;

	commands = mem_zalloc(sizeof(*commands), commands_destructor);
	if (!commands)
		return ENOMEM;

	list_init(&commands->cmdl);

	*commandsp = commands;

	return 0;
}
