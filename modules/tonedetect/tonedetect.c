/**
 * @file tonedetect.c  Audio filter module for tone generation and detection
 *
 * Copyright (C) 2025
 */

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#define PI 3.14159265358979323846
#define TONE_AMPLITUDE 0.3f
#define DETECTION_WINDOW_MS 20   /* shorter window for lower timestamp quantization */
#define DETECTION_HOP_MS    2    /* evaluate every 2ms for finer detection timing */

/* Detection tuning (receiver) - balanced for reliable detection */
#define DETECT_RATIO_THRESHOLD       0.17  /* stricter for real-audio environments */
#define DETECT_PEAK_SEPARATION       1.35  /* stronger separation from other peaks */
#define DETECT_MIN_BLOCK_ENERGY      5.0e8 /* raise energy floor to reject normal program audio */
#define DETECT_CONSECUTIVE_BLOCKS    3     /* add temporal stability against speech/music transients */
#define DETECT_SUPPRESS_MS           3000  /* suppress repeat events */
#define DETECT_MIN_MAGNITUDE         120.0  /* reduce weak false positives */
#define DETECT_DUAL_BALANCE_MIN      0.65  /* second peak must be close enough to first */
#define DETECT_TOP2_SHARE_MIN        0.85  /* top 2 peaks must dominate tracked target energy */
#define STABILITY_RTT_DEVIATION_MAX   0.20  /* max 20% deviation between consecutive RTTs for stability */

/* Sender tone shaping to reduce spectral leakage */
#define TONE_RAMP_MS                 2     /* fade-in/out (2ms) to reduce spectral leakage */

/**
 * @defgroup tonedetect tonedetect
 *
 * Audio filter module that can:
 * - Generate and inject tones of specified frequencies into the encoder path
 * - Detect tones of specified frequencies in the decoder path
 * - Emit events when tones are sent or received
 *
 * Configuration:
 * \verbatim
 *  audio_filter        tonedetect
 * \endverbatim
 */

struct tonedetect_st {
	union {
		struct aufilt_enc_st eaf;
		struct aufilt_dec_st daf;
	} u;

	/* Tone generation (encoder) */
	struct {
		bool active;
		uint32_t frequency;
		uint32_t frequency2;
		uint32_t duration_ms;
		uint64_t start_time;
		uint64_t last_tone_end_time;  /* Time when last tone ended */
		double phase;
		double phase2;
		uint32_t srate;
		size_t sample_index;
		size_t total_samples;
		size_t ramp_samples;
		size_t current_tone_index; /* index in pair-list */
		size_t tone_id;  /* ID of currently active tone */
		double first_packet_timestamp;  /* Host timestamp when tone generation starts */
		bool full_amplitude_reached;  /* Flag to track if full amplitude timestamp was captured */
		bool is_ping_mode;  /* true = ping/pong phase (until stability), false = regular tones */
		size_t regular_tone_start_index;  /* Per-instance starting offset for sequential regular tones */
	} gen;

	/* Tone detection (decoder) */
	struct {
		uint32_t *frequencies;      /* Array of frequencies to detect */
		size_t num_frequencies;
		double *goertzel_coeffs;    /* Goertzel coefficients */
		double *goertzel_q1;        /* Goertzel state Q1 */
		double *goertzel_q2;        /* Goertzel state Q2 */
		size_t detection_window_samples;  /* Window size in samples */
		size_t hop_samples;               /* Hop size in samples */
		int16_t *ring;                    /* Ring buffer for windowed evaluation */
		size_t ring_pos;                  /* Next write position (points to oldest sample) */
		size_t ring_count;                /* Number of valid samples in ring */
		size_t hop_count;                 /* Samples since last evaluation */
		double *window;            /* Window coefficients (Hamming) */
		size_t candidate_pair_index;
		uint8_t candidate_count;
		uint8_t failure_count;  /* Count consecutive failures to avoid resetting timestamp on brief interruptions */
		double first_packet_timestamp;  /* Unix timestamp when first packet with tone is decoded */
		uint64_t last_emit_time;
		size_t last_emit_index;    /* 0-based index */
		bool last_emit_valid;
		uint32_t srate;
		bool full_amplitude_detected;  /* Flag to track if full amplitude timestamp was captured */
	} det;
};

/* Global configuration */
static struct {
	uint32_t *send_frequencies;     /* Frequencies to send (low + high sets) */
	size_t num_send_frequencies;
	size_t num_low_frequencies;     /* Number of low frequencies (first N) */
	size_t num_high_frequencies;    /* Number of high frequencies (remaining) */
	uint8_t *send_pair_a;           /* Pair-list: index into send_frequencies (low) */
	uint8_t *send_pair_b;           /* Pair-list: index into send_frequencies (high) */
	size_t num_send_pairs;
	uint32_t *detect_frequencies;   /* Frequencies to detect */
	size_t num_detect_frequencies;
	size_t num_detect_low;          /* Number of low frequencies in detect set */
	size_t num_detect_high;         /* Number of high frequencies in detect set */
	uint32_t tone_duration_ms;      /* Duration of generated tones */
	bool enable_tone_generation;    /* Enable/disable tone generation */
} config = {
	.send_frequencies = NULL,
	.num_send_frequencies = 0,
	.send_pair_a = NULL,
	.send_pair_b = NULL,
	.num_send_pairs = 0,
	.num_low_frequencies = 0,
	.num_high_frequencies = 0,
	.detect_frequencies = NULL,
	.num_detect_frequencies = 0,
	.num_detect_low = 0,
	.num_detect_high = 0,
	.tone_duration_ms = 80,   /* longer tone improves robust lock with short windows */
	.enable_tone_generation = false,  /* Default: disabled */
};

/* Global state to track if call is ready for tone generation/detection */
static struct {
	bool call_established;  /* CALL_ESTABLISHED event received */
	bool rtp_established;   /* CALL_RTPESTAB event received (for audio) */
	uint64_t rtp_established_time; /* jiffies when audio RTP became established */
	struct call *current_call;  /* Current call object */
} tonedetect_call_state = {
	.call_established = false,
	.rtp_established = false,
	.rtp_established_time = 0,
	.current_call = NULL
};

/* RTT tracking for ping/pong stability detection */
static struct {
	double last_ping_sent_timestamp;  /* Timestamp when last ping (tone_id 1) was sent */
	double last_pong_received_timestamp;  /* Timestamp when last pong (tone_id 2) was received */
	double last_rtt;                /* Last calculated RTT */
	double prev_rtt;                /* Previous RTT for stability check */
	bool last_rtt_valid;            /* Whether last_rtt is valid */
	bool prev_rtt_valid;             /* Whether prev_rtt is valid */
	bool connection_stable;          /* Whether connection is detected as stable */
	uint64_t last_ping_sent_time;    /* Global: jiffies when last ping was sent (to coordinate across encoder instances) */
	uint64_t last_pong_received_time; /* Global: jiffies when last pong was received (for timeout calculation) */
	uint64_t next_ping_time;         /* Global: jiffies when next ping should be sent (50ms after pong received) */
	/* Regular tone scheduling is global (per-process) to avoid bursts when multiple encoder instances exist */
	uint64_t last_regular_tone_end_time;  /* Global: jiffies when last regular tone ended */
	bool regular_tone_active;             /* Global: a regular tone is currently being generated */
	struct tonedetect_st *regular_tone_owner; /* Global: encoder instance currently generating the regular tone */
	size_t regular_tone_index;            /* Global: next sequential tone offset (0..num_regular-1) */
	size_t regular_tone_start_offset;     /* Global: start offset for sequential tones to desync peers */
	bool pending_pong;                /* Whether we need to send a pong (tone_id 2) */
	bool pending_ping;                /* Whether we need to send a ping (tone_id 1) */
	bool first_ping_sent;             /* Whether the first ping has been sent */
} rtt_tracking = {
	.last_ping_sent_timestamp = 0.0,
	.last_pong_received_timestamp = 0.0,
	.last_rtt = 0.0,
	.prev_rtt = 0.0,
	.last_rtt_valid = false,
	.prev_rtt_valid = false,
	.connection_stable = false,
	.last_ping_sent_time = 0,
	.last_pong_received_time = 0,
	.next_ping_time = 0,
	.last_regular_tone_end_time = 0,
	.regular_tone_active = false,
	.regular_tone_owner = NULL,
	.regular_tone_index = 0,
	.regular_tone_start_offset = 0,
	.pending_pong = false,
	.pending_ping = false,
	.first_ping_sent = false
};

static void enc_destructor(void *arg)
{
	struct tonedetect_st *st = arg;
	list_unlink(&st->u.eaf.le);
	/* Note: mem_deref(st) is called automatically by the mem system */
}

static void dec_destructor(void *arg)
{
	struct tonedetect_st *st = arg;
	list_unlink(&st->u.daf.le);
	mem_deref(st->det.frequencies);
	mem_deref(st->det.goertzel_coeffs);
	mem_deref(st->det.goertzel_q1);
	mem_deref(st->det.goertzel_q2);
	mem_deref(st->det.window);
	mem_deref(st->det.ring);
	/* Note: mem_deref(st) is called automatically by the mem system */
}

static size_t pair_index_from_two(size_t i, size_t j, size_t n)
{
	/* i < j, n >= 2 */
	const size_t base = i * (n - 1) - (i * (i - 1)) / 2;
	return base + (j - i - 1);
}

/**
 * Initialize Goertzel algorithm for a specific frequency
 */
static double goertzel_init_coeff(uint32_t target_freq, uint32_t srate)
{
	double normalized_freq = (double)target_freq / (double)srate;
	return 2.0 * cos(2.0 * PI * normalized_freq);
}

static double unix_time_now_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	/* Seconds since epoch with microsecond precision (historical name). */
	return ((double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec) / 1000000.0;
}

static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct tonedetect_st *st;
	(void)af;
	(void)ctx;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	st->gen.active = false;
	st->gen.frequency = 0;
	st->gen.frequency2 = 0;
	st->gen.duration_ms = config.tone_duration_ms;
	st->gen.last_tone_end_time = 0;
	st->gen.first_packet_timestamp = 0.0;
	st->gen.full_amplitude_reached = false;
	st->gen.phase = 0.0;
	st->gen.phase2 = 0.0;
	st->gen.srate = prm->srate;
	st->gen.sample_index = 0;
	st->gen.total_samples = 0;
	st->gen.ramp_samples = 0;
	/* Randomize starting tone index to avoid caller/callee sending same tone simultaneously */
	if (config.num_send_pairs > 0)
		st->gen.current_tone_index = rand_u32() % config.num_send_pairs;
	else
		st->gen.current_tone_index = 0;
	st->gen.tone_id = 0;
	st->gen.is_ping_mode = true;  /* Start in ping mode until connection is stable */
	st->gen.regular_tone_start_index = 0;  /* Will be set when switching to regular tone mode */

	*stp = (struct aufilt_enc_st *)st;

	debug("tonedetect: encoder initialized: num_send_frequencies=%zu duration_ms=%u srate=%u ch=%u starting_tone_index=%zu\n",
	      config.num_send_frequencies, config.tone_duration_ms, prm->srate, prm->ch, st->gen.current_tone_index);

	return 0;
}

static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct tonedetect_st *st;
	size_t i;
	(void)af;
	(void)ctx;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	st->det.srate = prm->srate;
	/* Calculate number of samples for detection window */
	st->det.detection_window_samples = (prm->srate * DETECTION_WINDOW_MS) / 1000;
	st->det.hop_samples = (prm->srate * DETECTION_HOP_MS) / 1000;
	if (st->det.hop_samples == 0)
		st->det.hop_samples = 1;
	if (st->det.hop_samples >= st->det.detection_window_samples)
		st->det.hop_samples = st->det.detection_window_samples;
	st->det.ring_pos = 0;
	st->det.ring_count = 0;
	st->det.hop_count = 0;
	st->det.candidate_pair_index = 0;
	st->det.candidate_count = 0;
	st->det.failure_count = 0;
	st->det.first_packet_timestamp = 0.0;
	st->det.last_emit_time = 0;
	st->det.last_emit_index = 0;
	st->det.last_emit_valid = false;
	st->det.full_amplitude_detected = false;
	st->det.num_frequencies = config.num_detect_frequencies;

	if (st->det.num_frequencies > 0) {
		size_t alloc_size = st->det.num_frequencies * sizeof(uint32_t);
		st->det.frequencies = mem_zalloc(alloc_size, NULL);
		st->det.goertzel_coeffs = mem_zalloc(
			st->det.num_frequencies * sizeof(double), NULL);
		st->det.goertzel_q1 = mem_zalloc(
			st->det.num_frequencies * sizeof(double), NULL);
		st->det.goertzel_q2 = mem_zalloc(
			st->det.num_frequencies * sizeof(double), NULL);
		st->det.window = mem_zalloc(
			st->det.detection_window_samples * sizeof(double), NULL);
		st->det.ring = mem_zalloc(
			st->det.detection_window_samples * sizeof(int16_t), NULL);

		if (!st->det.frequencies || !st->det.goertzel_coeffs ||
		    !st->det.goertzel_q1 || !st->det.goertzel_q2 ||
		    !st->det.window || !st->det.ring) {
			mem_deref(st);
			return ENOMEM;
		}

		/* Precompute Hamming window */
		for (i = 0; i < st->det.detection_window_samples; i++) {
			st->det.window[i] = 0.54 - 0.46 * cos(
				(2.0 * PI * (double)i) /
				((double)st->det.detection_window_samples - 1.0));
		}

		/* Copy frequencies and initialize Goertzel coefficients */
		for (i = 0; i < st->det.num_frequencies; i++) {
			st->det.frequencies[i] = config.detect_frequencies[i];
			st->det.goertzel_coeffs[i] = goertzel_init_coeff(
				config.detect_frequencies[i], prm->srate);
			st->det.goertzel_q1[i] = 0.0;
			st->det.goertzel_q2[i] = 0.0;
		}
	}

	*stp = (struct aufilt_dec_st *)st;

	debug("tonedetect: decoder initialized: num_detect_frequencies=%zu srate=%u ch=%u window_samples=%zu hop_samples=%zu\n",
	      st->det.num_frequencies, prm->srate, prm->ch,
	      st->det.detection_window_samples, st->det.hop_samples);

	return 0;
}


/**
 * Start generating a tone
 */
static void start_tone_generation(struct tonedetect_st *st,
				  uint32_t freq1, uint32_t freq2,
				  size_t tone_id, uint32_t srate)
{
	if (!st)
		return;

	st->gen.active = true;
	st->gen.frequency = freq1;
	st->gen.frequency2 = freq2;
	st->gen.tone_id = tone_id;
	st->gen.start_time = tmr_jiffies();
	st->gen.phase = 0.0;
	st->gen.phase2 = 0.0;
	st->gen.srate = srate;
	st->gen.sample_index = 0;
	st->gen.total_samples = ((size_t)st->gen.duration_ms * (size_t)srate) / 1000;
	st->gen.ramp_samples = ((size_t)TONE_RAMP_MS * (size_t)srate) / 1000;
	if (st->gen.ramp_samples * 2 > st->gen.total_samples)
		st->gen.ramp_samples = st->gen.total_samples / 2;

	/* Timestamp is captured when the tone reaches full amplitude. */
	st->gen.first_packet_timestamp = 0.0;
	st->gen.full_amplitude_reached = false;

	info("tonedetect: tone start: frequency=%u frequency2=%u duration=%u tone_id=%zu\n",
	     freq1, freq2, st->gen.duration_ms, tone_id);
}

/* Simplified: Pings always use tone_id 1, pongs always use tone_id 2 */
#define PING_TONE_ID 1
#define PONG_TONE_ID 2
#define MIN_REGULAR_TONE_ID 3

static int encode(struct aufilt_enc_st *aufilt_enc_st, struct auframe *af)
{
	struct tonedetect_st *st = (struct tonedetect_st *)aufilt_enc_st;
	size_t i;
	int16_t *sampv;
	uint64_t now;

	if (!st || !af)
		return EINVAL;

	now = tmr_jiffies();

	/* Stop any active tone if generation is disabled (except pongs - they must always be sent) */
	if (!config.enable_tone_generation && st->gen.active && st->gen.tone_id != PONG_TONE_ID) {
		st->gen.active = false;
		st->gen.last_tone_end_time = now;
	}

	/* Once the connection is stable, switch from ping/pong phase to regular tone mode. */
	if (rtt_tracking.connection_stable) {
		if (st->gen.is_ping_mode) {
			st->gen.is_ping_mode = false;
			/* Derive a per-instance starting offset to de-sync regular tones across instances. */
			size_t num_regular = config.num_send_pairs - (MIN_REGULAR_TONE_ID - 1);
			if (num_regular > 0) {
				uintptr_t addr = (uintptr_t)st;
				st->gen.regular_tone_start_index = (size_t)(addr % num_regular);
				debug("tonedetect: switching from ping mode to regular tone mode (encoder instance), starting at index %zu\n",
				     st->gen.regular_tone_start_index);
			}
			else {
				st->gen.regular_tone_start_index = 0;
			}
		}
	}

	/* Priority 1: Send pong reply (tone_id 2) if we received a ping (always, even if generation is disabled) */
	/* Pongs must always be sent in reply to pings, regardless of generation setting */
	if (rtt_tracking.pending_pong && tonedetect_call_state.call_established && tonedetect_call_state.rtp_established &&
	    !st->gen.active && config.num_send_pairs > 0) {
		/* Send pong immediately (tone_id 2) - interrupt any other tone sending */
		size_t pair_index = PONG_TONE_ID - 1;
		const uint8_t ia = config.send_pair_a[pair_index];
		const uint8_t ib = config.send_pair_b[pair_index];
		uint32_t f1 = config.send_frequencies[ia];
		uint32_t f2 = config.send_frequencies[ib];
		
		debug("tonedetect: sending pong reply (tone_id=%d)\n", PONG_TONE_ID);
		start_tone_generation(st, f1, f2, PONG_TONE_ID, af->srate);
		
		rtt_tracking.pending_pong = false;
	}

	/* Priority 2: Send ping (tone_id 1) dynamically - first ping immediately, then after pong or timeout */
	/* Send first ping as soon as call/RTP is established */
	if (!rtt_tracking.first_ping_sent && tonedetect_call_state.call_established && tonedetect_call_state.rtp_established &&
	    !st->gen.active && config.num_send_pairs > 0 && PING_TONE_ID <= config.num_send_pairs) {
		/* Send first ping immediately */
		size_t pair_index = PING_TONE_ID - 1;
		const uint8_t ia = config.send_pair_a[pair_index];
		const uint8_t ib = config.send_pair_b[pair_index];
		uint32_t f1 = config.send_frequencies[ia];
		uint32_t f2 = config.send_frequencies[ib];
		
		debug("tonedetect: sending first ping (tone_id=%d) - call/RTP established\n", PING_TONE_ID);
		start_tone_generation(st, f1, f2, PING_TONE_ID, af->srate);
		
		rtt_tracking.last_ping_sent_time = now;
		rtt_tracking.first_ping_sent = true;
		rtt_tracking.pending_ping = false;
	}
	/* Send ping if scheduled (50ms after receiving a pong and stability not detected) or if timeout (2 seconds without pong) */
	else if (!rtt_tracking.connection_stable && rtt_tracking.next_ping_time > 0 && now >= rtt_tracking.next_ping_time &&
	         tonedetect_call_state.call_established && tonedetect_call_state.rtp_established &&
	         !st->gen.active && config.num_send_pairs > 0 && PING_TONE_ID <= config.num_send_pairs) {
		/* Send ping 50ms after receiving a pong (if stability not detected) */
		size_t pair_index = PING_TONE_ID - 1;
		const uint8_t ia = config.send_pair_a[pair_index];
		const uint8_t ib = config.send_pair_b[pair_index];
		uint32_t f1 = config.send_frequencies[ia];
		uint32_t f2 = config.send_frequencies[ib];
		
		debug("tonedetect: sending ping (tone_id=%d) - 50ms after pong received\n", PING_TONE_ID);
		start_tone_generation(st, f1, f2, PING_TONE_ID, af->srate);
		
		rtt_tracking.last_ping_sent_time = now;
		rtt_tracking.next_ping_time = 0;  /* Clear scheduled ping time */
		rtt_tracking.pending_ping = false;
	}
	/* Check for timeout: if 1 second passes without receiving a pong, send another ping */
	else if (!rtt_tracking.connection_stable && rtt_tracking.first_ping_sent && rtt_tracking.last_ping_sent_time > 0 &&
	         tonedetect_call_state.call_established && tonedetect_call_state.rtp_established &&
	         !st->gen.active && config.num_send_pairs > 0 && PING_TONE_ID <= config.num_send_pairs) {
		uint64_t ping_timeout_ms = 1000;  /* 1 second timeout */
		uint64_t time_since_ping = now - rtt_tracking.last_ping_sent_time;
		uint64_t time_since_pong = (rtt_tracking.last_pong_received_time > 0) ? (now - rtt_tracking.last_pong_received_time) : ping_timeout_ms;
		
		/* If no pong received within 1 second, send another ping */
		if (time_since_ping >= ping_timeout_ms && time_since_pong >= ping_timeout_ms) {
			size_t pair_index = PING_TONE_ID - 1;
			const uint8_t ia = config.send_pair_a[pair_index];
			const uint8_t ib = config.send_pair_b[pair_index];
			uint32_t f1 = config.send_frequencies[ia];
			uint32_t f2 = config.send_frequencies[ib];
			
			debug("tonedetect: sending ping (tone_id=%d) - timeout (no pong received in 1s)\n", PING_TONE_ID);
			start_tone_generation(st, f1, f2, PING_TONE_ID, af->srate);
			
			rtt_tracking.last_ping_sent_time = now;
		}
	}

	/* Check if we should start a new tone (only if generation is enabled and call is ready) */
	if (config.enable_tone_generation && tonedetect_call_state.call_established && tonedetect_call_state.rtp_established &&
	    !st->gen.active && config.num_send_pairs > 0) {
		/* Priority 3: Send regular tones (sequential IDs 3..N) once connection is stable */
		if (rtt_tracking.connection_stable) {
			/* Regular tones: sequential IDs (3..N), globally scheduled every 5 seconds per process */
			const uint64_t tone_spacing_ms = 5000;  /* 5s for regular tones */

			if (!rtt_tracking.regular_tone_active) {
				const uint64_t since_last =
					(rtt_tracking.last_regular_tone_end_time == 0) ? tone_spacing_ms :
					(now - rtt_tracking.last_regular_tone_end_time);
				const bool tone_should_send =
					(rtt_tracking.last_regular_tone_end_time == 0 ||
					 since_last >= tone_spacing_ms);

				if (tone_should_send && config.num_send_pairs >= MIN_REGULAR_TONE_ID) {
					const size_t num_regular = config.num_send_pairs - (MIN_REGULAR_TONE_ID - 1);
					if (num_regular > 0) {
						/* Sequential: 3..(3+num_regular-1) */
						const size_t idx = (rtt_tracking.regular_tone_start_offset +
								    rtt_tracking.regular_tone_index) % num_regular;
						const size_t tone_id = MIN_REGULAR_TONE_ID + idx;
						const size_t pair_index = tone_id - 1;
						const uint8_t ia = config.send_pair_a[pair_index];
						const uint8_t ib = config.send_pair_b[pair_index];
						const uint32_t f1 = config.send_frequencies[ia];
						const uint32_t f2 = config.send_frequencies[ib];

						start_tone_generation(st, f1, f2, tone_id, af->srate);

						/* Claim ownership so only one encoder instance sends regular tones */
						rtt_tracking.regular_tone_active = true;
						rtt_tracking.regular_tone_owner = st;
						rtt_tracking.regular_tone_index++;
					}
				}
			}
		}
	}

	sampv = (int16_t *)af->sampv;

	/* Generate tone if active (always generate pongs, even if generation is disabled) */
	if (st->gen.active && (config.enable_tone_generation || st->gen.tone_id == PONG_TONE_ID)) {
		uint64_t elapsed_ms = (now - st->gen.start_time);

		if (elapsed_ms >= st->gen.duration_ms) {
			st->gen.active = false;
			/* Record when tone ended */
			/* If we just switched to regular tone mode and this was a ping/pong, reset timer to send first regular tone immediately */
			if (rtt_tracking.connection_stable && !st->gen.is_ping_mode &&
			    (st->gen.tone_id == PING_TONE_ID || st->gen.tone_id == PONG_TONE_ID)) {
				/* Just finished a ping/pong after stability - reset timer to send first regular tone immediately */
				st->gen.last_tone_end_time = 0;
				debug("tonedetect: ping/pong ended after stability, resetting timer for first regular tone\n");
			}
			else {
				st->gen.last_tone_end_time = now;
			}

			/* If this instance is generating the global regular tone, update the global scheduler */
			if (st->gen.tone_id >= MIN_REGULAR_TONE_ID &&
			    rtt_tracking.regular_tone_owner == st) {
				rtt_tracking.last_regular_tone_end_time = now;
				rtt_tracking.regular_tone_active = false;
				rtt_tracking.regular_tone_owner = NULL;
			}
			/* Event already emitted when tone started, no need to emit again */
		}
		else {
			/* Calculate phase increment */
			double phase_inc1 = 2.0 * PI * st->gen.frequency /
					    (double)af->srate;
			double phase_inc2 = 2.0 * PI * st->gen.frequency2 /
					    (double)af->srate;

			/* Mix tone into audio */
			for (i = 0; i < af->sampc; i++) {
				/* Envelope to reduce spectral splatter */
				double env = 1.0;
				if (st->gen.ramp_samples) {
					if (st->gen.sample_index < st->gen.ramp_samples) {
						env = (double)st->gen.sample_index /
						      (double)st->gen.ramp_samples;
					}
					else if (st->gen.total_samples &&
						 st->gen.sample_index >
							 st->gen.total_samples - st->gen.ramp_samples) {
						size_t tail = st->gen.total_samples - st->gen.sample_index;
						env = (double)tail / (double)st->gen.ramp_samples;
					}
				}

				/* Capture timestamp when tone reaches full amplitude for the first time */
				if (!st->gen.full_amplitude_reached && env >= 1.0) {
					/* Get timestamp at exact moment when full amplitude is reached (microsecond precision) */
					const double full_amplitude_timestamp = unix_time_now_ms();
					st->gen.first_packet_timestamp = full_amplitude_timestamp;
					st->gen.full_amplitude_reached = true;
					
					/* Track ping timestamps for RTT calculation */
					if (st->gen.tone_id == PING_TONE_ID) {
						rtt_tracking.last_ping_sent_timestamp = full_amplitude_timestamp;
						debug("tonedetect: ping sent (tone_id=%d) timestamp=%.6f (tracked for RTT calculation)\n",
						     PING_TONE_ID, full_amplitude_timestamp);
					}
					
					/* Emit event with timestamp when full amplitude is reached */
					if (st->gen.tone_id == PING_TONE_ID) {
						/* Ping: use "ping" as type */
						bevent_app_emit(UA_EVENT_AUDIO_LATENCY_OUTGOING, NULL,
								"ping timestamp=%.6f",
								full_amplitude_timestamp);
					}
					else if (st->gen.tone_id == PONG_TONE_ID) {
						/* Pong: use "pong" as type */
						bevent_app_emit(UA_EVENT_AUDIO_LATENCY_OUTGOING, NULL,
								"pong timestamp=%.6f",
								full_amplitude_timestamp);
					}
					else {
						/* Regular tones: just tone_id, no type */
						bevent_app_emit(UA_EVENT_AUDIO_LATENCY_OUTGOING, NULL,
								"tone_id=%zu timestamp=%.6f",
								st->gen.tone_id, full_amplitude_timestamp);
					}
				}

				/* Dual-tone (DTMF-style): sum of two sines, scaled to keep level */
				const double s1 = sin(st->gen.phase);
				const double s2 = sin(st->gen.phase2);
				st->gen.phase += phase_inc1;
				st->gen.phase2 += phase_inc2;
				if (st->gen.phase >= 2.0 * PI)
					st->gen.phase -= 2.0 * PI;
				if (st->gen.phase2 >= 2.0 * PI)
					st->gen.phase2 -= 2.0 * PI;

				const double dual = (s1 + s2) * 0.5;
				int16_t tone_sample = (int16_t)(dual * TONE_AMPLITUDE * 32767.0 * env);
				/* Mix with existing audio (simple addition) */
				int32_t mixed = (int32_t)sampv[i] + tone_sample;
				/* Clamp to int16_t range */
				if (mixed > 32767)
					mixed = 32767;
				else if (mixed < -32768)
					mixed = -32768;
				sampv[i] = (int16_t)mixed;

				st->gen.sample_index++;
			}
		}
	}

	return 0;
}

static int decode(struct aufilt_dec_st *aufilt_dec_st, struct auframe *af)
{
	struct tonedetect_st *st = (struct tonedetect_st *)aufilt_dec_st;
	size_t i, j;
	int16_t *sampv;
	const uint64_t now = tmr_jiffies();

	if (!st || !af || st->det.num_frequencies == 0 ||
	    st->det.detection_window_samples == 0) {
		return 0;
	}

	/* Only detect tones if call is established and RTP is established */
	if (!tonedetect_call_state.call_established || !tonedetect_call_state.rtp_established) {
		return 0;
	}

	sampv = (int16_t *)af->sampv;

	/* Feed samples into a ring buffer and evaluate overlapping windows.
	 * This greatly reduces "missed tones" when a short tone straddles a
	 * window boundary, without loosening false-positive thresholds.
	 */
	for (i = 0; i < af->sampc; i++) {
		/* Ring buffer store */
		if (st->det.ring) {
			st->det.ring[st->det.ring_pos] = sampv[i];
			st->det.ring_pos =
				(st->det.ring_pos + 1) % st->det.detection_window_samples;
			if (st->det.ring_count < st->det.detection_window_samples)
				st->det.ring_count++;
			st->det.hop_count++;
		}

		/* Not enough samples yet */
		if (!st->det.ring ||
		    st->det.ring_count < st->det.detection_window_samples) {
			continue;
		}

		/* Evaluate on hop boundary */
		if (st->det.hop_count < st->det.hop_samples)
			continue;
		st->det.hop_count = 0;

		{
			double best_ratio = 0.0;
			double second_ratio = 0.0;
			double third_ratio = 0.0;
			double sum_ratio = 0.0;
			double best_power = 0.0;
			double second_power = 0.0;
			size_t best_index = (size_t)-1;
			size_t second_index = (size_t)-1;
			double block_energy = 0.0;
			const size_t winN = st->det.detection_window_samples;
			const size_t ring_pos = st->det.ring_pos;
			const double *window = st->det.window;
			const double *coeffs = st->det.goertzel_coeffs;
			double *q1v = st->det.goertzel_q1;
			double *q2v = st->det.goertzel_q2;

			/* Reset Goertzel state for this evaluation */
			for (j = 0; j < st->det.num_frequencies; j++) {
				q1v[j] = 0.0;
				q2v[j] = 0.0;
			}

			/* Compute Goertzel over the current window (oldest sample at ring_pos).
			 * Split ring into two linear segments to avoid modulo in the inner loop.
			 */
			{
				const size_t len1 = (ring_pos < winN) ? (winN - ring_pos) : 0;
				const size_t len2 = winN - len1;
				const int16_t *ring = st->det.ring;
				const size_t nf = st->det.num_frequencies;

				/* Segment 1: ring[ring_pos .. winN-1] -> window[0 .. len1-1] */
				for (size_t k = 0; k < len1; k++) {
					const double w = window ? window[k] : 1.0;
					const double x = (double)ring[ring_pos + k] * w;
					for (j = 0; j < nf; j++) {
						/* Inline Goertzel step */
						const double q0 = coeffs[j] * q1v[j] - q2v[j] + x;
						q2v[j] = q1v[j];
						q1v[j] = q0;
					}
					block_energy += x * x;
				}

				/* Segment 2: ring[0 .. ring_pos-1] -> window[len1 .. winN-1] */
				for (size_t k = 0; k < len2; k++) {
					const size_t wk = len1 + k;
					const double w = window ? window[wk] : 1.0;
					const double x = (double)ring[k] * w;
					for (j = 0; j < nf; j++) {
						const double q0 = coeffs[j] * q1v[j] - q2v[j] + x;
						q2v[j] = q1v[j];
						q1v[j] = q0;
					}
					block_energy += x * x;
				}
			}

			if (block_energy >= DETECT_MIN_BLOCK_ENERGY) {
				for (j = 0; j < st->det.num_frequencies; j++) {
					const double q1 = q1v[j];
					const double q2 = q2v[j];
					const double c = coeffs[j];
					const double power = q1 * q1 + q2 * q2 - q1 * q2 * c;
					const double ratio = power / block_energy;
					sum_ratio += ratio;

					if (ratio > best_ratio) {
						third_ratio = second_ratio;
						second_ratio = best_ratio;
						best_ratio = ratio;
						second_power = best_power;
						best_power = power;
						second_index = best_index;
						best_index = j;
					}
					else if (ratio > second_ratio) {
						third_ratio = second_ratio;
						second_ratio = ratio;
						second_power = power;
						second_index = j;
					}
					else if (ratio > third_ratio) {
						third_ratio = ratio;
					}
				}
			}

			if (best_index == (size_t)-1 || second_index == (size_t)-1) {
				st->det.candidate_count = 0;
				st->det.failure_count++;
				/* Avoid recapturing timestamps on brief interruptions. */
				if (st->det.failure_count >= 10) {
					st->det.full_amplitude_detected = false;
				}
				continue;
			}

			/* Dual-tone requirement: top2 must both be present, and separated from others */
			const double dual_balance =
				(best_ratio > 0.0) ? (second_ratio / best_ratio) : 0.0;
			const double top2_share =
				(sum_ratio > 0.0) ? ((best_ratio + second_ratio) / sum_ratio) : 0.0;
			const bool passes =
				(best_ratio >= DETECT_RATIO_THRESHOLD) &&
				(second_ratio >= DETECT_RATIO_THRESHOLD) &&
				(dual_balance >= DETECT_DUAL_BALANCE_MIN) &&
				(top2_share >= DETECT_TOP2_SHARE_MIN) &&
				(third_ratio <= 0.0 ||
				 (best_ratio >= (third_ratio * DETECT_PEAK_SEPARATION) &&
				  second_ratio >= (third_ratio * DETECT_PEAK_SEPARATION)));

			if (!passes) {
				st->det.candidate_count = 0;
				st->det.failure_count++;
				if (st->det.failure_count >= 10) {
					st->det.full_amplitude_detected = false;
				}
				continue;
			}

			/* Early validation: ensure top 2 frequencies are one low + one high */
			/* This prevents false positives from detecting two low or two high frequencies */
			size_t a = best_index;
			size_t b = second_index;
			/* Check if indices correspond to low or high frequencies */
			/* Low frequencies are first num_detect_low in the array */
			size_t num_low = (config.num_detect_low > 0) ? config.num_detect_low : 
				((st->det.num_frequencies >= 6) ? 3 : st->det.num_frequencies / 2);
			bool a_is_low = (a < num_low);
			bool b_is_low = (b < num_low);
			
			/* Reject if both are low or both are high (must be one of each) */
			if (a_is_low == b_is_low) {
				/* Both from same set - reject this detection early */
				st->det.candidate_count = 0;
				st->det.failure_count++;
				if (st->det.failure_count >= 10) {
					st->det.full_amplitude_detected = false;
				}
				continue;
			}

			/* Ensure a is low and b is high for consistent processing */
			if (!a_is_low) {
				/* Swap so a is low, b is high */
				size_t tmp = a;
				a = b;
				b = tmp;
			}

			/* Calculate pair_index for debouncing (using local indices) */
			const size_t pair_index = pair_index_from_two(a, b, st->det.num_frequencies);
			const size_t local_npairs = (st->det.num_frequencies * (st->det.num_frequencies - 1)) / 2;
			if (pair_index >= local_npairs)
				continue;

			/* Debounce on the pair-index */
			if (st->det.candidate_count == 0 ||
			    st->det.candidate_pair_index != pair_index) {
				st->det.candidate_pair_index = pair_index;
				st->det.candidate_count = 1;
				/* Don't reset full_amplitude_detected here - only reset when detection truly fails */
			}
			else if (st->det.candidate_count < 255) {
				st->det.candidate_count++;
			}
			
			/* Reset failure count when detection is successful */
			st->det.failure_count = 0;

			/* Require stable detection before emitting an event. */
			if (st->det.candidate_count < DETECT_CONSECUTIVE_BLOCKS)
				continue;

			/* Keep "magnitude" semantics similar to prior code */
			double mag1 = sqrt(best_power) / (double)st->det.detection_window_samples;
			double mag2 = sqrt(second_power) / (double)st->det.detection_window_samples;
			double magnitude = mag1 < mag2 ? mag1 : mag2;

			/* Absolute guard: ignore very weak detections */
			if (mag1 < DETECT_MIN_MAGNITUDE || mag2 < DETECT_MIN_MAGNITUDE) {
				continue;
			}

			/* Calculate tone_id based on detected frequencies */
			/* First, find the indices of detected frequencies in config arrays */
			uint32_t detected_f1 = st->det.frequencies[a];
			uint32_t detected_f2 = st->det.frequencies[b];
			size_t config_idx1 = (size_t)-1;
			size_t config_idx2 = (size_t)-1;

			/* Find indices in config.detect_frequencies (must match config.send_frequencies) */
			for (size_t k = 0; k < config.num_detect_frequencies; k++) {
				if (config.detect_frequencies[k] == detected_f1 && config_idx1 == (size_t)-1)
					config_idx1 = k;
				if (config.detect_frequencies[k] == detected_f2 && config_idx2 == (size_t)-1)
					config_idx2 = k;
			}

			/* Validate that we found both frequencies in config */
			if (config_idx1 == (size_t)-1 || config_idx2 == (size_t)-1 ||
			    config.num_detect_frequencies != st->det.num_frequencies) {
				/* Ignore unidentified pairs for latency reporting. */
				debug("tonedetect: tone detect (unidentified): frequency=%u frequency2=%u magnitude=%.3f\n",
				     detected_f1, detected_f2, magnitude);
				continue;
			}

			/* Determine which is low and which is high */
			size_t low_idx, high_idx;
			if (config_idx1 < config.num_detect_low && config_idx2 >= config.num_detect_low) {
				/* idx1 is low, idx2 is high */
				low_idx = config_idx1;
				high_idx = config_idx2 - config.num_detect_low;
			}
			else if (config_idx2 < config.num_detect_low && config_idx1 >= config.num_detect_low) {
				/* idx2 is low, idx1 is high */
				low_idx = config_idx2;
				high_idx = config_idx1 - config.num_detect_low;
			}
			else {
				/* Invalid pair for this scheme: ignore for latency reporting. */
				debug("tonedetect: tone detect (invalid pair): frequency=%u frequency2=%u (both low or both high)\n",
				     detected_f1, detected_f2);
				continue;
			}

			/* Calculate tone_id: (low_index * num_high) + high_index + 1 */
			/* tone_id is 1-based (1..9) */
			size_t tone_id = (low_idx * config.num_detect_high) + high_idx + 1;
			if (low_idx >= config.num_detect_low || high_idx >= config.num_detect_high)
				tone_id = 0;
			
			/* Suppress repeats for the same pair while it's still present */
			/* Use shorter suppression window for ping/pong (they're shorter tones) */
			bool is_ping_or_pong = (tone_id == PING_TONE_ID || tone_id == PONG_TONE_ID);
			uint64_t suppress_ms = is_ping_or_pong ? 200 : DETECT_SUPPRESS_MS;  /* 200ms for ping/pong, 3000ms for regular tones */
			if (st->det.last_emit_valid &&
			    st->det.last_emit_index == pair_index &&
			    (now - st->det.last_emit_time) < suppress_ms) {
				/* Suppress if same pair was detected recently */
				continue;
			}

			/* Capture RX timestamp once we're safely past the onset transient. */
			if (!st->det.full_amplitude_detected) {
				/* Wait one extra block beyond the debounce threshold. */
				if (st->det.candidate_count >= DETECT_CONSECUTIVE_BLOCKS + 1) {
					st->det.first_packet_timestamp = unix_time_now_ms();
					st->det.full_amplitude_detected = true;
				}
				else {
					continue;
				}
			}

			info("tonedetect: tone detect: frequency=%u frequency2=%u magnitude=%.3f tone_id=%zu (low_idx=%zu high_idx=%zu) timestamp=%.6f\n",
			     detected_f1, detected_f2, magnitude, tone_id, low_idx, high_idx,
			     st->det.first_packet_timestamp);

			/* Determine tone type: ping (tone_id 1), pong (tone_id 2), or regular tone (3-9) */
			if (tone_id == PING_TONE_ID) {
				/* Only accept ping if call and RTP are established */
				if (!tonedetect_call_state.call_established || !tonedetect_call_state.rtp_established) {
					debug("tonedetect: ignoring ping detection - call/RTP not established\n");
					continue;
				}
				
				/* Ping received: emit event and always queue pong reply */
				bevent_app_emit(UA_EVENT_AUDIO_LATENCY_INCOMING, NULL,
						"ping magnitude=%.3f timestamp=%.6f",
						magnitude, st->det.first_packet_timestamp);
				
				/* Always reply with pong (tone_id 2) when we receive a ping */
				rtt_tracking.pending_pong = true;
				debug("tonedetect: ping received (tone_id=%d), queuing pong reply (tone_id=%d)\n",
				     PING_TONE_ID, PONG_TONE_ID);
				
				/* Reset detection state to prevent detecting the same ping multiple times */
				st->det.full_amplitude_detected = false;
				st->det.candidate_count = 0;
			}
			else if (tone_id == PONG_TONE_ID) {
				/* Only accept pong if call and RTP are established, and we actually sent a ping */
				if (!tonedetect_call_state.call_established || !tonedetect_call_state.rtp_established) {
					debug("tonedetect: ignoring pong detection - call/RTP not established\n");
					continue;
				}
				
				/* Only accept pong if we actually sent a ping (validate against false positives) */
				if (rtt_tracking.last_ping_sent_timestamp <= 0.0) {
					debug("tonedetect: ignoring pong detection - no ping was sent (false positive)\n");
					continue;
				}
				
				/* Pong received: emit event and always calculate RTT */
				bevent_app_emit(UA_EVENT_AUDIO_LATENCY_INCOMING, NULL,
						"pong magnitude=%.3f timestamp=%.6f",
						magnitude, st->det.first_packet_timestamp);
				
				/* Update last pong received time for timeout calculation */
				rtt_tracking.last_pong_received_time = tmr_jiffies();
				
				/* Calculate RTT between last sent ping and this received pong (always check) */
				if (rtt_tracking.last_ping_sent_timestamp > 0.0) {
					rtt_tracking.last_pong_received_timestamp = st->det.first_packet_timestamp;
					const double rtt = rtt_tracking.last_pong_received_timestamp - rtt_tracking.last_ping_sent_timestamp;
					
					/* Only use valid RTT (positive and reasonable, e.g., less than 1 second) */
					/* This filters out cases where ping timestamp was overwritten by a newer ping */
					if (rtt > 0.0 && rtt < 1.0) {
						/* Log RTT calculation */
						info("tonedetect: RTT calculated: ping_ts=%.6f pong_ts=%.6f rtt=%.6f (%.3f ms)\n",
						     rtt_tracking.last_ping_sent_timestamp, rtt_tracking.last_pong_received_timestamp,
						     rtt, rtt * 1000.0);
						
						/* Update RTT tracking */
						rtt_tracking.prev_rtt = rtt_tracking.last_rtt;
						rtt_tracking.prev_rtt_valid = rtt_tracking.last_rtt_valid;
						rtt_tracking.last_rtt = rtt;
						rtt_tracking.last_rtt_valid = true;
						
						/* Check stability: two consecutive pongs with RTT deviation < 50% */
						if (rtt_tracking.prev_rtt_valid && rtt_tracking.last_rtt_valid) {
							/* Convert RTT to milliseconds for deviation calculation */
							const double prev_rtt_ms = rtt_tracking.prev_rtt * 1000.0;
							const double last_rtt_ms = rtt_tracking.last_rtt * 1000.0;
							const double avg_rtt_ms = (prev_rtt_ms + last_rtt_ms) / 2.0;
							const double deviation_ms = fabs(last_rtt_ms - prev_rtt_ms);
							const double deviation_percent = (avg_rtt_ms > 0.0) ? (deviation_ms / avg_rtt_ms) : 1.0;
							
							debug("tonedetect: RTT stability check: prev_rtt=%.3f ms last_rtt=%.3f ms avg_rtt=%.3f ms deviation=%.3f ms (%.2f%%)\n",
							     prev_rtt_ms, last_rtt_ms, avg_rtt_ms, deviation_ms, deviation_percent * 100.0);
							
							if (deviation_percent < STABILITY_RTT_DEVIATION_MAX && !rtt_tracking.connection_stable) {
								rtt_tracking.connection_stable = true;
								/* Initialize regular-tone scheduler so first tone can send immediately */
								rtt_tracking.last_regular_tone_end_time = 0;
								rtt_tracking.regular_tone_active = false;
								rtt_tracking.regular_tone_owner = NULL;
								rtt_tracking.regular_tone_index = 0;
								if (config.num_send_pairs >= MIN_REGULAR_TONE_ID) {
									size_t num_regular = config.num_send_pairs - (MIN_REGULAR_TONE_ID - 1);
									if (num_regular > 0) {
										rtt_tracking.regular_tone_start_offset = rand_u32() % num_regular;
									}
								}
								/* Force all encoder instances to switch to regular tone mode immediately */
								info("tonedetect: connection stable detected: RTT=%.3f ms prev_RTT=%.3f ms deviation=%.3f ms (%.2f%%) - switching to regular tones\n",
								     last_rtt_ms, prev_rtt_ms, deviation_ms, deviation_percent * 100.0);
								/* Don't emit a stability event (per requirements) */
							}
							else if (!rtt_tracking.connection_stable) {
								/* Stability not detected yet - schedule next ping 50ms after pong received */
								rtt_tracking.next_ping_time = rtt_tracking.last_pong_received_time + 50;  /* 50ms after pong */
								rtt_tracking.pending_ping = true;
								debug("tonedetect: stability not detected (deviation=%.2f%%), scheduling next ping in 50ms\n",
								     deviation_percent * 100.0);
							}
						}
						else if (!rtt_tracking.prev_rtt_valid) {
							debug("tonedetect: RTT tracking: first pong received, rtt=%.3f ms, waiting for second pong for stability check - scheduling next ping in 50ms\n",
							     rtt * 1000.0);
							/* First pong received - schedule next ping 50ms after pong to get second RTT measurement */
							rtt_tracking.next_ping_time = rtt_tracking.last_pong_received_time + 50;  /* 50ms after pong */
							rtt_tracking.pending_ping = true;
						}
					}
					else {
						debug("tonedetect: RTT calculation skipped: invalid rtt=%.6f (%.3f ms) - ping_ts=%.6f pong_ts=%.6f\n",
						     rtt, rtt * 1000.0, rtt_tracking.last_ping_sent_timestamp, rtt_tracking.last_pong_received_timestamp);
						/* Invalid RTT - schedule next ping 50ms after pong to retry */
						rtt_tracking.next_ping_time = rtt_tracking.last_pong_received_time + 50;  /* 50ms after pong */
						rtt_tracking.pending_ping = true;
					}
				}
				else {
					/* No ping timestamp available - schedule next ping 50ms after pong */
					rtt_tracking.next_ping_time = rtt_tracking.last_pong_received_time + 50;  /* 50ms after pong */
					rtt_tracking.pending_ping = true;
				}
				
				/* Reset detection state to prevent detecting the same pong multiple times */
				st->det.full_amplitude_detected = false;
				st->det.candidate_count = 0;
			}
			else {
				/* Regular tone (3-9): emit event with tone_id only, no type */
				bevent_app_emit(UA_EVENT_AUDIO_LATENCY_INCOMING, NULL,
						"tone_id=%zu magnitude=%.3f timestamp=%.6f",
						tone_id, magnitude, st->det.first_packet_timestamp);
			}

			st->det.last_emit_time = now;
			st->det.last_emit_index = pair_index;
			st->det.last_emit_valid = true;
		}
	}

	return 0;
}

/* Event handler for call events */
static void event_handler(enum ua_event ev, struct bevent *event, void *arg)
{
	struct call *call = bevent_get_call(event);
	const char *prm = bevent_get_text(event);
	(void)arg;

	switch (ev) {
	case UA_EVENT_CALL_INCOMING:
		/* Track incoming call */
		if (call) {
			tonedetect_call_state.current_call = call;
			debug("tonedetect: CALL_INCOMING - call ready for tone generation/detection\n");
		}
		break;

	case UA_EVENT_CALL_OUTGOING:
		/* Track outgoing call */
		if (call) {
			tonedetect_call_state.current_call = call;
			debug("tonedetect: CALL_OUTGOING - call ready for tone generation/detection\n");
		}
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		/* Only set state if we have a valid call */
		if (call) {
			tonedetect_call_state.call_established = true;
			tonedetect_call_state.current_call = call;
			info("tonedetect: CALL_ESTABLISHED - call ready for tone generation/detection\n");
		}
		break;

	case UA_EVENT_CALL_RTPESTAB:
		/* Only enable if it's an audio stream - call may not be available */
		if (prm && strstr(prm, "audio")) {
			tonedetect_call_state.rtp_established = true;
			tonedetect_call_state.rtp_established_time = tmr_jiffies();
			info("tonedetect: CALL_RTPESTAB (audio) - RTP ready for tone generation/detection\n");
		}
		break;

	case UA_EVENT_CALL_HOLD:
		/* When call is put on hold, pause tone generation/detection */
		tonedetect_call_state.rtp_established = false;
		tonedetect_call_state.rtp_established_time = 0;
		info("tonedetect: CALL_HOLD - pausing tone generation/detection\n");
		break;

	case UA_EVENT_CALL_RESUME:
		/* When call is resumed, re-enable if call is still established */
		if (tonedetect_call_state.call_established) {
			/* RTP will be re-established via CALL_RTPESTAB event */
			info("tonedetect: CALL_RESUME - waiting for RTP re-establishment\n");
		}
		break;

	case UA_EVENT_CALL_CLOSED:
	case UA_EVENT_CALL_ENDED_LOCAL:
	case UA_EVENT_CALL_ENDED_REMOTE:
		/* Reset state when call ends - don't require call object as it may be freed */
		tonedetect_call_state.call_established = false;
		tonedetect_call_state.rtp_established = false;
		tonedetect_call_state.rtp_established_time = 0;
		/* Reset RTT tracking for next call */
		rtt_tracking.last_ping_sent_timestamp = 0.0;
		rtt_tracking.last_pong_received_timestamp = 0.0;
		rtt_tracking.last_rtt = 0.0;
		rtt_tracking.prev_rtt = 0.0;
		rtt_tracking.last_rtt_valid = false;
		rtt_tracking.prev_rtt_valid = false;
		rtt_tracking.connection_stable = false;
		rtt_tracking.last_ping_sent_time = 0;
		rtt_tracking.last_pong_received_time = 0;
		rtt_tracking.next_ping_time = 0;
		rtt_tracking.last_regular_tone_end_time = 0;
		rtt_tracking.regular_tone_active = false;
		rtt_tracking.regular_tone_owner = NULL;
		rtt_tracking.regular_tone_index = 0;
		rtt_tracking.regular_tone_start_offset = 0;
		rtt_tracking.pending_pong = false;
		rtt_tracking.pending_ping = false;
		rtt_tracking.first_ping_sent = false;
		tonedetect_call_state.current_call = NULL;
		/* Note: gen.is_ping_mode will be reset in encode_update when new call starts */
		info("tonedetect: Call ended - resetting state\n");
		break;

	default:
		/* Ignore all other events */
		break;
	}
}

static struct aufilt tonedetect = {
	.name = "tonedetect",
	.encupdh = encode_update,
	.ench = encode,
	.decupdh = decode_update,
	.dech = decode
};

static int module_init(void)
{
	/* Two sets: 3 low frequencies + 3 high frequencies = 9 tone IDs */
	/* Low: 400Hz, 500Hz, 600Hz */
	/* High: 2000Hz, 2500Hz, 3000Hz */
	/* Tone IDs: 1=400+2000, 2=400+2500, 3=400+3000, 4=500+2000, ..., 9=600+3000 */
	uint32_t default_send[] = {400, 500, 600, 2000, 2500, 3000};
	uint32_t default_detect[] = {400, 500, 600, 2000, 2500, 3000};

	config.send_frequencies = mem_zalloc(sizeof(default_send), NULL);
	config.detect_frequencies = mem_zalloc(sizeof(default_detect), NULL);

	if (config.send_frequencies && config.detect_frequencies) {
		memcpy(config.send_frequencies, default_send,
		       sizeof(default_send));
		config.num_send_frequencies =
			sizeof(default_send) / sizeof(default_send[0]);
		config.num_low_frequencies = 3;
		config.num_high_frequencies = 3;
		memcpy(config.detect_frequencies, default_detect,
		       sizeof(default_detect));
		config.num_detect_frequencies =
			sizeof(default_detect) / sizeof(default_detect[0]);
		config.num_detect_low = 3;
		config.num_detect_high = 3;
	}

	/* Build pair list: all combinations of low x high (3x3 = 9 pairs) */
	config.num_send_pairs = 0;
	mem_deref(config.send_pair_a);
	mem_deref(config.send_pair_b);
	config.send_pair_a = NULL;
	config.send_pair_b = NULL;
	if (config.num_low_frequencies > 0 && config.num_high_frequencies > 0) {
		size_t npairs = config.num_low_frequencies * config.num_high_frequencies;
		config.send_pair_a = mem_zalloc(npairs * sizeof(uint8_t), NULL);
		config.send_pair_b = mem_zalloc(npairs * sizeof(uint8_t), NULL);
		if (config.send_pair_a && config.send_pair_b) {
			size_t k = 0;
			size_t i, j;
			/* Low frequencies are indices 0..(num_low-1) */
			/* High frequencies are indices num_low..(num_low+num_high-1) */
			for (i = 0; i < config.num_low_frequencies; i++) {
				for (j = 0; j < config.num_high_frequencies; j++) {
					config.send_pair_a[k] = (uint8_t)i;  /* Low frequency index */
					config.send_pair_b[k] = (uint8_t)(config.num_low_frequencies + j);  /* High frequency index */
					k++;
				}
			}
			config.num_send_pairs = npairs;
		}
	}

	/* Initialize RTT tracking (ping=1, pong=2). */
	rtt_tracking.last_rtt = 0.0;
	rtt_tracking.prev_rtt = 0.0;
	rtt_tracking.last_rtt_valid = false;
	rtt_tracking.prev_rtt_valid = false;
	rtt_tracking.connection_stable = false;

	/* Read configuration parameter for tone generation */
	conf_get_bool(conf_cur(), "tone_generation",
		      &config.enable_tone_generation);

	aufilt_register(baresip_aufiltl(), &tonedetect);
	
	/* Register event handler for call events */
	bevent_register(event_handler, NULL);
	
	info("tonedetect: module loaded - %zu low + %zu high frequencies = %zu tone IDs, generation=%s\n",
	     config.num_low_frequencies, config.num_high_frequencies, config.num_send_pairs,
	     config.enable_tone_generation ? "enabled" : "disabled");

	return 0;
}

static int module_close(void)
{
	/* Unregister event handler */
	bevent_unregister(event_handler);
	
	/* Reset call state */
	tonedetect_call_state.call_established = false;
	tonedetect_call_state.rtp_established = false;
	tonedetect_call_state.rtp_established_time = 0;
	
	mem_deref(config.send_frequencies);
	mem_deref(config.send_pair_a);
	mem_deref(config.send_pair_b);
	mem_deref(config.detect_frequencies);
	aufilt_unregister(&tonedetect);

	return 0;
}

EXPORT_SYM const struct mod_export DECL_EXPORTS(tonedetect) = {
	"tonedetect",
	"filter",
	module_init,
	module_close
};

