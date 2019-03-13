/**
 * @file soap.c
 *
 * Performance speedup possible: Write wrapper functions for the creation of
 * the children which allow stuff like create child AND set the value or
 * parameter, and so on.
 *
 * Copyright (C) 2018 commend.com - Christoph Huber
 */

/**
 * UNIVERSAL UDP Port    : 3702
 * BROADCAST IPv4		 : 239.255.255.250
 * BROADCAST IPv6		 : FF02::C
 */

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

#include <re.h>
#include <baresip.h>

#include "soap.h"
#include "onvif_auth.h"
#include "filter.h"
#include "rtspd.h"
#include "scopes.h"
#include "device.h"
#include "wsd.h"
#include "media.h"

#define DEBUG_MODULE "onvif"
#define DEBUG_LEVEL 6
#include <re_dbg.h>

struct pl onvif_config_path;


static int com_onvif_src_en(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;

	(void)pf;

	if (str_isset(carg->prm)) {
		if (!str_bool(&value, carg->prm)) {
			onvif_set_aufilter_src_en(value);
			info ("Onvif Src: %s", value ? "Enabled" : "Disabled");
		}
	}

	return 0;
}


static int com_onvif_play_en(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;

	(void)pf;

	if (str_isset(carg->prm)) {
		if (!str_bool(&value, carg->prm)) {
			onvif_set_aufilter_play_en(value);
			info ("Onvif Play: %s", value ? "Enabled" : "Disabled");
		}
	}

	return 0;
}


static const struct cmd cmdv[] = {
	{"onvif_rec_enable", 0, CMD_PRM,
		"Enable the Audio Source in the Onvif Pipeline", com_onvif_src_en},
	{"onvif_play_enable", 0, CMD_PRM,
		"Enable the Audio Play in the Onvif Pipeline", com_onvif_play_en},
};


static int module_init(void)
{
	int err = 0;
	struct sa udp_laddr, http_laddr, rtsp_laddr, bc_laddr;
	const struct sa *laddr;

	rtsp_init();

	err = conf_get(conf_cur(), "onvif_ConfigPath", &onvif_config_path);
	if (err) {
		warning ("onvif: %s Could not find onvif config path\n", __func__);
		return err;
	}

	err = media_init();
	if (err) {
		warning ("onvif: %s Could not load standard Media Profile (%m)\n",
			__func__, err);
		return err;
	}

	err = services_init();
	if (err) {
		warning ("onvif: %s Could not load device services and"
			"capabilities (%m)\n", __func__, err);
		return err;
	}

	err = onvif_auth_init_users();
	if (err) {
		warning ("onvif: %s Could not load user settings (%m)\n",
			__func__, err);
		return err;
	}

	err = scope_init();
	if (err) {
		warning ("onvif: %s Could not load dynamic scopes (%m)\n",
			__func__, err);
		return err;
	}

	wsd_init();

	laddr = net_laddr_af(baresip_network(), AF_INET);
	sa_cpy(&http_laddr, laddr);
	sa_cpy(&rtsp_laddr, laddr);
	sa_set_port(&http_laddr, DEFAULT_ONVIF_PORT);
	sa_set_port(&rtsp_laddr, DEFAULT_RTSP_PORT);

	err = sa_set_str(&udp_laddr, "0.0.0.0", SOAP_BC_PORT);
	if (err) {
		warning("onvif: %s Could not parse %s:%d\n",
			__func__, "0.0.0.0", SOAP_BC_PORT);
		return err;
	}

	err |= sa_set_str(&bc_laddr, SOAP_BC_IP4, SOAP_BC_PORT);
	if (err) {
		warning("onvif: %s Could not parse %s:%d\n",
			__func__, SOAP_BC_IP4, SOAP_BC_PORT);
		return err;
	}


	err = rtsp_listen(&rtspsock, &rtsp_laddr, rtsp_msg_handler, NULL);
	if (err) {
		warning ("onvif/RTSP: %s Could not listen on %J\n",
			__func__, &rtsp_laddr);
		return err;
	}

	err = udp_listen(&udps, &udp_laddr, soap_udp_recv_handler, NULL);
	if (err) {
		warning ("onvif/UDP: %s Could not listen on %J\n",
			__func__, &udp_laddr);
		return err;
	}

	err = udp_multicast_join(udps, &bc_laddr);
	if (err) {
		warning ("onvif/MC: %s Could not listen on %J\n",
			__func__, &bc_laddr);
		return err;
	}

	err = http_listen(&httpsock, &http_laddr, http_req_handler, NULL);
	if (err) {
		warning ("onvif/HTTP: %s Could not listen on %J\n",
			__func__, &http_laddr);
		return err;
	}

	info ("onvif: listen\n"
		"  -RTSP   %J\n"
		"  -HTTP   %J\n"
		"  -UDP    %J\n"
		"  -WSD-BC %J\n",
		&rtsp_laddr, &http_laddr, &udp_laddr, &bc_laddr);

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	register_onvif_filter();

	return err;
}


static int module_close(void)
{
	int err;

	err = wsd_deinit();
	media_deinit();
	services_deinit();
	rtsp_session_deinit();
	scope_deinit();
	onvif_auth_deinit_users();

	cmd_unregister(baresip_commands(), cmdv);
	unregister_onvif_filter();

	rtspsock = mem_deref(rtspsock);
	udps = mem_deref(udps);
	httpsock = mem_deref(httpsock);

	return err;
}


const struct mod_export DECL_EXPORTS(onvif) = {
	"onvif",
	"application",
	module_init,
	module_close
};