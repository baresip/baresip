/**
 * @file multicast.c
 *
 * @note only std payload types (pt) of RTP are acceped! (PCMU, PCMA, G722 ...)
 *
 * Copyright (C) 2021 Commend.com - c.huber@commend.com
 */

#include <re.h>
#include <baresip.h>

#include "multicast.h"

#define DEBUG_MODULE "multicast"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


/**
 * @brief Decode IP-addess <IP>:<PORT>
 *
 * @param pladdr	Parameter string
 * @param addr		Address ptr
 *
 * @return int 0 if success, errorcode otherwise
 */
static int decode_addr(struct pl *pladdr, struct sa *addr)
{
	int err = 0;

	err = sa_decode(addr, pladdr->p, pladdr->l);
	if (err)
		warning ("multicast: addess decode %m\n", err);


	if (sa_port(addr) % 2) {
		err = EINVAL;
		warning("multicast: addess port for RTP should be even (%d)\n",
			sa_port(addr));
	}

	return err;
}


/**
 * @brief Decode Audiocodec <CODEC>
 *
 * @param plcodec	Parameter string
 * @param codecptr	Codec ptr
 *
 * @return int 0 if success, errorcode otherwise
 */
static int decode_codec(struct pl *plcodec, struct aucodec **codecptr)
{
	int err = 0;
	struct list *acodeclist = baresip_aucodecl();
	struct aucodec *codec;
	struct le *le;

	LIST_FOREACH(acodeclist, le) {
		codec = list_ledata(le);
		if (0 == pl_strcasecmp(plcodec, codec->name))
			break;

		codec = NULL;
	}

	if (!codec) {
		err = EINVAL;
		warning ("multicast: codec not found (%r)\n", plcodec);
	}

	*codecptr = codec;
	return err;
}


/**
 * @brief Check audio encoder RTP payload type
 *
 * @param ac	Audiocodec object
 *
 * @return int 0 if succes, errorcode otherwise
 */
static int check_rtp_pt(struct aucodec *ac)
{
	if (!ac)
		return EINVAL;

	return ac->pt ? 0 : ENOTSUP;
}


/**
 * @brief Create a new multicast sender
 *
 * @param pf	Printer
 * @param arg	Command arguments
 *
 * @return int 0 if success, errorcode otherwise
 */
static int cmd_mcsend(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl pladdr, plcodec;
	struct sa addr;
	struct aucodec *codec = NULL;

	err = re_regex(carg->prm, str_len(carg->prm),
		"addr=[^ ]* codec=[^ ]*", &pladdr, &plcodec);
	if (err)
		goto out;

	err = decode_addr(&pladdr, &addr);
	err |= decode_codec(&plcodec, &codec);
	if (err)
		goto out;

	err = check_rtp_pt(codec);
	if (err) {
		warning ("multicast: non standardized RTP "
			"payload type found as codec: %m\n", err);
		goto out;
	}

	err = mcsender_alloc(&addr, codec);

  out:
	if (err) {
		re_hprintf(pf,
			"usage: /mcsend addr=<IP>:<PORT> codec=<CODEC>\n");
		re_hprintf(pf, "errorcode: %d (%m)\n", err, err);
	}

	return err;
}


/**
 * @brief Stop all multicast sender
 *
 * @param pf	Printer
 * @param arg	Command arguments
 *
 * @return int always 0
 */
static int cmd_mcstopall(struct re_printf *pf, void *arg)
{
	(void) pf;
	(void) arg;

	mcsender_stopall();
	return 0;
}


/**
 * @brief Stop a specified multicast sender
 *
 * @param pf	Printer
 * @param arg	Command arguments
 *
 * @return int 0 if success, errorcode otherwise
 */
static int cmd_mcstop(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl pladdr;
	struct sa addr;

	err = re_regex(carg->prm, str_len(carg->prm),
		"addr=[^ ]*", &pladdr);
	if (err)
		goto out;

	err = decode_addr(&pladdr, &addr);
	if (err)
		goto out;

	mcsender_stop(&addr);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcstop addr=<IP>:<PORT>\n");

	return err;
}


/**
 * Print all multicast information
 */
static int cmd_mcinfo(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	mcsender_print(pf);
	/* mcrecv_printinfo(); */

	return 0;
}


/**
 * user callable methods via menue
 */
static const struct cmd cmdv[] = {
	{"mcinfo",    0, CMD_PRM, "Show multicast information", cmd_mcinfo   },

	{"mcsend",    0, CMD_PRM, "Send multicast"            , cmd_mcsend   },
	{"mcstop",    0, CMD_PRM, "Stop multicast"            , cmd_mcstop   },
	{"mcstopall", 0, CMD_PRM, "Stop all multicast"        , cmd_mcstopall},

	/*
	{"mc_register", 0, CMD_PRM, "Register a new MC to listen on",
		cmd_mc_register},
	{"mc_unregister", 0, CMD_PRM, "Unregister an MC", cmd_mc_unregister},
	{"mc_chprio", 0, CMD_PRM, "Change priority of existing MC listener",
		cmd_mc_chprio},
	{"mc_clearl", 0, CMD_PRM, "Clear all existing MC listeners",
	cmd_mc_clearl},
	{"mc_clears", 0, CMD_PRM, "Clear all existing MC sender",
	cmd_mc_clears},
	{"mc_create", 0, CMD_PRM, "Create a MC", cmd_mc_create},
	{"mc_stop", 0, CMD_PRM, "Stop a MC", cmd_mc_stop},
	{"mc_prio_en", 0, CMD_PRM, "Enable/Disable all MC lower than given",
		cmd_mc_disableprio},
	{"mc_send_en", 0, CMD_PRM, "Enable/Disable MC send", cmd_mc_send_en},
	{"mc_receive_en", 0, CMD_PRM, "Enable/Disable MC receive",
		cmd_mc_receive_en}
	*/
};


static int module_init(void)
{
	int err = 0;

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));

	return err;
}


static int module_close(void)
{
	mcsender_stopall();
	/* mcrecever_stopall(); */

	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


const struct mod_export DECL_EXPORTS(multicast) = {
	"multicast",
	"application",
	module_init,
	module_close
};
