/**
 * @file audio.c  OpenAI Realtime API - Audio Driver Implementation
 *
 * This module implements a virtual audio device that:
 * - Captures audio from SIP calls and sends it to OpenAI Realtime API
 * - Receives audio from OpenAI and injects it back into SIP calls
 * - Uses minimal buffering for low latency
 * - Works as a proper baresip audio driver
 */

#include "openai_rt.h"
#include "ai_model.h"
#include <re.h>
#include <re_atomic.h>
#include <re_thread.h>
#include <rem.h>
#include <baresip.h>
#include <pthread.h>


/* Forward declarations */
static void ausrc_destructor(void *arg);
static void auplay_destructor(void *arg);
static int ausrc_read_thread(void *arg);
static int auplay_write_thread(void *arg);
static void free_audio_queue(struct list *queue, mtx_t *mutex);
static void free_event_queue(struct list *queue, mtx_t *mutex);
static int safe_clear_buffer(struct mbuf *buffer, const char *buffer_name);
static void send_audio_commit(void);
static void force_audio_commit(void);
static void maybe_shrink_injection_buffer(void);
static int audio_init_resamplers(uint32_t input_srate, uint32_t output_srate);

/* Audio source structure - provides audio to baresip */
struct ausrc_st {
    struct ausrc_prm prm;
    ausrc_read_h *rh;        /* Baresip's callback - we call this when we have audio */
    void *arg;                /* Baresip's argument */
    size_t sampc;             /* Samples per frame */
    bool ready;               /* Ready to provide audio */
    uint64_t total_samples_sent;
    thrd_t thread;            /* Thread for continuous audio provision */
    int run;                  /* Thread run flag */
    int16_t *sampv;          /* Audio buffer */
    mtx_t mutex;              /* Mutex for thread safety */
};

/* Audio playback structure - receives audio from baresip */
struct auplay_st {
    struct auplay_prm prm;
    auplay_write_h *wh;      /* Baresip's callback - called when baresip has audio */
    void *arg;                /* Baresip's argument */
    size_t sampc;             /* Samples per frame */
    bool ready;               /* Ready to receive audio */
    uint64_t total_samples_received;
    thrd_t thread;            /* Thread for continuous audio processing */
    int run;                  /* Thread run flag */
    int16_t *sampv;          /* Audio buffer */
    mtx_t mutex;              /* Mutex for thread safety */
};

/* Check for pending call start events and start threads if needed */
static void check_and_start_pending_call(void)
{
    /* We need both the playback driver (for receiving audio from call) and 
       the source driver (for injecting audio back into call) to be ready */
    if (g_audio.play_st && g_audio.src_st && g_oairt.call_active && 
        g_audio.play_st->ready && g_audio.src_st->ready) {
        DEBUG_INFO("Both audio drivers ready and call active - starting threads\n");
        audio_restart_threads();
        return;
    }
    
    /* Also check if there are any pending call start events in the queue */
    mtx_lock(&g_audio.event_queue_mutex);
    struct le *le = list_head(&g_audio.event_queue);
    while (le) {
        struct audio_event *event = list_ledata(le);
        if (event && event->type == EVENT_CALL_START) {
            DEBUG_INFO("Found pending call start event, starting threads\n");
            mtx_unlock(&g_audio.event_queue_mutex);
            audio_restart_threads();
            return;
        }
        le = le->next;
    }
    mtx_unlock(&g_audio.event_queue_mutex);
}

/* Global audio state */
struct audio_state g_audio = {0};

/* Initialize audio subsystem */
int audio_init(void)
{   
    /* Initialize audio buffers */
    g_audio.buffer_size = 160;  /* 160 samples = 20ms at 24kHz */
    g_audio.g711u_input_buffer = mbuf_alloc(g_audio.buffer_size);
    if (!g_audio.g711u_input_buffer) {
        warning("openai_rt: Failed to allocate input buffer\n");
        return ENOMEM;
    }
    
    g_audio.g711u_output_buffer = mbuf_alloc(g_audio.buffer_size);
    if (!g_audio.g711u_output_buffer) {
        warning("openai_rt: Failed to allocate output buffer\n");
        mem_deref(g_audio.g711u_input_buffer);
        return ENOMEM;
    }
    
    /* Initialize audio accumulation tracking */
    g_audio.audio_accumulated = 0;
    g_audio.commit_threshold = AUDIO_COMMIT_THRESHOLD;
    g_audio.response_created = false;
    
    /* Initialize circular buffer for smooth audio injection */
    g_audio.injection_buffer_size = INJECTION_BUFFER_INITIAL_SIZE;
    g_audio.injection_buffer = mem_alloc(g_audio.injection_buffer_size * sizeof(int16_t), NULL);
    if (!g_audio.injection_buffer) {
        warning("openai_rt: Failed to allocate injection buffer\n");
        goto cleanup;
    }
    
    DEBUG_INFO("Injection buffer initialized: %zu samples (%zu ms at 24kHz)\n", 
               g_audio.injection_buffer_size, g_audio.injection_buffer_size * 1000 / 24000);
    
    /* Initialize buffer state */
    g_audio.injection_read_pos = 0;
    g_audio.injection_write_pos = 0;
    g_audio.injection_available = 0;
    
    /* Initialize injection buffer mutex */
    int err = mtx_init(&g_audio.injection_buffer_mutex, mtx_plain);
    if (err != thrd_success) {
        warning("openai_rt: Failed to initialize injection buffer mutex: %d\n", err);
        goto cleanup;
    }
    
    /* Initialize queues */
    list_init(&g_audio.read_queue);
    list_init(&g_audio.write_queue);
    list_init(&g_audio.event_queue);
    
    /* Initialize mutexes */
    err = mtx_init(&g_audio.read_queue_mutex, mtx_plain);
    if (err != thrd_success) {
        warning("openai_rt: Failed to initialize read queue mutex: %d\n", err);
        goto cleanup;
    }
    
    err = mtx_init(&g_audio.write_queue_mutex, mtx_plain);
    if (err != thrd_success) {
        warning("openai_rt: Failed to initialize write queue mutex: %d\n", err);
        goto cleanup;
    }
    
    err = mtx_init(&g_audio.event_queue_mutex, mtx_plain);
    if (err != thrd_success) {
        warning("openai_rt: Failed to initialize event queue mutex: %d\n", err);
        goto cleanup;
    }
    
    /* Initialize condition variables */
    err = cnd_init(&g_audio.read_queue_cond);
    if (err != thrd_success) {
        warning("openai_rt: Failed to initialize read queue condition: %d\n", err);
        goto cleanup;
    }
    
    err = cnd_init(&g_audio.write_queue_cond);
    if (err != thrd_success) {
        warning("openai_rt: Failed to initialize write queue condition: %d\n", err);
        goto cleanup;
    }
    
    err = cnd_init(&g_audio.event_queue_cond);
    if (err != thrd_success) {
        warning("openai_rt: Failed to initialize event queue condition: %d\n", err);
        goto cleanup;
    }
    
    /* Initialize resamplers - will be set up when audio drivers are allocated */
    g_audio.resamplers_initialized = false;
    g_audio.tx_resamp_buffer = NULL;
    g_audio.rx_resamp_buffer = NULL;
    g_audio.tx_resamp_buffer_size = 0;
    g_audio.rx_resamp_buffer_size = 0;
    g_audio.configured_srate = 0;  /* Will be set when audio drivers are allocated */
    
    DEBUG_INFO("Audio subsystem initialized with %zu byte buffers and queues\n", g_audio.buffer_size);
    return 0;

cleanup:
    /* Clean up on error - mem_deref handles NULL safely, but mutexes/conds need to be destroyed if initialized */
    /* Destroy mutexes and condition variables (they're initialized before resampler setup) */
    mtx_destroy(&g_audio.read_queue_mutex);
    mtx_destroy(&g_audio.write_queue_mutex);
    mtx_destroy(&g_audio.event_queue_mutex);
    mtx_destroy(&g_audio.injection_buffer_mutex);
    cnd_destroy(&g_audio.read_queue_cond);
    cnd_destroy(&g_audio.write_queue_cond);
    cnd_destroy(&g_audio.event_queue_cond);
    mem_deref(g_audio.g711u_input_buffer);
    mem_deref(g_audio.g711u_output_buffer);
    mem_deref(g_audio.injection_buffer);
    mem_deref(g_audio.tx_resamp_buffer);
    mem_deref(g_audio.rx_resamp_buffer);
    /* Note: src_st and play_st are not allocated during init, so no need to free them here */
    return ENOMEM;
}

/* Initialize resamplers for converting between configured rate and backend rate */
static int audio_init_resamplers(uint32_t input_srate, uint32_t output_srate)
{
    int err;
    
    /* If rates are the same, no resampling needed */
    if (input_srate == output_srate) {
        g_audio.resamplers_initialized = false;
        return 0;
    }
    
    /* Initialize resamplers */
    auresamp_init(&g_audio.tx_resamp);
    auresamp_init(&g_audio.rx_resamp);
    
    /* Setup TX resampler: configured rate -> backend rate (mono) */
    err = auresamp_setup(&g_audio.tx_resamp, input_srate, 1, output_srate, 1);
    if (err) {
        warning("openai_rt: Failed to setup TX resampler (%u Hz -> %u Hz): %m\n", 
               input_srate, output_srate, err);
        g_audio.resamplers_initialized = false;
        return err;
    }
    
    /* Setup RX resampler: backend rate -> configured rate (mono) */
    err = auresamp_setup(&g_audio.rx_resamp, output_srate, 1, input_srate, 1);
    if (err) {
        warning("openai_rt: Failed to setup RX resampler (%u Hz -> %u Hz): %m\n", 
               output_srate, input_srate, err);
        g_audio.resamplers_initialized = false;
        return err;
    }
    
    /* Allocate resampler buffers (enough for 20ms at target rates) */
    g_audio.tx_resamp_buffer_size = output_srate * 1 * 20 / 1000; /* 20ms @ output rate */
    g_audio.tx_resamp_buffer = mem_alloc(g_audio.tx_resamp_buffer_size * sizeof(int16_t), NULL);
    if (!g_audio.tx_resamp_buffer) {
        warning("openai_rt: Failed to allocate TX resampler buffer\n");
        g_audio.resamplers_initialized = false;
        return ENOMEM;
    }
    
    g_audio.rx_resamp_buffer_size = input_srate * 1 * 20 / 1000; /* 20ms @ input rate */
    g_audio.rx_resamp_buffer = mem_alloc(g_audio.rx_resamp_buffer_size * sizeof(int16_t), NULL);
    if (!g_audio.rx_resamp_buffer) {
        warning("openai_rt: Failed to allocate RX resampler buffer\n");
        g_audio.tx_resamp_buffer = mem_deref(g_audio.tx_resamp_buffer);
        g_audio.tx_resamp_buffer_size = 0;
        g_audio.resamplers_initialized = false;
        return ENOMEM;
    }
    
    g_audio.resamplers_initialized = true;
    info("openai_rt: Resamplers initialized (%u Hz <-> %u Hz)\n", input_srate, output_srate);
    return 0;
}

/* Clean up audio subsystem */
void audio_close(void)
{
    /* Always clean up, regardless of whether audio threads were active */
    
    /* Stop audio threads if they exist */
    if (g_audio.src_st && g_audio.src_st->thread) {
        /* Log queue sizes before cleanup */
        DEBUG_INFO("Cleaning up audio system - read queue: %u, event queue: %u\n",
                   list_count(&g_audio.read_queue), list_count(&g_audio.event_queue));
        
        /* Properly free all remaining audio frames and events in queues */
        free_audio_queue(&g_audio.read_queue, &g_audio.read_queue_mutex);
        free_event_queue(&g_audio.event_queue, &g_audio.event_queue_mutex);
    }
    
    /* Free resampler buffers first (before destroying mutexes) */
    g_audio.tx_resamp_buffer = mem_deref(g_audio.tx_resamp_buffer);
    g_audio.rx_resamp_buffer = mem_deref(g_audio.rx_resamp_buffer);
    g_audio.resamplers_initialized = false;
    
    /* Always clean up synchronization objects and buffers */
    /* Note: Only destroy if they were initialized - but we can't check, so we assume they were */
    /* If audio_init() failed early, these might not be initialized, but destroying uninitialized */
    /* mutexes/conds is undefined behavior. We'll rely on the fact that audio_init() should either */
    /* succeed completely or fail before initializing these. */
    mtx_destroy(&g_audio.read_queue_mutex);
    mtx_destroy(&g_audio.write_queue_mutex);
    mtx_destroy(&g_audio.event_queue_mutex);
    mtx_destroy(&g_audio.injection_buffer_mutex);
    cnd_destroy(&g_audio.read_queue_cond);
    cnd_destroy(&g_audio.write_queue_cond);
    cnd_destroy(&g_audio.event_queue_cond);
    
    /* Free G711u buffers */
    g_audio.g711u_input_buffer = mem_deref(g_audio.g711u_input_buffer);
    g_audio.g711u_output_buffer = mem_deref(g_audio.g711u_output_buffer);
    
    /* Free injection buffer */
    g_audio.injection_buffer = mem_deref(g_audio.injection_buffer);
    
    /* Free audio structures - only if they were allocated */
    if (g_audio.src_st) {
        g_audio.src_st = mem_deref(g_audio.src_st);
    }
    if (g_audio.play_st) {
        g_audio.play_st = mem_deref(g_audio.play_st);
    }
    
    DEBUG_INFO("Audio subsystem closed\n");
}

void handle_incoming_audio(const int16_t *s16_data, size_t sampc)
{
    struct ai_model *model = get_ai_model();
    char *json_msg = NULL;
    int err;

    if (!s16_data || sampc == 0) {
        DEBUG_INFO("handle_incoming_audio: No audio data or zero samples\n");
        return;
    }
    
    if (!g_oairt.call_active) {
        DEBUG_INFO("handle_incoming_audio: Call not active, dropping audio\n");
        return;
    }
    
    if (!model || !model->build_audio_append) {
        warning("openai_rt: AI model not initialized\n");
        return;
    }

    const int16_t *audio_to_encode = s16_data;
    size_t samples_to_encode = sampc;
    size_t byte_len;
    
    /* Resample if needed (configured rate -> backend rate) */
    if (g_audio.resamplers_initialized) {
        /* Determine backend rate */
        uint32_t backend_rate = (g_oairt.backend_type == AI_BACKEND_GEMINI_LIVE) ? 16000 : 24000;
        
        /* Ensure buffer is large enough (round up) */
        size_t max_output_samples = (sampc * backend_rate + g_audio.configured_srate - 1) / g_audio.configured_srate;
        if (max_output_samples > g_audio.tx_resamp_buffer_size) {
            g_audio.tx_resamp_buffer = mem_deref(g_audio.tx_resamp_buffer);
            g_audio.tx_resamp_buffer = mem_alloc(max_output_samples * sizeof(int16_t), NULL);
            if (!g_audio.tx_resamp_buffer) {
                warning("openai_rt: Failed to reallocate TX resampler buffer\n");
                return;
            }
            g_audio.tx_resamp_buffer_size = max_output_samples;
        }
        
        /* Perform resampling */
        size_t output_samples = g_audio.tx_resamp_buffer_size;
        err = auresamp(&g_audio.tx_resamp, g_audio.tx_resamp_buffer, &output_samples,
                      s16_data, sampc);
        if (err) {
            warning("openai_rt: TX resampling failed: %m\n", err);
            return;
        }
        
        audio_to_encode = g_audio.tx_resamp_buffer;
        samples_to_encode = output_samples;
        info("[AUDIO TX] Resampled %zu samples (%u Hz) -> %zu samples (%u Hz)\n",
             sampc, g_audio.configured_srate, output_samples, backend_rate);
    }
    
    byte_len = samples_to_encode * sizeof(int16_t);
    info("[AUDIO TX] Received %zu samples (%zu bytes) from pipeline\n", samples_to_encode, byte_len);
    
    char *b64 = encode_audio_base64((const uint8_t *)audio_to_encode, byte_len);
    if (!b64) {
        warning("openai_rt: base64 encode failed for %zu bytes\n", byte_len);
        return;
    }

    size_t b64_len = str_len(b64);
    info("[AUDIO TX] Base64 encoded %zu bytes to %zu chars\n", byte_len, b64_len);

    err = model->build_audio_append(b64, &json_msg);
    mem_deref(b64);

    if (err) {
        warning("openai_rt: Failed to build audio append message: %m\n", err);
        return;
    }

    if (json_msg) {
        size_t msg_len = str_len(json_msg);
        info("[AUDIO TX] Built audio message (%zu bytes), queuing to WebSocket (ws_state=%d, ws_client=%p)\n",
                   msg_len, g_oairt.ws_state, g_oairt.ws_client);
        
        err = queue_message_to_openai(json_msg, msg_len, NULL, NULL);
        if (err) {
            warning("openai_rt: Failed to queue PCM audio: %m\n", err);
        } else {
            info("[AUDIO TX] Audio message queued successfully (total queued: %zu bytes)\n", g_audio.audio_accumulated + byte_len);
            g_audio.audio_accumulated += byte_len;
            if (g_audio.audio_accumulated >= g_audio.commit_threshold) {
                if (g_oairt.session_cfg_applied) {
                    info("[AUDIO TX] Threshold reached, sending commit\n");
                    send_audio_commit();
                } else {
                    DEBUG_INFO("Skipping commit - session not ready yet\n");
                }
                g_audio.audio_accumulated = 0;
            }
        }
        mem_deref(json_msg);
    }
}

/* Handle outgoing audio from OpenAI - convert from G711u and buffer for injection */
void handle_outgoing_audio(const uint8_t *g711u_data, size_t len)
{
    if (!g711u_data || len == 0 || !g_oairt.call_active || !g_audio.g711u_output_buffer) {
        return;
    }

    if (!g_audio.src_st || !g_audio.src_st->ready) {
        return;
    }

    mtx_lock(&g_audio.src_st->mutex);
    
    /* Clear buffer if it's getting too full */
    if (mbuf_get_left(g_audio.g711u_output_buffer) + len > g_audio.buffer_size) {
        safe_clear_buffer(g_audio.g711u_output_buffer, "output");
    }
    
    /* Write audio data to buffer */
    if (mbuf_get_space(g_audio.g711u_output_buffer) >= len) {
        mbuf_write_mem(g_audio.g711u_output_buffer, g711u_data, len);
    }
    
    mtx_unlock(&g_audio.src_st->mutex);
}

/* Audio source destructor */
static void ausrc_destructor(void *arg)
{
    struct ausrc_st *st = arg;

    if (st) {
        st->ready = false;
        st->run = false;
        
        /* Signal condition variable to wake up waiting thread */
        mtx_lock(&g_audio.read_queue_mutex);
        cnd_signal(&g_audio.read_queue_cond);
        mtx_unlock(&g_audio.read_queue_mutex);
        
        /* Wait for thread to finish */
        if (st->thread) {
            thrd_join(st->thread, NULL);
            st->thread = 0;
        }
        
        /* Free audio buffer */
        st->sampv = mem_deref(st->sampv);
        
        /* Destroy mutex */
        mtx_destroy(&st->mutex);
        
        /* Clear global reference */
        if (g_audio.src_st == st) {
            g_audio.src_st = NULL;
        }
        
        DEBUG_INFO("Audio source destructor completed\n");
    }
}

/* Audio playback destructor */
static void auplay_destructor(void *arg)
{
    struct auplay_st *st = arg;

    if (st) {
        st->ready = false;
        st->run = false;
        
        /* Signal condition variable to wake up waiting thread */
        mtx_lock(&g_audio.write_queue_mutex);
        cnd_signal(&g_audio.write_queue_cond);
        mtx_unlock(&g_audio.write_queue_mutex);
        
        /* Wait for thread to finish */
        if (st->thread) {
            thrd_join(st->thread, NULL);
            st->thread = 0;
        }
        
        /* Free audio buffer */
        st->sampv = mem_deref(st->sampv);
        
        /* Destroy mutex */
        mtx_destroy(&st->mutex);
        
        if (g_audio.play_st == st) {
            g_audio.play_st = NULL;
        }
        
        DEBUG_INFO("Audio playback destructor completed\n");
    }
}

/* Read audio data from the circular injection buffer */
static int read_from_injection_buffer(int16_t *samples, size_t max_samples)
{
    if (!samples || max_samples == 0) return EINVAL;
    
    mtx_lock(&g_audio.injection_buffer_mutex);
    
    size_t samples_to_read = (g_audio.injection_available < max_samples) ? 
                             g_audio.injection_available : max_samples;
    
    if (samples_to_read == 0) {
        mtx_unlock(&g_audio.injection_buffer_mutex);
        return 0; /* No samples available */
    }
    
    /* Read samples from buffer */
    for (size_t i = 0; i < samples_to_read; i++) {
        samples[i] = g_audio.injection_buffer[g_audio.injection_read_pos];
        g_audio.injection_read_pos = (g_audio.injection_read_pos + 1) % g_audio.injection_buffer_size;
        g_audio.injection_available--;
    }
    
    mtx_unlock(&g_audio.injection_buffer_mutex);
    
    /* Periodically try to shrink the buffer if it's mostly empty */
    if ((samples_to_read > 0) && (g_audio.injection_available < g_audio.injection_buffer_size / 8)) {
        maybe_shrink_injection_buffer();
    }
    
    return (int)samples_to_read;
}

/* audio.c */
static int ausrc_read_thread(void *arg)
{
    struct ausrc_st *st = arg;
    if (!st || !st->ready) return 0;

    DEBUG_INFO("Audio source thread started (srate=%u, ch=%u, ptime=%u, sampc=%zu)\n",
               st->prm.srate, st->prm.ch, st->prm.ptime, st->sampc);

    const uint32_t ptime_ms = st->prm.ptime ? st->prm.ptime : 20;
    uint64_t slot_due = tmr_jiffies();

    st->run = true;
    while (st->run && st->ready) {

        if (!g_oairt.call_active) {
            DEBUG_INFO("Audio source thread stopping - call ended\n");
            break;
        }

        /* ---- build exactly sampc samples for this tick ---- */
        size_t have = 0;

        if (audio_source_ready_for_injection()) {
            int got = read_from_injection_buffer(st->sampv, st->sampc);
            if (got > 0) have = (size_t)got;
        }

        if (have < st->sampc) {
            struct audio_frame *frame = NULL;
            mtx_lock(&g_audio.read_queue_mutex);
            if (!list_isempty(&g_audio.read_queue)) {
                frame = list_ledata(list_head(&g_audio.read_queue));
                list_unlink(&frame->le);
            }
            mtx_unlock(&g_audio.read_queue_mutex);

            if (frame) {
                size_t take = frame->sampc;
                if (take > st->sampc - have) take = st->sampc - have;
                memcpy(st->sampv + have, frame->sampv, take * sizeof(int16_t));
                have += take;
                mem_deref(frame->sampv);
                mem_deref(frame);
            }
        }

        if (have < st->sampc) {
            memset(st->sampv + have, 0, (st->sampc - have) * sizeof(int16_t));
        } else if (have > st->sampc) {
            have = st->sampc;
        }

        struct auframe af;
        auframe_init(&af, st->prm.fmt, st->sampv, st->sampc, st->prm.srate, st->prm.ch);

        st->rh(&af, st->arg);
        st->total_samples_sent += st->sampc;

        /* Realtime pacing with drift correction */
        slot_due += ptime_ms;
        uint64_t now = tmr_jiffies();

        if (now < slot_due) {
            sys_msleep((int)(slot_due - now));
        } else {
            uint64_t lag = now - slot_due;
            uint64_t missed = lag / ptime_ms + 1;
            slot_due += missed * ptime_ms;
        }
    }

    DEBUG_INFO("Audio source thread ended\n");
    return 0;
}

/* Audio playback write thread - continuously calls st->wh() to get audio from baresip */
static int auplay_write_thread(void *arg)
{
    struct auplay_st *st = arg;
    
    if (!st || !st->ready) {
        return 0;
    }
    
    DEBUG_INFO("Audio playback thread started\n");
    
    /* Create a single auframe ONCE at the beginning (like WASAPI/ALSA) */
    struct auframe af;
    auframe_init(&af, st->prm.fmt, st->sampv, st->sampc,
               st->prm.srate, st->prm.ch);
    
    while (st->run && st->ready) {
        /* Check if call is still active - stop if not */
        if (!g_oairt.call_active) {
            DEBUG_INFO("Audio playback thread stopping - call ended\n");
            break;
        }
        
        /* Wait for audio data from the SIP call */
        /* This is the key: we need to wait for actual audio, not return immediately */
        if (st->wh) {
            /* Call baresip's write handler - this should provide audio data */
            st->wh(&af, st->arg);
            st->total_samples_received += af.sampc;
            
            /* Process the audio data from the populated frame */
            if (af.sampc > 0) {
                openai_rt_receive_audio(af.sampv, af.sampc);
            }
        } else {
            DEBUG_INFO("No write handler available\n");
        }
        
        /* Sleep to maintain proper timing - this prevents the 1ms loop */
        sys_msleep(st->prm.ptime);
    }
    
    DEBUG_INFO("Audio playback thread ended\n");
    return 0;
}

/* Audio source allocation - provides audio to baresip */
int openai_rt_ausrc_alloc(struct ausrc_st **stp, const struct ausrc *as,
                          struct ausrc_prm *prm, const char *dev,
                          ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
    struct ausrc_st *st;

    (void)as;
    (void)dev;
    (void)errh;

    info("openai_rt: opening audio source\n");

    st = mem_zalloc(sizeof(*st), ausrc_destructor);
    if (!st) {
        return ENOMEM;
    }

    /* Store baresip's callback - we'll call this when we have audio */
    st->rh = rh;
    st->arg = arg;
    
    /* Use the parameters baresip provides, but ensure compatibility */
    st->prm = *prm;
    if (st->prm.fmt != AUFMT_S16LE) {
        warning("openai_rt: Forcing S16LE format (was %s)\n", aufmt_name(st->prm.fmt));
        st->prm.fmt = AUFMT_S16LE;
    }
    
    /* Store configured sample rate and set up resamplers if needed */
    g_audio.configured_srate = st->prm.srate;
    info("openai_rt: Audio source configured with sample rate: %u Hz\n", st->prm.srate);
    
    /* Initialize resamplers if configured rate doesn't match backend requirement */
    if (g_oairt.backend_type == AI_BACKEND_GEMINI_LIVE && st->prm.srate != 16000) {
        /* Gemini requires 16kHz - need resampling if configured rate is different */
        int resamp_err = audio_init_resamplers(st->prm.srate, 16000);
        if (resamp_err) {
            warning("openai_rt: Failed to initialize resamplers for Gemini (configured: %u Hz, required: 16000 Hz)\n", 
                   st->prm.srate);
        }
    } else if (g_oairt.backend_type == AI_BACKEND_OPENAI_REALTIME && st->prm.srate != 24000) {
        /* OpenAI requires 24kHz - need resampling if configured rate is different */
        int resamp_err = audio_init_resamplers(st->prm.srate, 24000);
        if (resamp_err) {
            warning("openai_rt: Failed to initialize resamplers for OpenAI (configured: %u Hz, required: 24000 Hz)\n", 
                   st->prm.srate);
        }
    } else {
        /* No resampling needed - configured rate matches backend requirement */
        info("openai_rt: No resampling needed (configured rate %u Hz matches backend requirement)\n", 
             st->prm.srate);
    }
    
    /* Ensure reasonable packet time (20ms is typical) */
    if (st->prm.ptime < 10 || st->prm.ptime > 100) {
        warning("openai_rt: Adjusting packet time from %u to 20ms\n", st->prm.ptime);
        st->prm.ptime = 20;
    }
    
    st->sampc = st->prm.srate * st->prm.ch * st->prm.ptime / 1000;
    st->total_samples_sent = 0;
    st->ready = true;
    st->sampv = mem_alloc(st->sampc * sizeof(int16_t), NULL);
    if (!st->sampv) {
        mem_deref(st);
        return ENOMEM;
    }
    
    DEBUG_INFO("Audio source configured: srate=%u, ch=%u, ptime=%u, sampc=%zu\n", 
               st->prm.srate, st->prm.ch, st->prm.ptime, st->sampc);
    
    /* Initialize mutex for thread safety */
    int err = mtx_init(&st->mutex, mtx_plain);
    if (err != thrd_success) {
        warning("openai_rt: Failed to initialize audio source mutex: %d\n", err);
        mem_deref(st->sampv);
        mem_deref(st);
        return ENOMEM;
    }

    /* Register this source instance */
    g_audio.src_st = st;

    /* Don't start thread immediately - wait for call to be active */
    st->run = false; /* Will be set to true when call starts */
    st->thread = 0;  /* No thread yet */

    DEBUG_INFO("Audio source allocated, ready to provide audio (call_active=%d)\n", g_oairt.call_active);
    
    /* Check for pending call start events and start threads if needed */
    check_and_start_pending_call();
    
    *stp = st;
    return 0;
}

/* Audio playback allocation - receives audio from baresip */
int openai_rt_auplay_alloc(struct auplay_st **stp, const struct auplay *ap,
                           struct auplay_prm *prm, const char *dev,
                           auplay_write_h *wh, void *arg)
{
    struct auplay_st *st;

    (void)ap;
    (void)dev;

    info("openai_rt: opening audio playback\n");

    st = mem_zalloc(sizeof(*st), auplay_destructor);
    if (!st) {
        return ENOMEM;
    }

    /* Store baresip's callback - baresip will call this when it has audio */
    st->wh = wh;
    st->arg = arg;
    
    /* Use the parameters baresip provides, but ensure compatibility */
    st->prm = *prm;
    if (st->prm.fmt != AUFMT_S16LE) {
        warning("openai_rt: Forcing S16LE format (was %s)\n", aufmt_name(st->prm.fmt));
        st->prm.fmt = AUFMT_S16LE;
    }
    
    /* Store configured sample rate if not already set (should match ausrc) */
    if (g_audio.configured_srate == 0) {
        g_audio.configured_srate = st->prm.srate;
        info("openai_rt: Audio playback configured with sample rate: %u Hz\n", st->prm.srate);
    }
    
    st->sampc = st->prm.srate * st->prm.ch * st->prm.ptime / 1000;
    st->total_samples_received = 0;
    st->ready = true;
    st->sampv = mem_alloc(st->sampc * sizeof(int16_t), NULL);
    if (!st->sampv) {
        mem_deref(st);
        return ENOMEM;
    }
    
    /* Initialize mutex for thread safety */
    int err = mtx_init(&st->mutex, mtx_plain);
    if (err != thrd_success) {
        warning("openai_rt: Failed to initialize audio playback mutex: %d\n", err);
        mem_deref(st->sampv);
        mem_deref(st);
        return err;
    }

    /* Register this playback instance */
    g_audio.play_st = st;

    /* Don't start thread immediately - wait for call to be active */
    st->run = false; /* Will be set to true when call starts */
    st->thread = 0;  /* No thread yet */

    DEBUG_INFO("Audio playback allocated, ready to receive audio (call_active=%d)\n", g_oairt.call_active);
    
    /* Check for pending call start events and start threads if needed */
    check_and_start_pending_call();
    
    *stp = st;
    return 0;
}

/* Check for and process incoming messages from OpenAI */
void openai_rt_check_messages(void)
{
    /* This function will be called periodically to check for messages */
    /* The actual message processing is now handled in the WebSocket thread */
    /* This is just a placeholder for future integration if needed */
}

/* Stop audio threads when call ends */
void audio_stop_threads(void)
{
    
    /* Properly free all audio frames in queues to prevent memory leaks */
    free_audio_queue(&g_audio.read_queue, &g_audio.read_queue_mutex);
    free_event_queue(&g_audio.event_queue, &g_audio.event_queue_mutex);
    
    /* Clear G711u buffers to prevent stale audio data */
    if (g_audio.g711u_input_buffer) {
        int clear_result = safe_clear_buffer(g_audio.g711u_input_buffer, "input");
        if (clear_result) {
            warning("openai_rt: Failed to clear input buffer: %m\n", clear_result);
        }
    }
    if (g_audio.g711u_output_buffer) {
        int clear_result = safe_clear_buffer(g_audio.g711u_output_buffer, "output");
        if (clear_result) {
            warning("openai_rt: Failed to clear output buffer: %m\n", clear_result);
        }
    }
    
    /* Stop audio source thread */
    if (g_audio.src_st && g_audio.src_st->ready) {
        g_audio.src_st->ready = false;
        g_audio.src_st->run = false;
        
        /* Signal the condition variables to wake up waiting threads */
        mtx_lock(&g_audio.read_queue_mutex);
        cnd_signal(&g_audio.read_queue_cond);
        mtx_unlock(&g_audio.read_queue_mutex);
        
        /* Wait for thread to finish */
        if (g_audio.src_st->thread) {
            thrd_join(g_audio.src_st->thread, NULL);
            g_audio.src_st->thread = 0;
        }
        
        g_audio.src_st->ready = true; /* Reset for next call */
        g_audio.src_st->run = true;   /* Reset for next call */
        
        DEBUG_INFO("Audio source thread stopped\n");
    }
    
    /* Stop audio playback thread */
    if (g_audio.play_st && g_audio.play_st->ready) {
        g_audio.play_st->ready = false;
        g_audio.play_st->run = false;
        
        /* Signal the condition variables to wake up waiting threads */
        mtx_lock(&g_audio.write_queue_mutex);
        cnd_signal(&g_audio.write_queue_cond);
        mtx_unlock(&g_audio.write_queue_mutex);
        
        /* Wait for thread to finish */
        if (g_audio.play_st->thread) {
            thrd_join(g_audio.play_st->thread, NULL);
            g_audio.play_st->thread = 0;
        }
        
        g_audio.play_st->ready = true; /* Reset for next call */
        g_audio.play_st->run = true;   /* Reset for next call */
        
        DEBUG_INFO("Audio playback thread ended\n");
    }
    
    /* Force commit any remaining audio data */
    force_audio_commit();
    
    /* Don't clear WebSocket queue immediately - let it finish sending any pending messages */
    /* The queue will be cleared when the WebSocket is properly closed */
}

/* Restart audio threads for new call */
void audio_restart_threads(void)
{
    
    /* Check if audio system is ready for a new call */
    if (!audio_ready_for_call()) {
        warning("openai_rt: Audio system not ready for new call\n");
        return;
    }
    
    /* Check if call is active */
    if (!g_oairt.call_active) {
        DEBUG_INFO("Call not active, not starting audio threads\n");
        return;
    }
    
    DEBUG_INFO("Starting audio threads for new call\n");
    
    /* Start audio source thread */
    if (g_audio.src_st && g_audio.src_st->ready) {
        /* If thread already exists, check if it's still running */
        if (g_audio.src_st->thread) {
            /* Thread exists - check if it's actually running */
            if (g_audio.src_st->run) {
                DEBUG_INFO("Audio source thread already running\n");
            } else {
                /* Thread exists but not running - wait for it to finish and restart */
                DEBUG_INFO("Audio source thread exists but not running, waiting to join...\n");
                thrd_join(g_audio.src_st->thread, NULL);
                g_audio.src_st->thread = 0;
            }
        }
        
        /* Start thread if it doesn't exist or was stopped */
        if (!g_audio.src_st->thread) {
            g_audio.src_st->run = true;
            int thread_err = thread_create_name(&g_audio.src_st->thread, "openai_rt_src", ausrc_read_thread, g_audio.src_st);
            if (thread_err) {
                warning("openai_rt: Failed to restart audio source thread: %m\n", thread_err);
                g_audio.src_st->run = false;
            } else {
                info("openai_rt: Audio source thread started successfully\n");
            }
        }
    } else {
        DEBUG_INFO("Audio source not ready for thread start: ready=%d, src_st=%p\n", 
                   g_audio.src_st ? g_audio.src_st->ready : 0,
                   g_audio.src_st);
    }
    
    /* Start audio playback thread */
    if (g_audio.play_st && g_audio.play_st->ready) {
        /* If thread already exists, check if it's still running */
        if (g_audio.play_st->thread) {
            /* Thread exists - check if it's actually running */
            if (g_audio.play_st->run) {
                DEBUG_INFO("Audio playback thread already running\n");
            } else {
                /* Thread exists but not running - wait for it to finish and restart */
                DEBUG_INFO("Audio playback thread exists but not running, waiting to join...\n");
                thrd_join(g_audio.play_st->thread, NULL);
                g_audio.play_st->thread = 0;
            }
        }
        
        /* Start thread if it doesn't exist or was stopped */
        if (!g_audio.play_st->thread) {
            g_audio.play_st->run = true;
            int thread_err = thread_create_name(&g_audio.play_st->thread, "openai_rt_play", auplay_write_thread, g_audio.play_st);
            if (thread_err) {
                warning("openai_rt: Failed to restart audio playback thread: %m\n", thread_err);
                g_audio.play_st->run = false;
            } else {
                info("openai_rt: Audio playback thread started successfully\n");
            }
        }
    } else {
        DEBUG_INFO("Audio playback not ready for thread start: ready=%d, play_st=%p\n", 
                   g_audio.play_st ? g_audio.play_st->ready : 0,
                   g_audio.play_st);
    }
    
    DEBUG_INFO("Audio threads restart attempted (src_running=%d, play_running=%d)\n",
               audio_threads_running(), 
               g_audio.play_st && g_audio.play_st->thread && g_audio.play_st->run);
}

/* Check if audio threads are currently running */
bool audio_threads_running(void)
{
    bool src_running = (g_audio.src_st && g_audio.src_st->thread && g_audio.src_st->run);
    bool play_running = (g_audio.play_st && g_audio.play_st->thread && g_audio.play_st->run);
    
    return src_running || play_running;
}

/* Check if audio system is ready for a new call */
bool audio_ready_for_call(void)
{
    /* We need both the playback driver (for receiving audio from call) and 
       the source driver (for injecting audio back into call) to be ready */
    return (g_audio.play_st && g_audio.play_st->ready && 
            g_audio.src_st && g_audio.src_st->ready);
}

/* Check if audio source is ready to receive injected audio */
bool audio_source_ready_for_injection(void)
{
    return (g_audio.src_st && g_audio.src_st->ready);
}

/* Public function to receive audio from baresip */
void openai_rt_receive_audio(const int16_t *sampv, size_t sampc)
{
    if (!g_audio.play_st || !g_audio.play_st->ready || !g_oairt.call_active) {
        return;
    }

    g_audio.play_st->total_samples_received += sampc;
    handle_incoming_audio(sampv, sampc);
}

/* Add audio frame to read queue (from WebSocket thread) */
int audio_queue_read_frame(const int16_t *sampv, size_t sampc, uint32_t srate, uint8_t ch)
{
    if (!sampv || sampc == 0 || !g_oairt.call_active) {
        return EINVAL;
    }
    
    struct audio_frame *frame = mem_zalloc(sizeof(*frame), NULL);
    if (!frame) {
        return ENOMEM;
    }
    
    frame->sampv = mem_alloc(sampc * sizeof(int16_t), NULL);
    if (!frame->sampv) {
        mem_deref(frame);
        return ENOMEM;
    }
    
    memcpy(frame->sampv, sampv, sampc * sizeof(int16_t));
    frame->sampc = sampc;
    frame->srate = srate;
    frame->ch = ch;
    
    mtx_lock(&g_audio.read_queue_mutex);
    list_append(&g_audio.read_queue, &frame->le, frame);
    cnd_signal(&g_audio.read_queue_cond);
    mtx_unlock(&g_audio.read_queue_mutex);
    
    DEBUG_INFO("Added %zu samples to read queue\n", sampc);
    return 0;
}

/* Add event to event queue (from main thread) */
int audio_queue_event(enum event_type type, void *data)
{
    /* Allow call end events even when no call is active */
    if (type != EVENT_CALL_END && !g_oairt.call_active) {
        return EINVAL;
    }
    
    struct audio_event *event = mem_zalloc(sizeof(*event), NULL);
    if (!event) {
        return ENOMEM;
    }
    
    event->type = type;
    event->data = data;
    
    mtx_lock(&g_audio.event_queue_mutex);
    list_append(&g_audio.event_queue, &event->le, event);
    cnd_signal(&g_audio.event_queue_cond);
    mtx_unlock(&g_audio.event_queue_mutex);
    
    /* If this is a call start event and audio drivers are ready, start threads */
    if (type == EVENT_CALL_START && audio_ready_for_call()) {
        DEBUG_INFO("Call start event - starting audio threads\n");
        audio_restart_threads();
    } else if (type == EVENT_CALL_START) {
        DEBUG_INFO("Call start event - audio drivers not ready yet\n");
    }
    
    DEBUG_INFO("Added event %d to event queue\n", type);
    return 0;
}

/* Get next event from event queue (for WebSocket thread) */
struct audio_event *audio_get_next_event(void)
{
    struct audio_event *event = NULL;
    
    mtx_lock(&g_audio.event_queue_mutex);
    if (!list_isempty(&g_audio.event_queue)) {
        event = list_ledata(list_head(&g_audio.event_queue));
        list_unlink(&event->le);
    }
    mtx_unlock(&g_audio.event_queue_mutex);
    
    return event;
}

/* Get next audio frame from write queue (for WebSocket thread) */
struct audio_frame *audio_get_next_write_frame(void)
{
    struct audio_frame *frame = NULL;
    
    mtx_lock(&g_audio.write_queue_mutex);
    if (!list_isempty(&g_audio.write_queue)) {
        frame = list_ledata(list_head(&g_audio.write_queue));
        list_unlink(&frame->le);
    }
    mtx_unlock(&g_audio.write_queue_mutex);
    
    return frame;
}

/* Free an audio frame after it's been processed */
void audio_free_frame(struct audio_frame *frame)
{
    if (frame) {
        if (frame->sampv) {
            mem_deref(frame->sampv);
        }
        mem_deref(frame);
    }
}

/* Free all audio frames in a queue to prevent memory leaks */
static void free_audio_queue(struct list *queue, mtx_t *mutex)
{
    if (!queue || !mutex) return;
    
    mtx_lock(mutex);
    struct le *le = list_head(queue);
    while (le) {
        struct le *next = le->next;
        struct audio_frame *frame = list_ledata(le);
        if (frame) {
            list_unlink(le);
            if (frame->sampv) {
                mem_deref(frame->sampv);
            }
            mem_deref(frame);
        }
        le = next;
    }
    mtx_unlock(mutex);
}

/* Free all events in the event queue to prevent memory leaks */
static void free_event_queue(struct list *queue, mtx_t *mutex)
{
    if (!queue || !mutex) return;
    
    mtx_lock(mutex);
    struct le *le = list_head(queue);
    while (le) {
        struct le *next = le->next;
        struct audio_event *event = list_ledata(le);
        if (event) {
            list_unlink(le);
            mem_deref(event);
        }
        le = next;
    }
    mtx_unlock(mutex);
}

static int safe_clear_buffer(struct mbuf *buffer, const char *buffer_name)
{
    if (!buffer) return EINVAL;
    mbuf_rewind(buffer);      /* pos = 0 */
    mbuf_set_end(buffer, 0);  /* end = 0 -> truly empty */
    DEBUG_INFO("Successfully cleared %s buffer\n", buffer_name);
    return 0;
}

/* Send commit message to AI model to process accumulated audio */
static void send_audio_commit(void)
{
    struct ai_model *model = get_ai_model();
    char *response_msg = NULL;
    int err;

    /* Note: explicit commit not needed since we're using server_vad */
    
    if (g_oairt.wait_for_greeting) {
        g_audio.response_created = true;
    }
    
    if (!g_audio.response_created && model && model->build_response_create) {
        err = model->build_response_create(g_oairt.prompt, &response_msg);
        
        if (!err && response_msg) {
            err = queue_message_to_openai(response_msg, str_len(response_msg), NULL, NULL);
            if (err) {
                warning("openai_rt: Failed to queue response.create message: %m\n", err);
            } else {
                DEBUG_INFO("Response.create message queued\n");
                g_audio.response_created = true;
            }
            mem_deref(response_msg);
        }
    }
    
    g_audio.audio_accumulated = 0;
}

/* Force commit any remaining audio data */
static void force_audio_commit(void)
{
    if (g_audio.audio_accumulated > 0) {
        DEBUG_INFO("Forcing commit of remaining %zu bytes of audio\n", g_audio.audio_accumulated);
        send_audio_commit();
    }
}

/* Reset audio state for new call */
void audio_reset_for_new_call(void)
{
    
    /* Reset accumulation tracking */
    g_audio.audio_accumulated = 0;
    g_audio.response_created = false;
    
    /* Reset injection buffer state */
    mtx_lock(&g_audio.injection_buffer_mutex);
    g_audio.injection_read_pos = 0;
    g_audio.injection_write_pos = 0;
    g_audio.injection_available = 0;
    mtx_unlock(&g_audio.injection_buffer_mutex);
    
    /* Clear any stale audio data */
    if (g_audio.g711u_input_buffer) {
        safe_clear_buffer(g_audio.g711u_input_buffer, "input");
    }
    if (g_audio.g711u_output_buffer) {
        safe_clear_buffer(g_audio.g711u_output_buffer, "output");
    }
    
    DEBUG_INFO("Audio state reset for new call\n");
}

/* Flush accumulated audio when session becomes ready */
void audio_flush_accumulated(void)
{
    if (g_audio.audio_accumulated > 0) {
        double ms = (double)g_audio.audio_accumulated / 2 / 24000.0 * 1000.0;
        info("openai_rt: flushing %.1f ms (%u bytes) of accumulated audio\n",
             ms, (unsigned)g_audio.audio_accumulated);
        send_audio_commit();
    }
}

/* audio.c */

/* Resize the injection buffer to accommodate more data */
int resize_injection_buffer(size_t new_size_samples)
{
    if (new_size_samples < INJECTION_BUFFER_MIN_SIZE) {
        new_size_samples = INJECTION_BUFFER_MIN_SIZE;
    }
    if (new_size_samples > INJECTION_BUFFER_MAX_SIZE) {
        new_size_samples = INJECTION_BUFFER_MAX_SIZE;
    }
    
    mtx_lock(&g_audio.injection_buffer_mutex);
    
    /* If not growing or already at max, don't resize */
    if (new_size_samples <= g_audio.injection_buffer_size) {
        mtx_unlock(&g_audio.injection_buffer_mutex);
        return 0;
    }
    
    /* Allocate new larger buffer */
    int16_t *new_buffer = mem_alloc(new_size_samples * sizeof(int16_t), NULL);
    if (!new_buffer) {
        mtx_unlock(&g_audio.injection_buffer_mutex);
        warning("openai_rt: Failed to resize injection buffer to %zu samples\n", new_size_samples);
        return ENOMEM;
    }
    
    /* If we need to preserve existing data (shouldn't happen with our growth logic) */
    if (g_audio.injection_available > 0) {
        /* This is a safety check - with our growth logic, we shouldn't have data when growing */
        warning("openai_rt: Growing buffer with %zu samples still in use\n", g_audio.injection_available);
        
        /* Copy existing data to the beginning of the new buffer */
        int16_t *temp_buffer = mem_alloc(g_audio.injection_available * sizeof(int16_t), NULL);
        if (temp_buffer) {
            size_t temp_pos = 0;
            size_t old_pos = g_audio.injection_read_pos;
            
            /* Extract all data from old buffer */
            while (temp_pos < g_audio.injection_available) {
                temp_buffer[temp_pos++] = g_audio.injection_buffer[old_pos];
                old_pos = (old_pos + 1) % g_audio.injection_buffer_size;
            }
            
            /* Copy back to beginning of new buffer */
            memcpy(new_buffer, temp_buffer, g_audio.injection_available * sizeof(int16_t));
            mem_deref(temp_buffer);
            
            /* Reset positions */
            g_audio.injection_read_pos = 0;
            g_audio.injection_write_pos = g_audio.injection_available;
        }
    }
    
    /* Free the old buffer and assign the new one */
    mem_deref(g_audio.injection_buffer);
    
    g_audio.injection_buffer = new_buffer;
    g_audio.injection_buffer_size = new_size_samples;
    
    mtx_unlock(&g_audio.injection_buffer_mutex);
    
    info("openai_rt: Injection buffer resized to %zu samples (%.1f seconds @ 24kHz)\n", 
         new_size_samples, (double)new_size_samples / 24000.0);
    
    return 0;
}

/* Attempt to shrink the injection buffer if it's mostly empty */
static void maybe_shrink_injection_buffer(void)
{
    mtx_lock(&g_audio.injection_buffer_mutex);
    
    /* Only consider shrinking if we're well under capacity and buffer is large */
    if (g_audio.injection_available < g_audio.injection_buffer_size / 4 && 
        g_audio.injection_buffer_size > INJECTION_BUFFER_INITIAL_SIZE * 2) {
        
        size_t new_size = g_audio.injection_buffer_size / INJECTION_BUFFER_GROWTH_FACTOR;
        
        /* Don't shrink below initial size */
        if (new_size >= INJECTION_BUFFER_INITIAL_SIZE) {
            mtx_unlock(&g_audio.injection_buffer_mutex);
            
            int err = resize_injection_buffer(new_size);
            if (err) {
                warning("openai_rt: Failed to shrink injection buffer: %m\n", err);
            }
            return;
        }
    }
    
    mtx_unlock(&g_audio.injection_buffer_mutex);
}

int write_to_injection_buffer(const int16_t *samples, size_t sample_count)
{
    if (!samples || sample_count == 0) return EINVAL;

    mtx_lock(&g_audio.injection_buffer_mutex);

    size_t space = g_audio.injection_buffer_size - g_audio.injection_available;

    /* If we don't have enough space, grow the buffer */
    if (sample_count > space) {
        size_t needed_space = sample_count - space;
        size_t new_size = g_audio.injection_buffer_size + needed_space;
        
        /* Grow by a factor for efficiency */
        new_size = new_size * INJECTION_BUFFER_GROWTH_FACTOR;
        
        mtx_unlock(&g_audio.injection_buffer_mutex);
        
        int err = resize_injection_buffer(new_size);
        if (err) {
            warning("openai_rt: Failed to grow injection buffer, dropping %zu samples\n", sample_count);
            return err;
        }
        
        /* Re-acquire lock and recalculate space */
        mtx_lock(&g_audio.injection_buffer_mutex);
        space = g_audio.injection_buffer_size - g_audio.injection_available;
    }

    /* Write contiguous, wrapping as needed */
    for (size_t i = 0; i < sample_count; i++) {
        g_audio.injection_buffer[g_audio.injection_write_pos] = samples[i];
        g_audio.injection_write_pos =
            (g_audio.injection_write_pos + 1) % g_audio.injection_buffer_size;
    }
    g_audio.injection_available += sample_count;

    mtx_unlock(&g_audio.injection_buffer_mutex);
    return 0;
}