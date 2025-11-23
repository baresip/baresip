/**
 * @file utils.c  OpenAI Realtime API - Utility functions with fixed base64
 */
#include "openai_rt.h"
#include <re_base64.h>

/* Base64 encoding wrapper for audio data */
char *encode_audio_base64(const void *data, size_t len)
{

    char *encoded;
    size_t out_len;
    int err;

    //DEBUG_INFO("Encoding %zu bytes to base64\n", len);

    if (!data || len == 0) {
        DEBUG_INFO("Invalid input data\n");
        return NULL;
    }

    /* Calculate required buffer size for base64 encoding
     * Base64 encoding produces 4 characters for every 3 bytes
     * Plus potential newlines and padding
     * Formula: ((len + 2) / 3) * 4 + extra space for safety
     *
     * The libre base64_encode might add newlines, so we allocate extra
     */
    out_len = ((len + 2) / 3) * 4 + (len / 40) + 64;  // Extra space for safety
    //DEBUG_INFO("++++ out_len: %zu\n", out_len);

    encoded = mem_zalloc(out_len, NULL);
    if (!encoded) {
        DEBUG_INFO("Failed to allocate base64 buffer of size %zu\n", out_len);
        return NULL;
    }
    memset(encoded, 0, out_len);   

    /* IMPORTANT: Pass the actual buffer size to base64_encode */
    size_t enc_len = out_len - 1;  // Leave room for null terminator

    /* Now encode with the allocated buffer */
    err = base64_encode((uint8_t *)data, len, encoded, &enc_len);
    if (err) {
        DEBUG_INFO("Failed to encode base64: %m (input %zu bytes, buffer %zu)\n",
                   err, len, out_len);
        mem_deref(encoded);
        return NULL;
    }

    /* Null terminate */
    encoded[enc_len] = '\0';

    //DEBUG_INFO("++++ encoded (%zu): %b\n", enc_len, encoded, enc_len);   
    //DEBUG_INFO("++++ encoded done\n");

    return encoded;
}

/* Base64 decoding wrapper for audio data */
size_t decode_audio_base64(const char *data, uint8_t **out)
{
    size_t in_len = str_len(data);
    size_t out_len;
    uint8_t *decoded;
    int err;

    //DEBUG_INFO("Decoding %zu bytes from base64\n", in_len);

    if (!data || in_len == 0) {
        DEBUG_INFO("Invalid input data\n");
        return 0;
    }

    /* Calculate max possible output size
     * Base64 decodes 4 chars to 3 bytes, but we need to be generous
     * because of potential whitespace and padding
     */
    out_len = ((in_len + 3) / 4) * 3 + 16;  // Extra space for safety

    decoded = mem_zalloc(out_len, NULL);
    if (!decoded) {
        DEBUG_INFO("Failed to allocate decode buffer of size %zu\n", out_len);
        return 0;
    }

    /* Decode - out_len will be updated with actual decoded size */
    err = base64_decode(data, in_len, decoded, &out_len);
    if (err) {
        DEBUG_INFO("Failed to decode base64: %m\n", err);
        mem_deref(decoded);
        return 0;
    }

    *out = decoded;
/*
    DEBUG_INFO("Decoded to %zu bytes (first 4 bytes: %02x %02x %02x %02x)\n",
               out_len,
               out_len > 0 ? decoded[0] : 0,
               out_len > 1 ? decoded[1] : 0,
               out_len > 2 ? decoded[2] : 0,
               out_len > 3 ? decoded[3] : 0);
*/
    return out_len;
}

int read_config(void)
{

    conf_get_str(conf_cur(), "openai_rt_prompt", g_oairt.prompt, sizeof(g_oairt.prompt));
    conf_get_str(conf_cur(), "openai_rt_api_key", g_oairt.api_key, sizeof(g_oairt.api_key));
    conf_get_str(conf_cur(), "openai_rt_tool_calls", g_oairt.enabled_tools, sizeof(g_oairt.enabled_tools));
    conf_get_str(conf_cur(), "openai_rt_backend", g_oairt.backend, sizeof(g_oairt.backend));
    g_oairt.wait_for_greeting = true;
    conf_get_bool(conf_cur(), "openai_rt_wait_for_greeting", &g_oairt.wait_for_greeting);

    /* Set defaults if not configured */
    if (!str_isset(g_oairt.prompt))
        str_ncpy(g_oairt.prompt, "You are a helpful voice assistant for phone calls.", sizeof(g_oairt.prompt));

    /* Set default enabled tools if not configured - enable all by default */
    if (!str_isset(g_oairt.enabled_tools))
        str_ncpy(g_oairt.enabled_tools, "hangup_call,send_dtmf", sizeof(g_oairt.enabled_tools));

    /* Set default backend if not configured - default to OpenAI */
    if (!str_isset(g_oairt.backend))
        str_ncpy(g_oairt.backend, "openai_realtime", sizeof(g_oairt.backend));

    /* Parse backend type */
    if (str_casecmp(g_oairt.backend, "openai_realtime") == 0) {
        g_oairt.backend_type = AI_BACKEND_OPENAI_REALTIME;
    } else if (str_casecmp(g_oairt.backend, "gemini_live") == 0) {
        g_oairt.backend_type = AI_BACKEND_GEMINI_LIVE;
    } else {
        warning("openai_rt: Unknown backend '%s', defaulting to openai_realtime\n", g_oairt.backend);
        g_oairt.backend_type = AI_BACKEND_OPENAI_REALTIME;
        str_ncpy(g_oairt.backend, "openai_realtime", sizeof(g_oairt.backend));
    }

    DEBUG_INFO("Config loaded - Backend: %s, API key: %s, Prompt: %.50s%s, Tools: %s\n",
           g_oairt.backend,
           str_isset(g_oairt.api_key) ? "[CONFIGURED]" : "[MISSING]",
           g_oairt.prompt,
           str_len(g_oairt.prompt) > 50 ? "..." : "",
           g_oairt.enabled_tools);

    return 0;
}