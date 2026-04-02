/**
 * @file aaudio/recorder.c  AAudio audio driver for Android
 *
 * Copyright (C) 2024 Juha Heinanen
 * Copyright (C) 2024 Sebastian Reimers
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <stdatomic.h>
#include <string.h>

#include "aaudio.h"


enum {
	CONTROL_WAIT_MS    = 2,
	SHUTDOWN_WAIT_MS   = 1000,
	SHUTDOWN_POLL_MS   = 10,
};


struct ausrc_state {
	_Atomic(AAudioStream *) published_stream;
	_Atomic(AAudioStream *) pending_stream;
	_Atomic unsigned epoch;      /* even = running, odd = quiescing */
	_Atomic unsigned cb_active;
	_Atomic bool closing;
	_Atomic bool broken;
	_Atomic bool restart_requested;
	_Atomic bool close_requested;
	_Atomic bool ctl_exited;

	ausrc_read_h *rh;
	void *arg;
	struct ausrc_prm src_prm;
	ausrc_error_h *errh;
	void   *sampv;
	size_t  sampsz;
	size_t  bytes_per_frame;
	size_t  sampc;
	uint64_t samps;

	mtx_t *cmd_lock;
	cnd_t cmd_cnd;
	bool cmd_cnd_ok;
	thrd_t ctl_thr;
	bool ctl_started;
};


struct ausrc_st {
	struct ausrc_state *state;
};


static int open_recorder_stream(struct ausrc_state *state,
		AAudioStream **streamp);
static int prepare_recorder_stream(struct ausrc_state *state,
		AAudioStream *stream);
static int recorder_control_thread(void *arg);


static void ausrc_state_destructor(void *arg)
{
	struct ausrc_state *state = arg;

	if (state->cmd_cnd_ok)
		cnd_destroy(&state->cmd_cnd);
	mem_deref(state->sampv);
	mem_deref(state->cmd_lock);
}


static void signal_control_thread(struct ausrc_state *state)
{
	if (!state || !state->cmd_lock || !state->cmd_cnd_ok)
		return;

	mtx_lock(state->cmd_lock);
	cnd_signal(&state->cmd_cnd);
	mtx_unlock(state->cmd_lock);
}


static void request_stop_if_needed(AAudioStream *stream)
{
	if (stream)
		(void)AAudioStream_requestStop(stream);
}


static void wait_callbacks_drain(struct ausrc_state *state)
{
	while (atomic_load(&state->cb_active) != 0)
		sys_msleep(CONTROL_WAIT_MS);
}


static void begin_quiesce_running(struct ausrc_state *state,
		AAudioStream **publishedp,
		AAudioStream **pendingp)
{
	atomic_fetch_add(&state->epoch, 1u); /* even -> odd */

	*publishedp = atomic_exchange(&state->published_stream, NULL);
	*pendingp   = atomic_exchange(&state->pending_stream, NULL);

	if (*pendingp && *pendingp != *publishedp)
		request_stop_if_needed(*pendingp);
	if (*publishedp)
		request_stop_if_needed(*publishedp);

	wait_callbacks_drain(state);
}


static void begin_quiesce_starting(struct ausrc_state *state,
		AAudioStream *stream)
{
	AAudioStream *expected = stream;

	atomic_fetch_add(&state->epoch, 1u); /* even -> odd */
	(void)atomic_compare_exchange_strong(&state->pending_stream,
			&expected, NULL);

	request_stop_if_needed(stream);
	wait_callbacks_drain(state);
}


static void end_quiesce(struct ausrc_state *state)
{
	atomic_fetch_add(&state->epoch, 1u); /* odd -> even */
}


static int ensure_recorder_buffer(struct ausrc_state *state, size_t frames)
{
	void *buf;

	if (!state)
		return EINVAL;

	if (frames <= state->sampc)
		return 0;

	buf = mem_realloc(state->sampv, state->bytes_per_frame * frames);
	if (!buf)
		return ENOMEM;

	state->sampv = buf;
	state->sampc = frames;
	return 0;
}


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;
	struct ausrc_state *state = st->state;
	uint32_t waited = 0;

	info("aaudio: recorder: closing stream\n");

	if (!state)
		return;

	st->state = NULL;

	atomic_store(&state->closing, true);
	atomic_store(&state->broken, true);
	atomic_store(&state->close_requested, true);
	signal_control_thread(state);

	if (state->ctl_started) {
		while (!atomic_load(&state->ctl_exited) &&
				waited < SHUTDOWN_WAIT_MS) {
			sys_msleep(SHUTDOWN_POLL_MS);
			waited += SHUTDOWN_POLL_MS;
		}

		if (!atomic_load(&state->ctl_exited)) {
			warning("aaudio: recorder: control thread did not exit "
					"within %u ms, leaking state to avoid race\n",
					SHUTDOWN_WAIT_MS);
			return;
		}
	}

	mem_deref(state);
}


/**
 * For an input stream, this function should read and process numFrames of
 * data from the audioData buffer. The data in the audioData buffer must not
 * be modified directly. Instead, it should be copied to another buffer
 * before doing any modification.
 */
static aaudio_data_callback_result_t dataCallback(AAudioStream *stream,
		void *userData,
		void *audioData,
		int32_t numFrames)
{
	struct ausrc_state *state = userData;
	struct auframe af;
	ausrc_read_h *rh;
	void *arg;
	struct ausrc_prm src_prm;
	AAudioStream *published;
	unsigned epoch;
	size_t sampc;

	if (!state)
		return AAUDIO_CALLBACK_RESULT_CONTINUE;

	epoch = atomic_load(&state->epoch);
	published = atomic_load(&state->published_stream);

	if ((epoch & 1u) || atomic_load(&state->closing) ||
			atomic_load(&state->broken) || stream != published || !state->rh)
		return AAUDIO_CALLBACK_RESULT_CONTINUE;

	atomic_fetch_add(&state->cb_active, 1u);

	published = atomic_load(&state->published_stream);
	if (epoch != atomic_load(&state->epoch) ||
			atomic_load(&state->closing) ||
			atomic_load(&state->broken) ||
			stream != published || !state->rh) {
		atomic_fetch_sub(&state->cb_active, 1u);
		return AAUDIO_CALLBACK_RESULT_CONTINUE;
	}

	rh = state->rh;
	arg = state->arg;
	src_prm = state->src_prm;
	sampc = (size_t)numFrames;

	if (sampc > state->sampc) {
		atomic_fetch_sub(&state->cb_active, 1u);
		return AAUDIO_CALLBACK_RESULT_CONTINUE;
	}

	auframe_init(&af, src_prm.fmt, state->sampv, numFrames,
			src_prm.srate, src_prm.ch);

	memcpy(state->sampv, audioData, state->bytes_per_frame * sampc);

	af.timestamp = state->samps * AUDIO_TIMEBASE /
			(src_prm.srate * src_prm.ch);
	state->samps += sampc;
	rh(&af, arg);

	atomic_fetch_sub(&state->cb_active, 1u);
	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}


static void errorCallback(AAudioStream *stream, void *userData,
		aaudio_result_t error)
{
	struct ausrc_state *state = userData;
	AAudioStream *published;
	AAudioStream *pending;
	AAudioStream *expected;
	unsigned epoch;
	(void)error;

	if (!state)
		return;

	epoch = atomic_load(&state->epoch);
	published = atomic_load(&state->published_stream);
	pending   = atomic_load(&state->pending_stream);

	if ((epoch & 1u) || atomic_load(&state->closing) ||
			(stream != published && stream != pending))
		return;

	atomic_fetch_add(&state->cb_active, 1u);

	published = atomic_load(&state->published_stream);
	pending   = atomic_load(&state->pending_stream);
	if (epoch != atomic_load(&state->epoch) ||
			atomic_load(&state->closing) ||
			(stream != published && stream != pending)) {
		atomic_fetch_sub(&state->cb_active, 1u);
		return;
	}

	atomic_store(&state->broken, true);

	if (stream == pending) {
		expected = stream;
		(void)atomic_compare_exchange_strong(&state->pending_stream,
				&expected, NULL);
	}

	if (!atomic_exchange(&state->restart_requested, true))
		signal_control_thread(state);

	atomic_fetch_sub(&state->cb_active, 1u);
}


static int open_recorder_stream(struct ausrc_state *state,
		AAudioStream **streamp)
{
	AAudioStreamBuilder *builder = NULL;
	AAudioStream *stream = NULL;
	aaudio_result_t result;

	if (!state || !streamp)
		return EINVAL;

	result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK) {
		warning("aaudio: recorder: failed to create stream builder: "
				"error %s\n", AAudio_convertResultToText(result));
		return result;
	}

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
	AAudioStreamBuilder_setSampleRate(builder, state->src_prm.srate);
	AAudioStreamBuilder_setChannelCount(builder, state->src_prm.ch);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setSessionId(builder, AAUDIO_SESSION_ID_ALLOCATE);
	AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_VOICE_COMMUNICATION);
	AAudioStreamBuilder_setPerformanceMode(
			builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setInputPreset(
			builder, AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);
	AAudioStreamBuilder_setDataCallback(builder, &dataCallback, state);
	AAudioStreamBuilder_setErrorCallback(builder, &errorCallback, state);

	result = AAudioStreamBuilder_openStream(builder, &stream);
	AAudioStreamBuilder_delete(builder);
	if (result != AAUDIO_OK) {
		warning("aaudio: recorder: failed to open stream: error %s\n",
				AAudio_convertResultToText(result));
		return result;
	}

	info("aaudio: recorder: opened stream with direction %d, "
		 "sharing mode %d, sample rate %d, format %d, sessionId %d, "
		 "input preset %d, usage %d, performance mode %d\n",
			AAudioStream_getDirection(stream),
			AAudioStream_getSharingMode(stream),
			AAudioStream_getSampleRate(stream),
			AAudioStream_getFormat(stream),
			AAudioStream_getSessionId(stream),
			AAudioStream_getInputPreset(stream),
			AAudioStream_getUsage(stream),
			AAudioStream_getPerformanceMode(stream));

	*streamp = stream;
	return AAUDIO_OK;
}


static int prepare_recorder_stream(struct ausrc_state *state,
		AAudioStream *stream)
{
	int32_t frames_per_burst;
	int32_t buffer_capacity;
	int32_t buffer_size;
	size_t want_frames;
	int err;

	if (!state || !stream)
		return EINVAL;

	frames_per_burst = AAudioStream_getFramesPerBurst(stream);
	(void)AAudioStream_setBufferSizeInFrames(stream, frames_per_burst * 2);

	buffer_capacity = AAudioStream_getBufferCapacityInFrames(stream);
	buffer_size = AAudioStream_getBufferSizeInFrames(stream);

	want_frames = state->src_prm.ptime * state->src_prm.ch *
			state->src_prm.srate / 1000;
	if (buffer_capacity > 0 && (size_t)buffer_capacity > want_frames)
		want_frames = (size_t)buffer_capacity;
	if (frames_per_burst > 0 && (size_t)frames_per_burst > want_frames)
		want_frames = (size_t)frames_per_burst;

	err = ensure_recorder_buffer(state, want_frames);
	if (err)
		return err;

	info("aaudio: recorder: buffer capacity: %d, buffer size: %d\n",
			buffer_capacity, buffer_size);
	return 0;
}


static int recorder_control_thread(void *arg)
{
	struct ausrc_state *state = arg;
	AAudioStream *published;
	AAudioStream *pending;
	AAudioStream *stream = NULL;
	AAudioStream *expected;
	aaudio_result_t result;
	int err;

	(void)thrd_detach(thrd_current());

	for (;;) {
		mtx_lock(state->cmd_lock);
		while (!atomic_load(&state->close_requested) &&
				!atomic_load(&state->restart_requested)) {
			cnd_wait(&state->cmd_cnd, state->cmd_lock);
		}
		if (atomic_load(&state->close_requested)) {
			mtx_unlock(state->cmd_lock);
			break;
		}
		(void)atomic_exchange(&state->restart_requested, false);
		mtx_unlock(state->cmd_lock);

		begin_quiesce_running(state, &published, &pending);

		if (pending && pending != published)
			aaudio_close_stream(pending);
		if (published)
			aaudio_close_stream(published);

		if (atomic_load(&state->closing))
			continue;

		end_quiesce(state);

		result = open_recorder_stream(state, &stream);
		if (result != AAUDIO_OK) {
			atomic_store(&state->broken, true);
			continue;
		}

		err = prepare_recorder_stream(state, stream);
		if (err) {
			aaudio_close_stream(stream);
			atomic_store(&state->broken, true);
			continue;
		}

		if (atomic_load(&state->closing)) {
			begin_quiesce_starting(state, stream);
			aaudio_close_stream(stream);
			continue;
		}

		atomic_store(&state->pending_stream, stream);

		result = AAudioStream_requestStart(stream);
		if (result != AAUDIO_OK) {
			warning("aaudio: recorder: restart failed to start "
					"stream: %s\n",
					AAudio_convertResultToText(result));
			begin_quiesce_starting(state, stream);
			aaudio_close_stream(stream);
			end_quiesce(state);
			atomic_store(&state->broken, true);
			continue;
		}

		if (atomic_load(&state->closing) ||
				atomic_load(&state->pending_stream) != stream) {
			begin_quiesce_starting(state, stream);
			aaudio_close_stream(stream);
			if (!atomic_load(&state->closing))
				end_quiesce(state);
			atomic_store(&state->broken, true);
			continue;
		}

		atomic_store(&state->published_stream, stream);
		expected = stream;
		(void)atomic_compare_exchange_strong(&state->pending_stream,
				&expected, NULL);
		atomic_store(&state->broken, false);
		info("aaudio: recorder: stream restarted\n");
	}

	begin_quiesce_running(state, &published, &pending);

	if (pending && pending != published)
		aaudio_close_stream(pending);
	if (published)
		aaudio_close_stream(published);

	atomic_store(&state->ctl_exited, true);
	mem_deref(state);
	return 0;
}


int aaudio_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
		struct ausrc_prm *prm, const char *dev,
		ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct ausrc_state *state;
	AAudioStream *stream = NULL;
	AAudioStream *expected;
	aaudio_result_t result;
	int err;
	struct ausrc_state *ref;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	info("aaudio: recorder: opening recorder(%u Hz, %d channels,"
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

	state = mem_zalloc(sizeof(*state), ausrc_state_destructor);
	if (!state) {
		mem_deref(st);
		return ENOMEM;
	}

	st->state = state;

	atomic_init(&state->published_stream, NULL);
	atomic_init(&state->pending_stream, NULL);
	atomic_init(&state->epoch, 0u);
	atomic_init(&state->cb_active, 0u);
	atomic_init(&state->closing, false);
	atomic_init(&state->broken, false);
	atomic_init(&state->restart_requested, false);
	atomic_init(&state->close_requested, false);
	atomic_init(&state->ctl_exited, false);

	state->src_prm = *prm;
	state->sampsz = aufmt_sample_size(prm->fmt);
	state->bytes_per_frame = state->sampsz * prm->ch;
	state->rh   = rh;
	state->errh = errh;
	state->arg  = arg;

	err = mutex_alloc(&state->cmd_lock);
	if (err) {
		mem_deref(st);
		return err;
	}

	err = cnd_init(&state->cmd_cnd);
	if (err != thrd_success) {
		mem_deref(st);
		return ENOMEM;
	}
	state->cmd_cnd_ok = true;

	result = open_recorder_stream(state, &stream);
	if (result != AAUDIO_OK)
		goto out;

	err = prepare_recorder_stream(state, stream);
	if (err) {
		result = err;
		aaudio_close_stream(stream);
		goto out;
	}

	atomic_store(&state->pending_stream, stream);

	result = AAudioStream_requestStart(stream);
	if (result != AAUDIO_OK) {
		warning("aaudio: recorder: failed to start stream\n");
		begin_quiesce_starting(state, stream);
		aaudio_close_stream(stream);
		goto out;
	}

	if (atomic_load(&state->closing) ||
			atomic_load(&state->pending_stream) != stream) {
		begin_quiesce_starting(state, stream);
		aaudio_close_stream(stream);
		result = AAUDIO_ERROR_DISCONNECTED;
		goto out;
	}

	atomic_store(&state->published_stream, stream);
	expected = stream;
	(void)atomic_compare_exchange_strong(&state->pending_stream,
			&expected, NULL);
	atomic_store(&state->broken, false);

	ref = mem_ref(state);
	err = thread_create_name(&state->ctl_thr, "AAudio Recorder Control",
			recorder_control_thread, ref);
	if (err) {
		mem_deref(ref);
		begin_quiesce_running(state, &stream, &expected);
		if (expected && expected != stream)
			aaudio_close_stream(expected);
		if (stream)
			aaudio_close_stream(stream);
		result = err;
		goto out;
	}
	state->ctl_started = true;

	module_event("aaudio", "recorder sessionid", NULL, NULL, "%d",
			AAudioStream_getSessionId(stream));
	info("aaudio: recorder: stream started\n");

	out:
	if (result != AAUDIO_OK)
		mem_deref(st);
	else
		*stp = st;

	return result;
}