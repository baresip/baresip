/**
 * @file calls.c  OpenAI Realtime API - Call management
 */

 #include <re.h>
 #include <rem.h>
#include <baresip.h>
#include "openai_rt.h"


/* Message queue events */
enum call_mq_events {
	MQ_HANGUP = 0,
	MQ_SEND_DIGIT,
	MQ_API_CALL,
	MQ_TRANSFER,
	MQ_OPENAI_RESPONSE,
};

/* Static module state */
static struct {
	struct mqueue *mq;      /* Message queue for thread-safe call operations */
	bool shutting_down;     /* Flag to prevent operations during shutdown */
} calls_state;

/* Message queue handler - executes in RE main thread */
static void mqueue_handler(int id, void *data, void *arg)
{
	(void)arg;
	
	/* Ignore events during shutdown to prevent crashes */
	if (calls_state.shutting_down) {
		DEBUG_INFO("mqueue_handler: Ignoring event %d during shutdown\n", id);
		return;
	}
	
	/* Validate event ID to detect corruption */
	if (id < 0 || id > MQ_OPENAI_RESPONSE) {
		warning("openai_rt: Invalid mqueue event ID: %d (possible corruption)\n", id);
		return;
	}
	
	switch ((enum call_mq_events)id) {
	case MQ_HANGUP:
		DEBUG_INFO("mqueue_handler: Processing hangup request\n");
		if (g_oairt.current_call) {
			struct ua *ua = call_get_ua(g_oairt.current_call);
			if (ua) {
				ua_hangup(ua, g_oairt.current_call, 0, NULL);
			}
			else {
				/* Fallback: at least send BYE */
				call_hangup(g_oairt.current_call, 0, NULL);
			}
		}
		else {
			DEBUG_INFO("mqueue_handler: No active call to hangup\n");
		}
		break;
		
	case MQ_SEND_DIGIT:
		{
			char key = (char)(uintptr_t)data;
			if (key == KEYCODE_REL) {
				DEBUG_INFO("mqueue_handler: Sending DTMF key release (KEYCODE_REL)\n");
			} else {
				DEBUG_INFO("mqueue_handler: Sending DTMF digit '%c'\n", key);
			}
			if (g_oairt.current_call) {
				call_send_digit(g_oairt.current_call, key);
			}
			else {
				DEBUG_INFO("mqueue_handler: No active call to send digit\n");
			}
		}
		break;

	case MQ_API_CALL:
		{
			/* API call is handled synchronously for now as it's called from RE thread */
			/* In a real scenario, we might want to offload this to a separate thread */
			/* but since it's a tool call from the AI, we need to return the result. */
			/* However, the tool call itself is triggered by a message from OpenAI */
			/* which is processed in the RE thread. */
		}
		break;

	case MQ_TRANSFER:
		{
			char *destination = (char *)data;
			int xfer_err;

			if (!destination) {
				warning("openai_rt: transfer: missing destination\n");
				break;
			}

			if (g_oairt.current_call) {
				xfer_err = call_hold(g_oairt.current_call, true);
				if (xfer_err) {
					warning("openai_rt: call_hold before transfer failed: %m\n",
					        xfer_err);
				}

				xfer_err = call_transfer(g_oairt.current_call, destination);
				if (xfer_err) {
					warning("openai_rt: call_transfer to '%s' failed: %m\n",
					        destination, xfer_err);
				}
				else {
					info("openai_rt: call transfer initiated to %s\n",
					     destination);
				}
			}
			else {
				warning("openai_rt: transfer: no active call\n");
			}

			mem_deref(destination);
		}
		break;

	case MQ_OPENAI_RESPONSE:
		{
			char *response_json = (char *)data;
			DEBUG_INFO("mqueue_handler: Processing OpenAI response\n");
			if (g_oairt.current_call) {
				warning("openai_rt: emit OPENAI_RESPONSE (%d)\n", UA_EVENT_OPENAI_RESPONSE);
				bevent_call_emit(UA_EVENT_OPENAI_RESPONSE, g_oairt.current_call,
				                "%s", response_json);
			}
			else {
				DEBUG_INFO("mqueue_handler: No active call for OpenAI response\n");
			}
			/* Free the allocated response string */
			mem_deref(response_json);
		}
		break;
		
	default:
		warning("openai_rt: Unknown mqueue event: %d\n", id);
		break;
	}
}

/* Event handler for UA events */
static void event_handler(enum ua_event ev, struct bevent *event, void *arg)
{
	struct call *call = bevent_get_call(event);
	(void)arg;

	if (!call) {
		//DEBUG_INFO("No call object in event %d\n", ev);
		return;
	}

	//DEBUG_INFO("UA event: %d for call %p\n", ev, call);

	switch (ev) {
	case UA_EVENT_CALL_INCOMING:
		{
			int qerr;
			info("openai_rt: Call INCOMING from %s\n", call_peeruri(call));
			DEBUG_INFO("Incoming call - initiating session setup\n");
			
			/* Store call reference for later use */
			g_oairt.current_call = call;
			
			/* Only reset Gemini session if WS is down or setup never completed */
			if (g_oairt.ws_state != WS_CONNECTED ||
			    !g_oairt.session_cfg_applied) {
				g_oairt.session_ready = false;
				g_oairt.session_cfg_applied = false;
			}
			
			/* Queue event to start WebSocket connection and session setup */
			qerr = audio_queue_event(EVENT_CALL_START, call);
			if (qerr) {
				warning("openai_rt: Failed to queue call start event (incoming): %m\n", qerr);
			}
		}
		break;
		
	case UA_EVENT_CALL_OUTGOING:
		{
			int qerr;
			info("openai_rt: Call OUTGOING to %s\n", call_peeruri(call));
			DEBUG_INFO("Outgoing call - initiating session setup\n");
			
			/* Store call reference for later use */
			g_oairt.current_call = call;
			
			/* Only reset Gemini session if WS is down or setup never completed */
			if (g_oairt.ws_state != WS_CONNECTED ||
			    !g_oairt.session_cfg_applied) {
				g_oairt.session_ready = false;
				g_oairt.session_cfg_applied = false;
			}
			
			/* Queue event to start WebSocket connection and session setup */
			qerr = audio_queue_event(EVENT_CALL_START, call);
			if (qerr) {
				warning("openai_rt: Failed to queue call start event (outgoing): %m\n", qerr);
			}
		}
		break;
		
	case UA_EVENT_CALL_ESTABLISHED:
		{
			info("openai_rt: Call ESTABLISHED with %s\n", call_peeruri(call));
			DEBUG_INFO("Call established - checking session readiness\n");

			/* Mark call as active */
			g_oairt.call_active = true;
			g_oairt.current_call = call;
			g_oairt.gemini_xfer_scheduled = false;
			g_oairt.gemini_turn_had_audio = false;

			/* Reset audio state for new call */
			audio_reset_for_new_call();
			
			/* Kick session.update without blocking the UA event chain (sndfile etc.) */
			if (!websocket_is_ready()) {
				int qerr;

				warning("openai_rt: Call established but WebSocket not ready (status=%s)\n",
				        websocket_status_string());

				qerr = audio_queue_event(EVENT_CALL_START, call);
				if (qerr) {
					warning("openai_rt: Failed to queue call start event (established): %m\n", qerr);
				}
				websocket_kick_session_setup();
			}
			else {
				info("openai_rt: Session ready, call can proceed\n");
			}
			
			/* Start audio threads; uplink commits wait for session_cfg_applied */
			if (audio_ready_for_call()) {
				DEBUG_INFO("Call established - starting audio threads\n");
				audio_restart_threads();
			} else {
				warning("openai_rt: Audio drivers not ready when call established\n");
			}
		}
		break;

	case UA_EVENT_CALL_CLOSED:
		info("openai_rt: Call CLOSED\n");
		DEBUG_INFO("Call closed - queuing end event\n");

		/* Stop audio threads before marking call as inactive */
		audio_stop_threads();

		/* Mark call as inactive; keep Gemini session if WS stays up */
		g_oairt.call_active = false;
		if (g_oairt.ws_state != WS_CONNECTED) {
			g_oairt.session_ready = false;
			g_oairt.session_cfg_applied = false;
		}
		g_oairt.current_call = NULL;

		/* Queue event for WebSocket thread to handle */
		audio_queue_event(EVENT_CALL_END, NULL);

		/* Reset state */
		g_oairt.speech_active = false;
		break;

	default:
		/* Log other events for debugging */
		//DEBUG_INFO("Unhandled UA event: %d\n", ev);
		break;
	}
}

int calls_init(void)
{
	int err;

	/* Initialize state */
	calls_state.shutting_down = false;

	/* Initialize message queue for thread-safe call operations */
	err = mqueue_alloc(&calls_state.mq, mqueue_handler, NULL);
	if (err) {
		warning("openai_rt: Failed to allocate mqueue: %m\n", err);
		return err;
	}

	/* Register UA event handler */
	err = bevent_register(event_handler, NULL);
	if (err) {
		DEBUG_INFO("Failed to register event handler: %m\n", err);
		mem_deref(calls_state.mq);
		calls_state.mq = NULL;
		return err;
	}

	DEBUG_INFO("Call management initialized\n");
	return 0;
}

void calls_close(void)
{
	DEBUG_INFO("Call management closing\n");
	
	/* Set shutdown flag to prevent new mqueue operations */
	calls_state.shutting_down = true;

	/* Unregister event handler first to stop receiving new events */
	bevent_unregister(event_handler);

	/* Clean up message queue */
	calls_state.mq = mem_deref(calls_state.mq);

	/* Clear call state */
	g_oairt.call_active = false;
	g_oairt.current_call = NULL;

	DEBUG_INFO("Call management closed\n");
}

/**
 * Request call hangup - thread-safe
 * This function can be called from any thread. The actual hangup
 * will be executed in the RE main event loop thread via mqueue.
 */
void calls_hangup(void)
{
	int err;
	
	if (calls_state.shutting_down) {
		DEBUG_INFO("calls_hangup: Ignoring hangup during shutdown\n");
		return;
	}
	
	if (!calls_state.mq) {
		warning("openai_rt: calls_hangup: mqueue not initialized\n");
		return;
	}
	
	DEBUG_INFO("calls_hangup: Queuing hangup request\n");
	err = mqueue_push(calls_state.mq, MQ_HANGUP, NULL);
	if (err) {
		warning("openai_rt: Failed to queue hangup: %m\n", err);
	}
}


/**
 * Send DTMF digit - thread-safe
 * This function can be called from any thread. The actual digit send
 * will be executed in the RE main event loop thread via mqueue.
 *
 * @param key  DTMF digit to send (0-9, *, #, A-D)
 */
void calls_send_digit(char key)
{
	int err;
	
	if (calls_state.shutting_down) {
		DEBUG_INFO("calls_send_digit: Ignoring digit send during shutdown\n");
		return;
	}
	
	if (!calls_state.mq) {
		warning("openai_rt: calls_send_digit: mqueue not initialized\n");
		return;
	}
	
	if (key == KEYCODE_REL) {
		DEBUG_INFO("calls_send_digit: Queuing key release (KEYCODE_REL)\n");
	} else {
		DEBUG_INFO("calls_send_digit: Queuing digit '%c'\n", key);
	}
	err = mqueue_push(calls_state.mq, MQ_SEND_DIGIT, (void *)(uintptr_t)key);
	if (err) {
		warning("openai_rt: Failed to queue digit send: %m\n", err);
	}
}


/**
 * Send DTMF string - thread-safe
 * This function can be called from any thread. It validates and sends
 * each digit in the string sequentially.
 *
 * @param digits  String of DTMF digits to send (0-9, *, #, A-D)
 * @return 0 if success, error code otherwise
 */
int calls_send_dtmf(const char *digits)
{
	const char *p;
	
	if (calls_state.shutting_down) {
		DEBUG_INFO("calls_send_dtmf: Ignoring DTMF send during shutdown\n");
		return EINTR;
	}
	
	if (!digits || !*digits) {
		warning("openai_rt: calls_send_dtmf: Empty or NULL digits string\n");
		return EINVAL;
	}
	
	/* Validate that all characters are valid DTMF digits */
	for (p = digits; *p; p++) {
		char c = *p;
		bool valid = (c >= '0' && c <= '9') || c == '*' || c == '#' ||
		            (c >= 'A' && c <= 'D') || (c >= 'a' && c <= 'd');
		if (!valid) {
			warning("openai_rt: Invalid DTMF character '%c' in string '%s'\n", 
			        c, digits);
			return EINVAL;
		}
	}
	
	DEBUG_INFO("calls_send_dtmf: Sending DTMF string: '%s' (%zu digits)\n", 
	           digits, strlen(digits));
	
	/* Send each digit sequentially with proper key press/release */
	for (p = digits; *p; p++) {
		char c = *p;
		/* Convert to uppercase for consistency */
		if (c >= 'a' && c <= 'd') {
			c = c - 'a' + 'A';
		}
		
		/* Send key press */
		calls_send_digit(c);
		
		/* Wait for tone to be audible (100ms) */
		sys_msleep(100);
		
		/* Send key release to stop the tone */
		calls_send_digit(KEYCODE_REL);
		
		/* Add inter-digit pause (except after the last digit) */
		if (*(p + 1)) {
			sys_msleep(150);  /* 150ms pause between digits */
		}
	}
	
	DEBUG_INFO("calls_send_dtmf: DTMF string '%s' sent successfully\n", digits);
	return 0;
}


/**
 * Transfer the active call - thread-safe
 *
 * @param destination  SIP URI or phone number to transfer to
 * @return 0 if success, error code otherwise
 */
int calls_transfer(const char *destination)
{
	char *dest_copy = NULL;
	int err;

	if (calls_state.shutting_down) {
		DEBUG_INFO("calls_transfer: Ignoring transfer during shutdown\n");
		return EINTR;
	}

	if (!calls_state.mq) {
		warning("openai_rt: calls_transfer: mqueue not initialized\n");
		return EINVAL;
	}

	if (!destination || !*destination) {
		warning("openai_rt: calls_transfer: Empty or NULL destination\n");
		return EINVAL;
	}

	if (!g_oairt.current_call) {
		warning("openai_rt: calls_transfer: No active call\n");
		return ENOENT;
	}

	err = str_dup(&dest_copy, destination);
	if (err) {
		warning("openai_rt: Failed to duplicate transfer destination: %m\n",
		        err);
		return err;
	}

	DEBUG_INFO("calls_transfer: Queuing transfer to '%s'\n", destination);
	err = mqueue_push(calls_state.mq, MQ_TRANSFER, dest_copy);
	if (err) {
		warning("openai_rt: Failed to queue transfer: %m\n", err);
		mem_deref(dest_copy);
		return err;
	}

	return 0;
}


/**
 * Perform an HTTP API call - thread-safe
 *
 * @param method         HTTP method (POST, PUT, GET, UPDATE, DELETE)
 * @param content_type   Content-Type header value
 * @param auth_type      Authentication type (basic, bearer)
 * @param auth_username  Username for basic auth or token for bearer auth
 * @param auth_password  Password for basic auth
 * @param body           HTTP request body
 * @param output         Output: Response body (allocated, must be freed)
 * @return 0 if success, error code otherwise
 */
struct api_call_data {
	char *method;
	char *content_type;
	char *auth_type;
	char *auth_username;
	char *auth_password;
	char *body;
	char **output;
	struct sync_obj *sync;
	int err;
};

struct sync_obj {
	mtx_t mtx;
	cnd_t cnd;
	bool done;
};

static void api_call_resph(int err, const struct http_msg *msg, void *arg)
{
	struct api_call_data *ad = arg;
	ad->err = err;

	if (!err && msg && msg->mb && ad->output) {
		size_t len = mbuf_get_left(msg->mb);
		*ad->output = mem_zalloc(len + 1, NULL);
		if (*ad->output) {
			memcpy(*ad->output, mbuf_buf(msg->mb), len);
		}
	}

	mtx_lock(&ad->sync->mtx);
	ad->sync->done = true;
	cnd_signal(&ad->sync->cnd);
	mtx_unlock(&ad->sync->mtx);
}

int calls_api_call(const char *method, const char *uri,
                   const char *content_type, const char *auth_type,
                   const char *auth_username, const char *auth_password,
                   const char *body,
                   char **output)
{
	struct http_cli *cli = NULL;
	struct http_reqconn *conn = NULL;
	struct sync_obj sync;
	struct api_call_data ad;
	struct pl pl_met, pl_uri;
	struct mbuf *mb_body = NULL;
	int err;

	if (!method || !uri || !output) return EINVAL;

	DEBUG_INFO("calls_api_call: %s %s\n", method, uri);

	re_thread_enter();

	memset(&ad, 0, sizeof(ad));
	memset(&sync, 0, sizeof(sync));
	ad.output = output;
	
	err = mtx_init(&sync.mtx, mtx_plain);
	if (err) goto out;
	err = cnd_init(&sync.cnd);
	if (err) {
		mtx_destroy(&sync.mtx);
		goto out;
	}
	sync.done = false;
	ad.sync = &sync;

	err = http_client_alloc(&cli, net_dnsc(baresip_network()));
	if (err) {
		warning("openai_rt: http_client_alloc failed: %m\n", err);
		goto out;
	}

#ifdef USE_TLS
	/* Disable server verification for now to avoid CA certificate issues */
	http_client_disable_verify_server(cli);
#endif

	err = http_reqconn_alloc(&conn, cli, api_call_resph, NULL, &ad);
	if (err) {
		warning("openai_rt: http_reqconn_alloc failed: %m\n", err);
		goto out;
	}

	pl_set_str(&pl_met, method);
	http_reqconn_set_method(conn, &pl_met);

	if (content_type) {
		struct pl pl_ct;
		pl_set_str(&pl_ct, content_type);
		http_reqconn_set_ctype(conn, &pl_ct);
	}

	if (auth_type && auth_username) {
		if (strcmp(auth_type, "bearer") == 0) {
			struct pl pl_b;
			pl_set_str(&pl_b, auth_username);
			http_reqconn_set_bearer(conn, &pl_b);
		}
		else if (strcmp(auth_type, "basic") == 0) {
			/* Construct Basic Auth header manually to ensure it's sent upfront */
			char *auth_buf = NULL;
			char *b64_buf = NULL;
			size_t b64_len;
			int r;

			re_sdprintf(&auth_buf, "%s:%s", auth_username, 
				auth_password ? auth_password : "");
			
			if (auth_buf) {
				b64_len = (strlen(auth_buf) * 4 / 3) + 4;
				b64_buf = mem_zalloc(b64_len, NULL);
				if (b64_buf) {
					r = base64_encode((uint8_t *)auth_buf, strlen(auth_buf), 
						b64_buf, &b64_len);
					if (r == 0) {
						char *hdr_buf = NULL;
						re_sdprintf(&hdr_buf, "Authorization: Basic %s", b64_buf);
						if (hdr_buf) {
							struct pl pl_hdr;
							pl_set_str(&pl_hdr, hdr_buf);
							http_reqconn_add_header(conn, &pl_hdr);
							mem_deref(hdr_buf);
						}
					}
					mem_deref(b64_buf);
				}
				mem_deref(auth_buf);
			}
		}
	}

	if (body) {
		mb_body = mbuf_alloc(strlen(body));
		if (!mb_body) {
			err = ENOMEM;
			goto out;
		}
		mbuf_write_str(mb_body, body);
		mbuf_set_pos(mb_body, 0);
		http_reqconn_set_body(conn, mb_body);
		
		/* If content type not set, default to text/plain as requested */
		if (!content_type) {
			struct pl pl_ct = PL("text/plain");
			http_reqconn_set_ctype(conn, &pl_ct);
		}
	}

	pl_set_str(&pl_uri, uri);

	DEBUG_INFO("calls_api_call: sending request...\n");
	err = http_reqconn_send(conn, &pl_uri);
	if (err) {
		warning("openai_rt: http_reqconn_send failed: %m\n", err);
		goto out;
	}

	/* Leave RE thread before waiting to allow RE event loop to process the request */
	re_thread_leave();

	DEBUG_INFO("calls_api_call: waiting for response...\n");
	mtx_lock(&sync.mtx);
	if (!sync.done) {
		/* Wait with timeout to prevent hanging baresip if callback is never called */
		uint64_t wait_start = tmr_jiffies();
		while (!sync.done) {
			cnd_wait(&sync.cnd, &sync.mtx);
			
			/* Break if we've waited more than 10 seconds */
			if (tmr_jiffies() - wait_start > 10000) {
				warning("openai_rt: calls_api_call timed out after 10s\n");
				ad.err = ETIMEDOUT;
				break;
			}
		}
	}
	mtx_unlock(&sync.mtx);

	/* Re-enter RE thread for cleanup */
	re_thread_enter();

	err = ad.err;
	DEBUG_INFO("calls_api_call: request completed with error=%d\n", err);

out:
	mem_deref(conn);
	mem_deref(cli);
	mem_deref(mb_body);
	mtx_destroy(&sync.mtx);
	cnd_destroy(&sync.cnd);

	re_thread_leave();

	return err;
}


/**
 * Queue OpenAI response for event emission - thread-safe
 * This function can be called from any thread. The actual bevent emission
 * will be executed in the RE main event loop thread via mqueue.
 *
 * @param response_json  JSON string of the response (will be copied)
 * @return 0 if success, error code otherwise
 */
int calls_queue_openai_response(const char *response_json)
{
	char *json_copy = NULL;
	int err;
	
	if (calls_state.shutting_down) {
		DEBUG_INFO("calls_queue_openai_response: Ignoring during shutdown\n");
		return EINTR;
	}
	
	if (!calls_state.mq) {
		warning("openai_rt: calls_queue_openai_response: mqueue not initialized\n");
		return EINVAL;
	}
	
	if (!response_json) {
		warning("openai_rt: calls_queue_openai_response: NULL response\n");
		return EINVAL;
	}
	
	/* Allocate memory for the JSON string (will be freed in mqueue handler) */
	err = str_dup(&json_copy, response_json);
	if (err) {
		warning("openai_rt: Failed to duplicate response JSON: %m\n", err);
		return err;
	}
	
	DEBUG_INFO("calls_queue_openai_response: Queuing OpenAI response\n");
	err = mqueue_push(calls_state.mq, MQ_OPENAI_RESPONSE, json_copy);
	if (err) {
		warning("openai_rt: Failed to queue OpenAI response: %m\n", err);
		mem_deref(json_copy);
		return err;
	}
	
	return 0;
}