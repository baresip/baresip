/**
 * @file src/event.c  Baresip event handling
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Get the name of the User-Agent event
 *
 * @param ev User-Agent event
 *
 * @return Name of the event
 */
const char *uag_event_str(enum ua_event ev)
{
	switch (ev) {

	case UA_EVENT_REGISTERING:          return "REGISTERING";
	case UA_EVENT_REGISTER_OK:          return "REGISTER_OK";
	case UA_EVENT_REGISTER_FAIL:        return "REGISTER_FAIL";
	case UA_EVENT_UNREGISTERING:        return "UNREGISTERING";
	case UA_EVENT_SHUTDOWN:             return "SHUTDOWN";
	case UA_EVENT_EXIT:                 return "EXIT";
	case UA_EVENT_CALL_INCOMING:        return "CALL_INCOMING";
	case UA_EVENT_CALL_RINGING:         return "CALL_RINGING";
	case UA_EVENT_CALL_PROGRESS:        return "CALL_PROGRESS";
	case UA_EVENT_CALL_ESTABLISHED:     return "CALL_ESTABLISHED";
	case UA_EVENT_CALL_CLOSED:          return "CALL_CLOSED";
	case UA_EVENT_CALL_TRANSFER_FAILED: return "TRANSFER_FAILED";
	case UA_EVENT_CALL_DTMF_START:      return "CALL_DTMF_START";
	case UA_EVENT_CALL_DTMF_END:        return "CALL_DTMF_END";
	case UA_EVENT_CALL_RTCP:            return "CALL_RTCP";
	default: return "?";
	}
}
