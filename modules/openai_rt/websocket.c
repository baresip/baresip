/**
 * @file websocket.c  OpenAI Realtime API - WebSocket communication
 */

 #include <re.h>
 #include <rem.h>
 #include <baresip.h>
 #include <libwebsockets.h>
 #include <json-c/json.h>
 #include <pthread.h>
 #include "openai_rt.h"
 
 /* Forward declarations */
 static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len);
 static int add_auth_headers(void *in, size_t len);
 static void process_outgoing_messages(void);
 
 /* WebSocket protocols (local binding for callbacks; NOT sent as WS subprotocol) */
 static const struct lws_protocols protocols[] = {
     {
         .name = "realtime",      /* Local name; not advertised unless ccinfo.protocol is set */
         .callback = ws_callback,
         .per_session_data_size = 0,
         .rx_buffer_size = 65536,
         .id = 0,
         .user = NULL,
         .tx_packet_size = 0,
     },
     { NULL, NULL, 0, 0, 0, NULL, 0 }  /* Terminator */
 };
 
 /* Message destructor */
 static void ws_message_destructor(void *arg)
 {
     struct ws_message *msg = arg;
     mem_deref(msg->data);
 }
 
 /* Process outgoing messages */
 static void process_outgoing_messages(void)
 {
     struct ws_message *msg = NULL;
     struct le *le;
 
     pthread_mutex_lock(&g_oairt.ws_mutex);
     le = list_head(&g_oairt.to_openai_queue);
     if (le) {
         msg = list_ledata(le);
         list_unlink(le);
     }
     pthread_mutex_unlock(&g_oairt.ws_mutex);
 
     if (!msg || !g_oairt.ws_client) {
         return;
     }
 
     /* Check if we can write */
     if (lws_send_pipe_choked(g_oairt.ws_client)) {
         /* Put message back in queue */
         pthread_mutex_lock(&g_oairt.ws_mutex);
         list_prepend(&g_oairt.to_openai_queue, &msg->le, msg);
         pthread_mutex_unlock(&g_oairt.ws_mutex);
         return;
     }
 
     //DEBUG_INFO("Sending message to OpenAI: %s\n", msg->data + LWS_PRE);
 
     /* Send the message */
     int written = lws_write(g_oairt.ws_client, msg->data + LWS_PRE, msg->len, LWS_WRITE_TEXT);
     if (written < 0) {
         warning("openai_rt: Failed to write WebSocket message\n");
         /* Put message back in queue */
         pthread_mutex_lock(&g_oairt.ws_mutex);
         list_prepend(&g_oairt.to_openai_queue, &msg->le, msg);
         pthread_mutex_unlock(&g_oairt.ws_mutex);
     } else if (written < (int)msg->len) {
         /* Partial write, update message and put back in queue */
         memmove(msg->data + LWS_PRE, msg->data + LWS_PRE + written, msg->len - written);
         msg->len -= written;
         pthread_mutex_lock(&g_oairt.ws_mutex);
         list_prepend(&g_oairt.to_openai_queue, &msg->le, msg);
         pthread_mutex_unlock(&g_oairt.ws_mutex);
     } else {
         /* Full message sent, call callback if provided */
         if (msg->callback) {
             msg->callback(msg->arg, 0);
         }
         mem_deref(msg);
     }
 
     /* If there are more messages to send, request another writable callback */
     pthread_mutex_lock(&g_oairt.ws_mutex);
     if (!list_isempty(&g_oairt.to_openai_queue) && g_oairt.ws_client) {
         lws_callback_on_writable(g_oairt.ws_client);
     }
     pthread_mutex_unlock(&g_oairt.ws_mutex);
 }
 
 static inline void ws_wake_service(void)
 {
     if (g_oairt.ws_context) {
         /* break poll and run lws_service() ASAP */
         lws_cancel_service(g_oairt.ws_context);
     }
 }
 
 static void handle_session_updated(const char *json_str)
{
    struct json_object *root = json_tokener_parse(json_str);
    if (!root) {
        warning("openai_rt: Failed to parse session update JSON\n");
        return;
    }

    struct json_object *type_obj = NULL;
    if (json_object_object_get_ex(root, "type", &type_obj)) {
        if (!json_object_is_type(type_obj, json_type_string)) {
            warning("openai_rt: Session update 'type' field is not a string\n");
            json_object_put(root);
            return;
        }
        
        const char *type = json_object_get_string(type_obj);
        if (type && strcmp(type, "session.updated") == 0) {
            g_oairt.session_cfg_applied = true;
            g_oairt.session_ready = true;   /* now it's truly ready */
            info("openai_rt: session.updated received; input_audio_format now active\n");

            /* Flush any accumulated audio now that session is ready */
            audio_flush_accumulated();
        }
    }
    json_object_put(root);
}
 
static void handle_openai_audio_delta(const char *json_str)
{
    /* Parse JSON and ensure it's an audio delta */
    struct json_object *root = json_tokener_parse(json_str);
    if (!root) {
        warning("openai_rt: Failed to parse audio delta JSON\n");
        return;
    }

    /* Get base64 payload */
    struct json_object *delta_obj = NULL;
    if (!json_object_object_get_ex(root, "delta", &delta_obj)) {
        warning("openai_rt: Audio delta JSON missing 'delta' field\n");
        json_object_put(root);
        return;
    }
    
    if (!json_object_is_type(delta_obj, json_type_string)) {
        warning("openai_rt: Audio delta 'delta' field is not a string\n");
        json_object_put(root);
        return;
    }
    
    const char *b64 = json_object_get_string(delta_obj);
    if (!b64 || !*b64) {
        warning("openai_rt: Audio delta 'delta' field is empty or null\n");
        json_object_put(root);
        return;
    }

    /* Decode base64 -> PCM16LE @ 24 kHz */
    uint8_t *decoded = NULL;
    size_t nbytes = decode_audio_base64(b64, &decoded);

    if (decoded && nbytes >= 2) {

        const int16_t *pcm24 = (const int16_t *)decoded;
        size_t nsamp24 = nbytes / 2;

        if (g_oairt.call_active) {
            int err = write_to_injection_buffer(pcm24, nsamp24);
            if (err) {
                warning("openai_rt: write_to_injection_buffer failed: %m\n", err);
            }
        }
        else {
            DEBUG_INFO("openai_rt: Call not active, skipping audio injection\n");
        }
    }

    if (decoded) mem_deref(decoded);

    json_object_put(root);
}

/* websocket.c */
static void handle_openai_function_call(const char *call_id, const char *name, const char *arguments)
{
    (void)arguments; /* Suppress unused parameter warning */

    if (strcmp(name, "hangup_call") == 0) {
        DEBUG_INFO("openai_rt: Executing hangup_call function\n");

        calls_hangup();
        DEBUG_INFO("openai_rt: hangup_call function executed\n");
        char *json_msg = NULL;
        int err;
        
        re_sdprintf(&json_msg,
            "{"
                "\"type\": \"conversation.item.create\","
                "\"item\": {"
                    "\"type\":\"function_call_output\","
                    "\"call_id\": \"%s\","
                    "\"output\": \"Call hung up\""
                "}"
            "}",
            call_id
        );
    
        info("openai_rt: function_call_output: %s\n", json_msg);
        
        if (json_msg) {
            err = queue_message_to_openai(json_msg, str_len(json_msg), NULL, NULL);
            if (err) {
                warning("openai_rt: Failed to queue function_call_output: %m\n", err);
            }
            mem_deref(json_msg);
        }

    }
    else {
        warning("openai_rt: Unknown function call: %s\n", name);
    }

}
static void handle_openai_handle_event(const char *json_str)
{
    /* Parse JSON and ensure it's a valid message */
    struct json_object *root = json_tokener_parse(json_str);
    if (!root) {
        warning("openai_rt: Failed to parse JSON message\n");
        return;
    }

    struct json_object *type_obj = NULL;
    if (!json_object_object_get_ex(root, "type", &type_obj)) {
        warning("openai_rt: JSON message missing 'type' field\n");
        json_object_put(root);
        return;
    }
    
    if (!json_object_is_type(type_obj, json_type_string)) {
        warning("openai_rt: JSON 'type' field is not a string\n");
        json_object_put(root);
        return;
    }
    
    const char *type = json_object_get_string(type_obj);
    if (!type) {
        warning("openai_rt: Failed to get string value from 'type' field\n");
        json_object_put(root);
        return;
    }

    if (strcmp(type, "response.output_audio.delta") == 0) {
        handle_openai_audio_delta(json_str);
    }
    else if (strcmp(type, "response.output_item.done") == 0) {

        /*
            {
            "type": "response.output_item.done",
            "event_id": "event_CCPrjw2MyBPd9VuKlUVpr",
            "response_id": "resp_CCPriWZsESu3xVKH0cA5d",
            "output_index": 0,
            "item": {
                "id": "item_CCPrilCP19fczE3iYlkpN",
                "type": "function_call",
                "status": "completed",
                "name": "hangup_call",
                "call_id": "call_l4ZCLwE1TjfrZe2Q",
                "arguments": "{}"
            }
            }        
        */

        DEBUG_INFO("openai_rt: response.output_item.done received\n");
        // get the 'item' field
        struct json_object *item_obj = NULL;
        if (!json_object_object_get_ex(root, "item", &item_obj)) {
            warning("openai_rt: JSON message missing 'item' field\n");
            json_object_put(root);
            return;
        }
        // get the 'type' field
        struct json_object *item_type_obj = NULL;
        if (!json_object_object_get_ex(item_obj, "type", &item_type_obj)) {
            warning("openai_rt: JSON message missing 'type' field\n");
            json_object_put(root);
            return;
        }
        if (!json_object_is_type(item_type_obj, json_type_string)) {
            warning("openai_rt: JSON 'type' field is not a string\n");
            json_object_put(root);
            return;
        }
        const char *item_type = json_object_get_string(item_type_obj);
        if (strcmp(item_type, "function_call") == 0) {
            // get name, call_id and arguments
            struct json_object *name_obj = NULL;
            if (!json_object_object_get_ex(item_obj, "name", &name_obj)) {
                warning("openai_rt: JSON message missing 'name' field\n");
                json_object_put(root);
                return;
            }
            if (!json_object_is_type(name_obj, json_type_string)) {
                warning("openai_rt: JSON 'name' field is not a string\n");
                json_object_put(root);
                return;
            }
            const char *name = json_object_get_string(name_obj);
            if (!name) {
                warning("openai_rt: Failed to get string value from 'name' field\n");
                json_object_put(root);
                return;
            }
            struct json_object *call_id_obj = NULL;
            if (!json_object_object_get_ex(item_obj, "call_id", &call_id_obj)) {
                warning("openai_rt: JSON message missing 'call_id' field\n");
                json_object_put(root);
                return;
            }
            if (!json_object_is_type(call_id_obj, json_type_string)) {
                warning("openai_rt: JSON 'call_id' field is not a string\n");
                json_object_put(root);
                return;
            }
            const char *call_id = json_object_get_string(call_id_obj);
            if (!call_id) {
                warning("openai_rt: Failed to get string value from 'call_id' field\n");
                json_object_put(root);
                return;
            }
            struct json_object *arguments_obj = NULL;
            if (!json_object_object_get_ex(item_obj, "arguments", &arguments_obj)) {
                warning("openai_rt: JSON message missing 'arguments' field\n");
                json_object_put(root);
                return;
            }
            if (!json_object_is_type(arguments_obj, json_type_string)) {
                warning("openai_rt: JSON 'arguments' field is not a string\n");
                json_object_put(root);
                return;
            }
            const char *arguments = json_object_get_string(arguments_obj);
            if (!arguments) {
                warning("openai_rt: Failed to get string value from 'arguments' field\n");
                json_object_put(root);
                return;
            }
            handle_openai_function_call(call_id, name, arguments);
        }
    }
    else {
        DEBUG_INFO("openai_rt: Unhandled message type: %s\n", type);
    }

    json_object_put(root);
}
 
 /* WebSocket callback */
 static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
 {
     (void)user; /* Unused parameter */
 
     //info("openai_rt: WebSocket callback: reason=%d\n", reason);
 
     switch (reason) {
     case LWS_CALLBACK_WSI_CREATE:
         info("WSI created\n");
         break;
 
     case LWS_CALLBACK_CLIENT_ESTABLISHED:
         info("openai_rt: WebSocket connection established\n");
         g_oairt.ws_state = WS_CONNECTED;
         g_oairt.ws_client = wsi;
 
         /* not ready yet: wait for server to ack session.update */
         g_oairt.session_ready = false;
         g_oairt.session_cfg_applied = false;
 
         send_session_update();
         lws_callback_on_writable(wsi);
         break;
 
     case LWS_CALLBACK_CLIENT_WRITEABLE:
         //info("openai_rt: WebSocket writable\n");
         /* Process outgoing messages */
         process_outgoing_messages();
         break;
 
     case LWS_CALLBACK_CLIENT_RECEIVE:
         //info("openai_rt: WebSocket received %zu bytes\n", len);
         DEBUG_INFO("Received message from OpenAI: %s\n", (const char *)in);
 
         if (len > 0 && g_oairt.ws_state == WS_CONNECTED) {
             queue_message_from_openai((const uint8_t *)in, len);
             handle_session_updated((const char *)in);     /* NEW */
             handle_openai_handle_event((const char *)in); 
         }
         break;
 
     case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
         warning("openai_rt: WebSocket connection error\n");
         g_oairt.ws_state = WS_DISCONNECTED;
         g_oairt.ws_client = NULL;
         g_oairt.session_ready = false;
         break;
 
     case LWS_CALLBACK_CLIENT_CLOSED:
         info("openai_rt: WebSocket disconnected\n");
         g_oairt.ws_state = WS_DISCONNECTED;
         g_oairt.ws_client = NULL;
         g_oairt.session_ready = false;
         break;
 
     case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
         info("openai_rt: Adding authentication headers\n");
         info("openai_rt: API key length: %zu\n", str_len(g_oairt.api_key));
         int ret = add_auth_headers(in, len);
         info("openai_rt: add_auth_headers returned: %d\n", ret);
         return ret;
     }
 
     case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
         info("openai_rt: Client filter pre-establish\n");
         return 0;
 
     case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
         info("openai_rt: HTTP connection established\n");
         break;
 
     case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
         info("openai_rt: HTTP writeable\n");
         break;
 
     case LWS_CALLBACK_CLIENT_HTTP_REDIRECT:
         info("openai_rt: HTTP redirect\n");
         break;
 
     case LWS_CALLBACK_CLIENT_HTTP_DROP_PROTOCOL:
         info("openai_rt: HTTP drop protocol\n");
         break;
 
     case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL:
         info("openai_rt: HTTP bind protocol\n");
         /* Called when the HTTP connection is established but before WS upgrade */
         break;
 
     case LWS_CALLBACK_GET_THREAD_ID:
         info("openai_rt: Get thread ID requested\n");
         /* Avoid returning pthread_t as int; most apps can just return 0 */
         return 0;
 
     case LWS_CALLBACK_PROTOCOL_INIT:
         info("openai_rt: Protocol init\n");
         break;
 
     case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
         //info("openai_rt: Event wait cancelled\n");
         break;
 
     case LWS_CALLBACK_HTTP:
         info("openai_rt: HTTP request received\n");
         break;
 
     case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE:
         info("openai_rt: HTTP upgrade confirmed\n");
         /* Called when the HTTP upgrade to WebSocket is confirmed */
         break;
 
     case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
         info("openai_rt: Filter protocol connection\n");
         break;
 
     case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
         info("openai_rt: Received HTTP response from server\n");
         break;
 
     case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
         info("openai_rt: HTTP client transaction completed\n");
         break;
 
     default:
         info("openai_rt: WebSocket callback: reason=%d (unhandled)\n", reason);
         break;
     }
 
     return 0;
 }
 
 static int add_auth_headers(void *in, size_t len)
 {
     unsigned char **p = (unsigned char **)in;
     unsigned char *end = (*p) + len;
     int written;
 
     if (!str_isset(g_oairt.api_key)) {
         warning("openai_rt: No API key available for headers\n");
         return -1;
     }
 
     written = lws_snprintf((char *)*p, end - *p,
             "Authorization: Bearer %s\r\n", g_oairt.api_key);
     if (written < 0 || written >= (int)(end - *p)) {
         warning("openai_rt: failed to add Authorization header\n");
         return -1;
     }
     *p += written;
 
     /*
     written = lws_snprintf((char *)*p, end - *p,
             "OpenAI-Beta: realtime=v1\r\n");
     if (written < 0 || written >= (int)(end - *p)) {
         warning("openai_rt: failed to add OpenAI-Beta header\n");
         return -1;
     }
     *p += written;
    */
     return 0;
 }
 
 /* Connect to OpenAI WebSocket */
 int connect_openai_ws(void)
 {
     struct lws_client_connect_info ccinfo;
 
     if (g_oairt.ws_state != WS_DISCONNECTED) {
         warning("openai_rt: WebSocket already connected or connecting\n");
         return EALREADY;
     }
     if (!g_oairt.ws_context) {
         warning("openai_rt: WebSocket context not initialized\n");
         return ENOMEM;
     }
     if (!g_oairt.api_key[0]) {
         warning("openai_rt: API key not set\n");
         return EINVAL;
     }
 
     info("openai_rt: Initiating WebSocket connection to OpenAI...\n");
     g_oairt.ws_state = WS_CONNECTING;
 
     memset(&ccinfo, 0, sizeof(ccinfo));
     ccinfo.context = g_oairt.ws_context;
     ccinfo.address = "api.openai.com";
     ccinfo.port = 443;
     ccinfo.path = "/v1/realtime?model=gpt-realtime";
     ccinfo.host = "api.openai.com";                  /* SNI / Host header */
     ccinfo.origin = NULL;                            /* Not required */
     ccinfo.protocol = NULL;                          /* DO NOT send Sec-WebSocket-Protocol */
     ccinfo.ietf_version_or_minus_one = -1;           /* Auto-select WS version */
     ccinfo.ssl_connection = LCCSCF_USE_SSL; /* Verify cert + hostname in prod */
     ccinfo.pwsi = &g_oairt.ws_client;                /* Single authoritative pointer */
 
     info("openai_rt: Connecting to wss://%s:%d%s\n",
          ccinfo.address, ccinfo.port, ccinfo.path);
     info("openai_rt: Calling lws_client_connect_via_info...\n");
 
     g_oairt.ws_client = lws_client_connect_via_info(&ccinfo);
     if (!g_oairt.ws_client) {
         warning("openai_rt: Failed to connect to OpenAI WebSocket\n");
         g_oairt.ws_state = WS_DISCONNECTED;
         return ENOMEM;
     }
 
     info("openai_rt: lws_client_connect_via_info returned: %p\n", (void*)g_oairt.ws_client);
     info("openai_rt: WebSocket connection initiated, waiting for callback...\n");
     return 0;
 }
 
 /* Disconnect from OpenAI */
 void disconnect_openai_ws(void)
 {
     if (g_oairt.ws_state == WS_CONNECTED && g_oairt.ws_client) {
         g_oairt.ws_state = WS_DISCONNECTING;
         lws_callback_on_writable(g_oairt.ws_client);
     }
 
     g_oairt.ws_state = WS_DISCONNECTED;
     g_oairt.ws_client = NULL;
     g_oairt.session_ready = false;
 
     info("openai_rt: WebSocket disconnected\n");
 }
 
 /* Check if WebSocket is ready for use */
 bool websocket_is_ready(void)
 {
     return (g_oairt.ws_state == WS_CONNECTED && g_oairt.session_ready);
 }
 
 /* Wait for WebSocket to be ready (with timeout) */
 int websocket_wait_ready(int timeout_ms)
 {
     uint64_t start_time = lws_now_usecs();
     uint64_t timeout_us = timeout_ms * 1000ULL;
 
     while (!websocket_is_ready()) {
         uint64_t now = lws_now_usecs();
         if (now - start_time > timeout_us) {
             warning("openai_rt: WebSocket ready timeout after %d ms\n", timeout_ms);
             return ETIMEDOUT;
         }
         /* Small sleep to avoid busy waiting */
         sys_msleep(10);
     }
 
     info("openai_rt: WebSocket is ready for use\n");
     return 0;
 }
 
 /* Get WebSocket status string for debugging */
 const char *websocket_status_string(void)
 {
     switch (g_oairt.ws_state) {
     case WS_DISCONNECTED:
         return "DISCONNECTED";
     case WS_CONNECTING:
         return "CONNECTING";
     case WS_CONNECTED:
         return g_oairt.session_ready ? "CONNECTED_READY" : "CONNECTED_NOT_READY";
     case WS_DISCONNECTING:
         return "DISCONNECTING";
     default:
         return "UNKNOWN";
     }
 }
 
 void send_session_update(void)
{
    char *json_msg = NULL;
    char *escaped_prompt = NULL;
    int err;

    if (g_oairt.ws_state != WS_CONNECTED) {
        warning("openai_rt: Cannot send session update - WebSocket not connected\n");
        return;
    }

    /* Escape JSON special characters in the prompt */
    escaped_prompt = mem_zalloc(str_len(g_oairt.prompt) * 2 + 1, NULL);
    if (!escaped_prompt) {
        warning("openai_rt: Failed to allocate memory for escaped prompt\n");
        return;
    }
    
    /* Simple JSON escaping - escape quotes, backslashes, and control characters */
    const char *src = g_oairt.prompt;
    char *dst = escaped_prompt;
    while (*src) {
        switch (*src) {
        case '"':
            *dst++ = '\\';
            *dst++ = '"';
            break;
        case '\\':
            *dst++ = '\\';
            *dst++ = '\\';
            break;
        case '\b':
            *dst++ = '\\';
            *dst++ = 'b';
            break;
        case '\f':
            *dst++ = '\\';
            *dst++ = 'f';
            break;
        case '\n':
            *dst++ = '\\';
            *dst++ = 'n';
            break;
        case '\r':
            *dst++ = '\\';
            *dst++ = 'r';
            break;
        case '\t':
            *dst++ = '\\';
            *dst++ = 't';
            break;
        default:
            if (*src < 0x20) {
                /* Escape other control characters as \uXXXX */
                re_snprintf(dst, 7, "\\u%04x", (unsigned char)*src);
                dst += 6;
            } else {
                *dst++ = *src;
            }
            break;
        }
        src++;
    }
    *dst = '\0';

    re_sdprintf(&json_msg,
       "{"
           "\"type\":\"session.update\","
           "\"session\":{"
               "\"type\":\"realtime\","
               "\"instructions\": \"%s\","
               "\"tool_choice\": \"auto\","
               "\"tools\": ["
                   "{"
                       "\"type\": \"function\","
                       "\"name\": \"hangup_call\","
                       "\"description\": \"Hang up the call\""
                   "}"
               "]"            
           "}"
       "}",
        escaped_prompt
    );

     info("openai_rt: Session update: %s\n", json_msg);

     if (json_msg) {
         err = queue_message_to_openai(json_msg, str_len(json_msg), NULL, NULL);
         if (err) {
             warning("openai_rt: Failed to queue session update: %m\n", err);
         } else {
             info("openai_rt: Session update queued (PCM16 format)\n");
         }
         mem_deref(json_msg);
     }
     
     mem_deref(escaped_prompt);
 }
 
 int queue_message_to_openai(const char *json_msg, size_t len,
    void (*callback)(void *arg, int err), void *arg)
 {
    struct ws_message *msg = mem_zalloc(sizeof(*msg), ws_message_destructor);
    if (!msg) return ENOMEM;
 
    msg->type = WS_MSG_TO_OPENAI;
    msg->data = mem_alloc(LWS_PRE + len + 1, NULL);
    if (!msg->data) { mem_deref(msg); return ENOMEM; }
 
    memcpy(msg->data + LWS_PRE, json_msg, len);
    msg->data[LWS_PRE + len] = '\0';
    msg->len = len;
    msg->callback = callback;
    msg->arg = arg;
 
    pthread_mutex_lock(&g_oairt.ws_mutex);
    //DEBUG_INFO("Queueing message to OpenAI: %s\n", json_msg);
    list_append(&g_oairt.to_openai_queue, &msg->le, msg);
    pthread_mutex_unlock(&g_oairt.ws_mutex);
 
    /* Wake the I/O thread right away */
    ws_wake_service();
 
    /* If already connected, also request a writable callback */
    if (g_oairt.ws_state == WS_CONNECTED && g_oairt.ws_client) {
        lws_callback_on_writable(g_oairt.ws_client);
    }
 
    return 0;
 }
 
 /* Queue message received from OpenAI */
 int queue_message_from_openai(const uint8_t *data, size_t len)
 {
     struct ws_message *msg;
 
     //DEBUG_INFO("Queueing message from OpenAI: %zu bytes\n", len);
 
     msg = mem_zalloc(sizeof(*msg), ws_message_destructor);
     if (!msg)
         return ENOMEM;
 
     msg->type = WS_MSG_FROM_OPENAI;
     msg->data = mem_alloc(len, NULL);
     if (!msg->data) {
         mem_deref(msg);
         return ENOMEM;
     }
 
     memcpy(msg->data, data, len);
     msg->len = len;
     msg->callback = NULL;
     msg->arg = NULL;
 
     pthread_mutex_lock(&g_oairt.ws_mutex);
     list_append(&g_oairt.from_openai_queue, &msg->le, msg);
     pthread_mutex_unlock(&g_oairt.ws_mutex);
 
     //DEBUG_INFO("Message queued from OpenAI, queue size: %u\n", list_count(&g_oairt.from_openai_queue));
 
     return 0;
 }
 
 /* WebSocket thread - processes events and audio queues */
 static void *websocket_thread(void *arg)
 {
     (void)arg;
 
     info("openai_rt: WebSocket thread started\n");
 
     /* Create WebSocket context in this thread */
     struct lws_context_creation_info ctx_info;
     memset(&ctx_info, 0, sizeof(ctx_info));
     ctx_info.port = CONTEXT_PORT_NO_LISTEN;
     ctx_info.protocols = protocols;
     ctx_info.gid = -1;
     ctx_info.uid = -1;
     ctx_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT; /* pure client; no explicit vhost */
 
     g_oairt.ws_context = lws_create_context(&ctx_info);
     if (!g_oairt.ws_context) {
         warning("openai_rt: Failed to create WebSocket context in thread\n");
         return NULL;
     }
 
     /* Enable libwebsockets debugging after context creation */
     lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE, NULL);
 
     info("openai_rt: WebSocket context created in thread\n");
 
     /* Connect immediately when thread starts */
     if (g_oairt.ws_state == WS_DISCONNECTED && g_oairt.ws_context) {
         info("openai_rt: WebSocket thread starting - connecting to OpenAI immediately\n");
         int err = connect_openai_ws();
         if (err) {
             warning("openai_rt: Failed to connect to OpenAI on thread start: %m\n", err);
         }
     }
 
     uint64_t last_connecting_log = 0;
     uint64_t connection_start_time = 0;
 
     while (g_oairt.ws_thread_running) {
         /* Use much shorter timeout for more responsive WebSocket processing */
         int timeout = g_oairt.ws_thread_running ? 10 : 1;
 
         /* Always service the context to advance the handshake / IO state machine */
         int ret = lws_service(g_oairt.ws_context, timeout);
         if (ret < 0) {
             warning("openai_rt: lws_service returned error: %d\n", ret);
         }
 
         /* Check shutdown flag more frequently during shutdown */
         if (!g_oairt.ws_thread_running) {
             break;
         }
 
         /* If shutdown was requested, exit immediately */
         if (!g_oairt.ws_thread_running) {
             info("openai_rt: WebSocket shutdown requested, exiting immediately\n");
             break;
         }
 
         /* During shutdown, use minimal processing to exit quickly */
         if (!g_oairt.ws_thread_running) {
             /* Skip all other processing during shutdown */
             sys_msleep(1);
             continue;
         }
 
         uint64_t now = lws_now_usecs();
 
         /* Log when actively connecting; manage a simple timeout */
         if (g_oairt.ws_state == WS_CONNECTING) {
             if (!connection_start_time) {
                 connection_start_time = now;
             }
             if (now - last_connecting_log > 1000000) { /* 1s */
                 info("openai_rt: WebSocket thread actively servicing connection, state: %d\n", g_oairt.ws_state);
                 last_connecting_log = now;
             }
             if (now - connection_start_time > 30000000) { /* 30s */
                 warning("openai_rt: WebSocket connection timeout, aborting\n");
                 g_oairt.ws_state = WS_DISCONNECTED;
                 g_oairt.ws_client = NULL;
                 connection_start_time = 0;
                 last_connecting_log = 0;
             }
         }
 
         /* Auto-reconnect if disconnected and not actively connecting */
         if (g_oairt.ws_state == WS_DISCONNECTED && !g_oairt.ws_client && g_oairt.ws_context) {
             static uint64_t last_reconnect_attempt = 0;
             uint64_t now3 = now;
             if (now3 - last_reconnect_attempt > 10000000) { /* 10s */
                 info("openai_rt: Attempting to reconnect to OpenAI...\n");
                 int err = connect_openai_ws();
                 if (err) {
                     warning("openai_rt: Reconnection attempt failed: %m\n", err);
                 }
                 last_reconnect_attempt = now3;
             }
         }
 
         /* Process events from event queue */
         struct audio_event *event = audio_get_next_event();
         if (event) {
             switch (event->type) {
             case EVENT_CALL_START:
                 info("openai_rt: Processing call start event\n");
                 if (g_oairt.ws_state == WS_DISCONNECTED && g_oairt.ws_context) {
                     info("openai_rt: Call start - ensuring OpenAI connection...\n");
                     int err = connect_openai_ws();
                     if (err) {
                         warning("openai_rt: Failed to connect to OpenAI: %m\n", err);
                     }
                 }
                 break;
 
             case EVENT_CALL_END:
                 info("openai_rt: Processing call end event\n");
                 /* Keep connection alive */
                 info("openai_rt: Call ended, keeping OpenAI connection alive\n");
                 break;
             }
             mem_deref(event);
         }

 
         /* Gentle pacing; a shorter sleep when connecting improves responsiveness */
         /* Use very short sleep during shutdown to be more responsive */
         int sleep_time = g_oairt.ws_thread_running ?
                        (g_oairt.ws_state == WS_CONNECTING ? 1 : 10) : 1;
         sys_msleep(sleep_time);
 
         /* Check shutdown flag again after sleep */
         if (!g_oairt.ws_thread_running) {
             break;
         }
     }
 
     info("openai_rt: WebSocket thread ended\n");
     return NULL;
 }
 
 /* Initialize WebSocket */
 int websocket_init(void)
 {
 
     g_oairt.ws_state = WS_DISCONNECTED;
     g_oairt.ws_context = NULL; /* Will be created in thread */
     g_oairt.ws_client = NULL;
 
     /* Initialize message queues */
     list_init(&g_oairt.to_openai_queue);
     list_init(&g_oairt.from_openai_queue);
 
     /* Initialize mutex and condition variable */
     pthread_mutex_init(&g_oairt.ws_mutex, NULL);
     pthread_cond_init(&g_oairt.ws_cond, NULL);
 
     /* Start WebSocket thread */
     g_oairt.ws_thread_running = true;
     int ret = pthread_create(&g_oairt.ws_thread, NULL, websocket_thread, NULL);
     if (ret != 0) {
         warning("openai_rt: Failed to create WebSocket thread: %d\n", ret);
         pthread_mutex_destroy(&g_oairt.ws_mutex);
         pthread_cond_destroy(&g_oairt.ws_cond);
         return ENOMEM;
     }
 
     info("openai_rt: WebSocket subsystem initialized - thread will create context and connect immediately\n");
     return 0;
 }
 
 /* Close WebSocket */
 void websocket_close(void)
 {
 
     /* Stop WebSocket thread */
     g_oairt.ws_thread_running = false;
 
     /* Cancel any ongoing WebSocket operations to speed up shutdown */
     if (g_oairt.ws_context) {
         lws_cancel_service(g_oairt.ws_context);
     }
 
     /* Signal thread to wake up and exit */
     pthread_mutex_lock(&g_oairt.ws_mutex);
     pthread_cond_signal(&g_oairt.ws_cond);
     pthread_mutex_unlock(&g_oairt.ws_mutex);
 
     /* Wait for thread to exit with timeout */
     struct timespec timeout;
     clock_gettime(CLOCK_REALTIME, &timeout);
     timeout.tv_sec += 2; /* 2 second timeout */
 
     int join_result = pthread_timedjoin_np(g_oairt.ws_thread, NULL, &timeout);
     if (join_result == ETIMEDOUT) {
         warning("openai_rt: WebSocket thread shutdown timeout, forcing termination\n");
         /* Force thread termination - this is not ideal but prevents hanging */
         pthread_cancel(g_oairt.ws_thread);
         pthread_join(g_oairt.ws_thread, NULL);
     } else if (join_result != 0) {
         warning("openai_rt: WebSocket thread join failed: %d\n", join_result);
     }
 
     /* Disconnect if connected */
     if (g_oairt.ws_state != WS_DISCONNECTED) {
         disconnect_openai_ws();
     }
 
     /* Destroy context */
     if (g_oairt.ws_context) {
         lws_context_destroy(g_oairt.ws_context);
         g_oairt.ws_context = NULL;
     }
 
     g_oairt.ws_client = NULL;
     g_oairt.ws_state = WS_DISCONNECTED;
     g_oairt.session_ready = false;
 
         /* Clear message queues properly */
    websocket_clear_message_queue();
 
     /* Clean up mutex and condition variable */
     pthread_mutex_destroy(&g_oairt.ws_mutex);
     pthread_cond_destroy(&g_oairt.ws_cond);
 
     DEBUG_INFO("WebSocket subsystem closed\n");
 }
 
 /* Request immediate WebSocket shutdown */
 void websocket_request_shutdown(void)
 {
 
     /* Set shutdown flag */
     g_oairt.ws_thread_running = false;
 
     /* Cancel any ongoing WebSocket operations */
     if (g_oairt.ws_context) {
         lws_cancel_service(g_oairt.ws_context);
     }
 
     /* Signal thread to wake up immediately */
     pthread_mutex_lock(&g_oairt.ws_mutex);
     pthread_cond_signal(&g_oairt.ws_cond);
     pthread_mutex_unlock(&g_oairt.ws_mutex);
 
     DEBUG_INFO("WebSocket shutdown requested\n");
 }
 
 /* Force WebSocket shutdown with timeout */
 void websocket_force_shutdown(int timeout_ms)
 {
 
     /* Request shutdown first */
     websocket_request_shutdown();
 
     /* Wait for thread to exit with timeout */
     struct timespec timeout;
     clock_gettime(CLOCK_REALTIME, &timeout);
     timeout.tv_nsec += timeout_ms * 1000000; /* Convert ms to ns */
     if (timeout.tv_nsec >= 1000000000) {
         timeout.tv_sec += timeout.tv_nsec / 1000000000;
         timeout.tv_nsec %= 1000000000;
     }
 
     int join_result = pthread_timedjoin_np(g_oairt.ws_thread, NULL, &timeout);
     if (join_result == ETIMEDOUT) {
         warning("openai_rt: WebSocket thread force shutdown timeout, cancelling thread\n");
         pthread_cancel(g_oairt.ws_thread);
         pthread_join(g_oairt.ws_thread, NULL);
     }
 
     DEBUG_INFO("WebSocket force shutdown completed\n");
 }
 
 /* Clear all pending messages in the WebSocket queue */
 void websocket_clear_message_queue(void)
 {
     struct ws_message *msg = NULL;
     struct le *le;
 
     DEBUG_INFO("Clearing WebSocket message queue\n");
 
     pthread_mutex_lock(&g_oairt.ws_mutex);
 
     /* Clear messages to OpenAI */
     while ((le = list_head(&g_oairt.to_openai_queue))) {
         msg = list_ledata(le);
         list_unlink(le);
         mem_deref(msg);
     }
 
     /* Clear messages from OpenAI */
     while ((le = list_head(&g_oairt.from_openai_queue))) {
         msg = list_ledata(le);
         list_unlink(le);
         mem_deref(msg);
     }
 
     pthread_mutex_unlock(&g_oairt.ws_mutex);
 
     DEBUG_INFO("WebSocket message queue cleared\n");
 }