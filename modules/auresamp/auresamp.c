/**
 * @file auresamp.c  A filter module that inserts a resampler into the audio
 *                   pipeline if needed
 *
 * Copyright (C) 2022 Commend.com - c.spielberger@commend.com
 */

 #include <re.h>
 #include <rem.h>
 #include <stdlib.h>
 #include <baresip.h>
 #include <time.h>
 #include <sys/time.h>


/**
 *  The auresamp module is one of the audio filters. The order of the filters
 *  is specified by the order in the config file.
 *
 *  .    .--------.   .-------.   .----------.   .--------.
 *  |    |        |   |       |   | filters  |   |        |
 *  |O-->| ausrc  |-->| aubuf |-->|   e.g.   |-->| encode |--> RTP
 *  |    |        |   |       |   | auresamp |   |        |
 *  '    '---- ---'   '-------'   '----------'   '--------'
 *
 *       .--------.   .-------.   .----------.   .--------.
 * |\    |        |   |       |   | filters  |   |        |
 * | |<--| auplay |<--| aubuf |<--|   e.g.   |<--| decode |<-- RTP
 * |/    |        |   |       |   | auresamp |   |        |
 *       '--------'   '-------'   '----------'   '--------'
 */

struct auresamp_st {
	union {
		struct aufilt_enc_st eaf;
		struct aufilt_dec_st daf;
	} u;                     /* inheritance                              */

	int16_t *sampv;          /* s16le audio data buffer                  */
	int16_t *rsampv;         /* resampled data                           */
	size_t rsampsz;          /* size of rsampv buffer                    */
	struct auresamp resamp;  /* resampler                                */
	struct aufilt_prm oprm;  /* filter output parameters                 */
	const char *dbg;         /* debugging "encoder"/"decoder"            */
};


static void common_destructor(void *arg)
{
	struct auresamp_st *st = arg;

	mem_deref(st->rsampv);
	mem_deref(st->sampv);
}


static void enc_destructor(void *arg)
{
	struct auresamp_st *st = arg;

	list_unlink(&st->u.eaf.le);
	common_destructor(st);
}


static void dec_destructor(void *arg)
{
	struct auresamp_st *st = arg;

	list_unlink(&st->u.daf.le);
	common_destructor(st);
}


static int sampv_alloc(struct auresamp_st *st, struct auframe *af)
{
	size_t psize_out;
	size_t psize;

	/* s16le used internally */
	psize = af->sampc * af->ch * 2;

	/* output format == input format */
	psize_out = aufmt_sample_size(af->fmt) * af->sampc *
			st->oprm.srate * st->oprm.ch / (af->srate * af->ch);

	st->sampv = mem_zalloc(max(psize, psize_out), NULL);

	if (!st->sampv)
		return ENOMEM;

	return 0;
}


static int rsampv_check_size(struct auresamp_st *st, struct auframe *af)
{
	size_t ptime;
	size_t psize;

	ptime = af->sampc * 1000 / af->srate;
	psize = ptime * st->oprm.srate * st->oprm.ch *
		aufmt_sample_size(af->fmt) / 1000;

	/* auresamp minimum output size is the input size */
	psize = max(psize, auframe_size(af));
	if (st->rsampsz < psize) {
		st->rsampsz = 0;
		st->rsampv = mem_deref(st->rsampv);
		st->rsampv = mem_zalloc(psize, NULL);
	}

	if (!st->rsampv)
		return ENOMEM;

	st->rsampsz = psize;
	return 0;
}


static int resamp_setup(struct auresamp_st *st, struct auframe *af)
{
	int err = 0;

	err = auresamp_setup(&st->resamp, af->srate, af->ch,
			     st->oprm.srate, st->oprm.ch);
	if (err) {
		warning("resample: auresamp_setup error (%m)\n", err);
		return err;
	}

	return rsampv_check_size(st, af);
}


static int common_update(struct auresamp_st **stp, struct aufilt_prm *oprm,
			 mem_destroy_h *dh)
{
	struct auresamp_st *st;
	if (!stp || !oprm)
		return EINVAL;

	if (!oprm->ch || !oprm->srate)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), dh);
	if (!st)
		return ENOMEM;

	st->oprm = *oprm;
	auresamp_init(&st->resamp);

	*stp = st;
	return 0;
}

/**
 * @brief Generates a formatted timestamp string with milliseconds.
 *
 * This function retrieves the current system time with microsecond precision,
 * formats it as "DD-MM-YYYY HH:MM:SS.mmm", and stores the result
 * in the provided buffer.
 *
 * @param char_buffer Pointer to a character buffer where the formatted
 * timestamp will be stored. The buffer must be at least 30 bytes in size.
 *
 * @note The function modifies the contents of `char_buffer` in place.
 * Ensure that the buffer is properly allocated before calling this function.
 */
void calculate_timestamp(char *char_buffer) {
	struct timeval tv;
	struct tm *tm_info;
	/* Get the current time with microseconds */
	gettimeofday(&tv, NULL);

	/* Convert to utc time (seconds) */
	tm_info = gmtime(&tv.tv_sec);

	/* Format date and time without milliseconds */
	char temp_buffer[20];
	size_t written_chars = strftime(temp_buffer,
		20, "%d-%m-%Y %H:%M:%S", tm_info);

	snprintf(char_buffer,
		written_chars+5, "%s.%03ld", temp_buffer, tv.tv_usec / 1000);
}

/**
 * Event handler that triggers a Baresip event when a click is detected.
 *
 * @param char_buffer Pointer to a character buffer where the formatted
 * timestamp will be stored. The buffer must be at least 30 bytes in size.
 *
 * @param index index where the Click was detected inside the audioframe
 */
void baresip_click_event_handler(
	const char *char_buffer,
	const int index,
	const enum ua_event event) {
	/* Notify Baresip */
	bevent_app_emit(
		event,
		NULL,
		"Click detected at %s, at frame index %d\n",
		char_buffer,
		index
	);
}

/**
* Detects a click in the audio by identifying a sudden amplitude change.
*
* @param audio_data   	Array of audio samples.
* @param num_samples  	Total number of samples in the audio.
* @param event_handler	Callback function that emits an baresipp event
*
* @return Index i of the click event if found, -1 otherwise.
*/
int detect_click(
	int16_t *audio_data,
	const int num_samples,
	char *char_buffer,
	ClickEventHandler event_handler,
	enum ua_event event
)
{
	double abs_amplitude_diff = .0;
	for (int i = 1; i < num_samples; i++) {
		/* Check if there is a sudden amplitude jump */
		abs_amplitude_diff = abs(audio_data[i] - audio_data[i - 1]);
		if (abs_amplitude_diff > CLICK_THRESHOLD_MIN) {
		 	calculate_timestamp(char_buffer);
			/* audio_data[i] = 30000 */;

			/* Call event handler (if provided) */
			if (event_handler) {
				event_handler(char_buffer, i, event);
			}
		 	return i;
		}
	}
	return -1;
}

/**
 * Resamples an audio frame to match the desired sample rate and
 * channel count.
 *
 * This function checks if resampling is necessary and, if so:
 * - Converts input audio to S16LE format if required.
 * - Configures or updates the resampler.
 * - Performs resampling and updates the audio frame.
 * - Converts the output back to the desired format if needed.
 *
 * @param st  Pointer to the resampler state.
 * @param af  Pointer to the audio frame to be resampled.
 *
 * @return 0 on success, or an error code on failure.
 */
static int common_resample(
	struct auresamp_st *st,
	struct auframe *af,
	enum ua_event ev)
{
	size_t rsampc;
	int16_t *sampv;
	int err = 0;

	char buffer[30];
	int click_index = detect_click(
		af->sampv,
		af->sampc,
		buffer,
		&baresip_click_event_handler,
		ev);

	/* click_index = detect_click(af->sampv, af->sampc, buffer); */
	if (st->dbg) {
		debug("auresamp: resample %s %u/%u --> %u/%u\n", st->dbg,
		      af->srate, af->ch, st->oprm.srate, st->oprm.ch);
		st->dbg = NULL;
	}

	if (!af->ch || !af->srate)
		return EINVAL;

	if (st->oprm.srate == af->srate && st->oprm.ch == af->ch) {
		st->rsampsz = 0;
		st->rsampv = mem_deref(st->rsampv);
		st->sampv  = mem_deref(st->sampv);

		/* nseddiki: if you want to get a free segmentation fault.
		There are two ways to achieve this feel free to

		1. invoke detect_click(st->rsampv, af->sampc, buffer)
		here after the memory was cleared!

		mem_deref() does the following:
		- Dereference a reference-counted memory object.
		- When the reference count is zero, the destroy
		handler will be called (if present) and the memory
		will be freed

		2. use 'st->rsampv' instead of 'af->sampv'

		100% guarantee

		click_index = detect_click(
			af->sampv,
			af->sampc,
			buffer,
			&baresip_click_event_handler,
			ev);

			if (click_index > -1) {
				af->sampv[click_index] = 30000
			}

			return 0;
		*/
	}

	sampv  = af->sampv;
	if (af->fmt != AUFMT_S16LE) {
		if (!st->sampv)
			err = sampv_alloc(st, af);

		if (err)
			return err;

		auconv_to_s16(st->sampv, af->fmt, af->sampv, af->sampc);
		sampv = st->sampv;
	}

	if (st->resamp.irate != af->srate || st->resamp.ich != af->ch)
		err = resamp_setup(st, af);
	else
		err = rsampv_check_size(st, af);

	if (err)
		return err;

	rsampc = st->rsampsz / 2;
	err = auresamp(&st->resamp, st->rsampv, &rsampc, sampv, af->sampc);
	if (err) {
		warning("resample: auresamp error (%m)\n", err);
		return err;
	}

	af->sampc = rsampc;
	af->fmt   = st->oprm.fmt;
	af->srate = st->oprm.srate;
	af->ch    = st->oprm.ch;
	if (st->oprm.fmt != AUFMT_S16LE) {
		auconv_from_s16(st->oprm.fmt, st->sampv, st->rsampv, rsampc);
		af->sampv = st->sampv;
	}
	else {
		af->sampv = st->rsampv;
	}

	/* click_index = detect_click(
	 	af->sampv,
	 	af->sampc,
	 	buffer,
	 	&baresip_click_event_handler,
	 	ev);
	*/

	return err;
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *oprm,
			 const struct audio *au)
{
	struct auresamp_st **cstp = (struct auresamp_st **) stp;
	int err;
	(void)af;
	(void)ctx;
	(void)au;

	err = common_update(cstp, oprm, enc_destructor);
	if (err)
		return err;

	(*cstp)->dbg = "encoder";
	return 0;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *oprm,
			 const struct audio *au)
{
	struct auresamp_st **cstp = (struct auresamp_st **) stp;
	int err;
	(void)af;
	(void)ctx;
	(void)au;

	err = common_update(cstp, oprm, dec_destructor);
	if (err)
		return err;

	(*cstp)->dbg = "decoder";
	return 0;
}


static int encode(struct aufilt_enc_st *aufilt_enc_st, struct auframe *af)
{
	struct auresamp_st *st = (struct auresamp_st *) aufilt_enc_st;

	if (!st || !af)
		return EINVAL;

	return common_resample(st, af, UA_EVENT_AUDIO_LATENCY_OUTGOING);
}


static int decode(struct aufilt_dec_st *aufilt_dec_st, struct auframe *af)
{
	struct auresamp_st *st = (struct auresamp_st *) aufilt_dec_st;

	if (!st || !af)
		return EINVAL;

	return common_resample(st, af, UA_EVENT_AUDIO_LATENCY_INCOMING);
}


static struct aufilt resample = {.name	  = "auresamp",
				 .encupdh = encode_update,
				 .ench	  = encode,
				 .decupdh = decode_update,
				 .dech	  = decode};


static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &resample);

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&resample);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(auresamp) = {
	"auresamp",
	"filter",
	module_init,
	module_close
};
