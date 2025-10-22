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
			call_hangup(g_oairt.current_call, 0, NULL);
		}
		else {
			DEBUG_INFO("mqueue_handler: No active call to hangup\n");
		}
		break;
		
	case MQ_SEND_DIGIT:
		{
			char key = (char)(uintptr_t)data;
			DEBUG_INFO("mqueue_handler: Sending DTMF digit '%c'\n", key);
			if (g_oairt.current_call) {
				call_send_digit(g_oairt.current_call, key);
			}
			else {
				DEBUG_INFO("mqueue_handler: No active call to send digit\n");
			}
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
static void event_handler(struct ua *ua, enum ua_event ev,
                         struct call *call, const char *prm, void *arg)
{
	(void)ua;
	(void)prm;
	(void)arg;

	if (!call) {
		//DEBUG_INFO("No call object in event %d\n", ev);
		return;
	}

	//DEBUG_INFO("UA event: %d for call %p\n", ev, call);

	switch (ev) {
	case UA_EVENT_CALL_ESTABLISHED:
		info("openai_rt: Call ESTABLISHED with %s\n", call_peeruri(call));
		DEBUG_INFO("Call established - queuing start event\n");

		/* Mark call as active */
		g_oairt.call_active = true;
		g_oairt.current_call = call;

		/* Reset audio state for new call */
		audio_reset_for_new_call();

		/* Queue event for WebSocket thread to handle */
		audio_queue_event(EVENT_CALL_START, call);
		
		/* Note: WebSocket connection should already be established by now */
		if (!websocket_is_ready()) {
			warning("openai_rt: Call established but WebSocket not ready - connection may still be in progress\n");
		}
		break;

	case UA_EVENT_CALL_CLOSED:
		info("openai_rt: Call CLOSED\n");
		DEBUG_INFO("Call closed - queuing end event\n");

		/* Stop audio threads before marking call as inactive */
		audio_stop_threads();

		/* Mark call as inactive */
		g_oairt.call_active = false;
		g_oairt.session_ready = false;
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
	err = uag_event_register(event_handler, NULL);
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
	uag_event_unregister(event_handler);

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
	
	DEBUG_INFO("calls_send_digit: Queuing digit '%c'\n", key);
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
	
	/* Send each digit sequentially with inter-digit pause */
	for (p = digits; *p; p++) {
		char c = *p;
		/* Convert to uppercase for consistency */
		if (c >= 'a' && c <= 'd') {
			c = c - 'a' + 'A';
		}
		calls_send_digit(c);
		
		/* Add inter-digit pause (except after the last digit) */
		if (*(p + 1)) {
			sys_msleep(150);  /* 150ms pause between digits */
		}
	}
	
	DEBUG_INFO("calls_send_dtmf: DTMF string '%s' sent successfully\n", digits);
	return 0;
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