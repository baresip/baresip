/**
 * @file multicast.c
 *
 * @note supported codecs are PCMU, PCMA, G722
 *
 * Copyright (C) 2021 Commend.com - c.huber@commend.com
 */

#include <re.h>
#include <baresip.h>

#include "multicast.h"

#define DEBUG_MODULE "multicast"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


struct mccfg {
	uint32_t callprio;
	uint32_t ttl;
	uint32_t tfade;
};

static struct mccfg mccfg = {
	0,
	1,
	125,
};


/**
 * Decode IP-address <IP>:<PORT>
 *
 * @param pladdr Parameter string
 * @param addr   Address ptr
 *
 * @return 0 if success, otherwise errorcode
 */
static int decode_addr(struct pl *pladdr, struct sa *addr)
{
	int err = 0;

	err = sa_decode(addr, pladdr->p, pladdr->l);
	if (err)
		warning ("multicast: address decode (%m)\n", err);


	if (sa_port(addr) % 2)
		warning("multicast: address port for RTP should be even"
			" (%d)\n" , sa_port(addr));

	return err;
}


/**
 * Decode audiocodec <CODEC>
 *
 * @param plcodec  Parameter string
 * @param codecptr Codec ptr
 *
 * @return 0 if success, otherwise errorcode
 */
static int decode_codec(struct pl *plcodec, struct aucodec **codecptr)
{
	int err = 0;
	struct list *acodeclist = baresip_aucodecl();
	struct aucodec *codec = NULL;
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
 * Check audio encoder RTP payload type
 *
 * @param ac Audiocodec object
 *
 * @return 0 if success, otherwise errorcode
 */
static int check_rtp_pt(struct aucodec *ac)
{
	if (!ac)
		return EINVAL;

	return ac->pt ? 0 : ENOTSUP;
}


/**
 * Getter for the call priority
 *
 * @return uint8_t call priority
 */
uint8_t multicast_callprio(void)
{
	return mccfg.callprio;
}


/**
 * Getter for configurable multicast TTL
 *
 * @return uint8_t multicast TTL
 */
uint8_t multicast_ttl(void)
{
	return mccfg.ttl;
}


/**
 * Getter for configurable multicast fade in/out time
 *
 * @return uint32_t multicast fade time
 */
uint32_t multicast_fade_time(void)
{
	return mccfg.tfade;
}


/**
 * Create a new multicast sender
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode
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
	if (err)
		goto out;

	err = mcsender_alloc(&addr, codec);

  out:
	if (err)
		re_hprintf(pf,
			"usage: /mcsend addr=<IP>:<PORT> codec=<CODEC>\n");

	return err;
}


/**
 * Enable / Disable all multicast sender without removing it
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mcsenden(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl plenable;
	bool enable;

	err = re_regex(carg->prm, str_len(carg->prm),
		"enable=[^ ]*", &plenable);
	if (err)
		goto out;

	enable = pl_u32(&plenable);
	mcsender_enable(enable);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcsenden enable=<0,1>\n");

	return err;
}


/**
 * Stop all multicast sender
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return always 0
 */
static int cmd_mcstopall(struct re_printf *pf, void *arg)
{
	(void) pf;
	(void) arg;

	mcsender_stopall();
	return 0;
}


/**
 * Stop a specified multicast sender
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode
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
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return alwasys 0
 */
static int cmd_mcinfo(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	mcsender_print(pf);
	mcreceiver_print(pf);

	return 0;
}


/**
 * Create a new multicast listener with prio
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mcreg(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl pladdr, plprio;
	struct sa addr;
	uint32_t prio;

	err = re_regex(carg->prm, str_len(carg->prm), "addr=[^ ]* prio=[^ ]*",
		&pladdr, &plprio);
	if (err)
		goto out;

	prio = pl_u32(&plprio);
	err = decode_addr(&pladdr, &addr);
	if (err || !prio) {
		if (!prio)
			err = EINVAL;
		goto out;
	}

	err = mcreceiver_alloc(&addr, prio);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcreg addr=<IP>:<PORT> "
			   "prio=<1-255>\n");

	return err;
}


/**
 * Un-register a multicast listener
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mcunreg(struct re_printf *pf, void *arg)
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

	mcreceiver_unreg(&addr);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcunreg addr=<IP>:<PORT>\n");

	return err;
}


/**
 * Un-register all multicast listener
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return always 0
 */
static int cmd_mcunregall(struct re_printf *pf, void *arg)
{
	(void) pf;
	(void) arg;

	mcreceiver_unregall();
	return 0;
}


/**
 * Change priority of existing multicast listener
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return  0 if success, otherwise errorcode
 */
static int cmd_mcchprio(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl pladdr, plprio;
	uint32_t prio;
	struct sa addr;

	err = re_regex(carg->prm, str_len(carg->prm),
		"addr=[^ ]* prio=[^ ]*", &pladdr, &plprio);
	if (err)
		goto out;

	err = decode_addr(&pladdr, &addr);
	if (err)
		goto out;

	prio = pl_u32(&plprio);

	err = mcreceiver_chprio(&addr, prio);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcchprio addr=<IP>:<PORT> "
			"prio=<1-255>\n");

	return err;
}


/**
 * Enables all multicast listener with prio <= given prio and
 * disables those with prio > given pri
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mcprioen(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl plprio;
	uint32_t prio;

	err = re_regex(carg->prm, str_len(carg->prm),
		"prio=[^ ]*", &plprio);
	if (err)
		goto out;

	prio = pl_u32(&plprio);
	mcreceiver_enprio(prio);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcprioen prio=<1-255>\n");

	return err;
}


/**
 * Enable / Disable a certain range of priorities
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mcprioren(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl plpriol, plprioh, plenable;
	uint32_t priol = 0, prioh = 0;
	bool enable = false;

	err = re_regex(carg->prm, str_len(carg->prm),
		"range=[0-9]*-[0-9]* enable=[0-1]1", &plpriol, &plprioh,
		&plenable);
	if (err)
		goto out;

	priol = pl_u32(&plpriol);
	prioh = pl_u32(&plprioh);
	enable = pl_u32(&plenable);

	if (priol > prioh) {
		err = EINVAL;
		goto out;
	}

	mcreceiver_enrangeprio(priol, prioh, enable);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcprioren range=<1-255>-<1-255>"
			" enable=<0,1>\n");

	return err;
}


/**
 * Set specified multicast as ignored
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode

 */
static int cmd_mcignore(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl plprio;
	uint32_t prio = 0;

	err = re_regex(carg->prm, str_len(carg->prm),
		"prio=[^ ]*", &plprio);
	if (err)
		goto out;

	prio = pl_u32(&plprio);

	if (!prio) {
		err = EINVAL;
		goto out;
	}

	err = mcreceiver_prioignore(prio);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcignore prio=<1-255>\n");

	return err;
}


/**
 * Toggle mute of multicast
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mcmute(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl plprio;
	uint32_t prio = 0;

	err = re_regex(carg->prm, str_len(carg->prm), "prio=[^ ]*", &plprio);
	if (err)
		goto out;

	prio = pl_u32(&plprio);

	if (!prio) {
		err = EINVAL;
		goto out;
	}

	err = mcreceiver_mute(prio);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcmute prio=<1-255>\n");

	return err;
}


/**
 * Enable / Disable all multicast receiver without removing it
 *
 * @param pf  Printer
 * @param arg Command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mcregen(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;
	struct pl plenable;
	bool enable;

	err = re_regex(carg->prm, str_len(carg->prm),
		"enable=[^ ]*", &plenable);
	if (err)
		goto out;

	enable = pl_u32(&plenable);
	mcreceiver_enable(enable);

  out:
	if (err)
		re_hprintf(pf, "usage: /mcregen enable=<0,1>");

	return err;
}


/**
 * config handler: call this handler foreach line given by @conf_apply function
 *
 * @param pl  PL containing the parameter of the config line
 * @param arg (int*) external priority counter
 *
 * @return 0 if success, otherwise errorcode
 */
static int module_read_config_handler(const struct pl *pl, void *arg)
{
	struct cmd_arg cmd_arg;
	char buf[64];
	int err = 0;
	int n = 0;
	int *prio = (int *) arg;

	if (pl_strchr(pl, '-'))
		goto out;

	n = re_snprintf(buf, sizeof(buf), "addr=%r prio=%d", pl, *prio);
	if (n < 0)
		goto out;

	cmd_arg.prm = buf;
	err = cmd_mcreg(NULL, &cmd_arg);

  out:
	if (!err)
		(*prio)++;

	return err;
}


/**
 * Read the config lines for configured multicast addresses
 *
 * @return 0 if success, otherwise errorcode
 */
static int module_read_config(void)
{
	int err = 0, prio = 1;
	struct sa laddr;

	(void)conf_get_u32(conf_cur(), "multicast_call_prio", &mccfg.callprio);
	if (mccfg.callprio > 255)
		mccfg.callprio = 255;

	(void)conf_get_u32(conf_cur(), "multicast_ttl", &mccfg.ttl);
	if (mccfg.ttl > 255)
		mccfg.ttl = 255;

	(void)conf_get_u32(conf_cur(), "multicast_fade_time", &mccfg.tfade);
	if (mccfg.tfade > 2000)
		mccfg.tfade = 2000;

	sa_init(&laddr, AF_INET);
	err = conf_apply(conf_cur(), "multicast_listener",
		module_read_config_handler, &prio);
	if (err)
		warning("Could not parse multicast config from file");

	return err;
}


static const struct cmd cmdv[] = {
	{"mcinfo",    0, CMD_PRM, "Show multicast information", cmd_mcinfo   },

	{"mcsend",    0, CMD_PRM, "Send multicast"            , cmd_mcsend   },
	{"mcstop",    0, CMD_PRM, "Stop multicast"            , cmd_mcstop   },
	{"mcstopall", 0, CMD_PRM, "Stop all multicast"        , cmd_mcstopall},
	{"mcsenden",  0, CMD_PRM, "Enable/Disable all sender" , cmd_mcsenden },

	{"mcreg",     0, CMD_PRM, "Reg. multicast listener"   , cmd_mcreg    },
	{"mcunreg",   0, CMD_PRM, "Unreg. multicast listener" , cmd_mcunreg  },
	{"mcunregall",0, CMD_PRM, "Unreg. all multicast listener",
		cmd_mcunregall},
	{"mcchprio"  ,0, CMD_PRM, "Change priority"           , cmd_mcchprio },
	{"mcprioen"  ,0, CMD_PRM, "Enable Listener Prio >="   , cmd_mcprioen },
	{"mcprioren", 0, CMD_PRM, "Enable Listener Prio range", cmd_mcprioren},
	{"mcignore",  0, CMD_PRM, "Ignore stream priority"    , cmd_mcignore },
	{"mcmute",    0, CMD_PRM, "Mute stream priority"      , cmd_mcmute   },
	{"mcregen"   ,0, CMD_PRM, "Enable / Disable all listener",
		cmd_mcregen},
};


static int module_init(void)
{
	int err = 0;

	err = module_read_config();
	err |= cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));

	err |= mcsource_init();
	err |= mcplayer_init();

	if (!err)
		info("multicast: module init\n");

	return err;
}


static int module_close(void)
{
	mcsender_stopall();
	mcreceiver_unregall();

	cmd_unregister(baresip_commands(), cmdv);

	mcsource_terminate();
	mcplayer_terminate();

	return 0;
}


const struct mod_export DECL_EXPORTS(multicast) = {
	"multicast",
	"application",
	module_init,
	module_close
};
