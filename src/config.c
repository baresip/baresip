/**
 * @file config.c  Core Configuration
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <string.h>
#ifndef WIN32
#include <dirent.h>
#endif
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


#ifndef PREFIX
#define PREFIX "/usr"
#endif


#if defined (DARWIN)
#include <TargetConditionals.h>
#endif


/** Core Run-time Configuration - populated from config file */
static struct config core_config = {

	/** SIP User-Agent */
	.sip = {
		.transp = SIP_TRANSP_UDP,
		.tls_resume = TLS_RESUMPTION_ALL,
		.tos = 0xa0,
	},

	.call = /** Call config */
	{
		.local_timeout = 120,
		.max_calls = 4,
		.hold_other_calls = true,
		.accept = false
	},

	/** Audio */
	.audio = {
		.audio_path = SHARE_PATH,
		.txmode = AUDIO_MODE_POLL,
		.level = false,
		.src_fmt = AUFMT_S16LE,
		.play_fmt = AUFMT_S16LE,
		.enc_fmt = AUFMT_S16LE,
		.dec_fmt = AUFMT_S16LE,
		.buffer = {20, 160},
		.adaptive = false,
		.silence = -35.0,
		.telev_pt = 101
	},

	/** Video */
	.video = {
		.width = 640, .height = 480,
		.bitrate = 1000000,
		.send_bitrate = 0,
		.burst_bits = 0,
		.fps = 30,
		.fullscreen = true,
		.enc_fmt = VID_FMT_YUV420P,
	},

	/** Audio/Video Transport */
	.avt = {
		.rtp_tos = 0xb8,
		.rtpv_tos = 0x88,
		.rtp_ports = {1024, 49152},
		.rtp_bw = {0, 0},
		.rtcp_mux = false,
		.audio = {
			JBUF_FIXED,
			{100, 200},
			50
		},
		.video = {
			JBUF_FIXED,
			{100, 200},
			250
		},
		.rtp_stats = false,
		.rtp_timeout = 0,
		.bundle = false,
		.rxmode = RECEIVE_MODE_MAIN,
	},

	/* Network */
	.net = {
		.af = AF_UNSPEC,
		.nsv = { {"",0} },
		.use_linklocal = true,
		.use_getaddrinfo = false,
	},
};


static int range_print(struct re_printf *pf, const struct range *rng)
{
	if (!rng)
		return 0;

	return re_hprintf(pf, "%u-%u", rng->min, rng->max);
}


static int dns_handler(const struct pl *pl, void *arg, bool fallback)
{
	struct config_net *cfg = arg;
	const size_t max_count = RE_ARRAY_SIZE(cfg->nsv);
	int err;

	if (cfg->nsc >= max_count) {
		warning("config: too many DNS nameservers (max %zu)\n",
			max_count);
		return EOVERFLOW;
	}

	/* Append dns_server to the network config */
	err = pl_strcpy(pl, cfg->nsv[cfg->nsc].addr,
			sizeof(cfg->nsv[0].addr));

	cfg->nsv[cfg->nsc].fallback = fallback;

	if (err) {
		warning("config: dns_server: could not copy string (%r)\n",
			pl);
		return err;
	}

	++cfg->nsc;

	return 0;
}


static int dns_server_handler(const struct pl *pl, void *arg)
{
	int err;

	err = dns_handler(pl, arg, false);

	return err;
}


static int dns_fallback_handler(const struct pl *pl, void *arg)
{
	int err;

	err = dns_handler(pl, arg, true);

	return err;
}


static enum aufmt resolve_aufmt(const struct pl *fmt)
{
	if (0 == pl_strcasecmp(fmt, "s16"))     return AUFMT_S16LE;
	if (0 == pl_strcasecmp(fmt, "s16le"))   return AUFMT_S16LE;
	if (0 == pl_strcasecmp(fmt, "float"))   return AUFMT_FLOAT;
	if (0 == pl_strcasecmp(fmt, "s24_3le")) return AUFMT_S24_3LE;

	return (enum aufmt)-1;
}


enum rtp_receive_mode resolve_receive_mode(const struct pl *fmt)
{
	if (0 == pl_strcasecmp(fmt, "main"))     return RECEIVE_MODE_MAIN;
	if (0 == pl_strcasecmp(fmt, "thread"))   return RECEIVE_MODE_THREAD;

	warning("rtp_rxmode %r is not supported\n", fmt);
	return RECEIVE_MODE_MAIN;
}


const char *rtp_receive_mode_str(enum rtp_receive_mode rxmode)
{
	switch (rxmode) {
	case RECEIVE_MODE_MAIN:
		return "main";
	case RECEIVE_MODE_THREAD:
		return "thread";
	default:
		return "?";
	}
}


static int conf_get_aufmt(const struct conf *conf, const char *name,
			  int *fmtp)
{
	struct pl pl;
	int fmt;
	int err;

	err = conf_get(conf, name, &pl);
	if (err)
		return err;

	fmt = resolve_aufmt(&pl);
	if (fmt == -1) {
		warning("config: %s: sample format not supported"
			" (%r)\n", name, &pl);
		return EINVAL;
	}

	*fmtp = fmt;

	return 0;
}


static int conf_get_vidfmt(const struct conf *conf, const char *name,
			   int *fmtp)
{
	struct pl pl;
	int fmt;
	int err;

	err = conf_get(conf, name, &pl);
	if (err)
		return err;

	for (fmt=0; fmt<VID_FMT_N; fmt++) {

		const char *str = vidfmt_name(fmt);

		if (0 == pl_strcasecmp(&pl, str)) {

			*fmtp = fmt;
			return 0;
		}
	}

	warning("config: %s: pixel format not supported (%r)\n", name, &pl);

	return ENOENT;
}


static const char *jbuf_type_str(enum jbuf_type jbtype)
{
	switch (jbtype) {
	case JBUF_OFF:
		return "off";
	case JBUF_FIXED:
		return "fixed";
	case JBUF_ADAPTIVE:
		return "adaptive";
	}

	return "?";
}


static void decode_sip_transports(uint32_t *mask, const struct pl *pl)
{
	uint8_t i;

	for (i = 0; i < SIP_TRANSPC; ++i) {
		bool en;
		char buf[16];
		struct pl e=PL_INIT;

		if (re_snprintf(buf, sizeof(buf), "%s[^,]+",
					sip_transp_name(i)) <= 0)
			break;

		en = 0 == re_regex(pl->p, pl->l, sip_transp_name(i)) &&
		     0 != re_regex(pl->p, pl->l, buf, &e);
		u32mask_enable(mask, i, en);
	}
}


static int transp_print(struct re_printf *pf, uint32_t *mask, bool all)
{
	uint8_t i;
	int err = 0;
	bool first = true;

	for (i = 0; i < SIP_TRANSPC; ++i) {
		if (u32mask_enabled(*mask, i) || (all && *mask == 0)) {
			if (!first)
				err = re_hprintf(pf, ",");

			err |= re_hprintf(pf, "%s", sip_transp_name(i));
			first = false;
		}
	}

	return err;
}


static int sip_transports_print(struct re_printf *pf, uint32_t *mask)
{
	return transp_print(pf, mask, true);
}


static int sip_transports_print_mask(struct re_printf *pf, uint32_t *mask)
{
	return transp_print(pf, mask, false);
}


static const char *net_af_str(int af)
{
	if (af == AF_INET)
		return "ipv4";
	else if (af == AF_INET6)
		return "ipv6";
	else
		return "unspecified";
}


static const char *tls_resume_mode_str(enum tls_resume_mode mode)
{
	switch (mode) {
	case TLS_RESUMPTION_NONE:
		return "none";
	case TLS_RESUMPTION_ALL:
		return "all";
	case TLS_RESUMPTION_IDS:
		return "ids";
	case TLS_RESUMPTION_TICKETS:
		return "tickets";
	}

	return "?";
}


/**
 * Parse the core configuration file and update baresip core config
 *
 * @param cfg  Baresip core config to update
 * @param conf Configuration file to parse
 *
 * @return 0 if success, otherwise errorcode
 */
int config_parse_conf(struct config *cfg, const struct conf *conf)
{
	struct vidsz size = {0, 0};
	struct pl rxmode;
	struct pl txmode;
	struct pl jbtype;
	struct pl tr;
	struct pl pl;
	uint32_t v;
	int err = 0;

	if (!cfg || !conf)
		return EINVAL;

	/* SIP */
	(void)conf_get_str(conf, "sip_listen", cfg->sip.local,
			   sizeof(cfg->sip.local));
	(void)conf_get_str(conf, "sip_certificate", cfg->sip.cert,
			   sizeof(cfg->sip.cert));

	cfg->sip.verify_server = true;
	(void)conf_get_str(conf, "sip_cafile", cfg->sip.cafile,
			   sizeof(cfg->sip.cafile));
	(void)conf_get_str(conf, "sip_capath", cfg->sip.capath,
			   sizeof(cfg->sip.capath));
	if (0 == conf_get(conf, "sip_transports", &pl))
		decode_sip_transports(&cfg->sip.transports, &pl);
	if (!str_isset(cfg->sip.cafile) && !str_isset(cfg->sip.capath))
		cfg->sip.verify_server = false;

	(void)conf_get_bool(conf, "sip_verify_server",
			&cfg->sip.verify_server);

	(void)conf_get_bool(conf, "sip_verify_client",
			&cfg->sip.verify_client);

	if (0 == conf_get(conf, "sip_tls_resumption", &pl)) {
		if (0 == pl_strcasecmp(&pl, "none"))
			cfg->sip.tls_resume = TLS_RESUMPTION_NONE;
		else if (0 == pl_strcasecmp(&pl, "ids"))
			cfg->sip.tls_resume = TLS_RESUMPTION_IDS;
		else if (0 == pl_strcasecmp(&pl, "tickets"))
			cfg->sip.tls_resume = TLS_RESUMPTION_TICKETS;
		else
			cfg->sip.tls_resume = TLS_RESUMPTION_ALL;
	}
	else {
		cfg->sip.tls_resume = TLS_RESUMPTION_ALL;
	}

	if (!conf_get(conf, "sip_trans_def", &tr))
		cfg->sip.transp = sip_transp_decode(&tr);

	if (0 == conf_get_u32(conf, "sip_tos", &v))
		cfg->sip.tos = v;

	if (0 == conf_get(conf, "filter_registrar", &pl))
		decode_sip_transports(&cfg->sip.reg_filt, &pl);

	/* Call */
	(void)conf_get_u32(conf, "call_local_timeout",
			   &cfg->call.local_timeout);
	(void)conf_get_u32(conf, "call_max_calls",
			   &cfg->call.max_calls);
	(void)conf_get_bool(conf, "call_hold_other_calls",
			   &cfg->call.hold_other_calls);
	(void)conf_get_bool(conf, "call_accept",
			   &cfg->call.accept);

	/* Audio */
	(void)conf_get_str(conf, "audio_path", cfg->audio.audio_path,
			   sizeof(cfg->audio.audio_path));
	(void)conf_get_csv(conf, "audio_player",
			   cfg->audio.play_mod,
			   sizeof(cfg->audio.play_mod),
			   cfg->audio.play_dev,
			   sizeof(cfg->audio.play_dev));

	(void)conf_get_csv(conf, "audio_source",
			   cfg->audio.src_mod, sizeof(cfg->audio.src_mod),
			   cfg->audio.src_dev, sizeof(cfg->audio.src_dev));

	(void)conf_get_csv(conf, "audio_alert",
			   cfg->audio.alert_mod,
			   sizeof(cfg->audio.alert_mod),
			   cfg->audio.alert_dev,
			   sizeof(cfg->audio.alert_dev));

	(void)conf_get_u32(conf, "ausrc_srate", &cfg->audio.srate_src);
	(void)conf_get_u32(conf, "auplay_srate", &cfg->audio.srate_play);
	(void)conf_get_u32(conf, "ausrc_channels", &cfg->audio.channels_src);
	(void)conf_get_u32(conf, "auplay_channels", &cfg->audio.channels_play);

	if (0 == conf_get(conf, "audio_txmode", &txmode)) {

		if (0 == pl_strcasecmp(&txmode, "poll"))
			cfg->audio.txmode = AUDIO_MODE_POLL;
		else if (0 == pl_strcasecmp(&txmode, "thread"))
			cfg->audio.txmode = AUDIO_MODE_THREAD;
		else {
			warning("unsupported audio txmode (%r)\n", &txmode);
		}
	}

	(void)conf_get_bool(conf, "audio_level", &cfg->audio.level);

	conf_get_aufmt(conf, "ausrc_format", &cfg->audio.src_fmt);
	conf_get_aufmt(conf, "auplay_format", &cfg->audio.play_fmt);
	conf_get_aufmt(conf, "auenc_format", &cfg->audio.enc_fmt);
	conf_get_aufmt(conf, "audec_format", &cfg->audio.dec_fmt);

	conf_get_range(conf, "audio_buffer", &cfg->audio.buffer);
	if (!cfg->audio.buffer.min || !cfg->audio.buffer.max) {
		warning("config: audio_buffer cannot be zero\n");
		return EINVAL;
	}

	if (0 == conf_get(conf, "audio_buffer_mode", &pl))
		cfg->audio.adaptive = conf_aubuf_adaptive(&pl);

	(void)conf_get_float(conf, "audio_silence", &cfg->audio.silence);
	(void)conf_get_u32(conf, "audio_telev_pt", &cfg->audio.telev_pt);

	/* Video */
	(void)conf_get_csv(conf, "video_source",
			   cfg->video.src_mod, sizeof(cfg->video.src_mod),
			   cfg->video.src_dev, sizeof(cfg->video.src_dev));
	(void)conf_get_csv(conf, "video_display",
			   cfg->video.disp_mod, sizeof(cfg->video.disp_mod),
			   cfg->video.disp_dev, sizeof(cfg->video.disp_dev));
	if (0 == conf_get_vidsz(conf, "video_size", &size)) {
		cfg->video.width  = size.w;
		cfg->video.height = size.h;
	}
	(void)conf_get_u32(conf, "video_bitrate", &cfg->video.bitrate);
	(void)conf_get_u32(conf, "video_sendrate", &cfg->video.send_bitrate);
	(void)conf_get_u32(conf, "video_burst_bits", &cfg->video.burst_bits);
	(void)conf_get_float(conf, "video_fps", &cfg->video.fps);
	(void)conf_get_bool(conf, "video_fullscreen", &cfg->video.fullscreen);

	conf_get_vidfmt(conf, "videnc_format", &cfg->video.enc_fmt);

	/* AVT - Audio/Video Transport */
	if (0 == conf_get_u32(conf, "rtp_tos", &v))
		cfg->avt.rtp_tos = v;
	if (0 == conf_get_u32(conf, "rtp_video_tos", &v))
		cfg->avt.rtpv_tos = v;
	(void)conf_get_range(conf, "rtp_ports", &cfg->avt.rtp_ports);
	if (0 == conf_get_range(conf, "rtp_bandwidth",
				&cfg->avt.rtp_bw)) {
		cfg->avt.rtp_bw.min *= 1000;
		cfg->avt.rtp_bw.max *= 1000;
	}

	if (0 == conf_get(conf, "audio_jitter_buffer_delay", &jbtype)) {
		warning("config: audio_jitter_buffer_delay is deprecated, use "
			"audio_jitter_buffer_ms and "
			"audio_jitter_buffer_size\n");
	}
	if (0 == conf_get(conf, "video_jitter_buffer_delay", &jbtype)) {
		warning("config: video_jitter_buffer_delay is deprecated, use "
			"video_jitter_buffer_ms and "
			"video_jitter_buffer_size\n");
	}

	if (0 == conf_get(conf, "audio_jitter_buffer_type", &jbtype))
		cfg->avt.audio.jbtype = conf_get_jbuf_type(&jbtype);

	(void)conf_get_range(conf, "audio_jitter_buffer_ms",
			     &cfg->avt.audio.jbuf_del);

	(void)conf_get_u32(conf, "audio_jitter_buffer_size",
			   &cfg->avt.audio.jbuf_sz);

	if (0 == conf_get(conf, "video_jitter_buffer_type", &jbtype))
		cfg->avt.video.jbtype = conf_get_jbuf_type(&jbtype);

	(void)conf_get_range(conf, "video_jitter_buffer_ms",
			     &cfg->avt.video.jbuf_del);

	(void)conf_get_u32(conf, "video_jitter_buffer_size",
			   &cfg->avt.video.jbuf_sz);

	(void)conf_get_bool(conf, "rtp_stats", &cfg->avt.rtp_stats);
	(void)conf_get_u32(conf, "rtp_timeout", &cfg->avt.rtp_timeout);

	(void)conf_get_bool(conf, "avt_bundle", &cfg->avt.bundle);
	if (0 == conf_get(conf, "rtp_rxmode", &rxmode)) {
		cfg->avt.rxmode = resolve_receive_mode(&rxmode);
	}

	if (err) {
		warning("config: configure parse error (%m)\n", err);
	}

	/* Network */
	(void)conf_apply(conf, "dns_server", dns_server_handler, &cfg->net);
	(void)conf_apply(conf, "dns_fallback",
			   dns_fallback_handler, &cfg->net);
	(void)conf_get_bool(conf, "dns_getaddrinfo",
			    &cfg->net.use_getaddrinfo);
	(void)conf_get_str(conf, "net_interface",
			   cfg->net.ifname, sizeof(cfg->net.ifname));
	if (0 == conf_get(conf, "net_af", &pl)) {
		if (0 == pl_strcasecmp(&pl, "ipv4"))
			cfg->net.af = AF_INET;
		else if (0 == pl_strcasecmp(&pl, "ipv6"))
			cfg->net.af = AF_INET6;
		else {
			warning("unsupported af (%r)\n", &pl);
		}
	}

	return err;
}


/**
 * Print the baresip core config
 *
 * @param pf  Print function
 * @param cfg Baresip core config
 *
 * @return 0 if success, otherwise errorcode
 */
int config_print(struct re_printf *pf, const struct config *cfg)
{
	int err;

	if (!cfg)
		return 0;

	err = re_hprintf(pf,
			 "\n"
			 "# SIP\n"
			 "sip_listen\t\t%s\n"
			 "sip_certificate\t%s\n"
			 "sip_cafile\t\t%s\n"
			 "sip_capath\t\t%s\n"
			 "sip_transports\t\t%H\n"
			 "sip_trans_def\t%s\n"
			 "sip_verify_server\t\t\t%s\n"
			 "sip_verify_client\t\t\t%s\n"
			 "sip_tls_resumption\t\t\t%s\n"
			 "sip_tos\t%u\n"
			 "filter_registrar\t%H\n"
			 "\n"
			 "# Call\n"
			 "call_local_timeout\t%u\n"
			 "call_max_calls\t\t%u\n"
			 "call_hold_other_calls\t%s\n"
			 "call_accept\t\t%s\n"
			 "\n",
			 cfg->sip.local, cfg->sip.cert, cfg->sip.cafile,
			 cfg->sip.capath, sip_transports_print,
			 &cfg->sip.transports,
			 sip_transp_name(cfg->sip.transp),
			 cfg->sip.verify_server ? "yes" : "no",
			 cfg->sip.verify_client ? "yes" : "no",
			 tls_resume_mode_str(cfg->sip.tls_resume),
			 cfg->sip.tos, sip_transports_print_mask,
			 &cfg->sip.reg_filt,

			 cfg->call.local_timeout,
			 cfg->call.max_calls,
			 cfg->call.hold_other_calls ? "yes" : "no",
			 cfg->call.accept ? "yes" : "no");
	if (err)
		return err;

	err = re_hprintf(pf,
			 "# Audio\n"
			 "audio_path\t\t%s\n"
			 "audio_player\t\t%s,%s\n"
			 "audio_source\t\t%s,%s\n"
			 "audio_alert\t\t%s,%s\n"
			 "auplay_srate\t\t%u\n"
			 "ausrc_srate\t\t%u\n"
			 "auplay_channels\t\t%u\n"
			 "ausrc_channels\t\t%u\n"
			 "audio_txmode\t\t%s\n"
			 "audio_level\t\t%s\n"
			 "ausrc_format\t\t%s\n"
			 "auplay_format\t\t%s\n"
			 "auenc_format\t\t%s\n"
			 "audec_format\t\t%s\n"
			 "audio_buffer\t\t%H\t\t# ms\n"
			 "audio_buffer_mode\t%s\t\t# fixed, adaptive\n"
			 "audio_silence\t\t%.1lf\t\t# in [dB]\n"
			 "audio_telev_pt\t\t%u\n"
			 "\n",
			 cfg->audio.audio_path,
			 cfg->audio.play_mod,  cfg->audio.play_dev,
			 cfg->audio.src_mod,   cfg->audio.src_dev,
			 cfg->audio.alert_mod, cfg->audio.alert_dev,
			 cfg->audio.srate_play, cfg->audio.srate_src,
			 cfg->audio.channels_play, cfg->audio.channels_src,
			 cfg->audio.txmode == AUDIO_MODE_POLL ?
						"poll" : "thread",
			 cfg->audio.level ? "yes" : "no",
			 aufmt_name(cfg->audio.src_fmt),
			 aufmt_name(cfg->audio.play_fmt),
			 aufmt_name(cfg->audio.enc_fmt),
			 aufmt_name(cfg->audio.dec_fmt),
			 range_print, &cfg->audio.buffer,
			 cfg->audio.adaptive ? "adaptive" : "fixed",
			 cfg->audio.silence,
			 cfg->audio.telev_pt);
	if (err)
		return err;

	err = re_hprintf(pf,
			 "# Video\n"
			 "video_source\t\t%s,%s\n"
			 "#video_source\t\tavformat,rtmp://127.0.0.1/app/foo\n"
			 "video_display\t\t%s,%s\n"
			 "video_size\t\t\"%ux%u\"\n"
			 "video_bitrate\t\t%u\n"
			 "video_fps\t\t%.2f\n"
			 "video_fullscreen\t%s\n"
			 "videnc_format\t\t%s\n"
			 "\n",
			 cfg->video.src_mod, cfg->video.src_dev,
			 cfg->video.disp_mod, cfg->video.disp_dev,
			 cfg->video.width, cfg->video.height,
			 cfg->video.bitrate, cfg->video.fps,
			 cfg->video.fullscreen ? "yes" : "no",
			 vidfmt_name(cfg->video.enc_fmt));
	if (err)
		return err;

	err = re_hprintf(pf,
			 "# AVT\n"
			 "rtp_tos\t\t\t%u\n"
			 "rtp_video_tos\t\t%u\n"
			 "rtp_ports\t\t%H\n"
			 "rtp_bandwidth\t\t%H\n"
			 "audio_jitter_buffer_type\t%s\n"
			 "audio_jitter_buffer_ms\t%H\n"
			 "audio_jitter_buffer_size\t%u\n"
			 "video_jitter_buffer_type\t%s\n"
			 "video_jitter_buffer_ms\t%H\n"
			 "video_jitter_buffer_size\t%u\n"
			 "rtp_stats\t\t%s\n"
			 "rtp_timeout\t\t%u # in seconds\n"
			 "avt_bundle\t\t%s\n"
			 "rtp_rxmode\t\t\t%s\n"
			 "\n"
			 "# Network\n"
			 "net_interface\t\t%s\n"
			 "net_af\t\t\t%s\n"
			 "\n",

			 cfg->avt.rtp_tos,
			 cfg->avt.rtpv_tos,
			 range_print, &cfg->avt.rtp_ports,
			 range_print, &cfg->avt.rtp_bw,
			 jbuf_type_str(cfg->avt.audio.jbtype),
			 range_print, &cfg->avt.audio.jbuf_del,
			 cfg->avt.audio.jbuf_sz,
			 jbuf_type_str(cfg->avt.video.jbtype),
			 range_print, &cfg->avt.video.jbuf_del,
			 cfg->avt.video.jbuf_sz,
			 cfg->avt.rtp_stats ? "yes" : "no",
			 cfg->avt.rtp_timeout,
			 cfg->avt.bundle ? "yes" : "no",
			 rtp_receive_mode_str(cfg->avt.rxmode),

			 cfg->net.ifname,
			 net_af_str(cfg->net.af)
		   );

	return err;
}


static const char *default_cafile(void)
{
#if defined (DEFAULT_CAFILE)
	return DEFAULT_CAFILE;
#elif defined (DARWIN) || defined (FREEBSD)
	return "/etc/ssl/cert.pem";
#else
	return "/etc/ssl/certs/ca-certificates.crt";
#endif
}


static const char *default_capath(void)
{
#if defined (DEFAULT_CAPATH)
	return DEFAULT_CAPATH;
#elif defined (ANDROID)
	return "/system/etc/security/cacerts";
#else
	return "/etc/ssl/certs";
#endif
}


static const char *default_audio_device(void)
{
#if defined (DEFAULT_AUDIO_DEVICE)
	return DEFAULT_AUDIO_DEVICE;
#elif defined (ANDROID)
	return "opensles,nil";
#elif defined (DARWIN)
	#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE || \
	    defined(TARGET_OS_SIMULATOR) && TARGET_OS_SIMULATOR
	return "audiounit,default";
	#else
	return "coreaudio,default";
	#endif
#elif defined (FREEBSD)
	return "alsa,default";
#elif defined (OPENBSD)
	return "sndio,default";
#elif defined (WIN32)
	return "wasapi,default";
#else
	return "alsa,default";
#endif
}


static const char *default_video_device(void)
{
#ifdef DARWIN
	return "avcapture,nil";
#elif defined (WIN32)
	return "dshow,nil";
#else
	return "v4l2,/dev/video0";
#endif
}


static const char *default_video_display(void)
{
#ifdef DARWIN
	return "sdl,nil";
#elif defined (WIN32)
	return "sdl,nil";
#else
	return "x11,nil";
#endif
}


static const char *default_avcodec_hwaccel(void)
{
#if defined (LINUX)
	return "vaapi";
#elif defined (DARWIN)
	return "videotoolbox";
#elif defined (WIN32)
	return "nvenc";
#else
	return "none";
#endif
}


static int default_interface_print(struct re_printf *pf, void *unused)
{
	char ifname[64];
	(void)unused;

	if (0 == net_rt_default_get(AF_INET, ifname, sizeof(ifname)))
		return re_hprintf(pf, "%s", ifname);
	else
		return re_hprintf(pf, "eth0");
}


static const char *default_audio_path(void)
{
#if defined (SHARE_PATH)
	return SHARE_PATH;
#elif defined (PREFIX)
	return PREFIX "/share/baresip";
#else
	return "/usr/share/baresip";
#endif
}


static int core_config_template(struct re_printf *pf, const struct config *cfg)
{
	bool have_cafile = false;
	bool have_capath = false;
	int err = 0;

	if (!cfg)
		return 0;

#if defined (DEFAULT_CAFILE) || defined (DARWIN) || defined (LINUX) \
	|| defined (FREEBSD)
	have_cafile = true;
#endif

#if defined (DEFAULT_CAPATH) || defined (ANDROID) || defined (LINUX)
	have_capath = true;
#endif

	err |= re_hprintf(pf,
			  "\n# SIP\n"
			  "#sip_listen\t\t0.0.0.0:5060\n"
			  "#sip_certificate\tcert.pem\n"
			  "%ssip_cafile\t\t%s\n"
			  "%ssip_capath\t\t%s\n"
			  "#sip_transports\t\tudp,tcp,tls,ws,wss\n"
			  "#sip_trans_def\t\tudp\n"
			  "#sip_verify_server\tyes\n"
			  "#sip_verify_client\tno\n"
			  "#sip_tls_resumption\tall\n"
			  "sip_tos\t\t\t160\n"
			  "#filter_registrar\tudp,tcp,tls,ws,wss\n"
			  "\n"
			  ,
			  have_cafile ? "" : "#",
			  default_cafile(),
			  have_capath ? "" : "#",
			  default_capath());

	err |= re_hprintf(pf,
			  "# Call\n"
			  "call_local_timeout\t%u\n"
			  "call_max_calls\t\t%u\n"
			  "call_hold_other_calls\tyes\n"
			  "call_accept\t\tno\n"
			  "\n"
			  ,
			  cfg->call.local_timeout,
			  cfg->call.max_calls);

	err |= re_hprintf(pf,
			  "# Audio\n"
			  "#audio_path\t\t%s\n"
			  "audio_player\t\t%s\n"
			  "audio_source\t\t%s\n"
			  "audio_alert\t\t%s\n"
			  "#ausrc_srate\t\t48000\n"
			  "#auplay_srate\t\t48000\n"
			  "#ausrc_channels\t\t0\n"
			  "#auplay_channels\t0\n"
			  "#audio_txmode\t\tpoll\t\t# poll, thread\n"
			  "audio_level\t\tno\n"
			  "ausrc_format\t\ts16\t\t# s16, float, ..\n"
			  "auplay_format\t\ts16\t\t# s16, float, ..\n"
			  "auenc_format\t\ts16\t\t# s16, float, ..\n"
			  "audec_format\t\ts16\t\t# s16, float, ..\n"
			  "audio_buffer\t\t%H\t\t# ms\n"
			  "audio_buffer_mode\t%s\t\t# fixed, adaptive\n"
			  "audio_silence\t\t%.1lf\t\t# in [dB]\n"
			  "audio_telev_pt\t\t%u\t\t"
			  "# payload type for telephone-event\n"
			  "\n"
			  ,
			  default_audio_path(),
			  default_audio_device(),
			  default_audio_device(),
			  default_audio_device(),
			  range_print, &cfg->audio.buffer,
			  cfg->audio.adaptive ? "adaptive" : "fixed",
			  cfg->audio.silence,
			  cfg->audio.telev_pt);

	err |= re_hprintf(pf,
			  "# Video\n"
			  "#video_source\t\t%s\n"
			  "#video_display\t\t%s\n"
			  "video_size\t\t%dx%d\n"
			  "video_bitrate\t\t%u\n"
			  "video_fps\t\t%.2f\n"
			  "video_fullscreen\tno\n"
			  "videnc_format\t\t%s\n"
			  ,
			  default_video_device(),
			  default_video_display(),
			  cfg->video.width, cfg->video.height,
			  cfg->video.bitrate, cfg->video.fps,
			  vidfmt_name(cfg->video.enc_fmt));

	err |= re_hprintf(pf,
			  "\n# AVT - Audio/Video Transport\n"
			  "rtp_tos\t\t\t184\n"
			  "rtp_video_tos\t\t136\n"
			  "#rtp_ports\t\t10000-20000\n"
			  "#rtp_bandwidth\t\t512-1024 # [kbit/s]\n"
			  "audio_jitter_buffer_type\tfixed\t\t# off, fixed,"
				" adaptive\n"
			  "audio_jitter_buffer_ms\t%u-%u\t\t"
				"# Min. - Max. [ms]\n"
			  "audio_jitter_buffer_size\t50\t\t# [packets]\n"
			  "video_jitter_buffer_type\tfixed\t\t# off, fixed,"
				" adaptive\n"
			  "video_jitter_buffer_ms\t%u-%u\t\t"
				"# Min. - Max. [ms]\n"
			  "video_jitter_buffer_size\t250\t\t# [packets]\n"
			  "rtp_stats\t\tno\n"
			  "#rtp_timeout\t\t60\n"
			  "#avt_bundle\t\tno\n"
			  "#rtp_rxmode\t\tmain\n"
			  "\n# Network\n"
			  "#dns_server\t\t1.1.1.1:53\n"
			  "#dns_server\t\t1.0.0.1:53\n"
			  "#dns_fallback\t\t8.8.8.8:53\n"
			  "#dns_getaddrinfo\t\tno\n"
			  "#net_interface\t\t%H\n"
			  "\n"
			  "# Play tones\n"
			  "#file_ausrc\t\taufile\n"
			  "#file_srate\t\t16000\n"
			  "#file_channels\t\t1\n",
			  cfg->avt.audio.jbuf_del.min,
			  cfg->avt.audio.jbuf_del.max,
			  cfg->avt.video.jbuf_del.min,
			  cfg->avt.video.jbuf_del.max,
			  default_interface_print, NULL);

	return err;
}


static uint32_t count_modules(const char *path)
{
#ifdef WIN32
	(void)path;
	return 0;
#else
	DIR *dirp;
	struct dirent *dp;
	uint32_t n = 0;

	dirp = opendir(path);
	if (!dirp)
		return 0;

	while ((dp = readdir(dirp)) != NULL) {

		size_t len = strlen(dp->d_name);
		const size_t x = sizeof(MOD_EXT)-1;

		if (len <= x)
			continue;

		if (0==memcmp(&dp->d_name[len-x], MOD_EXT, x))
			++n;
	}

	(void)closedir(dirp);

	return n;
#endif
}


static const char *detect_module_path(bool *valid)
{
	static const char * const pathv[] = {
#if defined (MOD_PATH)
		MOD_PATH,
#elif defined (PREFIX)
		"" PREFIX "/lib/baresip/modules",
#else
		"/usr/local/lib/baresip/modules",
		"/usr/lib/baresip/modules",
#endif
	};
	const char *current = pathv[0];
	uint32_t nmax = 0;
	size_t i;

	for (i=0; i<RE_ARRAY_SIZE(pathv); i++) {

		uint32_t n = count_modules(pathv[i]);

		info("%s: detected %u modules\n", pathv[i], n);

		if (n > nmax) {
			nmax = n;
			current = pathv[i];
		}
	}

	if (nmax > 0)
		*valid = true;

	return current;
}


void u32mask_enable(uint32_t *mask, uint8_t bit, bool enable)
{
	if (!mask)
		return;

	if (enable)
		*mask |=  (1u << bit);
	else
		*mask &= ~(1u << bit);
}


bool u32mask_enabled(uint32_t mask, uint8_t bit)
{
	return 0 != (mask & (1u << bit));
}


/**
 * Write the baresip core config template to a file
 *
 * @param file Filename of output file
 * @param cfg  Baresip core config
 *
 * @return 0 if success, otherwise errorcode
 */
int config_write_template(const char *file, const struct config *cfg)
{
	FILE *f = NULL;
	int err = 0;
	const char *modpath;
	bool modpath_valid = false;

	if (!file || !cfg)
		return EINVAL;

	info("config: creating config template %s\n", file);

	err = fs_fopen(&f, file, "w");
	if (err) {
		warning("config: writing %s: %m\n", file, err);
		return err;
	}

	(void)re_fprintf(f,
			 "#\n"
			 "# baresip configuration\n"
			 "#\n"
			 "\n"
			 "#------------------------------------"
			 "------------------------------------------\n");

	(void)re_fprintf(f, "%H", core_config_template, cfg);

	(void)re_fprintf(f,
			 "\n#------------------------------------"
			 "------------------------------------------\n"
			 "# Modules\n"
			 "\n");

	modpath = detect_module_path(&modpath_valid);
	(void)re_fprintf(f, "%smodule_path\t\t%s\n",
			 modpath_valid ? "" : "#", modpath);

	(void)re_fprintf(f, "\n# UI Modules\n");
#if defined (WIN32)
	(void)re_fprintf(f, "module\t\t\t" "wincons" MOD_EXT "\n");
#else
	(void)re_fprintf(f, "module\t\t\t" "stdio" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" "cons" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "evdev" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "httpd" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Audio codec Modules (in order)\n");
	(void)re_fprintf(f, "#module\t\t\t" "opus" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "amr" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "g7221" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "g722" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "g726" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" "g711" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "l16" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "mpa" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "codec2" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Audio filter Modules (in encoding order)\n");
	(void)re_fprintf(f, "module\t\t\t"  "auconv" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t"  "auresamp" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "vumeter" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "sndfile" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "plc" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "webrtc_aec" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Audio driver Modules\n");
#if defined (ANDROID)
	(void)re_fprintf(f, "module\t\t\t" "opensles" MOD_EXT "\n");
#elif defined (DARWIN)
	#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE || \
	    defined(TARGET_OS_SIMULATOR) && TARGET_OS_SIMULATOR
	(void)re_fprintf(f, "#module\t\t\t" "coreaudio" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" "audiounit" MOD_EXT "\n");
	#else
	(void)re_fprintf(f, "module\t\t\t" "coreaudio" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "audiounit" MOD_EXT "\n");
	#endif
#elif defined (FREEBSD)
	(void)re_fprintf(f, "module\t\t\t" "alsa" MOD_EXT "\n");
#elif defined (OPENBSD)
	(void)re_fprintf(f, "module\t\t\t" "sndio" MOD_EXT "\n");
#elif defined (WIN32)
	(void)re_fprintf(f, "module\t\t\t" "wasapi" MOD_EXT "\n");
#else
	if (!strncmp(default_audio_device(), "pipewire", 8)) {
		(void)re_fprintf(f, "#module\t\t\t" "alsa" MOD_EXT "\n");
		(void)re_fprintf(f, "#module\t\t\t" "pulse" MOD_EXT "\n");
		(void)re_fprintf(f, "module\t\t\t" "pipewire" MOD_EXT "\n");
	}
	else if (!strncmp(default_audio_device(), "pulse", 5)) {
		(void)re_fprintf(f, "#module\t\t\t" "alsa" MOD_EXT "\n");
		(void)re_fprintf(f, "module\t\t\t" "pulse" MOD_EXT "\n");
		(void)re_fprintf(f, "#module\t\t\t" "pipewire" MOD_EXT "\n");
	}
	else {
		(void)re_fprintf(f, "module\t\t\t" "alsa" MOD_EXT "\n");
		(void)re_fprintf(f, "#module\t\t\t" "pulse" MOD_EXT"\n");
		(void)re_fprintf(f, "#module\t\t\t" "pipewire" MOD_EXT "\n");
	}
#endif
	(void)re_fprintf(f, "#module\t\t\t" "jack" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "portaudio" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "aubridge" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "aufile" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "ausine" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video codec Modules (in order)\n");
	(void)re_fprintf(f, "#module\t\t\t" "avcodec" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "vp8" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "vp9" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video filter Modules (in encoding order)\n");
	(void)re_fprintf(f, "#module\t\t\t" "selfview" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "snapshot" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "swscale" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "vidinfo" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "avfilter" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video source modules\n");
#if defined (DARWIN)

	(void)re_fprintf(f, "module\t\t\t" "avcapture" MOD_EXT "\n");

#elif defined (WIN32)
	(void)re_fprintf(f, "module\t\t\t" "dshow" MOD_EXT "\n");

#else
	(void)re_fprintf(f, "#module\t\t\t" "v4l2" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" "vidbridge" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video display modules\n");
#ifdef LINUX
	(void)re_fprintf(f, "#module\t\t\t" "directfb" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" "x11" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "sdl" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "fakevideo" MOD_EXT "\n");


	(void)re_fprintf(f, "\n# Audio/Video source modules\n");
	(void)re_fprintf(f, "#module\t\t\t" "avformat" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "gst" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Compatibility modules\n");
	(void)re_fprintf(f, "#module\t\t\t" "ebuacip" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" "uuid" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Media NAT modules\n");
	(void)re_fprintf(f, "module\t\t\t" "stun" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" "turn" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" "ice" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "natpmp" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "pcp" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Media encryption modules\n");
	(void)re_fprintf(f, "#module\t\t\t" "srtp" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "dtls_srtp" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" "gzrtp" MOD_EXT "\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n#------------------------------------"
			 "------------------------------------------\n");
	(void)re_fprintf(f, "# Application Modules\n");
	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "module_app\t\t" "account" MOD_EXT "\n");
	(void)re_fprintf(f, "module_app\t\t"  "contact"MOD_EXT"\n");
	(void)re_fprintf(f, "module_app\t\t"  "debug_cmd"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t"  "echo"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" "gtk" MOD_EXT "\n");
	(void)re_fprintf(f, "module_app\t\t"  "menu"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t"  "mwi"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" "presence"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" "serreg"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" "syslog"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" "mqtt" MOD_EXT "\n");
	(void)re_fprintf(f, "#module_app\t\t" "ctrl_tcp" MOD_EXT "\n");
	(void)re_fprintf(f, "#module_app\t\t" "ctrl_dbus"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" "httpreq"MOD_EXT"\n");
	(void)re_fprintf(f, "module_app\t\t" "netroam"MOD_EXT"\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n#------------------------------------"
			 "------------------------------------------\n");
	(void)re_fprintf(f, "# Module parameters\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "# DTLS SRTP parameters\n");
	(void)re_fprintf(f, "#dtls_srtp_use_ec\tprime256v1\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n# UI Modules parameters\n");
	(void)re_fprintf(f, "cons_listen\t\t0.0.0.0:5555 # cons - "
				"Console UI UDP/TCP sockets\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "http_listen\t\t0.0.0.0:8000 # httpd - "
				"HTTP Server\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "ctrl_tcp_listen\t\t0.0.0.0:4444 # ctrl_tcp - "
				"TCP interface JSON\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "evdev_device\t\t/dev/input/event0\n");

	(void)re_fprintf(f, "\n# Opus codec parameters\n");
	(void)re_fprintf(f, "opus_bitrate\t\t28000 # 6000-510000\n");
	(void)re_fprintf(f, "#opus_stereo\t\tyes\n");
	(void)re_fprintf(f, "#opus_sprop_stereo\tyes\n");
	(void)re_fprintf(f, "#opus_cbr\t\tno\n");
	(void)re_fprintf(f, "#opus_inbandfec\t\tno\n");
	(void)re_fprintf(f, "#opus_dtx\t\tno\n");
	(void)re_fprintf(f, "#opus_mirror\t\tno\n");
	(void)re_fprintf(f, "#opus_complexity\t10\n");
	(void)re_fprintf(f, "#opus_application\taudio\t# {voip,audio}\n");
	(void)re_fprintf(f, "#opus_samplerate\t48000\n");
	(void)re_fprintf(f, "#opus_packet_loss\t10\t# 0-100 percent "
				"(expected packet loss)\n");

	(void)re_fprintf(f, "\n# Opus Multistream codec parameters\n");
	(void)re_fprintf(f,
			 "#opus_ms_channels\t2\t#total channels (2 or 4)\n");
	(void)re_fprintf(f,
			 "#opus_ms_streams\t2\t#number of streams\n");
	(void)re_fprintf(f,
			"#opus_ms_c_streams\t2\t#number of coupled streams\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "vumeter_stderr\t\tyes\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "#jack_connect_ports\tyes\n");

	(void)re_fprintf(f,
			"\n# Selfview\n"
			"video_selfview\t\twindow # {window,pip}\n"
			"#selfview_size\t\t64x64\n");

	(void)re_fprintf(f,
			"\n# Menu\n"
			"#redial_attempts\t0 # Num or <inf>\n"
			"#redial_delay\t\t5 # Delay in seconds\n"
			"#ringback_disabled\tno\n"
			"#statmode_default\toff\n"
			"#menu_clean_number\tno\n"
			"#sip_autoanswer_method\trfc5373 "
			"# {rfc5373,call-info,alert-info}\n"
			"#ring_aufile\t\tring.wav\n"
			"#hangup_aufile\t\tnone\n"
			"#callwaiting_aufile\tcallwaiting.wav\n"
			"#ringback_aufile\tringback.wav\n"
			"#notfound_aufile\tnotfound.wav\n"
			"#busy_aufile\t\tbusy.wav\n"
			"#error_aufile\t\terror.wav\n"
			"#sip_autoanswer_aufile\tautoanswer.wav\n"
			"#menu_max_earlyaudio\t32\n"
			"#menu_max_earlyvideo_rx\t32\n"
			"#menu_max_earlyvideo_tx\t32\n"
			"#menu_message_tone\tyes\n"
			);

	(void)re_fprintf(f,
			"\n# GTK\n"
			"#gtk_clean_number\tno\n"
			"#gtk_use_status_icon\tyes\n"
			"gtk_use_window\tyes\n"
			);

	(void)re_fprintf(f,
			"\n# avcodec\n"
			"#avcodec_h264enc\tlibx264\n"
			"#avcodec_h264dec\th264\n"
			"#avcodec_h265enc\tlibx265\n"
			"#avcodec_h265dec\thevc\n"
			"#avcodec_hwaccel\t%s\n"
			"#avcodec_profile_level_id 42002a\n"
			"#avcodec_keyint\t\t10\n",
			default_avcodec_hwaccel()
			);

	(void)re_fprintf(f,
			"\n# vp8\n"
			"#vp8_enc_threads 1\n"
			"#vp8_enc_cpuused 16"
			" # range -16..16,"
			" greater 0 increases speed over quality\n"
			);

	(void)re_fprintf(f,
			"\n# ctrl_dbus\n"
			"#ctrl_dbus_use\tsystem\t\t# system, session\n");

	(void)re_fprintf(f,
			 "\n# mqtt\n"
			 "#mqtt_broker_host\tsollentuna.example.com\n"
			 "#mqtt_broker_port\t1883\n"
			 "#mqtt_broker_cafile\t/path/to/broker-ca.crt\t"
				"# set this to enforce TLS\n"
			 "#mqtt_broker_clientid\tbaresip01\t"
				"# has to be unique\n"
			 "#mqtt_broker_user\tuser\n"
			 "#mqtt_broker_password\tpass\n"
			 "#mqtt_basetopic\t\tbaresip/01\n");

	(void)re_fprintf(f,
			 "\n# sndfile\n"
			 "#snd_path\t\t/tmp\n");

	(void)re_fprintf(f,
			 "\n# EBU ACIP\n"
			 "#ebuacip_jb_type\tfixed\t# auto,fixed\n");

	(void)re_fprintf(f,
			 "\n# HTTP request module\n"
			 "#httpreq_ca\t\ttrusted1.pem\n"
			 "#httpreq_ca\t\ttrusted2.pem\n"
			 "#httpreq_dns\t\t1.1.1.1\n"
			 "#httpreq_dns\t\t8.8.8.8\n"
			 "#httpreq_hostname\tmyserver\n"
			 "#httpreq_cert\t\tcert.pem\n"
			 "#httpreq_key\t\tkey.pem\n");

	(void)re_fprintf(f,
			 "\n# avformat\n"
			 "#avformat_hwaccel\t%s\n"
			 "#avformat_inputformat\tmjpeg\n"
			 "#avformat_decoder\tmjpeg\n"
			 "#avformat_pass_through\tyes\n"
			 "#avformat_rtsp_transport\tudp\n",
			 default_avcodec_hwaccel());

	(void)re_fprintf(f, "\n# ice\n"
			    "#ice_policy\t\tall\t# all, relay (candidates)\n");

	if (f)
		(void)fclose(f);

	return err;
}


/**
 * Get the baresip core config
 *
 * @return Core config
 */
struct config *conf_config(void)
{
	return &core_config;
}
