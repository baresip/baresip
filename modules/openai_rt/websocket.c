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
 #include "ai_model.h"
 
/* Forward declarations */
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);
static int add_auth_headers(void *in, size_t len);
static void process_outgoing_messages(void);
static void send_function_call_output(const char *call_id, const char *output);
static void send_response_create(void);
static void process_conversation_kick_pending(void);

/* Callbacks for AI model message parsing */
static void handle_audio_delta(const char *base64_audio, void *arg);
static void handle_session_updated_cb(void *arg);
static void handle_speech_started_cb(void *arg);
static void handle_function_call_cb(const char *call_id, const char *name,
                                    const char *arguments, void *arg);
static void handle_response_done_cb(const char *response_json, void *arg);
 
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
 
#define WS_DRAIN_MAX_PER_WRITABLE 32

 /* Process outgoing messages (drain multiple per writable callback) */
 static void process_outgoing_messages(void)
 {
     unsigned n;

     for (n = 0; n < WS_DRAIN_MAX_PER_WRITABLE; n++) {
         struct ws_message *msg = NULL;
         struct le *le;
         bool is_audio;
         int written;

         if (!g_oairt.ws_client)
             break;

         if (lws_send_pipe_choked(g_oairt.ws_client))
             break;

         pthread_mutex_lock(&g_oairt.ws_mutex);
         le = list_head(&g_oairt.to_openai_queue);
         if (le) {
             msg = list_ledata(le);
             list_unlink(le);
         }
         pthread_mutex_unlock(&g_oairt.ws_mutex);

         if (!msg)
             break;

         is_audio = (msg->len > 0 &&
             (strstr((const char *)(msg->data + LWS_PRE), "\"audio\"") != NULL ||
              strstr((const char *)(msg->data + LWS_PRE), "\"realtimeInput\"") != NULL ||
              strstr((const char *)(msg->data + LWS_PRE), "\"audioBytes\"") != NULL));

         written = lws_write(g_oairt.ws_client, msg->data + LWS_PRE,
                             msg->len, LWS_WRITE_TEXT);
         if (written < 0) {
             warning("openai_rt: Failed to write WebSocket message\n");
             pthread_mutex_lock(&g_oairt.ws_mutex);
             list_prepend(&g_oairt.to_openai_queue, &msg->le, msg);
             pthread_mutex_unlock(&g_oairt.ws_mutex);
             break;
         }

         if (written < (int)msg->len) {
             warning("openai_rt: partial WS write (%d/%zu), requeueing message\n",
                     written, msg->len);
             pthread_mutex_lock(&g_oairt.ws_mutex);
             list_prepend(&g_oairt.to_openai_queue, &msg->le, msg);
             pthread_mutex_unlock(&g_oairt.ws_mutex);
             break;
         }

         if (!is_audio)
             DEBUG_INFO("process_outgoing_messages: Message sent (%zu bytes)\n",
                        msg->len);

         if (msg->callback)
             msg->callback(msg->arg, 0);
         mem_deref(msg);
     }

     pthread_mutex_lock(&g_oairt.ws_mutex);
     if (!list_isempty(&g_oairt.to_openai_queue) && g_oairt.ws_client)
         lws_callback_on_writable(g_oairt.ws_client);
     pthread_mutex_unlock(&g_oairt.ws_mutex);
 }
 
static inline void ws_wake_service(void)
{
    if (g_oairt.ws_context) {
        /* break poll and run lws_service() ASAP */
        lws_cancel_service(g_oairt.ws_context);
    }
}

static void send_function_call_output(const char *call_id, const char *output)
{
    struct ai_model *model = get_ai_model();
    if (!model || !model->build_function_call_output) {
        warning("openai_rt: AI model not initialized\n");
        return;
    }
    
    char *json_msg = NULL;
    int err = model->build_function_call_output(call_id, output, &json_msg);
    if (err) {
        warning("openai_rt: Failed to build function_call_output: %m\n", err);
        return;
    }
    
    if (json_msg) {
        err = queue_message_to_openai(json_msg, str_len(json_msg), NULL, NULL);
        if (err) {
            warning("openai_rt: Failed to queue function_call_output: %m\n", err);
        }
        mem_deref(json_msg);
    }
}

static void send_response_create(void)
{
    struct ai_model *model = get_ai_model();
    char *json_msg = NULL;
    int err;

    if (g_audio.response_created)
        return;

    if (!model || !model->build_response_create) {
        warning("openai_rt: AI model not initialized for response.create\n");
        return;
    }

    err = model->build_response_create(NULL, &json_msg);
    if (err) {
        warning("openai_rt: Failed to build response.create: %m\n", err);
        return;
    }

    if (!json_msg)
        return;

    err = queue_message_to_openai(json_msg, str_len(json_msg), NULL, NULL);
    if (err) {
        warning("openai_rt: Failed to queue response.create: %m\n", err);
    } else {
        info("openai_rt: Conversation start message queued\n");
        g_audio.response_created = true;
    }
    mem_deref(json_msg);
}
 
/* Callbacks for AI model message parsing */
static void handle_audio_delta(const char *base64_audio, void *arg)
{
    (void)arg;

    if (!base64_audio || !*base64_audio) {
        //DEBUG_INFO("[AUDIO RX] handle_audio_delta called but no audio data\n");
        return;
    }
    
    if (!g_oairt.call_active) {
        //DEBUG_INFO("[AUDIO RX] handle_audio_delta called but call not active, dropping audio\n");
        return;
    }

    //size_t b64_len = str_len(base64_audio);
    //info("[AUDIO RX] Received base64 audio data (%zu chars), decoding...\n", b64_len);

    /* Decode base64 -> PCM16LE */
    uint8_t *decoded = NULL;
    size_t nbytes = decode_audio_base64(base64_audio, &decoded);

    if (decoded && nbytes >= 2) {
        const int16_t *pcm = (const int16_t *)decoded;
        size_t nsamp = nbytes / 2;
        int16_t peak = 0;
        size_t i;

        for (i = 0; i < nsamp; i++) {
            int16_t a = pcm[i] < 0 ? (int16_t)-pcm[i] : pcm[i];
            if (a > peak)
                peak = a;
        }
        if (peak < OUTPUT_DELTA_PEAK_MIN) {
            mem_deref(decoded);
            return;
        }
        
        int err = write_to_injection_buffer(pcm, nsamp);
        if (err) {
            warning("openai_rt: write_to_injection_buffer failed: %m\n", err);
        } else {
            //info("[AUDIO RX] Successfully wrote %zu samples to injection buffer\n", nsamp);
        }
    } else {
        warning("[AUDIO RX] Failed to decode audio or insufficient data (nbytes=%zu)\n", nbytes);
    }

    if (decoded) mem_deref(decoded);
}

static void handle_session_updated_cb(void *arg)
{
    (void)arg;

    g_oairt.session_cfg_applied = true;
    g_oairt.session_ready = true;
    info("openai_rt: session.updated received; input_audio_format now active\n");
    audio_flush_accumulated();
    if (!g_oairt.wait_for_greeting)
        g_oairt.conversation_kick_pending = true;
}

static void handle_speech_started_cb(void *arg)
{
    (void)arg;

    DEBUG_INFO("openai_rt: speech.started, interrupting outbound TTS\n");
    audio_clear_injection_buffer();
}

static void handle_function_call_cb(const char *call_id, const char *name,
                                    const char *arguments, void *arg)
{
    (void)arg;

    /* Validate that the tool call is enabled in configuration */
    if (!ai_model_is_tool_enabled(name, g_oairt.enabled_tools)) {
        warning("openai_rt: Tool call '%s' is not enabled in configuration. Rejecting.\n", name);
        /* Send error response back to OpenAI */
        char error_msg[512];
        re_snprintf(error_msg, sizeof(error_msg), 
                    "Error: Tool call '%s' is not enabled. Only these tools are available: %s",
                    name, g_oairt.enabled_tools);
        send_function_call_output(call_id, error_msg);
        return;
    }

    /* Process enabled tool calls */
    if (strcmp(name, AI_TOOL_HANGUP_CALL.name) == 0) {
        DEBUG_INFO("openai_rt: Executing hangup_call function\n");
        calls_hangup();
        send_function_call_output(call_id, "Call hung up");
        /* No response.create needed after hangup */
    } 	else if (strcmp(name, AI_TOOL_SEND_DTMF.name) == 0) {
		DEBUG_INFO("openai_rt: Executing send_dtmf function\n");

		/* Parse arguments JSON */
		struct json_object *args_obj = json_tokener_parse(arguments);
		if (!args_obj) {
			warning("openai_rt: Failed to parse send_dtmf arguments\n");
			send_function_call_output(call_id, "Error: Failed to parse function arguments");
			return;
		}

		struct json_object *digits_obj = NULL;
		if (json_object_object_get_ex(args_obj, "digits", &digits_obj) &&
			json_object_is_type(digits_obj, json_type_string)) {
			const char *digits = json_object_get_string(digits_obj);
			if (digits && *digits) {
				int err = calls_send_dtmf(digits);
				if (!err) {
					char output[256];
					re_snprintf(output, sizeof(output), "DTMF tones sent: %s", digits);
					send_function_call_output(call_id, output);
				} else {
					char error_msg[256];
					re_snprintf(error_msg, sizeof(error_msg), 
							   "Error: Failed to send DTMF string '%s'", digits);
					send_function_call_output(call_id, error_msg);
					warning("openai_rt: Failed to send DTMF string '%s': %m\n", digits, err);
				}
			} else {
				send_function_call_output(call_id, "Error: Missing or empty 'digits' parameter");
			}
		} else {
			send_function_call_output(call_id, "Error: Missing or invalid 'digits' parameter");
		}
		json_object_put(args_obj);

		/* Trigger response for OpenAI to acknowledge DTMF */
		if (g_oairt.backend_type == AI_BACKEND_OPENAI_REALTIME) {
			send_response_create();
		}
	} else if (strcmp(name, AI_TOOL_API_CALL.name) == 0) {
		DEBUG_INFO("openai_rt: Executing api_call function\n");

		/* Parse arguments JSON */
		struct json_object *args_obj = json_tokener_parse(arguments);
		if (!args_obj) {
			warning("openai_rt: Failed to parse api_call arguments\n");
			send_function_call_output(call_id, "Error: Failed to parse function arguments");
			return;
		}

		const char *method = NULL;
		const char *uri = NULL;
		const char *content_type = NULL;
		const char *auth_type = NULL;
		const char *auth_username = NULL;
		const char *auth_password = NULL;
		const char *body = NULL;

		struct json_object *obj = NULL;
		if (json_object_object_get_ex(args_obj, "method", &obj))
			method = json_object_get_string(obj);
		if (json_object_object_get_ex(args_obj, "uri", &obj))
			uri = json_object_get_string(obj);
		if (json_object_object_get_ex(args_obj, "content_type", &obj))
			content_type = json_object_get_string(obj);
		if (json_object_object_get_ex(args_obj, "auth_type", &obj))
			auth_type = json_object_get_string(obj);
		if (json_object_object_get_ex(args_obj, "auth_username", &obj))
			auth_username = json_object_get_string(obj);
		if (json_object_object_get_ex(args_obj, "auth_password", &obj))
			auth_password = json_object_get_string(obj);
		if (json_object_object_get_ex(args_obj, "body", &obj))
			body = json_object_get_string(obj);

		if (method && uri) {
			char *api_output = NULL;
			int err = calls_api_call(method, uri, content_type, auth_type,
				auth_username, auth_password, body, &api_output);
			
			if (err == 0) {
				send_function_call_output(call_id, api_output ? api_output : "Success");
			} else {
				char error_msg[256];
				re_snprintf(error_msg, sizeof(error_msg), "Error: API call failed with code %d", err);
				send_function_call_output(call_id, error_msg);
			}
			mem_deref(api_output);
		} else {
			send_function_call_output(call_id, "Error: Missing 'method' or 'uri' parameter");
		}
		json_object_put(args_obj);

		/* Trigger response for OpenAI to process API result */
		if (g_oairt.backend_type == AI_BACKEND_OPENAI_REALTIME) {
			send_response_create();
		}
	} else {
        /* This shouldn't happen if validation above worked, but handle it anyway */
        warning("openai_rt: Unknown function call: %s (but was enabled in config?)\n", name);
        char error_msg[256];
        re_snprintf(error_msg, sizeof(error_msg), 
                   "Error: Unknown or unsupported tool call: %s", name);
        send_function_call_output(call_id, error_msg);
    }
}

static void handle_response_done_cb(const char *response_json, void *arg)
{
    (void)arg;

    DEBUG_INFO("openai_rt: response.done received\n");
    if (response_json) {
        int err = calls_queue_openai_response(response_json);
        if (err) {
            warning("openai_rt: Failed to queue OpenAI response: %m\n", err);
        }
    }
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
         {
             info("openai_rt: WebSocket connection established\n");
             g_oairt.ws_state = WS_CONNECTED;
             g_oairt.ws_client = wsi;

             /* not ready yet: wait for server to ack session.update */
             g_oairt.session_ready = false;
             g_oairt.session_cfg_applied = false;
             g_oairt.conversation_kick_pending = false;

             send_session_update();
             
             /* Request writable callback to send session update and any queued messages */
             pthread_mutex_lock(&g_oairt.ws_mutex);
             size_t established_queue_size = list_count(&g_oairt.to_openai_queue);
             pthread_mutex_unlock(&g_oairt.ws_mutex);
             DEBUG_INFO("WebSocket connected, queue size=%zu, requesting writable callback\n", established_queue_size);
             lws_callback_on_writable(wsi);
         }
         break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            pthread_mutex_lock(&g_oairt.ws_mutex);
            //size_t queue_size = list_count(&g_oairt.to_openai_queue);
            pthread_mutex_unlock(&g_oairt.ws_mutex);
            //DEBUG_INFO("openai_rt: WebSocket writable callback, queue size=%zu\n", queue_size);
            /* Process outgoing messages */
            process_outgoing_messages();
        }
        break;
 
    case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            /* Check if this message contains audio data */
            bool has_audio = (len > 0 && 
                              (strstr((const char *)in, "\"audio\"") != NULL ||
                               strstr((const char *)in, "\"audioBytes\"") != NULL ||
                               strstr((const char *)in, "\"serverContent\"") != NULL));
            
            if (has_audio) {
                //info("[AUDIO RX] WebSocket received message with audio data (%zu bytes)\n", len);
            } else {
                info("openai_rt: WebSocket received %zu bytes\n", len);
            }
            
            if (len > 0 && !has_audio) {
                info("openai_rt: Received message from AI model: %.200s%s\n", 
                     (const char *)in, len > 200 ? "..." : "");
            }

            if (len > 0 && g_oairt.ws_state == WS_CONNECTED) {
               struct ai_model *model = get_ai_model();
               if (model && model->parse_message) {
                   int parse_err = model->parse_message((const char *)in,
                                      handle_audio_delta,
                                      handle_session_updated_cb,
                                      handle_speech_started_cb,
                                      handle_function_call_cb,
                                      handle_response_done_cb,
                                      NULL);
                   if (parse_err) {
                       DEBUG_INFO("openai_rt: parse_message returned error %d\n", parse_err);
                   }
               }
            }
        }
        break;
 
     case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
         warning("openai_rt: WebSocket connection error\n");
         g_oairt.ws_state = WS_DISCONNECTED;
         g_oairt.ws_client = NULL;
         g_oairt.session_ready = false;
         g_oairt.session_cfg_applied = false;
         g_oairt.setup_sent = false;
         g_oairt.conversation_kick_pending = false;
         websocket_clear_message_queue();
         break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        info("openai_rt: WebSocket disconnected\n");
        if (len > 0 && in) {
            info("openai_rt: Close reason/data: %.200s\n", (const char *)in);
        }
        g_oairt.ws_state = WS_DISCONNECTED;
        g_oairt.ws_client = NULL;
        g_oairt.session_ready = false;
        g_oairt.session_cfg_applied = false;
        g_oairt.setup_sent = false;
        g_oairt.conversation_kick_pending = false;
        websocket_clear_message_queue();
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
 
    case 38:  /* LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED or similar */
        info("openai_rt: WebSocket callback: reason=38 (likely client confirm extension)\n");
        break;
    
    default:
        info("openai_rt: WebSocket callback: reason=%d (unhandled)\n", reason);
        if (len > 0 && in) {
            info("openai_rt: Callback data: %.200s%s\n", 
                 (const char *)in, len > 200 ? "..." : "");
        }
        break;
     }
 
     return 0;
 }
 
 static int add_auth_headers(void *in, size_t len)
 {
     struct ai_model *model = get_ai_model();
     if (!model || !model->add_auth_headers) {
         warning("openai_rt: AI model not initialized\n");
         return -1;
     }
     return model->add_auth_headers(in, len);
 }
 
 /* Connect to AI model WebSocket */
 int connect_openai_ws(void)
 {
     struct lws_client_connect_info ccinfo;
     struct ai_model *model = get_ai_model();
     char address[256];
     char path[512];
     int port;

     if (g_oairt.ws_state != WS_DISCONNECTED) {
         warning("openai_rt: WebSocket already connected or connecting\n");
         return EALREADY;
     }
     if (!g_oairt.ws_context) {
         warning("openai_rt: WebSocket context not initialized\n");
         return ENOMEM;
     }
     if (!model || !model->get_connection_info) {
         warning("openai_rt: AI model not initialized\n");
         return EINVAL;
     }

     int err = model->get_connection_info(address, sizeof(address), &port, path, sizeof(path));
     if (err) {
         warning("openai_rt: Failed to get connection info: %m\n", err);
         return err;
     }

     info("openai_rt: Initiating WebSocket connection to AI model...\n");
     g_oairt.ws_state = WS_CONNECTING;

     memset(&ccinfo, 0, sizeof(ccinfo));
     ccinfo.context = g_oairt.ws_context;
     ccinfo.address = address;
     ccinfo.port = port;
     ccinfo.path = path;
     ccinfo.host = address;                           /* SNI / Host header */
     ccinfo.origin = NULL;                            /* Not required */
     ccinfo.protocol = NULL;                          /* DO NOT send Sec-WebSocket-Protocol */
     ccinfo.ietf_version_or_minus_one = -1;           /* Auto-select WS version */
     ccinfo.ssl_connection = LCCSCF_USE_SSL;          /* Verify cert + hostname in prod */
     ccinfo.pwsi = &g_oairt.ws_client;                /* Single authoritative pointer */

     info("openai_rt: Connecting to wss://%s:%d%s\n",
          ccinfo.address, ccinfo.port, ccinfo.path);
     info("openai_rt: Calling lws_client_connect_via_info...\n");

     g_oairt.ws_client = lws_client_connect_via_info(&ccinfo);
     if (!g_oairt.ws_client) {
         warning("openai_rt: Failed to connect to AI model WebSocket\n");
         g_oairt.ws_state = WS_DISCONNECTED;
         return ENOMEM;
     }

     info("openai_rt: lws_client_connect_via_info returned: %p\n", (void*)g_oairt.ws_client);
     info("openai_rt: WebSocket connection initiated, waiting for callback...\n");
     return 0;
 }
 
 /* Disconnect from AI model */
 void disconnect_openai_ws(void)
 {
     if (g_oairt.ws_state == WS_CONNECTED && g_oairt.ws_client) {
         g_oairt.ws_state = WS_DISCONNECTING;
         lws_callback_on_writable(g_oairt.ws_client);
     }

     g_oairt.ws_state = WS_DISCONNECTED;
     g_oairt.ws_client = NULL;
     g_oairt.session_ready = false;
     g_oairt.session_cfg_applied = false;
     g_oairt.setup_sent = false;
     g_oairt.conversation_kick_pending = false;
     websocket_clear_message_queue();

     info("openai_rt: WebSocket disconnected from AI model\n");
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
 
void websocket_kick_session_setup(void)
{
	if (g_oairt.ws_state == WS_CONNECTED && !g_oairt.session_ready &&
	    !g_oairt.setup_sent)
		send_session_update();

	ws_wake_service();

	if (g_oairt.ws_state == WS_CONNECTED && g_oairt.ws_client)
		lws_callback_on_writable(g_oairt.ws_client);
}


static void process_conversation_kick_pending(void)
{
	if (!g_oairt.conversation_kick_pending)
		return;
	if (g_oairt.ws_state != WS_CONNECTED || !g_oairt.ws_client)
		return;
	if (!g_oairt.session_cfg_applied || g_oairt.wait_for_greeting)
		return;
	if (!g_oairt.call_active)
		return;
	if (g_audio.response_created)
		return;

	g_oairt.conversation_kick_pending = false;
	info("openai_rt: Starting AI conversation (wait_for_greeting=no)\n");
	send_response_create();
	if (g_oairt.ws_client)
		lws_callback_on_writable(g_oairt.ws_client);
}

 void send_session_update(void)
{
    struct ai_model *model = get_ai_model();
    char *json_msg = NULL;
    int err;

    if (g_oairt.ws_state != WS_CONNECTED) {
        warning("openai_rt: Cannot send session update - WebSocket not connected\n");
        return;
    }

    if (g_oairt.setup_sent) {
        DEBUG_INFO("openai_rt: Setup already sent on this connection, skipping\n");
        return;
    }

    if (!model || !model->build_session_update) {
        warning("openai_rt: AI model not initialized\n");
        return;
    }

    err = model->build_session_update(g_oairt.prompt, &json_msg);
    if (err) {
        warning("openai_rt: Failed to build session update: %m\n", err);
        return;
    }

    info("openai_rt: Session update: %s\n", json_msg);

    if (json_msg) {
        err = queue_message_to_openai(json_msg, str_len(json_msg), NULL, NULL);
        if (err) {
            warning("openai_rt: Failed to queue session update: %m\n", err);
        } else {
            g_oairt.setup_sent = true;
            info("openai_rt: Session update queued (PCM16 format)\n");
        }
        mem_deref(json_msg);
    }
}
 
 int queue_message_to_openai(const char *json_msg, size_t len,
    void (*callback)(void *arg, int err), void *arg)
 {
    if (g_oairt.ws_state == WS_DISCONNECTED ||
        g_oairt.ws_state == WS_DISCONNECTING) {
        return ENOTCONN;
    }

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
        //DEBUG_INFO("queue_message_to_openai: Requesting writable callback\n");
        lws_callback_on_writable(g_oairt.ws_client);
    } else {
        DEBUG_INFO("queue_message_to_openai: Not requesting writable (state=%d, client=%p)\n",
                   g_oairt.ws_state, g_oairt.ws_client);
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
        /* Service the context to advance the handshake / IO state machine */
        int ret = lws_service(g_oairt.ws_context, 10);
        if (ret < 0) {
            warning("openai_rt: lws_service returned error: %d\n", ret);
        }

        if (!g_oairt.ws_thread_running) {
            break;
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
                 g_oairt.setup_sent = false;
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
                } else if (g_oairt.ws_state == WS_CONNECTED && !g_oairt.session_ready &&
                           !g_oairt.setup_sent) {
                    info("openai_rt: Call start - WebSocket connected but session not ready, sending session.update\n");
                    send_session_update();
                    if (g_oairt.ws_client) {
                        lws_callback_on_writable(g_oairt.ws_client);
                    }
                } else {
                    DEBUG_INFO("Call start event ignored (ws_state=%d, session_ready=%d)\n",
                               g_oairt.ws_state, g_oairt.session_ready);
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

        process_conversation_kick_pending();

        /* Gentle pacing */
        sys_msleep(g_oairt.ws_state == WS_CONNECTING ? 1 : 10);
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
     g_oairt.session_cfg_applied = false;
     g_oairt.setup_sent = false;
 
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