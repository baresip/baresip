/**
 * @file config.c  Core Configuration
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <dirent.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


#undef MOD_PRE
#define MOD_PRE ""  /**< Module prefix */


#undef SA_INIT
#define SA_INIT { { {0} }, 0}


/** Core Run-time Configuration - populated from config file */
static struct config core_config = {

	/** SIP User-Agent */
	{
		16,
		"",
		"",
		""
	},

	/** Call config */
	{
		120,
		4
	},

	/** Audio */
	{
		"",
		"","",
		"","",
		"","",
		{8000, 48000},
		{1, 2},
		0,
		0,
		0,
		0,
		false,
		AUDIO_MODE_POLL,
	},

#ifdef USE_VIDEO
	/** Video */
	{
		"", "",
		"", "",
		352, 288,
		500000,
		25,
	},
#endif

	/** Audio/Video Transport */
	{
		0xb8,
		{1024, 49152},
		{0, 0},
		true,
		false,
		{5, 10},
		false,
		0
	},

	/* Network */
	{
		"",
		{ {""} },
		0
	},

#ifdef USE_VIDEO
	/* BFCP */
	{
		""
	},
#endif
};


static int range_print(struct re_printf *pf, const struct range *rng)
{
	if (!rng)
		return 0;

	return re_hprintf(pf, "%u-%u", rng->min, rng->max);
}


static int dns_server_handler(const struct pl *pl, void *arg)
{
	struct config_net *cfg = arg;
	const size_t max_count = ARRAY_SIZE(cfg->nsv);
	int err;

	if (cfg->nsc >= max_count) {
		warning("config: too many DNS nameservers (max %zu)\n",
			max_count);
		return EOVERFLOW;
	}

	/* Append dns_server to the network config */
	err = pl_strcpy(pl, cfg->nsv[cfg->nsc].addr,
			sizeof(cfg->nsv[0].addr));
	if (err) {
		warning("config: dns_server: could not copy string (%r)\n",
			pl);
		return err;
	}

	++cfg->nsc;

	return 0;
}


int config_parse_conf(struct config *cfg, const struct conf *conf)
{
	struct pl pollm, as, ap;
	enum poll_method method;
	struct vidsz size = {0, 0};
	uint32_t v;
	int err = 0;

	if (!cfg || !conf)
		return EINVAL;

	/* Core */
	if (0 == conf_get(conf, "poll_method", &pollm)) {
		if (0 == poll_method_type(&method, &pollm)) {
			err = poll_method_set(method);
			if (err) {
				warning("config: poll method (%r) set: %m\n",
					&pollm, err);
			}
		}
		else {
			warning("config: unknown poll method (%r)\n", &pollm);
		}
	}

	/* SIP */
	(void)conf_get_u32(conf, "sip_trans_bsize", &cfg->sip.trans_bsize);
	(void)conf_get_str(conf, "sip_listen", cfg->sip.local,
			   sizeof(cfg->sip.local));
	(void)conf_get_str(conf, "sip_certificate", cfg->sip.cert,
			   sizeof(cfg->sip.cert));

	/* Call */
	(void)conf_get_u32(conf, "call_local_timeout",
			   &cfg->call.local_timeout);
	(void)conf_get_u32(conf, "call_max_calls",
			   &cfg->call.max_calls);

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

	(void)conf_get_range(conf, "audio_srate", &cfg->audio.srate);
	(void)conf_get_range(conf, "audio_channels", &cfg->audio.channels);
	(void)conf_get_u32(conf, "ausrc_srate", &cfg->audio.srate_src);
	(void)conf_get_u32(conf, "auplay_srate", &cfg->audio.srate_play);
	(void)conf_get_u32(conf, "ausrc_channels", &cfg->audio.channels_src);
	(void)conf_get_u32(conf, "auplay_channels", &cfg->audio.channels_play);

	if (0 == conf_get(conf, "audio_source", &as) &&
	    0 == conf_get(conf, "audio_player", &ap))
		cfg->audio.src_first = as.p < ap.p;

#ifdef USE_VIDEO
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
	(void)conf_get_u32(conf, "video_fps", &cfg->video.fps);
#else
	(void)size;
#endif

	/* AVT - Audio/Video Transport */
	if (0 == conf_get_u32(conf, "rtp_tos", &v))
		cfg->avt.rtp_tos = v;
	(void)conf_get_range(conf, "rtp_ports", &cfg->avt.rtp_ports);
	if (0 == conf_get_range(conf, "rtp_bandwidth",
				&cfg->avt.rtp_bw)) {
		cfg->avt.rtp_bw.min *= 1000;
		cfg->avt.rtp_bw.max *= 1000;
	}
	(void)conf_get_bool(conf, "rtcp_enable", &cfg->avt.rtcp_enable);
	(void)conf_get_bool(conf, "rtcp_mux", &cfg->avt.rtcp_mux);
	(void)conf_get_range(conf, "jitter_buffer_delay",
			     &cfg->avt.jbuf_del);
	(void)conf_get_bool(conf, "rtp_stats", &cfg->avt.rtp_stats);
	(void)conf_get_u32(conf, "rtp_timeout", &cfg->avt.rtp_timeout);

	if (err) {
		warning("config: configure parse error (%m)\n", err);
	}

	/* Network */
	(void)conf_apply(conf, "dns_server", dns_server_handler, &cfg->net);
	(void)conf_get_str(conf, "net_interface",
			   cfg->net.ifname, sizeof(cfg->net.ifname));

#ifdef USE_VIDEO
	/* BFCP */
	(void)conf_get_str(conf, "bfcp_proto", cfg->bfcp.proto,
			   sizeof(cfg->bfcp.proto));
#endif

	return err;
}


int config_print(struct re_printf *pf, const struct config *cfg)
{
	int err;

	if (!cfg)
		return 0;

	err = re_hprintf(pf,
			 "\n"
			 "# SIP\n"
			 "sip_trans_bsize\t\t%u\n"
			 "sip_listen\t\t%s\n"
			 "sip_certificate\t%s\n"
			 "\n"
			 "# Call\n"
			 "call_local_timeout\t%u\n"
			 "call_max_calls\t%u\n"
			 "\n"
			 "# Audio\n"
			 "audio_path\t\t%s\n"
			 "audio_player\t\t%s,%s\n"
			 "audio_source\t\t%s,%s\n"
			 "audio_alert\t\t%s,%s\n"
			 "audio_srate\t\t%H\n"
			 "audio_channels\t\t%H\n"
			 "auplay_srate\t\t%u\n"
			 "ausrc_srate\t\t%u\n"
			 "auplay_channels\t\t%u\n"
			 "ausrc_channels\t\t%u\n"
			 "\n"
#ifdef USE_VIDEO
			 "# Video\n"
			 "video_source\t\t%s,%s\n"
			 "video_display\t\t%s,%s\n"
			 "video_size\t\t\"%ux%u\"\n"
			 "video_bitrate\t\t%u\n"
			 "video_fps\t\t%u\n"
			 "\n"
#endif
			 "# AVT\n"
			 "rtp_tos\t\t\t%u\n"
			 "rtp_ports\t\t%H\n"
			 "rtp_bandwidth\t\t%H\n"
			 "rtcp_enable\t\t%s\n"
			 "rtcp_mux\t\t%s\n"
			 "jitter_buffer_delay\t%H\n"
			 "rtp_stats\t\t%s\n"
			 "rtp_timeout\t\t%u # in seconds\n"
			 "\n"
			 "# Network\n"
			 "net_interface\t\t%s\n"
			 "\n"
#ifdef USE_VIDEO
			 "# BFCP\n"
			 "bfcp_proto\t\t%s\n"
			 "\n"
#endif
			 ,

			 cfg->sip.trans_bsize, cfg->sip.local, cfg->sip.cert,

			 cfg->call.local_timeout,
			 cfg->call.max_calls,

			 cfg->audio.audio_path,
			 cfg->audio.play_mod,  cfg->audio.play_dev,
			 cfg->audio.src_mod,   cfg->audio.src_dev,
			 cfg->audio.alert_mod, cfg->audio.alert_dev,
			 range_print, &cfg->audio.srate,
			 range_print, &cfg->audio.channels,
			 cfg->audio.srate_play, cfg->audio.srate_src,
			 cfg->audio.channels_play, cfg->audio.channels_src,

#ifdef USE_VIDEO
			 cfg->video.src_mod, cfg->video.src_dev,
			 cfg->video.disp_mod, cfg->video.disp_dev,
			 cfg->video.width, cfg->video.height,
			 cfg->video.bitrate, cfg->video.fps,
#endif

			 cfg->avt.rtp_tos,
			 range_print, &cfg->avt.rtp_ports,
			 range_print, &cfg->avt.rtp_bw,
			 cfg->avt.rtcp_enable ? "yes" : "no",
			 cfg->avt.rtcp_mux ? "yes" : "no",
			 range_print, &cfg->avt.jbuf_del,
			 cfg->avt.rtp_stats ? "yes" : "no",
			 cfg->avt.rtp_timeout,

			 cfg->net.ifname

#ifdef USE_VIDEO
			 ,cfg->bfcp.proto
#endif
		   );

	return err;
}


static const char *default_audio_device(void)
{
#if defined (ANDROID)
	return "opensles,nil";
#elif defined (DARWIN)
	return "coreaudio,nil";
#elif defined (FREEBSD)
	return "oss,/dev/dsp";
#elif defined (OPENBSD)
	return "sndio,default";
#elif defined (WIN32)
	return "winwave,nil";
#else
	return "alsa,default";
#endif
}


#ifdef USE_VIDEO
static const char *default_video_device(void)
{
#ifdef DARWIN

#ifdef QTCAPTURE_RUNLOOP
	return "qtcapture,nil";
#else
	return "avcapture,nil";
#endif

#else
	return "v4l2,/dev/video0";
#endif
}


static const char *default_video_display(void)
{
#ifdef DARWIN
	return "opengl,nil";
#else
	return "x11,nil";
#endif
}
#endif


static int default_interface_print(struct re_printf *pf, void *unused)
{
	char ifname[64];
	(void)unused;

	if (0 == net_rt_default_get(AF_INET, ifname, sizeof(ifname)))
		return re_hprintf(pf, "%s", ifname);
	else
		return re_hprintf(pf, "eth0");
}


static int core_config_template(struct re_printf *pf, const struct config *cfg)
{
	int err = 0;

	if (!cfg)
		return 0;

	err |= re_hprintf(pf,
			  "\n# Core\n"
			  "poll_method\t\t%s\t\t# poll, select"
#ifdef HAVE_EPOLL
				", epoll .."
#endif
#ifdef HAVE_KQUEUE
				", kqueue .."
#endif
				"\n"
			  "\n# SIP\n"
			  "sip_trans_bsize\t\t128\n"
			  "#sip_listen\t\t0.0.0.0:5060\n"
			  "#sip_certificate\tcert.pem\n"
			  "\n"
			  "# Call\n"
			  "call_local_timeout\t%u\n"
			  "call_max_calls\t%u\n"
			  "\n"
			  "# Audio\n"
			  "#audio_path\t\t/usr/share/baresip\n"
			  "audio_player\t\t%s\n"
			  "audio_source\t\t%s\n"
			  "audio_alert\t\t%s\n"
			  "audio_srate\t\t%u-%u\n"
			  "audio_channels\t\t%u-%u\n"
			  "#ausrc_srate\t\t48000\n"
			  "#auplay_srate\t\t48000\n"
			  "#ausrc_channels\t\t0\n"
			  "#auplay_channels\t\t0\n"
			  ,
			  poll_method_name(poll_method_best()),
			  cfg->call.local_timeout,
			  cfg->call.max_calls,
			  default_audio_device(),
			  default_audio_device(),
			  default_audio_device(),
			  cfg->audio.srate.min, cfg->audio.srate.max,
			  cfg->audio.channels.min, cfg->audio.channels.max);

#ifdef USE_VIDEO
	err |= re_hprintf(pf,
			  "\n# Video\n"
			  "#video_source\t\t%s\n"
			  "#video_display\t\t%s\n"
			  "video_size\t\t%dx%d\n"
			  "video_bitrate\t\t%u\n"
			  "video_fps\t\t%u\n",
			  default_video_device(),
			  default_video_display(),
			  cfg->video.width, cfg->video.height,
			  cfg->video.bitrate, cfg->video.fps);
#endif

	err |= re_hprintf(pf,
			  "\n# AVT - Audio/Video Transport\n"
			  "rtp_tos\t\t\t184\n"
			  "#rtp_ports\t\t10000-20000\n"
			  "#rtp_bandwidth\t\t512-1024 # [kbit/s]\n"
			  "rtcp_enable\t\tyes\n"
			  "rtcp_mux\t\tno\n"
			  "jitter_buffer_delay\t%u-%u\t\t# frames\n"
			  "rtp_stats\t\tno\n"
			  "#rtp_timeout\t\t60\n"
			  "\n# Network\n"
			  "#dns_server\t\t10.0.0.1:53\n"
			  "#net_interface\t\t%H\n",
			  cfg->avt.jbuf_del.min, cfg->avt.jbuf_del.max,
			  default_interface_print, NULL);

#ifdef USE_VIDEO
	err |= re_hprintf(pf,
			  "\n# BFCP\n"
			  "#bfcp_proto\t\tudp\n");
#endif

	return err;
}


static uint32_t count_modules(const char *path)
{
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
}


static const char *detect_module_path(bool *valid)
{
	static const char * const pathv[] = {
#if defined (PREFIX)
		"" PREFIX "/lib/baresip/modules",
#else
		"/usr/local/lib/baresip/modules",
		"/usr/lib/baresip/modules",
#endif
	};
	const char *current = pathv[0];
	uint32_t nmax = 0;
	size_t i;

	for (i=0; i<ARRAY_SIZE(pathv); i++) {

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


int config_write_template(const char *file, const struct config *cfg)
{
	FILE *f = NULL;
	int err = 0;
	const char *modpath;
	bool modpath_valid = false;

	if (!file || !cfg)
		return EINVAL;

	info("config: creating config template %s\n", file);

	f = fopen(file, "w");
	if (!f) {
		warning("config: writing %s: %m\n", file, errno);
		return errno;
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
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "wincons" MOD_EXT "\n");
#else
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "stdio" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "cons" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "evdev" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "httpd" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Audio codec Modules (in order)\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "opus" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "silk" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "amr" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "g7221" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "g722" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "g726" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "g711" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "gsm" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "l16" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "speex" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "bv32" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "mpa" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Audio filter Modules (in encoding order)\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "vumeter" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "sndfile" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "speex_aec" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "speex_pp" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "plc" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Audio driver Modules\n");
#if defined (ANDROID)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "opensles" MOD_EXT "\n");
#elif defined (DARWIN)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "coreaudio" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "audiounit" MOD_EXT "\n");
#elif defined (FREEBSD)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "oss" MOD_EXT "\n");
#elif defined (OPENBSD)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "sndio" MOD_EXT "\n");
#elif defined (WIN32)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "winwave" MOD_EXT "\n");
#else
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "alsa" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "pulse" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "portaudio" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "aubridge" MOD_EXT "\n");

#ifdef USE_VIDEO

	(void)re_fprintf(f, "\n# Video codec Modules (in order)\n");
#ifdef USE_AVCODEC
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "avcodec" MOD_EXT "\n");
#else
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "avcodec" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "vp8" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video filter Modules (in encoding order)\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "selfview" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "vidinfo" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video source modules\n");
#if defined (DARWIN)

#ifdef QTCAPTURE_RUNLOOP
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "qtcapture" MOD_EXT "\n");
#else
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "avcapture" MOD_EXT "\n");
#endif

#else
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "v4l" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "v4l2" MOD_EXT "\n");
#endif
#ifdef USE_AVFORMAT
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "avformat" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "x11grab" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "cairo" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video display modules\n");
#ifdef DARWIN
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "opengl" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "x11" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "sdl2" MOD_EXT "\n");

#endif /* USE_VIDEO */

	(void)re_fprintf(f,
			"\n# Audio/Video source modules\n"
			"#module\t\t\t" MOD_PRE "rst" MOD_EXT "\n"
			"#module\t\t\t" MOD_PRE "gst" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Media NAT modules\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "stun" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "turn" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "ice" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "natpmp" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Media encryption modules\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "srtp" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "dtls_srtp" MOD_EXT "\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n#------------------------------------"
			 "------------------------------------------\n");
	(void)re_fprintf(f, "# Temporary Modules (loaded then unloaded)\n");
	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "module_tmp\t\t" MOD_PRE "uuid" MOD_EXT "\n");
	(void)re_fprintf(f, "module_tmp\t\t" MOD_PRE "account" MOD_EXT "\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n#------------------------------------"
			 "------------------------------------------\n");
	(void)re_fprintf(f, "# Application Modules\n");
	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "module_app\t\t" MOD_PRE "auloop"MOD_EXT"\n");
	(void)re_fprintf(f, "module_app\t\t"  MOD_PRE "contact"MOD_EXT"\n");
	(void)re_fprintf(f, "module_app\t\t"  MOD_PRE "debug_cmd"MOD_EXT"\n");
	(void)re_fprintf(f, "module_app\t\t"  MOD_PRE "menu"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t"  MOD_PRE "mwi"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" MOD_PRE "natbd"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" MOD_PRE "presence"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" MOD_PRE "syslog"MOD_EXT"\n");
#ifdef USE_VIDEO
	(void)re_fprintf(f, "module_app\t\t" MOD_PRE "vidloop"MOD_EXT"\n");
#endif
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n#------------------------------------"
			 "------------------------------------------\n");
	(void)re_fprintf(f, "# Module parameters\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "cons_listen\t\t0.0.0.0:5555\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "http_listen\t\t0.0.0.0:8000\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "evdev_device\t\t/dev/input/event0\n");

	(void)re_fprintf(f, "\n# Speex codec parameters\n");
	(void)re_fprintf(f, "speex_quality\t\t7 # 0-10\n");
	(void)re_fprintf(f, "speex_complexity\t7 # 0-10\n");
	(void)re_fprintf(f, "speex_enhancement\t0 # 0-1\n");
	(void)re_fprintf(f, "speex_mode_nb\t\t3 # 1-6\n");
	(void)re_fprintf(f, "speex_mode_wb\t\t6 # 1-6\n");
	(void)re_fprintf(f, "speex_vbr\t\t0 # Variable Bit Rate 0-1\n");
	(void)re_fprintf(f, "speex_vad\t\t0 # Voice Activity Detection 0-1\n");
	(void)re_fprintf(f, "speex_agc_level\t\t8000\n");

	(void)re_fprintf(f, "\n# Opus codec parameters\n");
	(void)re_fprintf(f, "opus_bitrate\t\t28000 # 6000-510000\n");

	(void)re_fprintf(f,
			"\n# Selfview\n"
			"video_selfview\t\twindow # {window,pip}\n"
			"#selfview_size\t\t64x64\n");

	(void)re_fprintf(f,
			"\n# ICE\n"
			"ice_turn\t\tno\n"
			"ice_debug\t\tno\n"
			"ice_nomination\t\tregular\t# {regular,aggressive}\n"
			"ice_mode\t\tfull\t# {full,lite}\n");

	(void)re_fprintf(f,
			"\n# Menu\n"
			"#redial_attempts\t\t3 # Num or <inf>\n"
			"#redial_delay\t\t5 # Delay in seconds\n");

	if (f)
		(void)fclose(f);

	return err;
}


struct config *conf_config(void)
{
	return &core_config;
}
