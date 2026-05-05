/**
 * @file sf_aufileloop.c Gapless looping audio source for Sipfront.
 *
 * This module intentionally lives outside the Baresip source tree. It is built
 * through Baresip's APP_MODULES hook so it tracks the exact Baresip/libre ABI in
 * sipfront-agent-base without carrying a source patch against modules/aufile.
 */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <re_atomic.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>


enum {
	initial_sample_capacity = 4096
};


struct ausrc_st {
	struct ausrc_prm prm;
	int16_t *samplev;
	size_t samplec;
	size_t sample_cap;
	size_t sample_pos;
	size_t frame_sampc;
	uint32_t ptime;
	RE_ATOMIC bool run;
	bool started;
	thrd_t thread;
	ausrc_read_h *rh;
	void *arg;
};


struct resample_state {
	struct SwrContext *swr;
	AVChannelLayout in_layout;
	AVChannelLayout out_layout;
	int in_srate;
	bool initialized;
};


static struct ausrc *ausrc;


/**
 * Stop the source thread and release decoded audio storage.
 *
 * @param arg Source state allocated by source_alloc().
 */
static void destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (st->started) {
		re_atomic_rlx_set(&st->run, false);
		thrd_join(st->thread, NULL);
	}

	mem_deref(st->samplev);
}


/**
 * Check whether a file path has an input type supported by this module.
 *
 * @param path Local media file path.
 *
 * @return true for .mp3 and .wav paths, otherwise false.
 */
static bool supported_media_path(const char *path)
{
	const char *ext;

	if (!str_isset(path))
		return false;

	ext = strrchr(path, '.');
	if (!ext)
		return false;

	return 0 == str_casecmp(ext, ".mp3") ||
	       0 == str_casecmp(ext, ".wav");
}


/**
 * Convert an FFmpeg error code into a stable errno-style error code.
 *
 * Baresip logging and source allocation expect positive errno-style values.
 *
 * @param averr Negative FFmpeg AVERROR code.
 *
 * @return Positive errno-style error code.
 */
static int av_error_to_errno(int averr)
{
	if (averr >= 0)
		return 0;

	switch (averr) {
	case AVERROR(ENOMEM):
		return ENOMEM;
	case AVERROR(EINVAL):
		return EINVAL;
	case AVERROR(ENOENT):
		return ENOENT;
	case AVERROR_DECODER_NOT_FOUND:
		return ENOSYS;
	default:
		return EIO;
	}
}


/**
 * Log an FFmpeg error with the human readable error string.
 *
 * @param context Short description of the operation that failed.
 * @param path    Media path related to the operation, or NULL.
 * @param averr   Negative FFmpeg AVERROR code.
 */
static void warning_av_error(const char *context, const char *path, int averr)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = "";

	(void)av_strerror(averr, buf, sizeof(buf));
	if (path)
		warning("sf_aufileloop: %s '%s': %s\n", context, path, buf);
	else
		warning("sf_aufileloop: %s: %s\n", context, buf);
}


/**
 * Ensure that the decoded sample buffer can hold at least `need` samples.
 *
 * The buffer grows geometrically to keep file loading amortized linear while
 * preserving previously decoded samples.
 *
 * @param st   Source state whose buffer should be resized.
 * @param need Required capacity, expressed in interleaved int16 samples.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int reserve_samples(struct ausrc_st *st, size_t need)
{
	size_t cap = st->sample_cap;
	int16_t *samplev;

	if (need <= st->sample_cap)
		return 0;

	if (!cap)
		cap = initial_sample_capacity;

	while (cap < need) {
		if (cap > SIZE_MAX / 2)
			return ENOMEM;
		cap *= 2;
	}

	samplev = mem_reallocarray(st->samplev, cap, sizeof(*st->samplev),
				   NULL);
	if (!samplev)
		return ENOMEM;

	st->samplev = samplev;
	st->sample_cap = cap;

	return 0;
}


/**
 * Release FFmpeg resampler state and channel layout storage.
 *
 * @param rs Resampler state initialized by resampler_init().
 */
static void resampler_close(struct resample_state *rs)
{
	if (!rs)
		return;

	swr_free(&rs->swr);
	if (rs->initialized) {
		av_channel_layout_uninit(&rs->in_layout);
		av_channel_layout_uninit(&rs->out_layout);
	}
	memset(rs, 0, sizeof(*rs));
}


/**
 * Copy the channel layout that FFmpeg decoded for the current frame.
 *
 * @param dst   Receives an owned copy of the input layout.
 * @param frame Decoded audio frame.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int copy_frame_layout(AVChannelLayout *dst, const AVFrame *frame)
{
	int ret;

	if (!dst || !frame || frame->ch_layout.nb_channels <= 0)
		return EINVAL;

	ret = av_channel_layout_copy(dst, &frame->ch_layout);
	return ret < 0 ? av_error_to_errno(ret) : 0;
}


/**
 * Initialize the decode-time resampler for the first decoded frame.
 *
 * The output is always packed signed 16-bit PCM with the sample rate and
 * channel count requested by Baresip's active transmit codec.
 *
 * @param st    Source state containing the target Baresip format.
 * @param rs    Resampler state to initialize.
 * @param frame First decoded frame from the input file.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int resampler_init(struct ausrc_st *st, struct resample_state *rs,
			  const AVFrame *frame)
{
	enum AVSampleFormat in_fmt;
	int ret;
	int err;

	if (!st || !rs || !frame || frame->sample_rate <= 0)
		return EINVAL;

	in_fmt = (enum AVSampleFormat)frame->format;
	if (in_fmt == AV_SAMPLE_FMT_NONE)
		return EINVAL;

	err = copy_frame_layout(&rs->in_layout, frame);
	if (err)
		return err;

	av_channel_layout_default(&rs->out_layout, st->prm.ch);
	if (rs->out_layout.nb_channels != st->prm.ch) {
		err = EINVAL;
		goto out;
	}

	ret = swr_alloc_set_opts2(&rs->swr,
				  &rs->out_layout, AV_SAMPLE_FMT_S16,
				  (int)st->prm.srate,
				  &rs->in_layout, in_fmt,
				  frame->sample_rate,
				  0, NULL);
	if (ret < 0) {
		warning_av_error("failed to configure resampler", NULL, ret);
		err = av_error_to_errno(ret);
		goto out;
	}

	ret = swr_init(rs->swr);
	if (ret < 0) {
		warning_av_error("failed to initialize resampler", NULL, ret);
		err = av_error_to_errno(ret);
		goto out;
	}

	rs->in_srate = frame->sample_rate;
	rs->initialized = true;

	return 0;

out:
	resampler_close(rs);
	return err;
}


/**
 * Calculate how many interleaved output samples may be produced.
 *
 * @param frames   Output frame count, where one frame contains all channels.
 * @param channels Output channel count.
 * @param samplec  Current decoded sample count.
 * @param needp    Receives required total sample capacity.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int output_need(size_t frames, uint8_t channels, size_t samplec,
		       size_t *needp)
{
	size_t samples;

	if (!needp || !channels)
		return EINVAL;

	if (frames > SIZE_MAX / channels)
		return ENOMEM;
	samples = frames * channels;

	if (samples > SIZE_MAX - samplec)
		return ENOMEM;

	*needp = samplec + samples;
	return 0;
}


/**
 * Decode one FFmpeg frame into the target Baresip PCM format.
 *
 * @param st    Source state receiving converted interleaved int16 samples.
 * @param rs    Decode-time resampler state.
 * @param frame Decoded FFmpeg audio frame.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int append_resampled_frame(struct ausrc_st *st,
				  struct resample_state *rs,
				  const AVFrame *frame)
{
	const uint8_t **in_data;
	uint8_t *out_data[1];
	int64_t delay;
	int64_t out_frames64;
	size_t need;
	int converted;
	int err;

	if (!st || !rs || !frame)
		return EINVAL;

	if (!frame->nb_samples)
		return 0;

	if (!rs->initialized) {
		err = resampler_init(st, rs, frame);
		if (err)
			return err;
	}

	delay = swr_get_delay(rs->swr, rs->in_srate);
	out_frames64 = av_rescale_rnd(delay + frame->nb_samples,
				      st->prm.srate, rs->in_srate,
				      AV_ROUND_UP);
	if (out_frames64 <= 0 || out_frames64 > INT_MAX)
		return EINVAL;

	err = output_need((size_t)out_frames64, st->prm.ch, st->samplec, &need);
	if (err)
		return err;

	err = reserve_samples(st, need);
	if (err)
		return err;

	out_data[0] = (uint8_t *)&st->samplev[st->samplec];
	in_data = (const uint8_t **)frame->extended_data;
	converted = swr_convert(rs->swr, out_data, (int)out_frames64,
				in_data, frame->nb_samples);
	if (converted < 0) {
		warning_av_error("failed to convert decoded frame", NULL,
				 converted);
		return av_error_to_errno(converted);
	}

	st->samplec += (size_t)converted * st->prm.ch;
	return 0;
}


/**
 * Flush delayed samples from the resampler into the decoded sample buffer.
 *
 * @param st Source state receiving converted interleaved int16 samples.
 * @param rs Decode-time resampler state.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int flush_resampler(struct ausrc_st *st, struct resample_state *rs)
{
	if (!st || !rs)
		return EINVAL;

	while (rs->initialized) {
		uint8_t *out_data[1];
		int64_t delay;
		int64_t out_frames64;
		size_t need;
		int converted;
		int err;

		delay = swr_get_delay(rs->swr, rs->in_srate);
		if (delay <= 0)
			return 0;

		out_frames64 = av_rescale_rnd(delay, st->prm.srate,
					      rs->in_srate, AV_ROUND_UP);
		if (out_frames64 <= 0 || out_frames64 > INT_MAX)
			return EINVAL;

		err = output_need((size_t)out_frames64, st->prm.ch,
				  st->samplec, &need);
		if (err)
			return err;

		err = reserve_samples(st, need);
		if (err)
			return err;

		out_data[0] = (uint8_t *)&st->samplev[st->samplec];
		converted = swr_convert(rs->swr, out_data, (int)out_frames64,
					NULL, 0);
		if (converted < 0) {
			warning_av_error("failed to flush resampler", NULL,
					 converted);
			return av_error_to_errno(converted);
		}
		if (!converted)
			return 0;

		st->samplec += (size_t)converted * st->prm.ch;
	}

	return 0;
}


/**
 * Receive all currently available decoded frames from the FFmpeg decoder.
 *
 * @param st    Source state receiving converted samples.
 * @param dec   Open FFmpeg decoder context.
 * @param rs    Decode-time resampler state.
 * @param frame Reusable FFmpeg frame buffer.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int receive_frames(struct ausrc_st *st, AVCodecContext *dec,
			  struct resample_state *rs, AVFrame *frame)
{
	for (;;) {
		int ret;
		int err;

		ret = avcodec_receive_frame(dec, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return 0;
		if (ret < 0) {
			warning_av_error("failed to receive decoded frame",
					 NULL, ret);
			return av_error_to_errno(ret);
		}

		err = append_resampled_frame(st, rs, frame);
		av_frame_unref(frame);
		if (err)
			return err;
	}
}


/**
 * Send one packet into the FFmpeg decoder and drain decoded frames.
 *
 * @param st    Source state receiving converted samples.
 * @param dec   Open FFmpeg decoder context.
 * @param rs    Decode-time resampler state.
 * @param frame Reusable FFmpeg frame buffer.
 * @param pkt   Packet to decode, or NULL to flush the decoder.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int send_packet(struct ausrc_st *st, AVCodecContext *dec,
		       struct resample_state *rs, AVFrame *frame,
		       const AVPacket *pkt)
{
	int ret;
	int err;

	ret = avcodec_send_packet(dec, pkt);
	if (ret == AVERROR(EAGAIN)) {
		err = receive_frames(st, dec, rs, frame);
		if (err)
			return err;
		ret = avcodec_send_packet(dec, pkt);
	}
	if (ret < 0) {
		warning_av_error(pkt ? "failed to send packet to decoder"
				     : "failed to flush decoder",
				 NULL, ret);
		return av_error_to_errno(ret);
	}

	return receive_frames(st, dec, rs, frame);
}


/**
 * Open the first audio stream in a media file.
 *
 * @param fmtp          Receives an opened input format context.
 * @param decp          Receives an opened decoder context.
 * @param stream_indexp Receives the selected audio stream index.
 * @param path          Local .mp3 or .wav file path.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int open_audio_stream(AVFormatContext **fmtp, AVCodecContext **decp,
			     int *stream_indexp, const char *path)
{
	AVFormatContext *fmt = NULL;
	AVCodecContext *dec = NULL;
	const AVCodec *codec = NULL;
	int stream_index;
	int ret;
	int err = 0;

	if (!fmtp || !decp || !stream_indexp || !str_isset(path))
		return EINVAL;

	ret = avformat_open_input(&fmt, path, NULL, NULL);
	if (ret < 0) {
		warning_av_error("failed to open media file", path, ret);
		return av_error_to_errno(ret);
	}

	ret = avformat_find_stream_info(fmt, NULL);
	if (ret < 0) {
		warning_av_error("failed to read media stream info", path, ret);
		err = av_error_to_errno(ret);
		goto out;
	}

	stream_index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1,
					   &codec, 0);
	if (stream_index < 0) {
		warning_av_error("failed to find audio stream", path,
				 stream_index);
		err = av_error_to_errno(stream_index);
		goto out;
	}

	dec = avcodec_alloc_context3(codec);
	if (!dec) {
		err = ENOMEM;
		goto out;
	}

	ret = avcodec_parameters_to_context(dec,
			fmt->streams[stream_index]->codecpar);
	if (ret < 0) {
		warning_av_error("failed to copy decoder parameters", path, ret);
		err = av_error_to_errno(ret);
		goto out;
	}

	ret = avcodec_open2(dec, codec, NULL);
	if (ret < 0) {
		warning_av_error("failed to open decoder", path, ret);
		err = av_error_to_errno(ret);
		goto out;
	}

	*fmtp = fmt;
	*decp = dec;
	*stream_indexp = stream_index;
	return 0;

out:
	avcodec_free_context(&dec);
	avformat_close_input(&fmt);
	return err;
}


/**
 * Decode a complete MP3/WAV file into the target Baresip PCM format.
 *
 * All decoding, resampling, and remixing happens once during source allocation.
 * The source thread later loops by copying from this prepared PCM buffer.
 *
 * @param st   Source state receiving prepared PCM samples.
 * @param path Local .mp3 or .wav media file path.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int decode_media_file(struct ausrc_st *st, const char *path)
{
	AVFormatContext *fmt = NULL;
	AVCodecContext *dec = NULL;
	AVPacket *pkt = NULL;
	AVFrame *frame = NULL;
	struct resample_state rs = {0};
	int stream_index = -1;
	int err;

	if (!st || !supported_media_path(path))
		return ENOTSUP;

	err = open_audio_stream(&fmt, &dec, &stream_index, path);
	if (err)
		return err;

	pkt = av_packet_alloc();
	frame = av_frame_alloc();
	if (!pkt || !frame) {
		err = ENOMEM;
		goto out;
	}

	for (;;) {
		int ret = av_read_frame(fmt, pkt);
		if (ret == AVERROR_EOF)
			break;
		if (ret < 0) {
			warning_av_error("failed while reading media file", path,
					 ret);
			err = av_error_to_errno(ret);
			goto out;
		}

		if (pkt->stream_index == stream_index) {
			err = send_packet(st, dec, &rs, frame, pkt);
			if (err)
				goto out;
		}

		av_packet_unref(pkt);
	}

	err = send_packet(st, dec, &rs, frame, NULL);
	if (err)
		goto out;

	err = flush_resampler(st, &rs);
	if (err)
		goto out;

	if (!st->samplec) {
		err = EINVAL;
		goto out;
	}
	if (st->samplec % st->prm.ch) {
		err = EINVAL;
		goto out;
	}

	info("sf_aufileloop: decoded %zu samples from '%s'\n",
	     st->samplec, path);

out:
	av_packet_free(&pkt);
	av_frame_free(&frame);
	resampler_close(&rs);
	avcodec_free_context(&dec);
	avformat_close_input(&fmt);
	return err;
}


/**
 * Fill one outgoing audio frame from the prepared PCM buffer.
 *
 * If the requested frame crosses the end of the decoded file, copying continues
 * from sample zero. That makes the loop gapless and avoids notifying Baresip of
 * EOF.
 *
 * @param st  Source state containing decoded samples and the current cursor.
 * @param dst Destination buffer with room for st->frame_sampc samples.
 */
static void fill_frame(struct ausrc_st *st, int16_t *dst)
{
	size_t written = 0;

	while (written < st->frame_sampc) {
		const size_t left = st->frame_sampc - written;
		const size_t available = st->samplec - st->sample_pos;
		const size_t n = left < available ? left : available;

		memcpy(&dst[written], &st->samplev[st->sample_pos],
		       n * sizeof(*dst));

		written += n;
		st->sample_pos += n;

		if (st->sample_pos >= st->samplec)
			st->sample_pos = 0;
	}
}


/**
 * Source worker thread.
 *
 * The thread wakes frequently, emits frames on the configured packet-time
 * cadence, and calls Baresip's ausrc read handler with an AUFMT_S16LE frame.
 *
 * @param arg Source state allocated by source_alloc().
 *
 * @return 0 on normal termination, otherwise an errno-style error code.
 */
static int src_thread(void *arg)
{
	struct ausrc_st *st = arg;
	uint64_t ts = tmr_jiffies();
	int16_t *framev;
	uint32_t ms = 4;

	framev = mem_alloc(st->frame_sampc * sizeof(*framev), NULL);
	if (!framev)
		return ENOMEM;

	while (re_atomic_rlx(&st->run)) {
		struct auframe af;
		uint64_t now;

		sys_msleep(ms);

		now = tmr_jiffies();
		if (ts > now)
			continue;

		fill_frame(st, framev);

		auframe_init(&af, AUFMT_S16LE, framev, st->frame_sampc,
			     st->prm.srate, st->prm.ch);

		st->rh(&af, st->arg);

		ts += st->ptime;
	}

	mem_deref(framev);

	return 0;
}


/**
 * Build the target source format for this Baresip source instance.
 *
 * When the source is switched by `/ausrc`, Baresip may pass the previous
 * source's parameters. The active transmit codec, when available from the audio
 * callback context, is the authoritative target for source frames.
 *
 * @param target Receives the source format this module should emit.
 * @param prm    Baresip source parameters passed to source_alloc().
 * @param arg    Opaque Baresip callback argument; expected to be struct audio.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int target_prm(struct ausrc_prm *target, const struct ausrc_prm *prm,
		      void *arg)
{
	const struct aucodec *ac = NULL;

	if (!target || !prm)
		return EINVAL;

	*target = *prm;
	if (arg)
		ac = audio_codec((const struct audio *)arg, true);

	if (ac && ac->srate && ac->ch) {
		target->srate = ac->srate;
		target->ch = ac->ch;
	}

	if (!target->srate || !target->ch)
		return EINVAL;

	if (!target->ptime)
		target->ptime = 20;

	if (target->fmt != AUFMT_S16LE) {
		warning("sf_aufileloop: unsupported source sample format %s\n",
			aufmt_name((enum aufmt)target->fmt));
		return ENOTSUP;
	}

	target->duration = 0;
	return 0;
}


/**
 * Allocate a looping audio source instance.
 *
 * Baresip calls this through the ausrc API when `sf_aufileloop` is selected as
 * the active audio source. The function decodes MP3/WAV input once, converts it
 * to the active transmit codec's sample rate and channel count, and starts the
 * source thread if a read handler was supplied.
 *
 * @param stp  Receives the allocated source state on success.
 * @param as   Registered audio source descriptor.
 * @param prm  In/out Baresip audio-source parameters.
 * @param dev  Device string; interpreted as a local MP3/WAV path.
 * @param rh   Baresip read callback receiving produced audio frames.
 * @param errh Baresip error callback; unused because this source does not emit
 *             EOF during normal looping.
 * @param arg  Opaque callback argument passed to `rh`.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int source_alloc(struct ausrc_st **stp, const struct ausrc *as,
			struct ausrc_prm *prm, const char *dev,
			ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct ausrc_prm target;
	uint64_t frame_sampc;
	int err;

	(void)as;
	(void)errh;

	if (!stp || !prm || !str_isset(dev))
		return EINVAL;

	if (!supported_media_path(dev)) {
		warning("sf_aufileloop: unsupported input file '%s'\n", dev);
		return ENOTSUP;
	}

	err = target_prm(&target, prm, arg);
	if (err)
		return err;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->rh = rh;
	st->arg = arg;
	st->ptime = target.ptime;
	st->prm = target;

	frame_sampc = (uint64_t)st->prm.srate * st->prm.ch * st->ptime / 1000;
	if (!frame_sampc || frame_sampc > SIZE_MAX / sizeof(int16_t)) {
		err = EINVAL;
		goto out;
	}
	st->frame_sampc = (size_t)frame_sampc;

	info("sf_aufileloop: %s: target %u Hz, %u channel%s, S16LE, "
	     "ptime=%u ms\n",
	     dev, st->prm.srate, st->prm.ch, st->prm.ch == 1 ? "" : "s",
	     st->ptime);

	err = decode_media_file(st, dev);
	if (err)
		goto out;

	*prm = st->prm;

	if (rh) {
		re_atomic_rlx_set(&st->run, true);
		st->started = true;
		err = thread_create_name(&st->thread, "sf_aufileloop",
					 src_thread, st);
		if (err) {
			st->started = false;
			re_atomic_rlx_set(&st->run, false);
			goto out;
		}
	}

out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


/**
 * Return metadata for a looping audio source.
 *
 * Duration is reported as 0 to match Baresip's convention for an infinite
 * source. Runtime allocation still adapts the output to the active transmit
 * codec, so this metadata is only descriptive.
 *
 * @param as  Registered audio source descriptor.
 * @param prm Receives source sample rate, channel count, output format, and
 *            infinite duration.
 * @param dev Device string; interpreted as a local MP3/WAV path.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int source_info(const struct ausrc *as, struct ausrc_prm *prm,
		       const char *dev)
{
	AVFormatContext *fmt = NULL;
	AVCodecContext *dec = NULL;
	int stream_index = -1;
	int err;

	(void)as;

	if (!prm || !str_isset(dev))
		return EINVAL;

	if (!supported_media_path(dev))
		return ENOTSUP;

	err = open_audio_stream(&fmt, &dec, &stream_index, dev);
	if (err)
		return err;

	prm->srate = dec->sample_rate;
	prm->ch = dec->ch_layout.nb_channels;
	prm->fmt = AUFMT_S16LE;
	prm->duration = 0;

	avcodec_free_context(&dec);
	avformat_close_input(&fmt);

	return prm->srate && prm->ch ? 0 : EINVAL;
}


/**
 * Register the Sipfront looping audio source with Baresip.
 *
 * @return 0 on success, otherwise an errno-style error code.
 */
static int module_init(void)
{
	int err;

	err = ausrc_register(&ausrc, baresip_ausrcl(), "sf_aufileloop",
			     source_alloc);
	if (err)
		return err;

	ausrc->infoh = source_info;

	return 0;
}


/**
 * Unregister the Sipfront looping audio source from Baresip.
 *
 * @return 0 on success.
 */
static int module_close(void)
{
	ausrc = mem_deref(ausrc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(sf_aufileloop) = {
	"sf_aufileloop",
	"ausrc",
	module_init,
	module_close
};
