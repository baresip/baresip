/**
 * @file player.c  Aaudio sound driver - player
 *
 */


#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>

#include <aaudio/AAudio.h>

#include "aaudio.h"

struct auplay_st {
	AAudioStream *stream;
	struct auplay_prm play_prm;
	auplay_write_h *wh;
	size_t sampsz;
	void *arg;
};

AAudioStream *playerStream = NULL;


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	if (st->stream)
		AAudioStream_close(st->stream);

	st->wh = NULL;
}

/**
 * For an output stream, this function should render and write from
 * userData numFrames of data in the streams current data format to
 * the audioData buffer.
 */
 static int dataCallback(AAudioStream *stream, void *userData,
			      void *audioData, int32_t numFrames) {
	(void)stream;
	struct auplay_st *st = userData;
	struct auframe af;

	auframe_init(&af, st->play_prm.fmt, audioData, numFrames / st->sampsz,
		     st->play_prm.srate, st->play_prm.ch);

	st->wh(&af, st->arg);

	for (size_t i = 0; i < af.sampc; ++i)
		((int16_t *)audioData)[i] = ((int16_t *)(af.sampv))[i];

	return 0;
}


static int open_player_stream(struct auplay_st *st) {

	AAudioStreamBuilder *builder;
	aaudio_result_t result;
	
	result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK) {
		warning("oboe: failed to create stream builder: error %s\n",
			AAudio_convertResultToText(result));
		return result;
	}
	
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStreamBuilder_setSharingMode(builder,
		AAUDIO_SHARING_MODE_EXCLUSIVE);
	AAudioStreamBuilder_setSampleRate(builder, st->play_prm.srate);
	AAudioStreamBuilder_setChannelCount(builder, 1);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setSessionId(builder, AAUDIO_SESSION_ID_ALLOCATE);
	AAudioStreamBuilder_setPerformanceMode(builder,
	       AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setUsage(builder,
               AAUDIO_USAGE_VOICE_COMMUNICATION),
	AAudioStreamBuilder_setDataCallback(builder, &dataCallback, st);

	result = AAudioStreamBuilder_openStream(builder, &st->stream);
	if (result != AAUDIO_OK) {
		warning("aaudio: failed to open player stream: error %s\n",
			AAudio_convertResultToText(result));
		if (recorderStream)
			AAudioStream_close(recorderStream);
		return result;
	}

	playerStream = st->stream;

	if (AAudioStream_getPerformanceMode(st->stream) !=
	    AAUDIO_PERFORMANCE_MODE_LOW_LATENCY)
		warning("aaudio: player stream is NOT low latency\n");

	AAudioStreamBuilder_delete(builder);
	
	AAudioStream_setBufferSizeInFrames(st->stream,
                AAudioStream_getFramesPerBurst(st->stream) * 2);

	return AAUDIO_OK;
}


int aaudio_player_alloc(struct auplay_st **stp, const struct auplay *ap,
	struct auplay_prm *prm, const char *dev, auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	aaudio_result_t result;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	info ("aadio: opening player (%u Hz, %d channels, device %s, "
		"ptime %u)\n", prm->srate, prm->ch, dev, prm->ptime);

	if (prm->fmt != AUFMT_S16LE) {
		warning("aaudio: player: unsupported sample format (%s)\n",
			aufmt_name((enum aufmt)prm->fmt));
		return ENOTSUP;
	}

	if (prm->ch != 1) {
		warning("aaudio: player: unsupported channel count (%u)\n",
			prm->ch);
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->play_prm.srate = prm->srate;
	st->play_prm.ch    = prm->ch;
	st->play_prm.ptime = prm->ptime;
	st->play_prm.fmt   = prm->fmt;

	st->sampsz = aufmt_sample_size(prm->fmt);

	st->wh  = wh;
	st->arg = arg;

	result = open_player_stream(st);
	if (result != AAUDIO_OK)
		goto out;

	result = AAudioStream_requestStart(st->stream);
	if (result != AAUDIO_OK) {
		warning("aaudio: player: failed to start stream\n");
		goto out;
	}

	module_event("aaudio", "player sessionid", NULL, NULL, "%d",
		     AAudioStream_getSessionId(st->stream));

	info ("aaudio: player: stream started\n");

  out:
	if (result != AAUDIO_OK)
		mem_deref(st);
	else
		*stp = st;

	return result;
}
