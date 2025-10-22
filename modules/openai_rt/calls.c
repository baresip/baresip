/**
 * @file calls.c  OpenAI Realtime API - Call management
 */

#include "openai_rt.h"

/* Message queue events */
enum call_mq_events {
	MQ_HANGUP = 0,
};

/* Static module state */
static struct {
	struct mqueue *mq;  /* Message queue for thread-safe call operations */
} calls_state;

/* Message queue handler - executes in RE main thread */
static void mqueue_handler(int id, void *data, void *arg)
{
	(void)arg;
	(void)data;
	
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
		return err;
	}

	DEBUG_INFO("Call management initialized\n");
	return 0;
}

void calls_close(void)
{
	/* Unregister event handler */
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