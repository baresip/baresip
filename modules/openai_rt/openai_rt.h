/**
 * @file openai_rt.h  OpenAI Realtime API module
 *
 * Copyright (C) 2025 Sipfront
 */

#ifndef OPENAI_RT_H
#define OPENAI_RT_H

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <pthread.h>
#include "ai_model.h"

/* WebSocket states */
enum ws_state {
    WS_DISCONNECTED,
    WS_CONNECTING,
    WS_CONNECTED,
    WS_DISCONNECTING
};

/* Message types for WebSocket communication */
enum ws_msg_type {
    WS_MSG_TO_OPENAI,    /* Messages to send to OpenAI */
    WS_MSG_FROM_OPENAI   /* Messages received from OpenAI */
};

/* WebSocket message structure */
struct ws_message {
    struct le le;
    enum ws_msg_type type;
    uint8_t *data;
    size_t len;
    void (*callback)(void *arg, int err);
    void *arg;
};

/* AI Backend types */
enum ai_backend_type {
	AI_BACKEND_OPENAI_REALTIME,
	AI_BACKEND_GEMINI_LIVE,
};

/* Global module state */
struct openai_rt {
    /* Configuration */
    char api_key[256];
    char prompt[4096];
    char enabled_tools[256];  /* Comma-separated list of enabled tool calls (e.g., "hangup_call,send_dtmf") */
    char backend[64];         /* AI backend selection: "openai_realtime" or "gemini_live" */
    enum ai_backend_type backend_type;  /* Parsed backend type */
    bool wait_for_greeting;
    float temperature;        /* Temperature for AI model generation (default 0.7) */
    char voice[64];           /* Voice name for Gemini (e.g., "Aoede") */
    /* Gemini VAD config */
    bool gemini_vad_enabled;
    char gemini_vad_start_sensitivity[64];
    int gemini_vad_silence_duration_ms;
    int gemini_vad_prefix_padding_ms;
    /* Call state */
    bool call_active;
    struct call *current_call;
    bool session_cfg_applied;   /* set after we see type=session.updated */
    
    /* WebSocket state */
    enum ws_state ws_state;
    struct lws_context *ws_context;
    struct lws *ws_client;
    bool session_ready;
    bool speech_active;
    
    /* WebSocket thread */
    pthread_t ws_thread;
    bool ws_thread_running;
    pthread_mutex_t ws_mutex;
    pthread_cond_t ws_cond;
    
    /* Message queues */
    struct list to_openai_queue;    /* Messages to send to OpenAI */
    struct list from_openai_queue;  /* Messages received from OpenAI */
    
    /* Audio state */
    struct ausrc *ausrc;
    struct auplay *auplay;
};

/* Global instance */
extern struct openai_rt g_oairt;

/* Module functions */
int openai_rt_init(void);
void openai_rt_close(void);

/* Call management */
int calls_init(void);
 void calls_close(void);
void openai_rt_call_started(struct call *call);
void openai_rt_call_ended(void);

/* WebSocket functions */
int websocket_init(void);
void websocket_close(void);
void websocket_request_shutdown(void);
void websocket_force_shutdown(int timeout_ms);
int connect_openai_ws(void);
void disconnect_openai_ws(void);
bool websocket_is_ready(void);
int websocket_wait_ready(int timeout_ms);
const char *websocket_status_string(void);
void send_session_update(void);
int queue_message_to_openai(const char *json_msg, size_t len, 
                           void (*callback)(void *arg, int err), void *arg);
int queue_message_from_openai(const uint8_t *data, size_t len);
void websocket_clear_message_queue(void);

/* Audio functions - Audio driver implementation */
int audio_init(void);
void audio_close(void);
void handle_incoming_audio(const int16_t *s16_data, size_t sampc);
void handle_outgoing_audio(const uint8_t *g711u_data, size_t len);
void openai_rt_check_messages(void);
void openai_rt_provide_audio(void);
void openai_rt_receive_audio(const int16_t *sampv, size_t sampc);
void audio_stop_threads(void);
void audio_restart_threads(void);
void audio_reset_for_new_call(void);
void audio_flush_accumulated(void);
bool audio_source_ready_for_injection(void);
bool audio_threads_running(void);
bool audio_ready_for_call(void);

/* Audio driver allocation functions */
int openai_rt_ausrc_alloc(struct ausrc_st **stp, const struct ausrc *as,
                          struct ausrc_prm *prm, const char *dev,
                          ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int openai_rt_auplay_alloc(struct auplay_st **stp, const struct auplay *ap,
                           struct auplay_prm *prm, const char *dev,
                           auplay_write_h *wh, void *arg);

/* Audio processing functions */
size_t downsample_pcm16_24k_to_8k(const int16_t *in, size_t in_sampc, int16_t *out);
int write_to_injection_buffer(const int16_t *samples, size_t sample_count);
int resize_injection_buffer(size_t new_size_samples);

/* Utility functions */
char *encode_audio_base64(const void *data, size_t len);
size_t decode_audio_base64(const char *data, uint8_t **out);
int json_escape(char **dst, const char *src);
int read_config(void);


/* Debug macros */
#define DEBUG_INFO(...) info("openai_rt: " __VA_ARGS__)

/* Audio frame structure for queue communication */
struct audio_frame {
    int16_t *sampv;          /* Audio samples */
    size_t sampc;             /* Number of samples */
    uint32_t srate;           /* Sample rate */
    uint8_t ch;               /* Number of channels */
    struct le le;             /* List element */
};

/* Event types for the event queue */
enum event_type {
    EVENT_CALL_START,         /* Call started - connect to OpenAI */
    EVENT_CALL_END,           /* Call ended - disconnect from OpenAI */
};

/* Event structure for the event queue */
struct audio_event {
    enum event_type type;     /* Event type */
    void *data;               /* Event-specific data */
    struct le le;             /* List element */
};

/* Audio system state */
struct audio_state {
    struct ausrc_st *src_st;           /* Audio source (provides audio to call) */
    struct auplay_st *play_st;         /* Audio player (receives audio from call) */
    struct list read_queue;            /* Queue of audio frames to provide to call */
    struct list write_queue;           /* Queue of audio frames from call */
    struct list event_queue;           /* Queue of events to process */
    mtx_t read_queue_mutex;            /* Mutex for read queue access */
    mtx_t write_queue_mutex;           /* Mutex for write queue access */
    mtx_t event_queue_mutex;           /* Mutex for event queue access */
    cnd_t read_queue_cond;             /* Condition variable for read queue */
    cnd_t write_queue_cond;            /* Condition variable for write queue */
    cnd_t event_queue_cond;            /* Condition variable for event queue */
    struct mbuf *g711u_output_buffer;  /* Buffer for OpenAI audio to inject */
    struct mbuf *g711u_input_buffer;   /* Buffer for audio from call to OpenAI */
    size_t buffer_size;                /* Size of audio buffers */
    size_t audio_accumulated;          /* Amount of audio accumulated since last commit */
    size_t commit_threshold;           /* Threshold to trigger commit (in bytes) */
    bool response_created;             /* Whether response.create has been sent for current session */
    
    /* Circular buffer for smooth audio injection */
    int16_t *injection_buffer;         /* Circular buffer for OpenAI audio */
    size_t injection_buffer_size;      /* Total size of injection buffer in samples */
    size_t injection_read_pos;         /* Read position in circular buffer */
    size_t injection_write_pos;        /* Write position in circular buffer */
    size_t injection_available;        /* Number of samples available for reading */
    mtx_t injection_buffer_mutex;      /* Mutex for injection buffer access */
    
};

/* Audio commit threshold - commit after accumulating this many bytes */
/* 800ms at 24kHz PCM16 = 24000 * 2 * 0.8 = 38400 bytes */
#define AUDIO_COMMIT_THRESHOLD 38400

/* Injection buffer sizing limits */
#define INJECTION_BUFFER_INITIAL_SIZE 128000    /* Initial size in samples (16 seconds @ 8kHz) */
#define INJECTION_BUFFER_MIN_SIZE 32000         /* Minimum size in samples (4 seconds @ 8kHz) */
#define INJECTION_BUFFER_MAX_SIZE 2000000       /* Maximum size in samples (250 seconds @ 8kHz) */
#define INJECTION_BUFFER_GROWTH_FACTOR 2        /* Multiply by this when growing */

/* Global audio state - defined in audio.c */
extern struct audio_state g_audio;

/* Queue management functions */
int audio_queue_read_frame(const int16_t *sampv, size_t sampc, uint32_t srate, uint8_t ch);
int audio_queue_event(enum event_type type, void *data);
struct audio_event *audio_get_next_event(void);
struct audio_frame *audio_get_next_write_frame(void);
void audio_free_frame(struct audio_frame *frame);

/* Call management functions accessible via tools */
void calls_hangup(void);
void calls_send_digit(char key);
int calls_send_dtmf(const char *digits);
int calls_api_call(const char *method, const char *uri,
                   const char *content_type, const char *auth_type,
                   const char *auth_username, const char *auth_password,
                   const char *body, char **output);
int calls_queue_openai_response(const char *response_json);

#endif /* OPENAI_RT_H */