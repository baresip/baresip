/**
 * @file src/mediatrack.c  RTC Media Track
 *
 * Copyright (C) 2021 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


static void destructor(void *data)
{
	struct media_track *media = data;

	list_unlink(&media->le);
	mem_deref(media->u.p);
}


struct media_track *media_track_add(struct list *lst,
				    enum media_kind kind,
				    mediatrack_close_h *closeh, void *arg)
{
	struct media_track *media;

	media = mem_zalloc(sizeof(*media), destructor);
	if (!media)
		return NULL;

	media->kind = kind;
	media->closeh = closeh;
	media->arg = arg;

	list_append(lst, &media->le, media);

	return media;
}


int mediatrack_start_audio(struct media_track *media,
			   struct list *ausrcl, struct list *aufiltl)
{
	const struct sdp_format *fmt;
	struct audio *au;
	int err = 0;

	if (!media)
		return EINVAL;

	au = media->u.au;

	if (!media->ice_conn || !media->dtls_ok) {
		warning("mediatrack: start_audio: ice or dtls not ready\n");
		return EPROTO;
	}

	info("mediatrack: start audio\n");

	struct sdp_media *sdpm = stream_sdpmedia(audio_strm(au));
	fmt = sdp_media_rformat(sdpm, NULL);

	if (!fmt || sdp_media_dir(sdpm) == SDP_INACTIVE) {
		info("mediatrack: audio stream is disabled..\n");
		return 0;
	}

	if (sdp_media_dir(sdpm) & SDP_RECVONLY) {
		struct aucodec *ac = fmt->data;

		err = audio_decoder_set(au, ac, fmt->pt, fmt->params);
		if (err) {
			warning("mediatrack: start:"
				" audio_decoder_set error: %m\n", err);
			return err;
		}
	}

	if (sdp_media_dir(sdpm) & SDP_SENDONLY) {
		struct aucodec *ac = fmt->data;

		err = audio_encoder_set(au, ac, fmt->pt, fmt->params);
		if (err) {
			warning("mediatrack: start:"
				" audio_encoder_set error: %m\n", err);
			return err;
		}

		err = audio_start_source(au, ausrcl, aufiltl);
		if (err) {
			warning("mediatrack: start:"
				" audio_start_source error: %m\n", err);
			return err;
		}
	}

	return 0;
}


int mediatrack_start_video(struct media_track *media)
{
	const struct sdp_format *fmt;
	struct video *vid;
	int err = 0;

	if (!media)
		return EINVAL;

	vid = media->u.vid;

	if (!media->ice_conn || !media->dtls_ok) {
		warning("mediatrack: start_video: ice or dtls not ready\n");
		return EPROTO;
	}

	info("mediatrack: start video\n");

	struct sdp_media *sdpm = stream_sdpmedia(video_strm(vid));
	enum sdp_dir dir = sdp_media_dir(sdpm);

	fmt = sdp_media_rformat(sdpm, NULL);
	if (!fmt) {
		info("mediatrack: video stream is disabled..\n");
		return 0;
	}

	struct vidcodec *vc = fmt->data;

	if (dir & SDP_SENDONLY) {
		err = video_encoder_set(vid, vc, fmt->pt, fmt->params);
		if (err) {
			warning("mediatrack: start:"
				" video_encoder_set error: %m\n",
				err);
			return err;
		}

		err = video_start_source(vid);
		if (err) {
			warning("mediatrack: start:"
				" video_start_source error: %m\n",
				err);
			return err;
		}
		info("mediatrack: video source started\n");
	}

	if (dir & SDP_RECVONLY) {
		err = video_start_display(vid, "webrtc");
		if (err) {
			warning("mediatrack: start:"
				" video_start_display error: %m\n",
				err);
			return err;
		}
		info("mediatrack: video display started\n");
	}

	stream_set_rtcp_interval(video_strm(vid), 1000);

	return 0;
}


void mediatrack_stop(struct media_track *media)
{
	if (!media)
		return;

	switch (media->kind) {

	case MEDIA_KIND_AUDIO:
		audio_stop(media->u.au);
		break;

	case MEDIA_KIND_VIDEO:
		video_stop(media->u.vid);
		break;
	}
}


struct stream *media_get_stream(const struct media_track *media)
{
	if (!media)
		return NULL;

	switch (media->kind) {

	case MEDIA_KIND_AUDIO: return audio_strm(media->u.au);
	case MEDIA_KIND_VIDEO: return video_strm(media->u.vid);
	default:               return NULL;
	}
}


struct audio *media_get_audio(const struct media_track *media)
{
	if (!media || media->kind != MEDIA_KIND_AUDIO)
		return NULL;

	return media->u.au;
}


struct video *media_get_video(const struct media_track *media)
{
	if (!media || media->kind != MEDIA_KIND_VIDEO)
		return NULL;

	return media->u.vid;
}


const char *media_kind_name(enum media_kind kind)
{
	switch (kind) {

	case MEDIA_KIND_AUDIO: return "audio";
	case MEDIA_KIND_VIDEO: return "video";
	default: return "???";
	}
}


int mediatrack_debug(struct re_printf *pf, const struct media_track *media)
{
	if (!media)
		return 0;

	switch (media->kind) {

	case MEDIA_KIND_AUDIO:
		return audio_debug(pf, media->u.au);

	case MEDIA_KIND_VIDEO:
		return video_debug(pf, media->u.vid);

	default:
		return 0;
	}
}


enum media_kind mediatrack_kind(const struct media_track *media)
{
	return media ? media->kind : (enum media_kind)-1;
}


void mediatrack_summary(const struct media_track *media)
{
	if (!media || !media->u.p)
		return;

	info(".. ice_conn: %d\n", media->ice_conn);
	info(".. dtls:     %d\n", media->dtls_ok);
	info(".. rtp:      %d\n", media->rtp);
	info(".. rtcp:     %d\n", media->rtcp);
	info("\n");
}


static void mnatconn_handler(struct stream *strm, void *arg)
{
	struct media_track *media = arg;
	int err;

	info("mediatrack: ice connected (%s)\n", stream_name(strm));

	media->ice_conn = true;

	err = stream_start_mediaenc(strm);
	if (err) {
		media->closeh(err, media->arg);
	}
}


static void rtpestab_handler(struct stream *strm, void *arg)
{
	struct media_track *media = arg;

	info("mediatrack: rtp established (%s)\n", stream_name(strm));

	media->rtp = true;
}


static void rtcp_handler(struct stream *strm,
			 struct rtcp_msg *msg, void *arg)
{
	struct media_track *media = arg;
	(void)strm;
	(void)msg;

	media->rtcp = true;
}


static void stream_error_handler(struct stream *strm, int err, void *arg)
{
	struct media_track *media = arg;

	warning("mediatrack: '%s' stream error (%m)\n",
		stream_name(strm), err);

	media->closeh(err, media->arg);
}


void mediatrack_set_handlers(struct media_track *media)
{
	struct stream *strm = media_get_stream(media);

	stream_set_session_handlers(strm, mnatconn_handler, rtpestab_handler,
				    rtcp_handler, stream_error_handler, media);
}


struct media_track *mediatrack_lookup_media(const struct list *medial,
					    struct stream *strm)
{
	for (struct le *le = list_head(medial); le; le = le->next) {
		struct media_track *media = le->data;

		if (strm == media_get_stream(media))
			return media;
	}

	return NULL;
}


void mediatrack_close(struct media_track *media, int err)
{
	if (!media)
		return;

	if (media->closeh)
		media->closeh(err, media->arg);
}


/* must be done after sdp_decode() */
void mediatrack_sdp_attr_decode(struct media_track *media)
{
	if (!media || !media->u.p)
		return;

	switch (media->kind) {

	case MEDIA_KIND_AUDIO:
		audio_sdp_attr_decode(media->u.au);
		break;

	case MEDIA_KIND_VIDEO:
		video_sdp_attr_decode(media->u.vid);
		break;

	default:
		break;
	}
}
