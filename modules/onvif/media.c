/**
 * @file media.c
 *
 * For more information see:
 * https://www.onvif.org/ver10/media/wsdl/media.wsdl
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "soap.h"
#include "fault.h"
#include "soap_str.h"
#include "wsd.h"
#include "media.h"
#include "rtspd.h"

#define DEBUG_MODULE "onvif_media"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


/* Only if this is a null ptr */
struct profile *std_profile = NULL;

/* List which hold all the existing configurations */
struct list profile_l = LIST_INIT;
struct list vs_l = LIST_INIT;
struct list ve_l = LIST_INIT;
struct list as_l = LIST_INIT;
struct list ae_l = LIST_INIT;
struct list ao_l = LIST_INIT;
struct list ad_l = LIST_INIT;


static void profile_destructor(void *arg)
{
	struct profile *prof = (struct profile *)arg;

	if (prof->vsc)
		prof->vsc->usecount--;

	if (prof->vec)
		prof->vec->usecount--;

	if (prof->asc)
		prof->asc->usecount--;

	if (prof->aec)
		prof->aec->usecount--;

	if (prof->aoc)
		prof->aoc->usecount--;

	if (prof->adc)
		prof->adc->usecount--;

	list_unlink(&prof->le);

	prof->vsc = mem_deref(prof->vsc);
	prof->vec = mem_deref(prof->vec);

	prof->asc = mem_deref(prof->asc);
	prof->aec = mem_deref(prof->aec);

	prof->aoc = mem_deref(prof->aoc);
	prof->adc = mem_deref(prof->adc);
}


/**
 * release all media profile informaions
 *
 * @return          0 if success, errorcode otherwise
 *
 */
void media_deinit(void)
{
	list_flush(&profile_l);
	list_flush(&vs_l);
	list_flush(&ve_l);
	list_flush(&as_l);
	list_flush(&ae_l);
	list_flush(&ao_l);
	list_flush(&ad_l);
}


static bool media_cmp_mediareftoken(struct le *le, void *arg)
{
	struct pl *reftoken = (struct pl *)arg;
	struct media_config *cfg = le->data;

	return (0 == pl_strcmp(reftoken, cfg->token));
}


static bool media_cmp_profilreftoken(struct le *le, void *arg)
{
	struct pl *reftoken = (struct pl *)arg;
	struct profile *p = le->data;

	return (0 == pl_strcmp(reftoken, p->token));
}


static bool media_cmp_vssourctoken(struct le *le, void *arg)
{
	struct pl *reftoken = (struct pl *)arg;
	struct media_config *cfg = le->data;

	return (0 == pl_strcmp(reftoken, cfg->t.vs.sourcetoken));
}


static bool media_cmp_assourctoken(struct le *le, void *arg)
{
	struct pl *reftoken = (struct pl *)arg;
	struct media_config *cfg = le->data;

	return (0 == pl_strcmp(reftoken, cfg->t.as.sourcetoken));
}


static bool media_cmp_aooutputtoken(struct le *le, void *arg)
{
	struct pl *reftoken = (struct pl *)arg;
	struct media_config *cfg = le->data;
	return (0 == pl_strcmp(reftoken, cfg->t.ao.outputtoken));
}


/**
 * load the standard profile with fixed configuration
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_init(void)
{
	int err = 0;

	if (std_profile)
		return err;

	std_profile = mem_zalloc(sizeof(struct profile), profile_destructor);
	if (!std_profile)
		return ENOMEM;

	std_profile->vsc = mem_zalloc(sizeof(struct media_config), NULL);
	std_profile->vec = mem_zalloc(sizeof(struct media_config), NULL);

	std_profile->asc = mem_zalloc(sizeof(struct media_config), NULL);
	std_profile->aec = mem_zalloc(sizeof(struct media_config), NULL);

	std_profile->aoc = mem_zalloc(sizeof(struct media_config), NULL);
	std_profile->adc = mem_zalloc(sizeof(struct media_config), NULL);
	if (!std_profile->vsc || !std_profile->vec ||
		!std_profile->asc || !std_profile->aec ||
		!std_profile->aoc || !std_profile->adc)
		return ENOMEM;

	std_profile->fixed = true;
	/* rand_str(std_profile->token, 64); */
	strncpy(std_profile->token, str_profile_token,
		sizeof(std_profile->token));
	strncpy(std_profile->name, str_profile_name,
		sizeof(std_profile->name));

	/* rand_str(std_profile->vsc->token, 64); */
	strncpy(std_profile->vsc->token, str_profile_vsc_token,
		sizeof(std_profile->vsc->token));
	/* rand_str(std_profile->vsc->t.vs.sourcetoken, 64); */
	strncpy(std_profile->vsc->t.vs.sourcetoken, str_profile_vs_token,
		sizeof(std_profile->vsc->t.vs.sourcetoken));
	strncpy(std_profile->vsc->name, str_profile_vs_name,
		sizeof(std_profile->vsc->name));
	std_profile->vsc->usecount = 1;
	std_profile->vsc->t.vs.maxprofile = 12;
	std_profile->vsc->t.vs.viewmodes = NULL;
	std_profile->vsc->t.vs.i.jpeg_i = 1;
	std_profile->vsc->t.vs.i.h264_i = 0;
	std_profile->vsc->t.vs.i.mpeg4_i = 0;
	std_profile->vsc->t.vs.b.x = std_profile->vsc->t.vs.b.y = 1;
	/* std_profile->vsc->t.vs.b.w = 640; */
	/* std_profile->vsc->t.vs.b.h = 480; */
	std_profile->vsc->t.vs.b.w = 128;
	std_profile->vsc->t.vs.b.h = 128;
	/* std_profile->vsc->t.vs.framerate = 15.f; */
	std_profile->vsc->t.vs.framerate = 1.f;

	/* rand_str(std_profile->vec->token, 64); */
	strncpy(std_profile->vec->token, str_profile_ve_token,
		sizeof(std_profile->vec->token));
	strncpy(std_profile->vec->name, str_profile_ve_name,
		sizeof(std_profile->vec->name));
	std_profile->vec->usecount = 1;
	std_profile->vec->t.ve.gfr = false;
	std_profile->vec->t.ve.enc = JPEG;
	/* std_profile->vec->t.ve.res.w   = 640; */
	/* std_profile->vec->t.ve.res.h   = 480; */
	std_profile->vec->t.ve.res.w   = 128;
	std_profile->vec->t.ve.res.h   = 128;
	std_profile->vec->t.ve.quality = 8.f;
	/* std_profile->vec->t.ve.ratec.frl = 15; */
	std_profile->vec->t.ve.ratec.frl = 1;
	std_profile->vec->t.ve.ratec.ei  = 1;
	std_profile->vec->t.ve.ratec.brl = 2048;
	std_profile->vec->t.ve.multicast.addr.type = 0;
	sa_set_str(&std_profile->vec->t.ve.multicast.addr.addr, "0.0.0.0", 0);
	std_profile->vec->t.ve.multicast.ttl = 0;
	std_profile->vec->t.ve.multicast.autostart = false;
	std_profile->vec->t.ve.st = 60;

	/* rand_str(std_profile->asc->token, 64); */
	strncpy(std_profile->asc->token, str_profile_asc_token,
		sizeof(std_profile->asc->token));
	/* rand_str(std_profile->asc->t.as.sourcetoken, 64); */
	strncpy(std_profile->asc->t.as.sourcetoken, str_profile_as_token,
		sizeof(std_profile->asc->t.as.sourcetoken));
	strncpy(std_profile->asc->name, str_profile_as_name,
		sizeof(std_profile->asc->name));
	std_profile->asc->usecount = 1;
	std_profile->asc->t.as.ch = 1;

	/* rand_str(std_profile->aec->token, 64); */
	strncpy(std_profile->aec->token, str_profile_ae_token,
		sizeof(std_profile->asc->token));
	strncpy(std_profile->aec->name, str_profile_ae_name,
		sizeof(std_profile->asc->name));
	std_profile->aec->usecount = 1;
	std_profile->aec->t.ae.enc = G711;
	std_profile->aec->t.ae.br = 64;
	std_profile->aec->t.ae.sr = 8;
	std_profile->aec->t.ae.multicast.addr.type = 0;
	sa_set_str(&std_profile->aec->t.ae.multicast.addr.addr, "0.0.0.0", 0);
	std_profile->aec->t.ae.multicast.ttl = 0;
	std_profile->aec->t.ae.multicast.autostart = false;
	std_profile->aec->t.ae.st = 60;

	/* rand_str(std_profile->aoc->token, 64); */
	strncpy(std_profile->aoc->token, str_profile_aoc_token,
		sizeof(std_profile->aoc->token));
	/* rand_str(std_profile->aoc->t.ao.outputtoken, 64); */
	strncpy(std_profile->aoc->t.ao.outputtoken, str_profile_ao_token,
		sizeof(std_profile->aoc->t.ao.outputtoken));
	strncpy(std_profile->aoc->name, str_profile_ao_name,
		sizeof(std_profile->aoc->name));
	std_profile->aoc->usecount = 1;
	std_profile->aoc->t.ao.sp = HALF_DUPLEX_AUTO;
	std_profile->aoc->t.ao.outputlevel = 8;

	/* rand_str(std_profile->adc->token, 64); */
	strncpy(std_profile->adc->token, str_profile_ad_token,
		sizeof(std_profile->adc->token));
	strncpy(std_profile->adc->name, str_profile_ad_name,
		sizeof(std_profile->adc->name));
	std_profile->adc->usecount = 1;
	std_profile->adc->t.ad.dec = G711;
	std_profile->adc->t.ad.br = 64;
	std_profile->adc->t.ad.sr = 8;
	std_profile->adc->t.ad.ch = 1;

	list_append(&profile_l, &std_profile->le, std_profile);

	list_append(&vs_l, &std_profile->vsc->le, mem_ref(std_profile->vsc));
	list_append(&ve_l, &std_profile->vec->le, mem_ref(std_profile->vec));
	list_append(&as_l, &std_profile->asc->le, mem_ref(std_profile->asc));
	list_append(&ae_l, &std_profile->aec->le, mem_ref(std_profile->aec));
	list_append(&ao_l, &std_profile->aoc->le, mem_ref(std_profile->aoc));
	list_append(&ad_l, &std_profile->adc->le, mem_ref(std_profile->adc));

	return err;
}


/**
 * add the steamsetup information
 *
 * @param msg       request message
 * @param gsu       getstreamuriresponse child where the infos must be added
 * @param p         streaming profile
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_add_streamsetup(const struct soap_msg *msg,
	struct soap_child *gsu, struct profile *p)
{
	int err = 0;
	struct soap_child *muc, *tmpc;
	const struct sa *laddr;
	size_t timeout = 0;

	if (!msg || !gsu)
		return EINVAL;

	laddr =  net_laddr_af(baresip_network(), AF_INET);
	if (!laddr) {
		warning("onvif: %s Could not get local IP address.", __func__);
		return EINVAL;
	}

	muc = soap_add_child(gsu->msg, gsu,
		str_pf_media_wsdl, str_streamuri_mediauri);
	tmpc = soap_add_child(gsu->msg, muc,
		str_pf_schema, str_streamuri_uri);
	err |= soap_set_value_fmt(tmpc, "rtsp://%j:%u%s",
		laddr, DEFAULT_RTSP_PORT, "/stream");

	tmpc = soap_add_child(gsu->msg, muc,
		str_pf_schema, str_streamuri_invalafterconnect);
	err |= soap_set_value_fmt(tmpc, "%s", str_false);
	tmpc = soap_add_child(gsu->msg, muc,
		str_pf_schema, str_streamuri_invalafterreboot);
	err |= soap_set_value_fmt(tmpc, "%s", str_false);
	tmpc = soap_add_child(gsu->msg, muc,
		str_pf_schema, str_streamuri_timeout);

	if (p->aec)
		timeout = p->aec->t.ae.st;
	else
		timeout = p->vec->t.ve.st;

	err |= soap_set_value_fmt(tmpc, "PT%dS", timeout);

	return err;
}


/**
 * add the video source configuration
 *
 * @param pc        child where to add the configuration
 * @param cfg       media config continig video source config
 * @param profile   defines the style of the config entry
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_add_video_source_config(struct soap_child *pc,
	struct media_config *cfg, bool profiles, bool configs)
{
	int err = 0;
	struct soap_child *vscc, *tmpc;

	if (!pc)
		return EINVAL;

	if (!cfg)
		return 0;

	if (profiles) {
		vscc = soap_add_child(pc->msg, pc, str_pf_schema,
				      str_profile_vsc);
	}
	else if (configs) {
		vscc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configurations);
	}
	else {
		vscc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configuration);
	}

	err |= soap_add_parameter_str(vscc, NULL, str_token, strlen(str_token),
		cfg->token, strlen(cfg->token));

	tmpc = soap_add_child(pc->msg, vscc, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->name);

	tmpc = soap_add_child(pc->msg, vscc, str_pf_schema,
			      str_profile_usecount);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->usecount);

	tmpc = soap_add_child(pc->msg, vscc, str_pf_schema,
		str_profile_source_token);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->t.vs.sourcetoken);

	tmpc = soap_add_child(pc->msg, vscc, str_pf_schema,
			      str_profile_bounds);
	err |= soap_add_parameter_uint(tmpc, NULL,
		str_height, strlen(str_height), cfg->t.vs.b.h);
	err |= soap_add_parameter_uint(tmpc, NULL,
		str_width, strlen(str_width), cfg->t.vs.b.w);
	err |= soap_add_parameter_uint(tmpc, NULL,
		"y", strlen("y"), cfg->t.vs.b.y);
	err |= soap_add_parameter_uint(tmpc, NULL,
		"x", strlen("x"), cfg->t.vs.b.x);

	return err;
}


/**
 * add the audio source configuration
 *
 * @param pc        child where to add the configuration
 * @param profile   defines the style of the config entry
 * @param configs   defines if configurations or configuration
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_add_audio_source_config(struct soap_child *pc,
	struct media_config *cfg, bool profile, bool configs)
{
	int err = 0;
	struct soap_child *ascc, *tmpc;

	if (!pc)
		return EINVAL;

	if (!cfg)
		return 0;

	if (profile) {
		ascc = soap_add_child(pc->msg, pc, str_pf_schema,
				      str_profile_asc);
	}
	else if (configs) {
		ascc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configurations);
	}
	else {
		ascc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configuration);
	}

	err |= soap_add_parameter_str(ascc, NULL, str_token,
				      strlen(str_token), cfg->token,
				      strlen(cfg->token));

	tmpc = soap_add_child(pc->msg, ascc, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->name);

	tmpc = soap_add_child(pc->msg, ascc, str_pf_schema,
			      str_profile_usecount);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->usecount);

	tmpc = soap_add_child(pc->msg, ascc, str_pf_schema,
		str_profile_source_token);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->t.as.sourcetoken);

	return err;
}


/**
 * add multicast information to @mc. Set everything to 0 to disable multicast
 *
 * @param mc        child where to add the configuration
 * @param cfg       media config
 * @param is_ve     is it a videoencoder config
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_add_multicastinfo(struct soap_child *mc,
	struct media_config *cfg, bool is_ve)
{
	int err = 0;
	struct soap_child *addrc, *tmpc;
	bool autostart;

	if (!mc || !cfg)
		return EINVAL;

	addrc = soap_add_child(mc->msg, mc, str_pf_schema,
			       str_profile_address);
	tmpc = soap_add_child(mc->msg, addrc, str_pf_schema, str_profile_type);
	err |= soap_set_value_fmt(tmpc, "%s", str_ipv4);

	tmpc = soap_add_child(mc->msg, addrc, str_pf_schema,
			      str_profile_ipv4addr);
	err |= soap_set_value_fmt(tmpc, "%j",
		is_ve ? &cfg->t.ve.multicast.addr.addr :
		&cfg->t.ae.multicast.addr.addr);

	tmpc = soap_add_child(mc->msg, mc, str_pf_schema, str_port);
	err |= soap_set_value_fmt(tmpc, "%d",
		is_ve ? sa_port(&cfg->t.ve.multicast.addr.addr) :
		sa_port(&cfg->t.ae.multicast.addr.addr));
	tmpc = soap_add_child(mc->msg, mc, str_pf_schema, str_ttl);
	err |= soap_set_value_fmt(tmpc, "%d",
		is_ve ? cfg->t.ve.multicast.ttl : cfg->t.ae.multicast.ttl);
	tmpc = soap_add_child(mc->msg, mc, str_pf_schema,
			      str_profile_autostart);

	autostart = is_ve ? cfg->t.ve.multicast.autostart :
		    cfg->t.ae.multicast.autostart;
	if (autostart)
		err |= soap_set_value_fmt(tmpc, "%s", str_true);
	else
		err |= soap_set_value_fmt(tmpc, "%s", str_false);

	return err;
}


/**
 * add the video encoding configuration
 *
 * @param pc        child where to add the configuration
 * @param cfg       media config continig video source config
 * @param profile   defines the style of the config entry
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_add_video_enc_config(struct soap_child *pc,
	struct media_config *cfg, bool profile, bool configs)
{
	int err = 0;
	struct soap_child *vecc, *rc, *rcc, *tmpc, *mcastc;

	if (!pc)
		return EINVAL;

	if (!cfg)
		return 0;

	if (profile) {
		vecc = soap_add_child(pc->msg, pc, str_pf_schema,
				      str_profile_vec);
	}
	else if (configs) {
		vecc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configurations);
	}
	else {
		vecc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configuration);
	}

	err |= soap_add_parameter_str(vecc, NULL, str_token, strlen(str_token),
				      cfg->token, strlen(cfg->token));

	tmpc = soap_add_child(pc->msg, vecc, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->name);

	tmpc = soap_add_child(pc->msg, vecc, str_pf_schema,
			      str_profile_usecount);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->usecount);

	tmpc = soap_add_child(pc->msg, vecc, str_pf_schema,
		str_profile_encoding);
	switch (cfg->t.ve.enc) {
	case JPEG:
		err |= soap_set_value_fmt(tmpc, "%s", str_jpeg);
		break;

	case MPEG4:
	case H264:
		return ENOTSUP;
	}

	rc = soap_add_child(pc->msg, vecc, str_pf_schema,
			    str_profile_resolution);
	tmpc = soap_add_child(pc->msg, rc, str_pf_schema, str_ucwidth);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->t.ve.res.w);
	tmpc = soap_add_child(pc->msg, rc, str_pf_schema, str_ucheight);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->t.ve.res.h);

	tmpc = soap_add_child(pc->msg, vecc, str_pf_schema,
			      str_profile_quality);
	err |= soap_set_value_fmt(tmpc, "%d", (int)cfg->t.ve.quality);

	rcc = soap_add_child(pc->msg, vecc, str_pf_schema,
			     str_profile_ratecontrol);
	tmpc = soap_add_child(pc->msg, rcc, str_pf_schema, str_profile_frl);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->t.ve.ratec.frl);
	tmpc = soap_add_child(pc->msg, rcc, str_pf_schema, str_profile_ei);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->t.ve.ratec.ei);
	tmpc = soap_add_child(pc->msg, rcc, str_pf_schema, str_profile_brl);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->t.ve.ratec.brl);

	/* multicast information ... how does it look like if no multicast
	 * is */
	/* supported */
	mcastc= soap_add_child(pc->msg, vecc, str_pf_schema,
			       str_profile_multicast);
	err |= media_add_multicastinfo(mcastc, cfg, true);

	tmpc = soap_add_child(pc->msg, vecc, str_pf_schema,
		str_profile_sess_timeout);
	err |= soap_set_value_fmt(tmpc, "PT%dS", cfg->t.ve.st);

	return err;
}


/**
 * add the audio encoding configuration
 *
 * @param pc        child where to add the configuration
 * @param profile   defines the style of the config entry
 * @param configs   defines if configuration or configurations should be used
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_add_audio_enc_config(struct soap_child *pc,
	struct media_config *cfg, bool profile, bool configs)
{
	int err = 0;
	struct soap_child *aecc, *tmpc, *mcastc;

	if (!pc)
		return EINVAL;

	if (!cfg)
		return 0;

	if (profile) {
		aecc = soap_add_child(pc->msg, pc, str_pf_schema,
				      str_profile_aec);
	}
	else if (configs) {
		aecc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configurations);
	}
	else {
		aecc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configuration);
	}

	err |= soap_add_parameter_str(aecc, NULL, str_token, strlen(str_token),
				      cfg->token, strlen(cfg->token));

	tmpc = soap_add_child(pc->msg, aecc, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->name);

	tmpc = soap_add_child(pc->msg, aecc, str_pf_schema,
			      str_profile_usecount);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->usecount);

	tmpc = soap_add_child(pc->msg, aecc, str_pf_schema,
		str_profile_encoding);
	switch (cfg->t.ae.enc) {
		case G711:
			err |= soap_set_value_fmt(tmpc, "%s", str_pcmu);
			break;

		case G726:
		case AAC:
			return ENOTSUP;
	}

	tmpc = soap_add_child(pc->msg, aecc, str_pf_schema,
			      str_profile_bitrate);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->t.ae.br);

	tmpc = soap_add_child(pc->msg, aecc, str_pf_schema,
			      str_profile_samplerate);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->t.ae.sr);

	/* multicast information ... how does it look like if no multicast
	 * is */
	/* supported */
	mcastc = soap_add_child(pc->msg, aecc, str_pf_schema,
				str_profile_multicast);
	err |= media_add_multicastinfo(mcastc, cfg, false);

	tmpc = soap_add_child(pc->msg, aecc, str_pf_schema,
		str_profile_sess_timeout);
	err |= soap_set_value_fmt(tmpc, "PT%dS", cfg->t.ae.st);

	return err;
}


/**
 * add the audio decoder configuration
 *
 * @param pc        child where to add the configuration
 * @param profile   defines the style of the config entry
 * @param configs   defines if configuration or configurations should be used
 *
 * @return          0 if success, errorcode otherwise
 */
static int media_add_audio_dec_config(struct soap_child *pc,
	struct media_config *cfg, bool profile, bool configs)
{
	int err = 0;
	struct soap_child *adcc;
	struct soap_child *tmpc;

	if (!pc)
		return EINVAL;

	if (!cfg)
		return 0;

	if (profile) {
		adcc = soap_add_child(pc->msg, pc, str_pf_schema,
				      str_profile_adc);
	}
	else if (configs) {
		adcc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configurations);
	}
	else {
		adcc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configuration);
	}

	err |= soap_add_parameter_str(adcc, NULL, str_token, strlen(str_token),
		cfg->token, strlen(cfg->token));

	tmpc = soap_add_child(pc->msg, adcc, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->name);

	tmpc = soap_add_child(pc->msg, adcc, str_pf_schema,
			      str_profile_usecount);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->usecount);

	return err;
}


/**
 * add the audio ouput configuration
 *
 * @param pc        child where to add the configuration
 * @param profile   defines the style of the config entry
 *
 * @return          0 if success, errorcode otherwise
 */
static int media_add_audio_output_config(struct soap_child *pc,
	struct media_config *cfg, bool profile, bool configs)
{
	int err = 0;
	struct soap_child *aocc, *tmpc;

	if (!pc)
		return EINVAL;

	if (!cfg)
		return 0;

	if (profile) {
		aocc = soap_add_child(pc->msg, pc, str_pf_schema,
				      str_profile_aoc);
	}
	else if (configs) {
		aocc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configurations);
	}
	else {
		aocc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configuration);
	}

	err |= soap_add_parameter_str(aocc, NULL, str_token, strlen(str_token),
		cfg->token, strlen(cfg->token));

	tmpc = soap_add_child(pc->msg, aocc, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->name);

	tmpc = soap_add_child(pc->msg, aocc, str_pf_schema,
			      str_profile_usecount);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->usecount);

	tmpc = soap_add_child(pc->msg, aocc, str_pf_schema,
		str_profile_output_token);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->t.ao.outputtoken);

	tmpc = soap_add_child(pc->msg, aocc, str_pf_schema,
		str_profile_sendprimacy);
	switch (cfg->t.ao.sp) {
		case HALF_DUPLEX_SERVER:
			err |= soap_set_value_fmt(tmpc, "%s",
						  str_profile_sp_hds);
			break;

		case HALF_DUPLEX_CLIENT:
			err |= soap_set_value_fmt(tmpc, "%s",
						  str_profile_sp_hdc);
			break;

		case HALF_DUPLEX_AUTO:
			err |= soap_set_value_fmt(tmpc, "%s",
						  str_profile_sp_hda);
			break;
	}

	tmpc = soap_add_child(pc->msg, aocc, str_pf_schema,
		str_profile_outputlevel);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->t.ao.outputlevel);

	return err;
}


/**
 * add the audio decoder configuration
 *
 * @param pc        child where to add the configuration
 * @param profile   defines the style of the config entry
 *
 * @return          0 if success, errorcode otherwise
 */
static int media_add_audio_decoder_config(struct soap_child *pc,
	struct media_config *cfg, bool profile)
{
	int err = 0;
	struct soap_child *adcc, *tmpc;

	if (!pc)
		return EINVAL;

	if (!cfg)
		return 0;

	if (profile)
		adcc = soap_add_child(pc->msg, pc, str_pf_schema,
				      str_profile_adc);
	else
		adcc = soap_add_child(pc->msg, pc, str_pf_media_wsdl,
				      str_configurations);

	err |= soap_add_parameter_str(adcc, NULL, str_token, strlen(str_token),
				      cfg->token, strlen(cfg->token));

	tmpc = soap_add_child(pc->msg, adcc, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->name);

	tmpc = soap_add_child(pc->msg, adcc, str_pf_schema,
			      str_profile_usecount);
	err |= soap_set_value_fmt(tmpc, "%d", cfg->usecount);

	return err;
}


/**
 * add a profile to the request containing Video & Audio Source config
 * and Video & Audio Encoder config
 *
 * @param pc        child where to add the configuration
 * @param profname  name of the profile to include
 * @param single    do we report a single profile like in CreateProfile
 *
 * @return          0 if success, errorcode otherwise
 */
static int media_add_profile(struct soap_child *gpc, struct profile *p,
	bool single)
{
	int err = 0;
	struct soap_child *pc, *tmpc, *extensionc;

	if (!gpc || !p)
		return EINVAL;

	if (single)
		pc = soap_add_child(gpc->msg, gpc, str_pf_media_wsdl,
			str_profile_profile);
	else
		pc = soap_add_child(gpc->msg, gpc, str_pf_media_wsdl,
			str_profile_profiles);

	if (p->fixed)
		err |= soap_add_parameter_str(pc, NULL,
			str_profile_fixed, strlen(str_profile_fixed),
			str_true, strlen(str_true));
	else
		err |= soap_add_parameter_str(pc, NULL,
			str_profile_fixed, strlen(str_profile_fixed),
			str_false, strlen(str_false));

	err |= soap_add_parameter_str(pc, NULL,
		str_token, strlen(str_token), p->token, strlen(p->token));


	tmpc = soap_add_child(gpc->msg, pc, str_pf_schema, str_name);
	err |= soap_set_value_fmt(tmpc, "%s", p->name);

	/*VideoSourceConfig */
	err |= media_add_video_source_config(pc, p->vsc, true, false);

	/*AudioSourceConfig */
	err |= media_add_audio_source_config(pc, p->asc, true, false);

	/*VideoEncoderConfig */
	err |= media_add_video_enc_config(pc, p->vec, true, false);

	/*AudioEncoderConfig */
	err |= media_add_audio_enc_config(pc, p->aec, true, false);

	extensionc = soap_add_child(gpc->msg, pc, str_pf_schema,
				    str_extension);
	/*AudioOutputConfig */
	err |= media_add_audio_output_config(extensionc, p->aoc, true, false);

	/*AudioDecoderConfig */
	err |= media_add_audio_decoder_config(extensionc, p->adc, true);
	return err;
}


/**
 * add a video source to GetVideoSources request
 *
 * @param gvsrc       GetVideoSourcesResponse child
 * @param cfg         Media Config (VS) to add
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_add_video_source(struct soap_child *gvsrc,
	struct media_config *cfg)
{
	int err = 0;
	struct soap_child *tmp, *vsc, *resc;

	vsc = soap_add_child(gvsrc->msg, gvsrc,
		str_pf_media_wsdl, str_vsources_vss);

	err |= soap_add_parameter_str(vsc, NULL, str_token, strlen(str_token),
		cfg->t.vs.sourcetoken, strlen(cfg->t.vs.sourcetoken));

	tmp = soap_add_child(gvsrc->msg, vsc, str_pf_schema, str_vsources_fr);
	err |= soap_set_value_fmt(tmp, "%d", (int)cfg->t.vs.framerate);

	resc = soap_add_child(gvsrc->msg, vsc, str_pf_schema,
			      str_vsources_res);

	tmp = soap_add_child(gvsrc->msg, resc, str_pf_schema, str_ucwidth);
	err |= soap_set_value_fmt(tmp, "%d", cfg->t.vs.b.w);
	tmp = soap_add_child(gvsrc->msg, resc, str_pf_schema, str_ucheight);
	err |= soap_set_value_fmt(tmp, "%d", cfg->t.vs.b.h);

	return err;
}


/**
 * add a audio source to GetAudioSources request
 *
 * @param gvsrc       GetAudioSourcesResponse child
 * @param cfg         Media Config (AS) to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int media_add_audio_source(struct soap_child *gasrc,
	struct media_config *cfg)
{
	int err = 0;
	struct soap_child *asc, *tmp;

	asc = soap_add_child(gasrc->msg, gasrc,
		str_pf_media_wsdl, str_asources_ass);

	err |= soap_add_parameter_str(asc, NULL, str_token, strlen(str_token),
		cfg->t.as.sourcetoken, strlen(cfg->t.as.sourcetoken));
	tmp = soap_add_child(gasrc->msg, asc, str_pf_schema, str_asources_ch);
	err |= soap_set_value_fmt(tmp, "%d", cfg->t.as.ch);

	return err;
}


/**
 * add a video encoder config options to
 * GetVideoEncoderConfigurationOptions request
 *
 * @param gvecorc     GetVideoEncoderConfigurationOptionsResponse child
 * @param cfg         Media Config (VE) to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int media_add_video_enc_config_opt(struct soap_child *gvecorc,
	struct media_config *cfg)
{
	int err = 0;
	struct soap_child *oc, *tmp, *minmax, *jpegc, *resav, *extc;

	oc = soap_add_child(gvecorc->msg, gvecorc, str_pf_media_wsdl,
			    str_options);
	err |= soap_add_parameter_str(oc, NULL,
		str_vecos_gfrs, strlen(str_vecos_gfrs), str_false,
		strlen(str_false));

	tmp = soap_add_child(gvecorc->msg, oc,
		str_pf_schema, str_vecos_qualityrange);
	minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema, str_min);
	err |= soap_set_value_fmt(minmax, "%d", (int)cfg->t.ve.quality);
	minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema, str_max);
	err |= soap_set_value_fmt(minmax, "%d", (int)cfg->t.ve.quality);

	switch (cfg->t.ve.enc) {
	case JPEG:
		jpegc = soap_add_child(gvecorc->msg, oc, str_pf_schema,
				       str_jpeg);
		resav = soap_add_child(gvecorc->msg, jpegc,
			str_pf_schema, str_vecos_resavailable);
		tmp = soap_add_child(gvecorc->msg, resav,
			str_pf_schema, str_ucwidth);
		err |= soap_set_value_fmt(tmp, "%d", cfg->t.ve.res.w);
		tmp = soap_add_child(gvecorc->msg, resav,
			str_pf_schema, str_ucheight);
		err |= soap_set_value_fmt(tmp, "%d", cfg->t.ve.res.h);

		tmp = soap_add_child(gvecorc->msg, jpegc,
			str_pf_schema, str_vecos_frramge);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_min);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.frl);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_max);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.frl);

		tmp = soap_add_child(gvecorc->msg, jpegc,
			str_pf_schema, str_vecos_eirange);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_min);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.ei);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_max);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.ei);
		break;

	case MPEG4:
	case H264:
		return ENOTSUP;
	}

	extc = soap_add_child(gvecorc->msg, oc, str_pf_schema, str_extension);
	switch (cfg->t.ve.enc) {
	case JPEG:
		jpegc = soap_add_child(gvecorc->msg, extc, str_pf_schema,
				       str_jpeg);
		resav = soap_add_child(gvecorc->msg, jpegc,
			str_pf_schema, str_vecos_resavailable);
		tmp = soap_add_child(gvecorc->msg, resav,
			str_pf_schema, str_ucwidth);
		err |= soap_set_value_fmt(tmp, "%d", cfg->t.ve.res.w);
		tmp = soap_add_child(gvecorc->msg, resav,
			str_pf_schema, str_ucheight);
		err |= soap_set_value_fmt(tmp, "%d", cfg->t.ve.res.h);

		tmp = soap_add_child(gvecorc->msg, jpegc,
			str_pf_schema, str_vecos_frramge);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_min);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.frl);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_max);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.frl);

		tmp = soap_add_child(gvecorc->msg, jpegc,
			str_pf_schema, str_vecos_eirange);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_min);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.ei);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_max);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.ei);

		tmp = soap_add_child(gvecorc->msg, jpegc,
			str_pf_schema, str_vecos_brrange);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_min);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.brl);
		minmax = soap_add_child(gvecorc->msg, tmp, str_pf_schema,
					str_max);
		err |= soap_set_value_fmt(minmax, "%d", cfg->t.ve.ratec.brl);
		break;

	case MPEG4:
	case H264:
		return ENOTSUP;
	}

	return err;
}


/**
 * add a audio encoder config options to
 * GetAudioEncoderConfigurationOptions request
 *
 * @param gaecorc     GetAudioEncoderConfigurationOptionsResponse child
 * @param cfg         Media Config (VE) to add
 *
 * @return          0 if success, errorcode otherwise
 */
static int media_add_audio_enc_config_opt(struct soap_child *gaecorc,
	struct media_config *cfg)
{
	int err = 0;
	struct soap_child *oc, *occ, *tmp;

	oc = soap_add_child(gaecorc->msg, gaecorc, str_pf_media_wsdl,
			    str_options);

	occ = soap_add_child(gaecorc->msg, oc, str_pf_schema, str_options);
	tmp = soap_add_child(gaecorc->msg, occ, str_pf_schema,
			     str_aecos_encoding);
	switch (cfg->t.ae.enc) {
	case G711:
		err |= soap_set_value_fmt(tmp, "%s", str_pcmu);
		break;

	case G726:
	case AAC:
		return ENOTSUP;
	}

	tmp = soap_add_child(gaecorc->msg, occ,
		str_pf_schema, str_aecos_bitratelist);
	tmp = soap_add_child(gaecorc->msg, tmp, str_pf_schema, str_items);
	err |= soap_set_value_fmt(tmp, "%d", cfg->t.ae.br);

	tmp = soap_add_child(gaecorc->msg, occ,
		str_pf_schema, str_aecos_sampleratelist);
	tmp = soap_add_child(gaecorc->msg, tmp, str_pf_schema, str_items);
	err |= soap_set_value_fmt(tmp, "%d", cfg->t.ae.sr);

	return err;
}


/**
 * add a audio decoder config options to
 * GetAudioDecoderConfigurationOptions request
 *
 * @param gadcorc     GetAudioDecoderConfigurationOptionsResponse child
 * @param cfg         Media Config (AD) to add
 *
 * @return           0 if success, errorcode otherwise
 */
static int media_add_audio_dec_config_opt(struct soap_child *gadcorc,
	struct media_config *cfg)
{
	int err = 0;
	struct soap_child *oc, *decc = NULL, *brlc, *srrc, *tmp;

	oc = soap_add_child(gadcorc->msg, gadcorc, str_pf_media_wsdl,
			    str_options);

	switch (cfg->t.ad.dec) {
		case G711:
			decc = soap_add_child(gadcorc->msg, oc,
				str_pf_schema, str_adcos_G711DecOptions);
			break;

		case G726:
		case AAC:
			return ENOTSUP;
	}

	brlc = soap_add_child(gadcorc->msg, decc, str_pf_schema,
			      str_adcos_bitrate);
	tmp = soap_add_child(gadcorc->msg, brlc, str_pf_schema, str_items);
	err |= soap_set_value_fmt(tmp, "%d", cfg->t.ad.br);
	/* tmp = soap_add_child(gadcorc->msg, brlc, str_pf_schema, str_max); */
	/* err |= soap_set_value_fmt(tmp, "%d", cfg->t.ad.br); */

	srrc = soap_add_child(gadcorc->msg, decc, str_pf_schema,
			      str_adcos_srr);
	tmp = soap_add_child(gadcorc->msg, srrc, str_pf_schema, str_items);
	err |= soap_set_value_fmt(tmp, "%d", cfg->t.ad.sr);
	/* tmp = soap_add_child(gadcorc->msg, srrc, str_pf_schema, str_max); */
	/* err |= soap_set_value_fmt(tmp, "%d", cfg->t.ad.sr); */

	return err;
}


/**
 * add a audio ouput config options to
 * GetAudioOutputConfigurationOptions request
 *
 * @param gaocorc     GetAudioOutputConfigurationOptionsResponse child
 * @param cfg         Media Config (AO) to add
 *
 * @return           0 if success, errorcode otherwise
 */
static int media_add_audio_ouput_config_opt(struct soap_child *gaocorc,
	struct media_config *cfg)
{
	int err = 0;
	struct soap_child *oc, *otac, *spoc, *olrc, *minmax;

	oc = soap_add_child(gaocorc->msg, gaocorc, str_pf_media_wsdl,
			    str_options);
	otac = soap_add_child(gaocorc->msg, oc, str_pf_schema,
		str_aocos_optokensavail);
	err |= soap_set_value_fmt(otac, "%s", cfg->t.ao.outputtoken);
	spoc = soap_add_child(gaocorc->msg, oc, str_pf_schema,
		str_aocos_sendprimacyoptions);

	switch (cfg->t.ao.sp) {
	case HALF_DUPLEX_CLIENT:
		err |= soap_set_value_fmt(spoc, "%s",
					  str_profile_sp_hdc);
		break;

	case HALF_DUPLEX_SERVER:
		err |= soap_set_value_fmt(spoc, "%s",
					  str_profile_sp_hds);
		break;

	case HALF_DUPLEX_AUTO:
		err |= soap_set_value_fmt(spoc, "%s",
					  str_profile_sp_hda);
		break;
	}

	olrc = soap_add_child(gaocorc->msg, oc, str_pf_schema,
		str_aocos_outputlevelrange);
	minmax = soap_add_child(gaocorc->msg, olrc, str_pf_schema, str_min);
	err |= soap_set_value_fmt(minmax, "%d", 0);
	minmax = soap_add_child(gaocorc->msg, olrc, str_pf_schema, str_max);
	err |= soap_set_value_fmt(minmax, "%d", 10);

	return err;
}


/**
 * add a video source config options to
 * GetVideoSourceConfigurationOptions request
 *
 * @param gvscorc     GetVideoSourceConfigurationOptionsResponse child
 * @param cfg         Media Config (VS) to add
 *
 * @return           0 if success, errorcode otherwise
 */
static int media_add_video_source_config_opt(struct soap_child *gaocorc,
	struct media_config *cfg)
{
	int err = 0;
	struct soap_child *oc, *brc, *tmpc, *minmax;

	oc = soap_add_child(gaocorc->msg, gaocorc, str_pf_media_wsdl,
			    str_options);
	err |= soap_add_parameter_uint(oc, NULL,
		str_vscos_maxprofiles, strlen(str_vscos_maxprofiles),
		cfg->t.vs.maxprofile);

	brc = soap_add_child(oc->msg, oc, str_pf_schema,
			     str_vscos_boundsrange);
	tmpc = soap_add_child(oc->msg, brc, str_pf_schema, str_vscos_xrange);
	minmax = soap_add_child(oc->msg, tmpc, str_pf_schema, str_min);
	err |= soap_set_value_fmt(minmax, "%d", cfg->t.vs.b.x);
	minmax = soap_add_child(oc->msg, tmpc, str_pf_schema, str_max);
	err |= soap_set_value_fmt(minmax, "%d", cfg->t.vs.b.x);
	tmpc = soap_add_child(oc->msg, brc, str_pf_schema, str_vscos_yrange);
	minmax = soap_add_child(oc->msg, tmpc, str_pf_schema, str_min);
	err |= soap_set_value_fmt(minmax, "%d", cfg->t.vs.b.y);
	minmax = soap_add_child(oc->msg, tmpc, str_pf_schema, str_max);
	err |= soap_set_value_fmt(minmax, "%d", cfg->t.vs.b.y);
	tmpc = soap_add_child(oc->msg, brc, str_pf_schema, str_vscos_wrange);
	minmax = soap_add_child(oc->msg, tmpc, str_pf_schema, str_min);
	err |= soap_set_value_fmt(minmax, "%d", cfg->t.vs.b.w);
	minmax = soap_add_child(oc->msg, tmpc, str_pf_schema, str_max);
	err |= soap_set_value_fmt(minmax, "%d", cfg->t.vs.b.w);
	tmpc = soap_add_child(oc->msg, brc, str_pf_schema, str_vscos_hrange);
	minmax = soap_add_child(oc->msg, tmpc, str_pf_schema, str_min);
	err |= soap_set_value_fmt(minmax, "%d", cfg->t.vs.b.h);
	minmax = soap_add_child(oc->msg, tmpc, str_pf_schema, str_max);
	err |= soap_set_value_fmt(minmax, "%d", cfg->t.vs.b.h);

	tmpc = soap_add_child(oc->msg, oc, str_pf_schema,
		str_vscos_vstokensavail);
	err |= soap_set_value_fmt(tmpc, "%s", cfg->t.vs.sourcetoken);

	return err;
}


/**
 * add a audio source config options to
 * GetAudioSourceConfigurationOptions request
 *
 * @param gvscorc     GetAudioSourceConfigurationOptionsResponse child
 * @param cfg         Media Config (VS) to add
 * @return           0 if success, errorcode otherwise
 *
 */
static int media_add_audio_source_config_opt(struct soap_child *gascorc,
	struct media_config *cfg)
{
	int err = 0;
	struct soap_child *oc, *itac;

	oc = soap_add_child(gascorc->msg, gascorc, str_pf_media_wsdl,
			    str_options);

	itac = soap_add_child(oc->msg, oc, str_pf_schema,
		str_ascos_astokensavail);
	err |= soap_set_value_fmt(itac, "%s", cfg->t.as.sourcetoken);

	return err;
}


/**
 * check and set a videosource config
 *
 * @param configc   child containing all information to set
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
static int media_set_video_source_config(const struct soap_child *configc,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_child *namec, *st, *bound;
	struct soap_parameter *tokenp, *xp, *yp, *wp, *hp;
	struct le *le = NULL;
	struct media_config *cfg = NULL;
	int64_t x, y, w, h;

	tokenp = soap_child_has_parameter(configc, str_token);
	namec = soap_child_has_child(configc, NULL, str_name);
	st = soap_child_has_child(configc, NULL, str_profile_source_token);
	bound = soap_child_has_child(configc, NULL, str_profile_bounds);
	xp = soap_child_has_parameter(bound, "x");
	yp = soap_child_has_parameter(bound, "y");
	wp = soap_child_has_parameter(bound, str_width);
	hp = soap_child_has_parameter(bound, str_height);

	if (tokenp) {
		le = list_apply(&vs_l, true, media_cmp_mediareftoken,
			&tokenp->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return err;
	}

	if (!namec || !st || !bound || !xp || !yp || !wp || !hp)
		return EINVAL;

	x = pl_u32(&xp->value);
	y = pl_u32(&yp->value);
	h = pl_u32(&hp->value);
	w = pl_u32(&wp->value);

	if (x > cfg->t.vs.b.x || y > cfg->t.vs.b.y  || h > cfg->t.vs.b.h ||
		w > cfg->t.vs.b.w) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_ConfigModify,
			str_fault_configparamnotset);
		return EINVAL;
	}

	le = list_apply(&vs_l, true, media_cmp_vssourctoken, &st->value);
	if (!le) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_ConfigModify,
			str_fault_configparamnotset);
		return err;
	}

	if (-1 == re_snprintf(cfg->name, sizeof(cfg->name), "%r",
			      &namec->value))
		return EINVAL;

	if (-1 == re_snprintf(cfg->t.vs.sourcetoken,
			      sizeof(cfg->t.vs.sourcetoken),
		"%r", &st->value))
		return EINVAL;

	cfg->t.vs.b.x = x;
	cfg->t.vs.b.y = y;
	cfg->t.vs.b.w = w;
	cfg->t.vs.b.h = h;

	return err;
}


/**
 * check and set a audiosource config
 *
 * @param configc   child containing all information to set
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_set_audio_source_config(const struct soap_child *configc,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_child *namec, *st;
	struct soap_parameter *tokenp;
	struct le *le = NULL;
	struct media_config *cfg = NULL;

	tokenp = soap_child_has_parameter(configc, str_token);
	namec = soap_child_has_child(configc, NULL, str_name);
	st = soap_child_has_child(configc, NULL, str_profile_source_token);

	if (tokenp) {
		le = list_apply(&as_l, true, media_cmp_mediareftoken,
			&tokenp->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return err;
	}

	if (!namec || !st)
		return EINVAL;

	le = list_apply(&as_l, true, media_cmp_assourctoken, &st->value);
	if (!le) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_ConfigModify,
			str_fault_configparamnotset);
		return err;
	}

	if (-1 == re_snprintf(cfg->name, sizeof(cfg->name), "%r",
			      &namec->value))
		return EINVAL;

	if (-1 == re_snprintf(cfg->t.as.sourcetoken,
			      sizeof(cfg->t.as.sourcetoken),
		"%r", &st->value))
		return EINVAL;

	return err;
}


/**
 * check and set a audiooutput config
 *
 * @param configc   child containing all information to set
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_set_audio_output_config(const struct soap_child *configc,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_child *namec, *ot, *olc, *spc;
	struct soap_parameter *tokenp;
	int ol;
	struct le *le = NULL;
	struct media_config *cfg = NULL;

	tokenp = soap_child_has_parameter(configc, str_token);
	namec = soap_child_has_child(configc, NULL, str_name);
	ot = soap_child_has_child(configc, NULL, str_profile_output_token);
	olc = soap_child_has_child(configc, NULL, str_profile_outputlevel);
	spc = soap_child_has_child(configc, NULL, str_profile_sendprimacy);

	if (tokenp) {
		le = list_apply(&ao_l, true, media_cmp_mediareftoken,
			&tokenp->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return err;
	}

	if (!namec || !ot || !olc || !spc)
		return EINVAL;

	le = list_apply(&ao_l, true, media_cmp_aooutputtoken, &ot->value);
	if (!le) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_ConfigModify,
			str_fault_configparamnotset);
		return err;
	}

	ol = pl_u64(&olc->value);
	if (ol > 10 || pl_strchr(&olc->value, '-')) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_ConfigModify,
			str_fault_configparamnotset);
		return err;
	}

	if (-1 == re_snprintf(cfg->name, sizeof(cfg->name), "%r",
			      &namec->value))
		return EINVAL;

	if (-1 == re_snprintf(cfg->t.ao.outputtoken,
			      sizeof(cfg->t.ao.outputtoken),
		"%r", &ot->value))
		return EINVAL;

	if (0 == pl_strcmp(&spc->value, str_profile_sp_hds)) {
		cfg->t.ao.sp = HALF_DUPLEX_SERVER;
	}
	else if (0 == pl_strcmp(&spc->value, str_profile_sp_hdc)) {
		cfg->t.ao.sp = HALF_DUPLEX_CLIENT;
	}
	else if (0 == pl_strcmp(&spc->value, str_profile_sp_hda)) {
		cfg->t.ao.sp = HALF_DUPLEX_AUTO;
	}
	else {
		return EINVAL;
	}

	cfg->t.ao.outputlevel = ol;

	return err;
}


/**
 * check and set a videoencoder config
 *
 * @param configc   child containing all information to set
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_set_video_encoder_config(struct soap_child *configc,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_child *namec, *encc, *resc, *wc, *hc, *qualityc;
	struct soap_child *rcc, *frlc, *encic, *brlc, *sesst;
	struct soap_parameter *gfrp, *tokenp;
	struct le *le = NULL;
	struct media_config *cfg = NULL;
	struct pl sesstreg;
	int w, h, frl, enci, brl;
	float quality;

	tokenp = soap_child_has_parameter(configc, str_token);
	gfrp = soap_child_has_parameter(configc, str_vecos_gfrs);

	namec = soap_child_has_child(configc, NULL, str_name);
	encc = soap_child_has_child(configc, NULL, str_profile_encoding);
	resc = soap_child_has_child(configc, NULL, str_profile_resolution);
	wc = soap_child_has_child(resc, NULL, str_ucwidth);
	hc = soap_child_has_child(resc, NULL, str_ucheight);
	qualityc = soap_child_has_child(configc, NULL, str_profile_quality);
	rcc = soap_child_has_child(configc, NULL, str_profile_ratecontrol);
	frlc = soap_child_has_child(rcc, NULL, str_profile_frl);
	encic = soap_child_has_child(rcc, NULL, str_profile_ei);
	brlc = soap_child_has_child(rcc, NULL, str_profile_brl);
	sesst = soap_child_has_child(configc, NULL, str_profile_sess_timeout);

	if (tokenp) {
		le = list_apply(&ve_l, true, media_cmp_mediareftoken,
			&tokenp->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return err;
	}

	if (!namec || !encc || !wc || !hc || !qualityc || !frlc ||
		!encic || !brlc || !sesst)
		return EINVAL;

	w = pl_u32(&wc->value);
	h = pl_u32(&hc->value);
	frl = pl_u32(&frlc->value);
	enci = pl_u32(&encic->value);
	brl = pl_u32(&brlc->value);
	quality = pl_float(&qualityc->value);

	if (w > cfg->t.ve.res.w || h > cfg->t.ve.res.h ||
		frl > cfg->t.ve.ratec.frl || enci > cfg->t.ve.ratec.ei ||
		quality > cfg->t.ve.quality) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_ConfigModify,
			str_fault_configparamnotset);
		return EINVAL;
	}

	if (0 != pl_strcmp(&encc->value, str_jpeg)) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_ConfigModify,
			str_fault_configparamnotset);
		return EINVAL;
	}

	if (-1 == re_snprintf(cfg->name, sizeof(cfg->name), "%r",
			      &namec->value))
		return EINVAL;

	if (re_regex(sesst->value.p, sesst->value.l, "[0-9]+", &sesstreg))
		return EINVAL;

	if (gfrp) {
		if (0 == pl_strcmp(&gfrp->value, str_true))
			cfg->t.ve.gfr = true;
		else
			cfg->t.ve.gfr = false;
	}
	cfg->t.ve.enc = JPEG;
	cfg->t.ve.res.w = w;
	cfg->t.ve.res.h = h;
	cfg->t.ve.quality = quality;
	cfg->t.ve.ratec.frl = frl;
	cfg->t.ve.ratec.ei = enci;
	cfg->t.ve.ratec.brl = brl;

	cfg->t.ve.st = pl_u32(&sesstreg);

	/* multicast not supported */
	return err;
}


/**
 * check and set a audioencoder config
 *
 * @param configc   child containing all information to set
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
static int media_set_audio_encoder_config(struct soap_child *configc,
	struct soap_fault *f)
{
	int err = 0;
	int br, sr;
	struct soap_child *namec, *encc, *brc, *src, *stc;
	struct soap_parameter *tokenp;
	struct pl sesstreg;
	struct le *le = NULL;
	struct media_config *cfg = NULL;

	tokenp = soap_child_has_parameter(configc, str_token);

	namec = soap_child_has_child(configc, NULL, str_name);
	encc = soap_child_has_child(configc, NULL, str_profile_encoding);
	brc = soap_child_has_child(configc, NULL, str_profile_bitrate);
	src = soap_child_has_child(configc, NULL, str_profile_samplerate);
	stc = soap_child_has_child(configc, NULL, str_profile_sess_timeout);

	if (tokenp) {
		le = list_apply(&ae_l, true, media_cmp_mediareftoken,
			&tokenp->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return err;
	}

	if (!namec || !encc || !brc || !src || !stc)
		return EINVAL;

	br = pl_u32(&brc->value);
	sr = pl_u32(&src->value);

	if (br > cfg->t.ae.br || sr > cfg->t.ae.sr) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_ConfigModify,
			str_fault_configparamnotset);
		return EINVAL;
	}

	if (0 != pl_strcmp(&encc->value, str_pcmu)) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_ConfigModify,
			str_fault_configparamnotset);
		return EINVAL;
	}

	if (re_regex(stc->value.p, stc->value.l, "[0-9]+", &sesstreg))
		return EINVAL;

	if (-1 == re_snprintf(cfg->name, sizeof(cfg->name), "%r",
			      &namec->value))
		return EINVAL;

	cfg->t.ae.br = br;
	cfg->t.ae.sr = sr;
	cfg->t.ae.st = pl_u32(&sesstreg);

	/* multicast not supported */
	return err;
}


/**
 * handle GetProfiles requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetProfiles_h(const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gpc;
	struct le *le;
	struct profile *p;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gpc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_profiles_r);

	le = profile_l.head;
	while (le) {
		p = le->data;
		err |= media_add_profile(gpc, p, false);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetProfile requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault struct
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetProfile_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp = NULL;
	struct soap_child *b, *gpc, *ptc;
	struct le *le = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp || !f)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	b = soap_child_has_child(b, NULL, str_method_get_profile);
	ptc = soap_child_has_child(b, NULL, str_profile_profiletoken);

	if (ptc) {
		le = list_apply(&profile_l, true,
			media_cmp_profilreftoken, &ptc->value);
		if (le)
			p = le->data;
	}

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gpc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_profile_r);

	err |= media_add_profile(gpc, p, true);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetVideoSourceConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetVSCS_h(const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *cc;
	struct le *le;
	struct media_config *m;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	cc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_vscs_r);

	le = vs_l.head;
	while (le) {
		m = le->data;
		err |= media_add_video_source_config(cc, m, false, true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;

}

/**
 * handle GetVideoEncoderConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetVECS_h(const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *cc;
	struct le *le;
	struct media_config *m;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	cc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_vecs_r);

	le = ve_l.head;
	while (le) {
		m = le->data;
		err |= media_add_video_enc_config(cc, m, false, true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}

/**
 * handle GetAudioSourceConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetASCS_h(const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *cc;
	struct le *le;
	struct media_config *m;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	cc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_ascs_r);

	le = as_l.head;
	while (le) {
		m = le->data;
		err |= media_add_audio_source_config(cc, m, false, true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;

}

/**
 * handle GetAutioEncoderConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAECS_h(const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *cc;
	struct le *le;
	struct media_config *m;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	cc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_aecs_r);

	le = ae_l.head;
	while (le) {
		m = le->data;
		err |= media_add_audio_enc_config(cc, m, false, true);
		le = le->next;
	}


  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAutioOutputConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAOCS_h(const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *cc;
	struct le *le;
	struct media_config *m;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	cc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_aocs_r);

	le = ao_l.head;
	while (le) {
		m = le->data;
		err |= media_add_audio_output_config(cc, m, false, true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAutioOutputConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAOC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gaocc, *gaocrc, *ctc;
	struct le *le = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gaocc = soap_child_has_child(b, NULL, str_method_get_aoc);
	ctc = soap_child_has_child(gaocc, NULL, str_profile_configtoken);

	if (ctc) {
		le = list_apply(&ao_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gaocrc = soap_add_child(resp, b, str_pf_media_wsdl,
				str_method_get_aoc_r);
	err |= media_add_audio_output_config(gaocrc, cfg, false, false);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAutioDecoderConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetADCS_h(const struct soap_msg *msg, struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *cc;
	struct le *le;
	struct media_config *m;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	cc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_adcs_r);

	le = ad_l.head;
	while (le) {
		m = le->data;
		err |= media_add_audio_decoder_config(cc, m, false);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetStreamUri requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetStreamUri_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *gsurc, *streamsetupc, *streamc, *protoc, *ptc;
	struct soap_child *b, *gsu;
	struct le *le = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gsurc = soap_child_has_child(
		soap_child_has_child(msg->envelope, NULL, str_body),
		NULL, str_method_get_suri);

	streamsetupc = soap_child_has_child(gsurc, NULL,
					    str_streamuri_streamsetup);
	streamc = soap_child_has_child(streamsetupc, NULL,
				       str_streamuri_stream);
	protoc = soap_child_has_child(streamsetupc, NULL,
				      str_streamuri_transport);
	protoc = soap_child_has_child(protoc, NULL, str_streamuri_proto);
	ptc = soap_child_has_child(gsurc, NULL, str_profile_profiletoken);

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
		if (le)
			p = le->data;
	}

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (0 == pl_strcmp(&streamc->value, "RTP-Multicast") ||
		(0 == pl_strcmp(&protoc->value, "TCP")) ||
		(0 == pl_strcmp(&streamc->value, "HTTP"))) {
		fault_set(f, FC_Sender, FS_InvalidArgVal,
			  FS_InvalidStreamSetup,
			  str_fault_streamsetupnotsupported);
		return ENOTSUP;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gsu = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_suri_r);
	err |= media_add_streamsetup(msg, gsu, p);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * !IN CASE OF NS=DEVICEIP @media_GetVideoSources_h IS JUST A WRAPPER FUNCTION!
 * handle GetVideoSources requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetVideoSources_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gvsrc;
	struct media_config *cfg;
	struct le *le;

	if (!msg || !prtresp)
		return EINVAL;

	/* in case call the deviceIO version */
	b = soap_child_has_child(msg->envelope, NULL, str_body);
	b = soap_child_has_child(b, NULL, str_method_get_videosources);
	/* if (0 == pl_strcmp(&b->ns->uri, str_uri_deviceio_wsdl)) { */
	/*     err = deviceio_GetVideoSources_h (msg, &resp); */
	/*     goto out; */
	/* } */

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gvsrc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_videosources_r);

	le = vs_l.head;
	while (le) {
		cfg = le->data;
		err |= media_add_video_source(gvsrc, cfg);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * !IN CASE OF NS=DEVICEIP @media_GetAudioSources_h IS JUST A WRAPPER FUNCTION!
 * handle GetAudioSources requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAudioSources_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gasrc;
	struct le *le;
	struct media_config *cfg;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	b = soap_child_has_child(b, NULL, str_method_get_audiosources);

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gasrc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_audiosources_r);

	if (list_isempty(&ao_l)) {
		fault_set(f, FC_Receiver, FS_ActionNotSupported,
			FS_AudioNotSupported, str_fault_audionotsupported);
		err = EINVAL;
		goto out;
	}

	le = as_l.head;
	while (le) {
		cfg = le->data;
		err |= media_add_audio_source(gasrc, cfg);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetMetadataConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 *
 * @return          0 if success, errorcode otherwise
 *
 * TODO 1: Actual work ... currently no metadata known
 */
int media_GetMetadataConfigurations_h(const struct soap_msg *msg,
	struct soap_msg **prtresp)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b;

	if (!msg || !prtresp)
		return EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_mdconfigs_r);
	/* TODO1 */

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAudioEncoderConfigurationOptions requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAudioEncoderConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gaecoc, *ctc, *ptc, *gaecorc;
	struct le *le = NULL;
	struct media_config *cfg = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gaecoc = soap_child_has_child(b, NULL, str_method_get_aecos);
	ctc = soap_child_has_child(gaecoc, NULL, str_profile_configtoken);
	ptc = soap_child_has_child(gaecoc, NULL, str_profile_profiletoken);


	if (ctc) {
		le = list_apply(&ae_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
				&ptc->value);
		if (le)
			p = le->data;
	}

	if (ptc && !p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (ctc && !cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p && !cfg)
		cfg = p->aec;


	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gaecorc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_aecos_r);

	if (!p && !cfg) {
		le = ae_l.head;
		while (le) {
			cfg = le->data;
			err |= media_add_audio_enc_config_opt(gaecorc, cfg);
			le = le->next;
		}
	}
	else {
		if (!cfg)
			cfg = ae_l.head->data;
		err |= media_add_audio_enc_config_opt(gaecorc, cfg);
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetVideoEncoderConfigurationOptions requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetVideoEncoderConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gvecoc, *gvecorc, *ctc, *ptc;
	struct le *le = NULL;
	struct media_config *cfg = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gvecoc = soap_child_has_child(b, NULL, str_method_get_vecos);
	ctc = soap_child_has_child(gvecoc, NULL, str_profile_configtoken);
	ptc = soap_child_has_child(gvecoc, NULL, str_profile_profiletoken);

	if (ctc) {
		le = list_apply(&ve_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
				&ptc->value);
		if (le)
			p = le->data;
	}

	if (ptc && !p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (ctc && !cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p && !cfg)
		cfg = p->vec;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gvecorc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_vecos_r);

	if (!p && !cfg) {
		le = ve_l.head;
		while (le) {
			cfg = le->data;
			err |= media_add_video_enc_config_opt(gvecorc, cfg);
			le = le->next;
		}
	}
	else {
		if (!cfg)
			cfg = ve_l.head->data;
		err |= media_add_video_enc_config_opt(gvecorc, cfg);
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAudioDecoderConfigurationOptions requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAudioDecoderConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gadcoc, *ctc = NULL, *ptc = NULL;
	struct soap_child *gadcorc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gadcoc = soap_child_has_child(b, NULL, str_method_get_adcos);
	ctc = soap_child_has_child(gadcoc, NULL, str_profile_configtoken);
	ptc = soap_child_has_child(gadcoc, NULL, str_profile_profiletoken);

	if (ctc) {
		le = list_apply(&ad_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (ptc) {
		le = list_apply(&profile_l, true,
			media_cmp_profilreftoken, &ptc->value);
		if (le)
			p = le->data;
	}

	if (ptc && !p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (ctc && !cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p && !cfg)
		cfg = p->adc;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gadcorc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_adcos_r);

	if (!p && !cfg) {

		le = ad_l.head;
		while (le) {
			cfg = le->data;
			err |= media_add_audio_dec_config_opt(gadcorc, cfg);
			le = le->next;
		}
	}
	else {
		if (!cfg)
			cfg = ad_l.head->data;
		err |= media_add_audio_dec_config_opt(gadcorc, cfg);
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle CreateProfile requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_CreateProfile_h(const struct soap_msg *msg,
			  struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *cpc, *namec, *tokenc, *cpr;
	struct profile *np;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	cpc = soap_child_has_child(b, NULL, str_method_create_profile);
	tokenc = soap_child_has_child(cpc, NULL, str_uctoken);
	namec = soap_child_has_child(cpc, NULL, str_name);

	if (!namec)
		return EINVAL;

	if (tokenc) {
		if (list_apply(&profile_l, true, media_cmp_profilreftoken,
			&tokenc->value)) {
			fault_set(f, FC_Sender, FS_InvalidArgVal,
				  FS_ProfilExists, str_fault_profileexists);
			return EINVAL;
		}
	}

	if (list_count(&profile_l) > MAXMEDIAPROFILE) {
		fault_set(f, FC_Receiver, FS_Action, FS_MaxNVTProfiles,
			str_fault_maxprofile);
		return EINVAL;
	}

	np = mem_zalloc(sizeof(struct profile), profile_destructor);
	if (!np)
		return ENOMEM;

	strncpy(np->name, namec->value.p,
		namec->value.l > 64 ? 64 : namec->value.l);
	np->fixed = false;

	if (tokenc)
		strncpy(np->token, tokenc->value.p,
			tokenc->value.l > 64 ? 64 : tokenc->value.l);
	else
		rand_str(np->token, 64);

	list_append(&profile_l, &np->le, np);
	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	cpr = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_create_profile_r);

	err |= media_add_profile(cpr, np, true);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle DeleteProfile requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_DeleteProfile_h(const struct soap_msg *msg,
			  struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *dpc, *ptc;
	struct le *le = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	dpc = soap_child_has_child(b, NULL, str_method_delete_profile);
	ptc = soap_child_has_child(dpc, NULL, str_profile_profiletoken);

	if (!ptc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (p->fixed) {
		fault_set(f, FC_Sender, FS_Action, FS_DeletionOfFixedProfile,
			str_fault_delfixedprofile);
		return EINVAL;
	}

	mem_deref(p);

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl,
		       str_method_delete_profile_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle AddVideoSourceConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_AddVideoSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *avscc, *ptc, *ctc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	avscc = soap_child_has_child(b, NULL, str_method_add_vsc);
	ptc = soap_child_has_child(avscc, NULL, str_profile_profiletoken);
	ctc = soap_child_has_child(avscc, NULL, str_profile_configtoken);

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	le = list_apply(&vs_l, true, media_cmp_mediareftoken, &ctc->value);
	if (le)
		cfg = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p->vsc != cfg) {
		if (p->vsc)
			p->vsc->usecount--;
		mem_deref(p->vsc);
		p->vsc = mem_ref(cfg);
		cfg->usecount++;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_add_vsc_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle AddVideoEncoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_AddVideoEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *avecc, *ptc, *ctc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	avecc = soap_child_has_child(b, NULL, str_method_add_vec);
	ptc = soap_child_has_child(avecc, NULL, str_profile_profiletoken);
	ctc = soap_child_has_child(avecc, NULL, str_profile_configtoken);

	if (!ptc || !ctc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	le = list_apply(&ve_l, true, media_cmp_mediareftoken, &ctc->value);
	if (le)
		cfg = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p->vec != cfg) {
		if (p->vec)
			p->vec->usecount--;
		mem_deref(p->vec);
		p->vec = mem_ref(cfg);
		cfg->usecount++;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_add_vec_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle RemoveVideoSourceConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_RemoveVideoSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *rvscc, *ptc;
	struct le *le = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	rvscc = soap_child_has_child(b, NULL, str_method_remove_vsc);
	ptc = soap_child_has_child(rvscc, NULL, str_profile_profiletoken);

	if (!ptc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (p->vsc) {
		p->vsc->usecount--;
		p->vsc = mem_deref(p->vsc);
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_remove_vsc_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle RemoveVideoEncoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_RemoveVideoEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *rvecc, *ptc;
	struct le *le = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	rvecc = soap_child_has_child(b, NULL, str_method_remove_vec);
	ptc = soap_child_has_child(rvecc, NULL, str_profile_profiletoken);

	if (!ptc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (p->vec) {
		p->vec->usecount--;
		p->vec = mem_deref(p->vec);
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_remove_vec_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * !IN CASE OF NS=DEVICEIP @media_GetAudioSources_h IS JUST A WRAPPER FUNCTION!
 * handle GetAudioOutputs requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAudioOutputs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gaosc, *auc;
	struct media_config *cfg;
	struct le *le;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	b = soap_child_has_child(b, NULL, str_method_get_audiooutputs);
	/* if (0 == pl_strcmp(&b->ns->uri, str_uri_deviceio_wsdl)) { */
	/*     err = deviceio_GetAudioOutputs_h (msg, &resp); */
	/*     goto out; */
	/* } */

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gaosc = soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_audiooutputs_r);

	if (list_isempty(&ao_l)) {
		fault_set(f, FC_Receiver, FS_ActionNotSupported,
			  FS_AudioOutputNotSupported,
			  str_fault_audiooutputnotsupported);
		err = EINVAL;
		goto out;
	}

	le = ao_l.head;
	while (le) {
		cfg = le->data;
		auc = soap_add_child(resp, gaosc, str_pf_media_wsdl,
				     str_device_ioaudiooutputs);
		err |= soap_add_parameter_str(auc, NULL, str_token,
					      strlen(str_token),
					      cfg->t.ao.outputtoken,
					      strlen(cfg->t.ao.outputtoken));
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAudioOutputConfigurationOptions requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap_fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAudioOutputConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gaococ, *ctc, *ptc, *gaocorc;
	struct le *le = NULL;
	struct media_config *cfg = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gaococ = soap_child_has_child(b, NULL, str_method_get_aocos);
	ctc = soap_child_has_child(gaococ, NULL, str_profile_configtoken);
	ptc = soap_child_has_child(gaococ, NULL, str_profile_profiletoken);

	if (ctc) {
		le = list_apply(&ao_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
				&ptc->value);
		if (le)
			p = le->data;
	}

	if (ptc && !p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (ctc && !cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p && !cfg)
		cfg = p->aoc;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gaocorc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_aocos_r);

	if (!p && !cfg) {
		le = ao_l.head;
		while (le) {
			cfg = le->data;
			err |= media_add_audio_ouput_config_opt(gaocorc, cfg);
			le = le->next;
		}
	}
	else {
		if (!cfg)
			cfg = ao_l.head->data;
		err |= media_add_audio_ouput_config_opt(gaocorc, cfg);
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetCompatibleVideoEncoderConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetCompVideoEncoderConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gcvecrc, *ptc, *gcvecc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gcvecc = soap_child_has_child(b, NULL, str_method_get_cvec);
	ptc = soap_child_has_child(gcvecc, NULL, str_profile_profiletoken);

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
		if (le)
			p = le->data;
	}

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gcvecrc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_cvec_r);

	le = ve_l.head;
	while (le) {
		cfg = le->data;
		err |= media_add_video_enc_config(gcvecrc, cfg, false, true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetGuaranteedNumberOfVideoEncoderInstances requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetGuaranteedNumberOfVEInstances_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *ggnovei, *ctc, *ggnoveir, *tmpc;
	struct le *le = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	ggnovei = soap_child_has_child(b, NULL, str_method_get_ggnovei);
	ctc = soap_child_has_child(ggnovei, NULL, str_profile_configtoken);

	if (ctc) {
		le = list_apply(&vs_l, true, media_cmp_mediareftoken,
			&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	ggnoveir= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_ggnovei_r);

	tmpc = soap_add_child(resp, ggnoveir, str_pf_media_wsdl,
			      str_totalnumb);
	err |= soap_set_value_fmt(tmpc, "%d",
		(cfg->t.vs.i.jpeg_i + cfg->t.vs.i.h264_i +
		 cfg->t.vs.i.mpeg4_i));

	if (cfg->t.vs.i.jpeg_i > 0) {
		tmpc = soap_add_child(resp, ggnoveir, str_pf_media_wsdl,
				      str_jpeg);
		err |= soap_set_value_fmt(tmpc, "%d", cfg->t.vs.i.jpeg_i);
	}

	if (cfg->t.vs.i.h264_i > 0) {
		tmpc = soap_add_child(resp, ggnoveir, str_pf_media_wsdl,
				      str_jpeg);
		err |= soap_set_value_fmt(tmpc, "%d", cfg->t.vs.i.h264_i);
	}

	if (cfg->t.vs.i.mpeg4_i > 0) {
		tmpc = soap_add_child(resp, ggnoveir, str_pf_media_wsdl,
				      str_jpeg);
		err |= soap_set_value_fmt(tmpc, "%d", cfg->t.vs.i.mpeg4_i);
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetCompatibleVideoSourceConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetCompVideoSourceConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gcvscrc, *ptc, *gcvscc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gcvscc = soap_child_has_child(b, NULL, str_method_get_cvsc);
	ptc = soap_child_has_child(gcvscc, NULL, str_profile_profiletoken);

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
		if (le)
			p = le->data;
	}

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gcvscrc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_cvsc_r);

	le = vs_l.head;
	while (le) {
		cfg = le->data;
		err |= media_add_video_source_config(gcvscrc, cfg, false,
						     true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetVideoSourceConfigurationOptions requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetVideoSourceConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gvscoc, *ctc, *ptc, *gvscors;
	struct le *le = NULL;
	struct media_config *cfg = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gvscoc = soap_child_has_child(b, NULL, str_method_get_vscos);
	ctc = soap_child_has_child(gvscoc, NULL, str_profile_configtoken);
	ptc = soap_child_has_child(gvscoc, NULL, str_profile_profiletoken);

	if (ctc) {
		le = list_apply(&vs_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
				&ptc->value);
		if (le)
			p = le->data;
	}

	if (p && cfg && (p->vsc != cfg)) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p && !cfg)
		cfg = p->vsc;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gvscors= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_vscos_r);

	if (!p && !cfg) {
		le = vs_l.head;
		while (le) {
			cfg = le->data;
			err |= media_add_video_source_config_opt(gvscors, cfg);
			le = le->next;
		}
	}
	else {
		if (!cfg)
			cfg = vs_l.head->data;
		err |= media_add_video_source_config_opt(gvscors, cfg);
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle SetVideoSourceConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_SetVideoSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *config, *b;

	if (!msg || !prtresp)
		return EINVAL;

	config = soap_child_has_child(msg->envelope, NULL, str_body);
	config = soap_child_has_child(config, NULL,
				      str_method_set_videosource);
	config = soap_child_has_child(config, NULL, str_configuration);

	err |= media_set_video_source_config(config, f);
	if (err || f->is_set)
		return err ? err : EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl,
		       str_method_set_videosource_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetVideoSourceConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetVSC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gvscc, *gvscrc, *ctc;
	struct le *le;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gvscc = soap_child_has_child(b, NULL, str_method_get_vsc);
	ctc = soap_child_has_child(gvscc, NULL, str_profile_configtoken);

	if (ctc) {
		le = list_apply(&vs_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoVideoSource,
			str_fault_vsnotexist);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gvscrc = soap_add_child(resp, b, str_pf_media_wsdl,
				str_method_get_vsc_r);
	err |= media_add_video_source_config(gvscrc, cfg, false, false);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle SetVideoEncoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_SetVideoEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *config, *b;

	if (!msg || !prtresp)
		return EINVAL;

	config = soap_child_has_child(msg->envelope, NULL, str_body);
	config = soap_child_has_child(config, NULL,
				      str_method_set_videoecnoder);
	config = soap_child_has_child(config, NULL, str_configuration);

	err |= media_set_video_encoder_config(config, f);
	if (err || f->is_set)
		return err ? err : EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl,
		       str_method_set_videoencoder_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetVideoEncoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetVEC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gvecc, *gvecrc, *ctc;
	struct le *le;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gvecc = soap_child_has_child(b, NULL, str_method_get_vec);
	ctc = soap_child_has_child(gvecc, NULL, str_profile_configtoken);

	if (ctc) {
		le = list_apply(&ve_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gvecrc = soap_add_child(resp, b, str_pf_media_wsdl,
				str_method_get_vec_r);
	err |= media_add_video_enc_config(gvecrc, cfg, false, false);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAudioSourceConfigurationOptions requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAudioSourceConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gascoc, *ctc, *ptc, *gascors;
	struct le *le = NULL;
	struct media_config *cfg = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gascoc = soap_child_has_child(b, NULL, str_method_get_ascos);
	ctc = soap_child_has_child(gascoc, NULL, str_profile_configtoken);
	ptc = soap_child_has_child(gascoc, NULL, str_profile_profiletoken);

	if (ctc) {
		le = list_apply(&as_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
				&ptc->value);
		if (le)
			p = le->data;
	}

	if (ptc && !p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (ctc && !cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p && !cfg)
		cfg = p->asc;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gascors= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_ascos_r);

	if (!p && !cfg) {
		le = as_l.head;
		while (le) {
			cfg = le->data;
			err |= media_add_audio_source_config_opt(gascors, cfg);
			le = le->next;
		}
	}
	else {
		if (!cfg)
			cfg = as_l.head->data;
		err |= media_add_audio_source_config_opt(gascors, cfg);
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle SetAudioEncoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_SetAudioEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *config, *b;

	if (!msg || !prtresp)
		return EINVAL;

	config = soap_child_has_child(msg->envelope, NULL, str_body);
	config = soap_child_has_child(config, NULL,
				      str_method_set_audioecnoder);
	config = soap_child_has_child(config, NULL, str_configuration);

	err |= media_set_audio_encoder_config(config, f);
	if (err || f->is_set)
		return err ? err : EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl,
		       str_method_set_audioencoder_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAudioEncoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetAEC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gaecc, *gaecrc, *ctc;
	struct le *le;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gaecc = soap_child_has_child(b, NULL, str_method_get_aec);
	ctc = soap_child_has_child(gaecc, NULL, str_profile_configtoken);

	if (ctc) {
		le = list_apply(&ae_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gaecrc = soap_add_child(resp, b, str_pf_media_wsdl,
				str_method_get_aec_r);
	err |= media_add_audio_enc_config(gaecrc, cfg, false, false);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetCompatibleAudioSourceConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetCompAudioSourceConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gcascrc, *ptc, *gcascc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gcascc = soap_child_has_child(b, NULL, str_method_get_casc);
	ptc = soap_child_has_child(gcascc, NULL, str_profile_profiletoken);

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
		if (le)
			p = le->data;
	}

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gcascrc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_casc_r);

	le = as_l.head;
	while (le) {
		cfg = le->data;
		err |= media_add_audio_source_config(gcascrc, cfg, false,
						     true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle AddAudioSourceConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault message
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_AddAudioSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *aascc, *ptc, *ctc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	aascc = soap_child_has_child(b, NULL, str_method_add_asc);
	ptc = soap_child_has_child(aascc, NULL, str_profile_profiletoken);
	ctc = soap_child_has_child(aascc, NULL, str_profile_configtoken);

	if (!ptc || !ctc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	le = list_apply(&as_l, true, media_cmp_mediareftoken, &ctc->value);
	if (le)
		cfg = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p->asc != cfg) {
		if (p->asc)
			p->asc->usecount--;
		mem_deref(p->asc);
		p->asc = mem_ref(cfg);
		cfg->usecount++;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_add_asc_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetCompatibleAudioEncoderConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetCompAudioEncoderConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gcaecrc, *ptc, *gcaecc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gcaecc = soap_child_has_child(b, NULL, str_method_get_caec);
	ptc = soap_child_has_child(gcaecc, NULL, str_profile_profiletoken);

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
		if (le)
			p = le->data;
	}

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gcaecrc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_caec_r);

	le = ae_l.head;
	while (le) {
		cfg = le->data;
		err |= media_add_audio_enc_config(gcaecrc, cfg, false, true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle AddAudioEncoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_AddAudioEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *aaecc, *ptc, *ctc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	aaecc = soap_child_has_child(b, NULL, str_method_add_aec);
	ptc = soap_child_has_child(aaecc, NULL, str_profile_profiletoken);
	ctc = soap_child_has_child(aaecc, NULL, str_profile_configtoken);

	if (!ptc || !ctc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	le = list_apply(&ae_l, true, media_cmp_mediareftoken, &ctc->value);
	if (le)
		cfg = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p->aec != cfg) {
		if (p->aec)
			p->aec->usecount--;
		mem_deref(p->aec);
		p->aec = mem_ref(cfg);
		cfg->usecount++;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_add_aec_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle SetAudioSourceConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_SetAudioSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *config, *b;

	if (!msg || !prtresp)
		return EINVAL;

	config = soap_child_has_child(msg->envelope, NULL, str_body);
	config = soap_child_has_child(config, NULL,
				      str_method_set_audiosource);
	config = soap_child_has_child(config, NULL, str_configuration);

	err |= media_set_audio_source_config(config, f);
	if (err || f->is_set)
		return err ? err : EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl,
		       str_method_set_audiosource_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAudioSourceConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_GetASC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gascc, *gascrc, *ctc;
	struct le *le = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gascc = soap_child_has_child(b, NULL, str_method_get_asc);
	ctc = soap_child_has_child(gascc, NULL, str_profile_configtoken);

	if (ctc) {
		le = list_apply(&as_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoVideoSource,
			str_fault_asnotexist);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gascrc = soap_add_child(resp, b, str_pf_media_wsdl,
				str_method_get_asc_r);
	err |= media_add_audio_source_config(gascrc, cfg, false, false);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle RemoveAudioSourceConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_RemoveAudioSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *rascc, *ptc;
	struct le *le = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	rascc = soap_child_has_child(b, NULL, str_method_remove_asc);
	ptc = soap_child_has_child(rascc, NULL, str_profile_profiletoken);

	if (!ptc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (p->asc) {
		p->asc->usecount--;
		p->asc = mem_deref(p->asc);
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_remove_asc_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle RemoveAudioEncoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_RemoveAudioEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *raecc, *ptc;
	struct le *le = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	raecc = soap_child_has_child(b, NULL, str_method_remove_aec);
	ptc = soap_child_has_child(raecc, NULL, str_profile_profiletoken);

	if (!ptc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (p->aec) {
		p->aec->usecount--;
		p->aec = mem_deref(p->aec);
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_remove_aec_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle RemoveAudioOutputConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_RemoveAudioOutputConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *raocc, *ptc;
	struct le *le = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	raocc = soap_child_has_child(b, NULL, str_method_remove_aoc);
	ptc = soap_child_has_child(raocc, NULL, str_profile_profiletoken);

	if (!ptc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (p->aoc) {
		p->aoc->usecount--;
		p->aoc = mem_deref(p->aoc);
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_remove_aoc_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle SetAudioOutputConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_SetAudioOutputConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *config, *b;

	if (!msg || !prtresp)
		return EINVAL;

	config = soap_child_has_child(msg->envelope, NULL, str_body);
	config = soap_child_has_child(config, NULL,
				      str_method_set_audiooutput);
	config = soap_child_has_child(config, NULL, str_configuration);

	err |= media_set_audio_output_config(config, f);
	if (err || f->is_set)
		return err ? err : EINVAL;

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl,
		       str_method_set_audiooutput_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetCompatibleVideoEncoderConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_GetCompAudioOutputConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gcaocrc, *ptc, *gcaocc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gcaocc = soap_child_has_child(b, NULL, str_method_get_caoc);
	ptc = soap_child_has_child(gcaocc, NULL, str_profile_profiletoken);

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
		if (le)
			p = le->data;
	}

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gcaocrc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_caoc_r);

	le = ao_l.head;
	while (le) {
		cfg = le->data;
		err |= media_add_audio_output_config(gcaocrc, cfg, false,
						     true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle AddAudioOutputConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault message
 *
 * @return          0 if success, errorcode otherwise
 */
int media_AddAudioOutputConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *aaocc, *ptc, *ctc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	aaocc = soap_child_has_child(b, NULL, str_method_add_aoc);
	ptc = soap_child_has_child(aaocc, NULL, str_profile_profiletoken);
	ctc = soap_child_has_child(aaocc, NULL, str_profile_configtoken);

	if (!ptc || !ctc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	le = list_apply(&ao_l, true, media_cmp_mediareftoken, &ctc->value);
	if (le)
		cfg = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p->aoc != cfg) {
		if (p->aoc)
			p->aoc->usecount--;
		mem_deref(p->aoc);
		p->aoc = mem_ref(cfg);
		cfg->usecount++;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_add_aoc_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}

/**
 * handle GetCompatibleAudioDecoderConfigurations requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_GetCompAudioDecoderConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gcadcrc, *ptc, *gcadcc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gcadcc = soap_child_has_child(b, NULL, str_method_get_cadc);
	ptc = soap_child_has_child(gcadcc, NULL, str_profile_profiletoken);

	if (ptc) {
		le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
		if (le)
			p = le->data;
	}

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gcadcrc= soap_add_child(resp, b, str_pf_media_wsdl,
		str_method_get_cadc_r);

	le = ad_l.head;
	while (le) {
		cfg = le->data;
		err |= media_add_audio_dec_config(gcadcrc, cfg, false, true);
		le = le->next;
	}

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle AddAudioDecoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_AddAudioDecoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *aadcc, *ptc, *ctc;
	struct le *le = NULL;
	struct profile *p = NULL;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	aadcc = soap_child_has_child(b, NULL, str_method_add_adc);
	ptc = soap_child_has_child(aadcc, NULL, str_profile_profiletoken);
	ctc = soap_child_has_child(aadcc, NULL, str_profile_configtoken);

	if (!ptc || !ctc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	le = list_apply(&ad_l, true, media_cmp_mediareftoken, &ctc->value);
	if (le)
		cfg = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	if (p->adc != cfg) {
		if (p->adc)
			p->adc->usecount--;
		mem_deref(p->adc);
		p->adc = mem_ref(cfg);
		cfg->usecount++;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_add_adc_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle RemoveAudioDecoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 */
int media_RemoveAudioDecoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *radcc, *ptc;
	struct le *le = NULL;
	struct profile *p = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	radcc = soap_child_has_child(b, NULL, str_method_remove_adc);
	ptc = soap_child_has_child(radcc, NULL, str_profile_profiletoken);

	if (!ptc)
		return EINVAL;

	le = list_apply(&profile_l, true, media_cmp_profilreftoken,
			&ptc->value);
	if (le)
		p = le->data;

	if (!p) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoProfile,
			str_fault_noprofile);
		return EINVAL;
	}

	if (p->adc) {
		p->adc->usecount--;
		p->adc = mem_deref(p->adc);
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	soap_add_child(resp, b, str_pf_media_wsdl, str_method_remove_adc_r);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}


/**
 * handle GetAudioDecoderConfiguration requests
 *
 * @param msg       request message
 * @param prtresp   response message
 * @param f         soap fault
 *
 * @return          0 if success, errorcode otherwise
 *
 */
int media_GetADC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f)
{
	int err = 0;
	struct soap_msg *resp;
	struct soap_child *b, *gadcc, *gadcrc, *ctc;
	struct le *le;
	struct media_config *cfg = NULL;

	if (!msg || !prtresp)
		return EINVAL;

	b = soap_child_has_child(msg->envelope, NULL, str_body);
	gadcc = soap_child_has_child(b, NULL, str_method_get_adc);
	ctc = soap_child_has_child(gadcc, NULL, str_profile_configtoken);

	if (ctc) {
		le = list_apply(&ad_l, true, media_cmp_mediareftoken,
				&ctc->value);
		if (le)
			cfg = le->data;
	}

	if (!cfg) {
		fault_set(f, FC_Sender, FS_InvalidArgVal, FS_NoConfig,
			str_fault_noconfig);
		return EINVAL;
	}

	err = soap_alloc_msg(&resp);
	if (err)
		return err;

	if (soap_msg_add_ns_str_param(
		resp, str_pf_media_wsdl, str_uri_media_wsdl) ||
		soap_msg_add_ns_str_param(
		resp, str_pf_schema, str_uri_schema)) {
		err = EINVAL;
		goto out;
	}

	b = soap_add_child(resp, resp->envelope, str_pf_envelope, str_body);
	gadcrc = soap_add_child(resp, b, str_pf_media_wsdl,
				str_method_get_adc_r);
	err |= media_add_audio_dec_config(gadcrc, cfg, false, false);

  out:
	if (err)
		mem_deref(resp);
	else
		*prtresp = resp;

	return err;
}
