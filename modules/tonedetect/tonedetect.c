/**
 * tone_detect.c
 *
 * Baresip audio filter module that detects configured pure-tone frequencies
 * and prints timestamps when they occur.
 *
 * Build: add module snippet module.mk (provided) and compile baresip as usual.
 *
 * Uses Goertzel algorithm to efficiently detect a small set of known frequencies.
 *
 * Author: (you)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <errno.h>

#include <re.h>
#include <baresip.h>

/* --- Configuration --- */
static const double detect_freqs[] = {440.0, 550.0, 660.0}; /* frequencies to detect */
static const size_t n_detect_freqs = sizeof(detect_freqs)/sizeof(detect_freqs[0]);

/* Goertzel / detection params */
static const double energy_threshold_ratio = 0.15; /* fraction of maximum mapped energy to treat as detection */
static const double freq_tolerance_hz = 5.0;       /* tolerance (used for sanity only) */
static const size_t min_contiguous_frames = 2;     /* collapse very short flutters */

/* internal debug macro */
#define TD_DBG(...) fprintf(stderr, "[tone_detect] " __VA_ARGS__)

/* Per-frequency state for Goertzel detection (per sampling-rate / channels) */
struct goertzel_state {
    double target_freq;
    double coeff;
    double q1;
    double q2;
    double scale;       /* scale factor for energy normalization */
    int last_detected;  /* 0 or 1 */
    size_t contiguous;  /* contiguous frames detected (for collapse) */
};

/* per-instance state (aufilt instance) - one per filter instance */
struct td_enc {
    struct aufilt_enc_st af; /* base class (must be first) */
    unsigned samplerate;
    unsigned channels;
    uint64_t samples_seen; /* running sample counter for timestamps */
    struct goertzel_state *gstates;
    size_t n_gstates;
    double max_energy;     /* keep running max per-chunk to allow relative thresholding */
};

/* forward */
static int enc_update(struct aufilt_enc_st **stp, void **ctx,
                      const struct aufilt *af, struct aufilt_prm *prm,
                      const struct audio *au);
static int enc_encode(struct aufilt_enc_st *st, void *sampv, size_t *sampc);

/* module registration */
static struct aufilt td_filter = {
    LE_INIT,           /* list element init (macro used in other modules) */
    "tone_detect",     /* name */
    NULL,              /* encupdh - filled below */
    NULL,              /* ench    - filled below */
    NULL,              /* decupdh */
    NULL               /* dech */
};

/* Helper: init Goertzel states for given sample-rate */
static struct goertzel_state *goertzel_states_alloc(unsigned sr, size_t *out_n)
{
    size_t i;
    struct goertzel_state *gs = malloc(n_detect_freqs * sizeof(*gs));
    if (!gs) return NULL;

    for (i=0; i<n_detect_freqs; ++i) {
        double f = detect_freqs[i];
        double normalized = 2.0 * M_PI * f / (double)sr;
        double coeff = 2.0 * cos(normalized);
        gs[i].target_freq = f;
        gs[i].coeff = coeff;
        gs[i].q1 = gs[i].q2 = 0.0;
        gs[i].scale = 1.0; /* we'll normalize by energy after computing */
        gs[i].last_detected = 0;
        gs[i].contiguous = 0;
    }
    *out_n = n_detect_freqs;
    return gs;
}

/* Reset goertzel accumulators for a window */
static void goertzel_reset_window(struct goertzel_state *gs, size_t n)
{
    for (size_t i=0;i<n;++i) {
        gs[i].q1 = gs[i].q2 = 0.0;
        gs[i].contiguous = 0;
        gs[i].last_detected = 0;
    }
}

/* Process a single sample (int16) for all Goertzel states */
static void goertzel_process_sample(struct goertzel_state *gs, size_t n, double x)
{
    for (size_t i=0;i<n;++i) {
        double q0 = gs[i].coeff * gs[i].q1 - gs[i].q2 + x;
        gs[i].q2 = gs[i].q1;
        gs[i].q1 = q0;
    }
}

/* Compute energy output for each Goertzel state (after window) */
static void goertzel_compute_energies(struct goertzel_state *gs, size_t n,
                                      double *out_energy)
{
    for (size_t i=0;i<n;++i) {
        double real = gs[i].q1 - gs[i].q2 * cos(2.0 * M_PI * gs[i].target_freq / 44100.0); /* cos term - approximate */
        /* more robust calculation: using q1, q2 and coeff:
           power = q1*q1 + q2*q2 - coeff*q1*q2
        */
        double power = gs[i].q1*gs[i].q1 + gs[i].q2*gs[i].q2 - gs[i].coeff * gs[i].q1 * gs[i].q2;
        if (power < 0.0) power = 0.0;
        out_energy[i] = power;
    }
}

/*
 * enc_update
 *  - Called to initialize encoder-side filter instance.
 *  - We allocate per-instance state and Goertzel states here.
 */
static int enc_update(struct aufilt_enc_st **stp, void **ctx,
                      const struct aufilt *af, struct aufilt_prm *prm,
                      const struct audio *au)
{
    struct td_enc *st;
    (void)ctx;
    (void)af;
    if (!stp || !prm || !au)
        return EINVAL;

    st = mem_zalloc(sizeof(*st), NULL);
    if (!st)
        return ENOMEM;

    st->samplerate = prm->srate ? prm->srate : 8000;
    st->channels   = prm->ch    ? prm->ch : 1;
    st->samples_seen = 0;
    st->gstates = goertzel_states_alloc(st->samplerate, &st->n_gstates);
    if (!st->gstates) {
        mem_deref(st);
        return ENOMEM;
    }

    td_filter.encupdh = enc_update;
    td_filter.ench    = enc_encode;

    *stp = (struct aufilt_enc_st *)st;
    TD_DBG("encoder update: sr=%u ch=%u\n", st->samplerate, st->channels);
    return 0;
}

/*
 * enc_encode
 *  - Called for each audio buffer going out (encode side).
 *  - sampv: void* to audio samples (we assume int16_t PCM interleaved if channels>1)
 *  - sampc: pointer to number of samples (per-channel samples)
 *
 *  We examine the buffer using Goertzel over the buffer and if detection occurs,
 *  print a timestamp.
 */
static int enc_encode(struct aufilt_enc_st *afst, void *sampv, size_t *sampc)
{
    struct td_enc *st = (struct td_enc *)afst;
    if (!st || !sampv || !sampc) return EINVAL;

    int16_t *samples = (int16_t *)sampv;
    size_t frames = *sampc; /* number of per-channel samples (frames) */
    size_t i, ch;

    /* Prepare a temporary copy of Goertzel states for this window */
    struct goertzel_state gtemp[n_detect_freqs];
    for (i=0;i<st->n_gstates;++i) {
        gtemp[i] = st->gstates[i];
        gtemp[i].q1 = gtemp[i].q2 = 0.0;
    }

    /* Process each frame: convert to mono by averaging channels */
    for (i=0; i<frames; ++i) {
        double sample_mono = 0.0;
        for (ch=0; ch<st->channels; ++ch) {
            int16_t s = samples[i * st->channels + ch];
            sample_mono += (double)s;
        }
        sample_mono /= (double)st->channels;
        /* scale to small double range */
        double x = sample_mono / 32768.0;

        goertzel_process_sample(gtemp, st->n_gstates, x);
    }

    /* Compute energies */
    double energies[n_detect_freqs];
    goertzel_compute_energies(gtemp, st->n_gstates, energies);

    /* find maximum energy (for normalization) */
    double max_e = 1e-12;
    for (i=0;i<st->n_gstates;++i) {
        if (energies[i] > max_e) max_e = energies[i];
    }

    /* detect per-frequency threshold crossing */
    for (i=0;i<st->n_gstates;++i) {
        double ratio = energies[i] / max_e;
        int detected = (ratio >= energy_threshold_ratio) ? 1 : 0;

        if (detected) {
            st->gstates[i].contiguous += 1;
        } else {
            st->gstates[i].contiguous = 0;
        }

        /* rising edge: previously not detected, now detected and sustained for min_contiguous_frames */
        if (!st->gstates[i].last_detected && st->gstates[i].contiguous >= min_contiguous_frames) {
            /* compute timestamp of start of this buffer (approx): samples_seen / sr */
            double ts = (double)st->samples_seen / (double)st->samplerate;
            TD_DBG("Detected %.1f Hz at %.6f sec (energy ratio %.3f)\n",
                   st->gstates[i].target_freq, ts, ratio);
            /* update last_detected flag */
            st->gstates[i].last_detected = 1;
        }

        /* falling edge: clear flag if not detected */
        if (!detected && st->gstates[i].last_detected) {
            st->gstates[i].last_detected = 0;
            st->gstates[i].contiguous = 0;
        }
    }

    /* Advance samples_seen */
    st->samples_seen += frames;

    /* Note: we don't modify the audio buffer; we are a passive detector. */
    return 0;
}

/* Module load/unload */
static int module_init(void)
{
    int err;

    /* set pointers */
    td_filter.encupdh = enc_update;
    td_filter.ench    = enc_encode;

    err = aufilt_register(baresip_aufiltl(), &td_filter);
    if (err) {
        TD_DBG("failed to register aufilt: %d\n", err);
        return err;
    }
    TD_DBG("tone_detect module loaded\n");
    return 0;
}

static int module_close(void)
{
    aufilt_unregister(baresip_aufiltl(), &td_filter);
    TD_DBG("tone_detect module unloaded\n");
    return 0;
}

/* module exports */
const struct mod_export DECL_EXPORTS(tone_detect) = {
    "tone_detect",
    "module",
    module_init,
    module_close
};

