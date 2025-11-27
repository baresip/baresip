/**
 * tonedetect.c
 *
 * Baresip audio filter module that detects configured pure-tone frequencies
 * and prints timestamps when they occur.
 *
 * Fully compatible with modern Baresip.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

static const double detect_freqs[] = {440.0, 550.0, 660.0};
static const size_t n_detect_freqs = sizeof(detect_freqs)/sizeof(detect_freqs[0]);
static const double energy_threshold_ratio = 0.15;
static const size_t min_contiguous_frames = 2;

#define TD_DBG(...) fprintf(stderr, "[tonedetect] " __VA_ARGS__)

struct goertzel_state {
    double target_freq;
    double coeff;
    double q1;
    double q2;
    int last_detected;
    size_t contiguous;
};

struct td_enc {
    struct aufilt_enc_st af;
    unsigned samplerate;
    unsigned channels;
    uint64_t samples_seen;
    struct goertzel_state *gstates;
    size_t n_gstates;
};

/* Forward declarations */
static int enc_update(struct aufilt_enc_st **stp, void **ctx,
                      const struct aufilt *af, struct aufilt_prm *prm,
                      const struct audio *au);
static int enc_encode(struct aufilt_enc_st *st, struct auframe *aframe);

/* Filter definition */
static struct aufilt td_filter = {
    .le = LE_INIT,
    .name = "tonedetect",
    .encupdh = NULL,
    .ench = NULL,
    .decupdh = NULL,
    .dech = NULL
};

/* Allocate Goertzel states */
static struct goertzel_state *goertzel_states_alloc(unsigned sr, size_t *out_n)
{
    size_t i;
    struct goertzel_state *gs = (struct goertzel_state *)malloc(n_detect_freqs * sizeof(*gs));
    if (!gs) return NULL;

    for (i = 0; i < n_detect_freqs; ++i) {
        double f = detect_freqs[i];
        double normalized = 2.0 * M_PI * f / (double)sr;
        gs[i].target_freq = f;
        gs[i].coeff = 2.0 * cos(normalized);
        gs[i].q1 = gs[i].q2 = 0.0;
        gs[i].last_detected = 0;
        gs[i].contiguous = 0;
    }
    *out_n = n_detect_freqs;
    return gs;
}

/* Process a single sample through Goertzel states */
static void goertzel_process_sample(struct goertzel_state *gs, size_t n, double x)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        double q0 = gs[i].coeff * gs[i].q1 - gs[i].q2 + x;
        gs[i].q2 = gs[i].q1;
        gs[i].q1 = q0;
    }
}

/* Compute energies */
static void goertzel_compute_energies(struct goertzel_state *gs, size_t n, double *out_energy)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        double power = gs[i].q1 * gs[i].q1 + gs[i].q2 * gs[i].q2 - gs[i].coeff * gs[i].q1 * gs[i].q2;
        if (power < 0.0) power = 0.0;
        out_energy[i] = power;
    }
}

/* Encoder update / initialization */
static int enc_update(struct aufilt_enc_st **stp, void **ctx,
                      const struct aufilt *af, struct aufilt_prm *prm,
                      const struct audio *au)
{
    struct td_enc *st;
    (void)ctx; (void)af; (void)au;

    if (!stp || !prm) return EINVAL;

    st = (struct td_enc *)mem_zalloc(sizeof(*st), NULL);
    if (!st) return ENOMEM;

    st->samplerate = prm->srate ? prm->srate : 8000;
    st->channels = prm->ch ? prm->ch : 1;
    st->samples_seen = 0;
    st->gstates = goertzel_states_alloc(st->samplerate, &st->n_gstates);
    if (!st->gstates) {
        mem_deref(st);
        return ENOMEM;
    }

    td_filter.encupdh = enc_update;
    td_filter.ench = enc_encode;

    *stp = (struct aufilt_enc_st *)st;
    TD_DBG("tonedetect: encoder initialized (sr=%u ch=%u)\n", st->samplerate, st->channels);
    return 0;
}

/* Encoder process hook */
static int enc_encode(struct aufilt_enc_st *afst, struct auframe *aframe)
{
    struct td_enc *st = (struct td_enc *)afst;
    if (!st || !aframe) return EINVAL;

    const int16_t *samples = auframe_s16(aframe);
    size_t frames = auframe_sampc(aframe) / st->channels;
    size_t i, ch;

    struct goertzel_state *gtemp = (struct goertzel_state *)malloc(st->n_gstates * sizeof(*gtemp));
    double *energies = (double *)malloc(st->n_gstates * sizeof(*energies));
    if (!gtemp || !energies) {
        free(gtemp); free(energies);
        return ENOMEM;
    }

    for (i = 0; i < st->n_gstates; ++i) gtemp[i] = st->gstates[i];

    for (i = 0; i < frames; ++i) {
        double sample_mono = 0.0;
        for (ch = 0; ch < st->channels; ++ch) {
            int16_t s = samples[i * st->channels + ch];
            sample_mono += (double)s;
        }
        sample_mono /= st->channels;
        double x = sample_mono / 32768.0;
        goertzel_process_sample(gtemp, st->n_gstates, x);
    }

    goertzel_compute_energies(gtemp, st->n_gstates, energies);

    double max_e = 1e-12;
    for (i = 0; i < st->n_gstates; ++i)
        if (energies[i] > max_e) max_e = energies[i];

    for (i = 0; i < st->n_gstates; ++i) {
        double ratio = energies[i] / max_e;
        int detected = (ratio >= energy_threshold_ratio) ? 1 : 0;

        if (detected)
            gtemp[i].contiguous++;
        else
            gtemp[i].contiguous = 0;

        if (!st->gstates[i].last_detected && gtemp[i].contiguous >= min_contiguous_frames) {
            double ts = (double)st->samples_seen / (double)st->samplerate;
            TD_DBG("Detected %.1f Hz at %.6f sec (energy ratio %.3f)\n",
                   gtemp[i].target_freq, ts, ratio);
            st->gstates[i].last_detected = 1;
        }

        if (!detected && st->gstates[i].last_detected) {
            st->gstates[i].last_detected = 0;
            st->gstates[i].contiguous = 0;
        }
    }

    st->samples_seen += frames;
    free(gtemp);
    free(energies);
    return 0;
}

/* Module load/unload */
static int module_init(void)
{
    td_filter.encupdh = enc_update;
    td_filter.ench = enc_encode;

    aufilt_register(baresip_aufiltl(), &td_filter);

    TD_DBG("tonedetect module loaded\n");
    return 0;
}

static int module_close(void)
{
    aufilt_unregister(&td_filter);
    TD_DBG("tonedetect module unloaded\n");
    return 0;
}

/* module exports */
const struct mod_export DECL_EXPORTS(tonedetect) = {
    "tonedetect",
    "module",
    module_init,
    module_close
};
