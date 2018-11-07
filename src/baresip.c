/**
 * @file baresip.c Top-level baresip struct
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/*
 * Top-level struct that holds all other subsystems
 * (move this instance to main.c later)
 */
static struct baresip {
	struct network *net;
	struct contacts *contacts;
	struct commands *commands;
	struct player *player;
	struct message *message;
	struct list mnatl;
	struct list mencl;
	struct list aucodecl;
	struct list ausrcl;
	struct list auplayl;
	struct list aufiltl;
	struct list vidcodecl;
	struct list vidsrcl;
	struct list vidispl;
	struct list vidfiltl;
	struct ui_sub uis;
} baresip;


static int cmd_quit(struct re_printf *pf, void *unused)
{
	int err;

	(void)unused;

	err = re_hprintf(pf, "Quit\n");

	ua_stop_all(false);

	return err;
}


static int insmod_handler(struct re_printf *pf, void *arg)
{
       const struct cmd_arg *carg = arg;
       int err;

       err = module_load(carg->prm);
       if (err) {
               return re_hprintf(pf, "insmod: ERROR: could not load module"
                                 " '%s': %m\n", carg->prm, err);
       }

       return re_hprintf(pf, "loaded module %s\n", carg->prm);
}


static int rmmod_handler(struct re_printf *pf, void *arg)
{
       const struct cmd_arg *carg = arg;
       (void)pf;

       module_unload(carg->prm);

       return 0;
}


static const struct cmd corecmdv[] = {
	{"quit", 'q', 0, "Quit",                     cmd_quit             },
	{"insmod", 0, CMD_PRM, "Load module",        insmod_handler       },
	{"rmmod",  0, CMD_PRM, "Unload module",      rmmod_handler        },
};


/**
 * Initialise the top-level baresip object
 *
 * @param cfg          Global configuration
 * @param prefer_ipv6  True to prefer IPv6, false to prefer IPv4
 *
 * @return 0 if success, otherwise errorcode
 */
int baresip_init(struct config *cfg, bool prefer_ipv6)
{
	int err;

	if (!cfg)
		return EINVAL;

	baresip.net = mem_deref(baresip.net);

	list_init(&baresip.mnatl);
	list_init(&baresip.mencl);
	list_init(&baresip.aucodecl);
	list_init(&baresip.ausrcl);
	list_init(&baresip.auplayl);
	list_init(&baresip.vidcodecl);
	list_init(&baresip.vidsrcl);
	list_init(&baresip.vidispl);
	list_init(&baresip.vidfiltl);

	/* Initialise Network */
	err = net_alloc(&baresip.net, &cfg->net,
			prefer_ipv6 ? AF_INET6 : AF_INET);
	if (err) {
		warning("ua: network init failed: %m\n", err);
		return err;
	}

	err = contact_init(&baresip.contacts);
	if (err)
		return err;

	err = cmd_init(&baresip.commands);
	if (err)
		return err;

	err = play_init(&baresip.player);
	if (err)
		return err;

	err = message_init(&baresip.message);
	if (err) {
		warning("baresip: message init failed: %m\n", err);
		return err;
	}

	err = cmd_register(baresip.commands, corecmdv, ARRAY_SIZE(corecmdv));
	if (err)
		return err;

	return 0;
}


/**
 * Close the top-level baresip object
 */
void baresip_close(void)
{
	cmd_unregister(baresip.commands, corecmdv);

	baresip.message = mem_deref(baresip.message);
	baresip.player = mem_deref(baresip.player);
	baresip.commands = mem_deref(baresip.commands);
	baresip.contacts = mem_deref(baresip.contacts);

	baresip.net = mem_deref(baresip.net);

	ui_reset(&baresip.uis);
}


/**
 * Get the network subsystem
 *
 * @return Network subsystem
 */
struct network *baresip_network(void)
{
	return baresip.net;
}


/**
 * Get the contacts subsystem
 *
 * @return Contacts subsystem
 */
struct contacts *baresip_contacts(void)
{
	return baresip.contacts;
}


/**
 * Get the commands subsystem
 *
 * @return Commands subsystem
 */
struct commands *baresip_commands(void)
{
	return baresip.commands;
}


/**
 * Get the audio player
 *
 * @return Audio player
 */
struct player *baresip_player(void)
{
	return baresip.player;
}


/**
 * Get the list of Media NATs
 *
 * @return List of Media NATs
 */
struct list *baresip_mnatl(void)
{
	return &baresip.mnatl;
}


/**
 * Get the list of Media encryptions
 *
 * @return List of Media encryptions
 */
struct list *baresip_mencl(void)
{
	return &baresip.mencl;
}


/**
 * Get the Message subsystem
 *
 * @return Message subsystem
 */
struct message *baresip_message(void)
{
	return baresip.message;
}


/**
 * Get the list of Audio Codecs
 *
 * @return List of audio-codecs
 */
struct list *baresip_aucodecl(void)
{
	return &baresip.aucodecl;
}


/**
 * Get the list of Audio Sources
 *
 * @return List of audio-sources
 */
struct list *baresip_ausrcl(void)
{
	return &baresip.ausrcl;
}


/**
 * Get the list of Audio Players
 *
 * @return List of audio-players
 */
struct list *baresip_auplayl(void)
{
	return &baresip.auplayl;
}


/**
 * Get the list of Audio Filters
 *
 * @return List of audio-filters
 */
struct list *baresip_aufiltl(void)
{
	return &baresip.aufiltl;
}


/**
 * Get the list of Video codecs
 *
 * @return List of video-codecs
 */
struct list *baresip_vidcodecl(void)
{
	return &baresip.vidcodecl;
}


/**
 * Get the list of Video sources
 *
 * @return List of video-sources
 */
struct list *baresip_vidsrcl(void)
{
	return &baresip.vidsrcl;
}


/**
 * Get the list of Video displays
 *
 * @return List of video-displays
 */
struct list *baresip_vidispl(void)
{
	return &baresip.vidispl;
}


/**
 * Get the list of Video filters
 *
 * @return List of video-filters
 */
struct list *baresip_vidfiltl(void)
{
	return &baresip.vidfiltl;
}


/**
 * Get the User Interface (UI) subsystem
 *
 * @return User Interface (UI) subsystem
 */
struct ui_sub *baresip_uis(void)
{
	return &baresip.uis;
}
