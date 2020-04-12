/**
 * @file ebuacip.c  EBU ACIP (Audio Contribution over IP) Profile
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>


/**
 * @defgroup ebuacip ebuacip
 *
 * EBU ACIP (Audio Contribution over IP) Profile
 *
 * Ref: https://tech.ebu.ch/docs/tech/tech3368.pdf
 *
 * Example config:
 *
 \verbatim
  ebuacip_jb_type       auto|fixed
 \endverbatim
 */

static char jb_type[16];


static int set_ebuacip_params(struct audio *au)
{
	struct stream *strm = audio_strm(au);
	struct sdp_media *sdp = stream_sdpmedia(strm);
	struct config *cfg = conf_config();
	const struct config_avt *avt = &cfg->avt;
	struct config_audio *audio = audio_config(au);
	const struct list *lst;
	struct le *le;
	int jb_id = 0;
	int err = 0;

	/* set ebuacip version fixed value 0 for now. */
	err |= sdp_media_set_lattr(sdp, false, "ebuacip", "version %i", 0);

	/* set jb option, only one in our case */
	err |= sdp_media_set_lattr(sdp, false, "ebuacip", "jb %i", jb_id);

	/* define jb value in option */
	if (0 == str_casecmp(jb_type, "auto")) {

		err |= sdp_media_set_lattr(sdp, false,
					   "ebuacip",
					   "jbdef %i auto %d-%d",
					   jb_id,
					   audio->buffer.min,
					   audio->buffer.max);
	}
	else if (0 == str_casecmp(jb_type, "fixed")) {

		/* define jb value in option from audio buffer min value */
		err |= sdp_media_set_lattr(sdp, false,
					   "ebuacip",
					   "jbdef %i fixed %d",
					   jb_id,
					   audio->buffer.min);
	}

	/* set QOS recomendation use tos / 4 to set DSCP value */
	err |= sdp_media_set_lattr(sdp, false, "ebuacip", "qosrec %u",
				   avt->rtp_tos / 4);

	/* EBU ACIP FEC:: NOT SET IN BARESIP */

	lst = sdp_media_format_lst(sdp, true);
	for (le = list_head(lst); le; le = le->next) {

		const struct sdp_format *fmt = le->data;
		struct aucodec *ac = fmt->data;

		if (!fmt->sup)
			continue;

		if (!fmt->data)
			continue;

		if (ac->ptime) {
			err |= sdp_media_set_lattr(sdp, false, "ebuacip",
						   "plength %s %u",
						   fmt->id, ac->ptime);
		}
	}

	return err;
}


static bool ebuacip_handler(const char *name, const char *value, void *arg)
{
	struct audio *au = arg;
	struct config_audio *cfg = audio_config(au);
	struct sdp_media *sdp;
	struct pl val, val_min, val_max;
	(void)name;

	/* check type first, if not fixed or auto, return false */

	if (0 == re_regex(value, str_len(value),
			  "jbdef [0-9]+ auto [0-9]+-[0-9]+",
			  NULL, &val_min, &val_max)) {

		/* set audio buffer from min and max value*/
		cfg->buffer.min = pl_u32(&val_min);
		cfg->buffer.max = pl_u32(&val_max);
	}
	else if (0 == re_regex(value, str_len(value),
			       "jbdef [0-9]+ fixed [0-9]+",
			       NULL, &val)) {

		uint32_t v = pl_u32(&val);

		/* set both audio buffer min and max value to val*/
		cfg->buffer.min = v;
		cfg->buffer.max = v;
	}
	else {
		return false;
	}

	sdp = stream_sdpmedia(audio_strm(au));
	sdp_media_del_lattr(sdp, "ebuacip");

	return true;
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct audio *au;
	(void)prm;
	(void)arg;

#if 1
	debug(".... ebuacip: [ ua=%s call=%s ] event: %s (%s)\n",
	      ua_aor(ua), call_id(call), uag_event_str(ev), prm);
#endif

	switch (ev) {

	case UA_EVENT_CALL_LOCAL_SDP:
		if (0 == str_casecmp(prm, "offer"))
			set_ebuacip_params(call_audio(call));
		break;

	case UA_EVENT_CALL_REMOTE_SDP:
		au = call_audio(call);
		sdp_media_rattr_apply(stream_sdpmedia(audio_strm(au)),
				      "ebuacip", ebuacip_handler, au);
		break;

	default:
		break;
	}
}


static int module_init(void)
{
	conf_get_str(conf_cur(), "ebuacip_jb_type", jb_type, sizeof(jb_type));

	return uag_event_register(ua_event_handler, NULL);
}


static int module_close(void)
{
	uag_event_unregister(ua_event_handler);

	return 0;
}


const struct mod_export DECL_EXPORTS(ebuacip) = {
	"ebuacip",
	"application",
	module_init,
	module_close
};
