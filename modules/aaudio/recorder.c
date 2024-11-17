/**
 * @file recorder.c  Aaudio sound driver - recorder
 *
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>

#include <aaudio/AAudio.h>

#include "aaudio.h"

struct ausrc_st {
	struct ausrc_prm src_prm;
	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void   *sampv;
	size_t  sampsz;
	size_t  sampc;
	uint64_t samps;
	void *arg;
};

AAudioStream *recorderStream = NULL;


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (recorderStream)
		AAudioStream_close(recorderStream);

	mem_deref(st->sampv);
	st->rh = NULL;
	st->errh = NULL;
}


/**
 * For an input stream, this function should read and process numFrames of
 * data from the audioData buffer. The data in the audioData buffer must not
 * be modified directly. Instead, it should be copied to another buffer
 * before doing any modification. Note that numFrames can vary unless
 * AAudioStreamBuilder_setFramesPerDataCallback() is called. Not currently
 * called.
 */
 static int dataCallback(AAudioStream *stream, void *userData,
			 void *audioData, int32_t numFrames) {
	(void)stream;
	struct ausrc_st *st = userData;
	struct auframe af;

	size_t sampc = 0;

	sampc = numFrames / st->sampsz;
	if (sampc > st->sampc) {
		st->sampv = mem_realloc(st->sampv, st->sampsz * sampc);
		st->sampc = sampc;
	}

	if (!st->sampv)
		return ENOMEM;

	memset((uint8_t *)st->sampv, 0, numFrames);

	auframe_init(&af, st->src_prm.fmt, audioData, sampc,
		     st->src_prm.srate, st->src_prm.ch);

	af.timestamp = st->samps * AUDIO_TIMEBASE /
		       (st->src_prm.srate * st->src_prm.ch);
	st->samps += sampc;
	st->rh(&af, st->arg);

	return 0;
}


static int open_recorder_stream(struct ausrc_st *st) {

	AAudioStreamBuilder *builder;
	aaudio_result_t result;

	result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK) {
		warning("oboe: failed to create stream builder: error %s\n",
			AAudio_convertResultToText(result));
		return result;
	}

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	AAudioStreamBuilder_setSharingMode(builder,
		AAUDIO_SHARING_MODE_EXCLUSIVE);
	AAudioStreamBuilder_setSampleRate(builder, st->src_prm.srate);
	AAudioStreamBuilder_setChannelCount(builder, 1);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setSessionId(builder, AAUDIO_SESSION_ID_ALLOCATE);
	AAudioStreamBuilder_setUsage(builder,
	       AAUDIO_USAGE_VOICE_COMMUNICATION),
	AAudioStreamBuilder_setDataCallback(builder, &dataCallback, st);

	result = AAudioStreamBuilder_openStream(builder, &recorderStream);
	if (result != AAUDIO_OK) {
		warning("aaudio: failed to open recorder stream: error %s\n",
			AAudio_convertResultToText(result));
		return result;
	}

	info("aaudio: opened recorder stream with direction %d, "
	     "sharing mode %d, sample rate %d, format %d, sessionId %d, "
	     "usage %d\n",
	     AAudioStream_getDirection(recorderStream),
	     AAudioStream_getSharingMode(recorderStream),
	     AAudioStream_getSampleRate(recorderStream),
	     AAudioStream_getFormat(recorderStream),
	     AAudioStream_getSessionId(recorderStream),
	     AAudioStream_getUsage(recorderStream));

	AAudioStreamBuilder_delete(builder);

	AAudioStream_setBufferSizeInFrames(recorderStream,
		AAudioStream_getFramesPerBurst(recorderStream) * 2);

	return AAUDIO_OK;
}


int aaudio_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			  struct ausrc_prm *prm, const char *dev,
			  ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	aaudio_result_t result;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	info ("aaudio: opening recorder(%u Hz, %d channels,"
	      "device '%s')\n", prm->srate, prm->ch, dev);

	if (prm->fmt != AUFMT_S16LE) {
		warning("aaudio: recorder: unsupported sample format (%s)\n",
			aufmt_name((enum aufmt)prm->fmt));
		return ENOTSUP;
	}

	if (prm->ch != 1) {
		warning("aaudio: recorder: unsupported channel count (%u)\n",
			prm->ch);
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->src_prm.srate = prm->srate;
	st->src_prm.ch    = prm->ch;
	st->src_prm.ptime = prm->ptime;
	st->src_prm.fmt   = prm->fmt;

	st->sampsz = aufmt_sample_size(prm->fmt);
	st->sampc  = prm->ptime * prm->ch * prm->srate / 1000;
	st->samps  = 0;
	st->sampv  = mem_zalloc(st->sampsz * st->sampc, NULL);
	if (!st->sampv) {
		result = ENOMEM;
		goto out;
	}


	st->rh  = rh;
	st->errh = errh;
	st->arg = arg;

	result = open_recorder_stream(st);
	if (result != AAUDIO_OK)
		goto out;

	result = AAudioStream_requestStart(recorderStream);
	if (result != AAUDIO_OK) {
		warning("aaudio: recorder: failed to start stream\n");
		goto out;
	}

	module_event("aaudio", "recorder sessionid", NULL, NULL, "%d",
		     AAudioStream_getSessionId(recorderStream));

	info ("aaudio: recorder: stream started\n");

  out:
	if (result != AAUDIO_OK) {
		if (playerStream)
			AAudioStream_close(playerStream);
		if (recorderStream)
			AAudioStream_close(recorderStream);
		mem_deref(st);
	}
	else
		*stp = st;

	return result;
}
