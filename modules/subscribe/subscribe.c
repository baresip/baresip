#include <re.h>
#include <baresip.h>
#include <stdlib.h>

struct subscription {
    struct le le;
    struct sipsub *sub;
    struct ua *ua;
};

static struct list subl;

static void sub_destructor(void *arg)
{
    struct subscription *sub = arg;
    mem_deref(sub->sub);
    mem_deref(sub->ua);
}

static void event_handler(enum ua_event ev, struct bevent *event, void *arg)
{
    (void)event;
    (void)arg;
    if (ev == UA_EVENT_SHUTDOWN) {
        struct le *le;
        for (le = subl.head; le; le = le->next) {
            struct subscription *sub = le->data;
            if (sub->sub){
                sub_destructor(sub);
                sub->sub = NULL;
                sub->ua = NULL; 

            }
        }
    }
}

static void notify_event_emit(struct ua *ua, const struct sip_msg *msg)
{
    const struct sip_hdr *hdr_event = sip_msg_hdr(msg, SIP_HDR_EVENT);
    const struct sip_hdr *hdr_subs  = sip_msg_hdr(msg, SIP_HDR_SUBSCRIPTION_STATE);
    const struct sip_hdr *hdr_ctype = sip_msg_hdr(msg, SIP_HDR_CONTENT_TYPE);

    struct odict *od = NULL;
    char *buf = NULL;
	int err;

    if (!ua || !msg)
        return;

    err = odict_alloc(&od, 32);
    if (err)
        return;

    if (hdr_event && pl_isset(&hdr_event->val)){
		re_sdprintf(&buf, "%r", &hdr_event->val);
        odict_entry_add(od, "event", ODICT_STRING, buf);
        mem_deref(buf);
    }

    if (hdr_subs && pl_isset(&hdr_subs->val)) {
        re_sdprintf(&buf, "%r", &hdr_subs->val);
		odict_entry_add(od, "substate", ODICT_STRING, buf);
        mem_deref(buf);
    }

    if (hdr_ctype && pl_isset(&hdr_ctype->val)) {
        re_sdprintf(&buf, "%r", &hdr_ctype->val);
		odict_entry_add(od, "ctype", ODICT_STRING, buf);
        mem_deref(buf);
    }

    if (msg->mb && mbuf_get_left(msg->mb) > 0) {
		re_sdprintf(&buf, "%b", mbuf_buf(msg->mb), mbuf_get_left(msg->mb));
		odict_entry_add(od, "body", ODICT_STRING, buf);
        mem_deref(buf);
	}

	bevent_ua_emit(UA_EVENT_SUB_NOTIFY, ua,
                    "%H", json_encode_odict, od);
    mem_deref(od);
}

static void notify_handler(struct sip *sip, const struct sip_msg *msg,
			   void *arg)
{
    struct subscription *sub = arg;
    struct ua *ua = sub->ua;

    // Accept everything for now
    (void) sip_treply(NULL, sip, msg, 200, "OK");

    if (!sub || !ua)
        return;

    // Trigger NOTIFY event
    notify_event_emit(ua, msg);
}

static int auth_handler(char **username, char **password,
			const char *realm, void *arg)
{
	return account_auth(arg, username, password, realm);
}

static void close_handler(int err, const struct sip_msg *msg,
                          const struct sipevent_substate *substate, void *arg)
{
    (void)err;
    (void)msg;
    (void)substate;
    struct subscription *sub = arg;
    list_unlink(&sub->le);
    mem_deref(sub);
}

static int cmd_subscribe(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
    struct le *le = NULL;
    struct ua *ua = NULL;

    // Find the first registered UA
    for (le = uag_list()->head; le; le = le->next) {
        ua = le->data;
        if (ua_isregistered(ua))
            break;
    }

    if (!ua) {
        re_hprintf(pf, "No registered UA available\n");
        return 0;
    }

    const char *line = (const char *)carg->prm;

    if (!line || *line == '\0') {
        re_hprintf(pf, "Usage: /subscribe <target> <event> <expires>\n");
        return 0;
    }

    // Duplicate line so strtok doesn't modify const memory
    char buf[256];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    // Parse target, event and expires
    char *target = strtok(buf, " \t");
    char *event  = strtok(NULL, " \t");
    char *expires_str = strtok(NULL, " \t");

    if (!target || !event || !expires_str) {
        re_hprintf(pf, "Usage: /subscribe <target> <event> <expires>\n");
        return 0;
    }
    uint32_t expires = (uint32_t)strtoul(expires_str, NULL, 10);

    struct subscription *sub = mem_zalloc(sizeof(*sub), sub_destructor);
    if (!sub) {
        re_hprintf(pf, "Memory allocation error\n");
        return 0;
    }

    sub->ua = mem_ref(ua);

	const char *routev[1];
	routev[0] = ua_outbound(ua);

    // Prepare custom headers
    char hdrstr[1024];
    size_t pos = 0;
    for (le = ua_custom_hdrs(ua)->head; le; le = le->next) {
        struct sip_hdr *hdr = le->data;
        if (!hdr) continue;
        pos += snprintf(hdrstr + pos, sizeof(hdrstr) - pos,
                        "%.*s: %.*s\r\n",
                        (int)hdr->name.l, hdr->name.p,
                        (int)hdr->val.l, hdr->val.p);
    }
    hdrstr[pos] = '\0';  // ensure null-terminated

    int err = sipevent_subscribe(&sub->sub,
                                 uag_sipevent_sock(),			// get the UA's sipevent socket
                                 target,						// URI to subscribe
                                 NULL,							// from_name
                                 account_aor(ua_account(ua)),	// from_uri
                                 event,							// event type
                                 NULL,							// id
                                 expires,						// expires
                                 ua_cuser(ua),					// cuser
                                 routev, routev[0] ? 1 : 0,		// routev, routec
                                 auth_handler, ua_account(ua),	// auth handler, arg
                                 false,							// aref
                                 NULL, notify_handler, close_handler, sub,		    // forkh, notifyh, closeh, arg
                                 "%s", hdrstr);	                // fmt

    if (err) {
        re_hprintf(pf, "Subscribe failed: %m\n", err);
        return 0;
    }
	/* TODO: Send SUBSCRIBE event*/
    re_hprintf(pf, "Subscription sent to %s for event %s\n", target, event);

    list_append(&subl, &sub->le, sub);

    return 0;
}

static const struct cmd cmdv[] = {
    { "subscribe", 0, CMD_PRM, "Send subscription", cmd_subscribe }
};

static int module_init(void)
{
    bevent_register(event_handler, NULL);
    cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
    return 0;
}

static int module_close(void)
{
    list_flush(&subl);
    bevent_unregister(event_handler);
    cmd_unregister(baresip_commands(), cmdv);
    return 0;
}

EXPORT_SYM const struct mod_export DECL_EXPORTS(subscribe) = {
    "subscribe",
    "application",
    module_init,
    module_close
};
