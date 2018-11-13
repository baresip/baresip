/**
 * @file gtk/gtk_mod.c GTK+ UI module
 *
 * Copyright (C) 2015 Charles E. Lehner
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <stdlib.h>
#include <pthread.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include "gtk_mod.h"

#ifdef USE_LIBNOTIFY
#include <libnotify/notify.h>
#endif

#if GLIB_CHECK_VERSION(2,40,0) || defined(USE_LIBNOTIFY)
#define USE_NOTIFICATIONS 1
#endif

/* About */
#define COPYRIGHT " Copyright (C) 2010 - 2015 Alfred E. Heggestad et al."
#define COMMENTS "A modular SIP User-Agent with audio and video support"
#define WEBSITE "http://www.creytiv.com/baresip.html"
#define LICENSE "BSD"

/**
 * @defgroup gtk_mod gtk_mod
 *
 * GTK+ Menu-based User-Interface module
 *
 * Creates a tray icon with a menu for making calls.
 *
 */

struct gtk_mod {
	pthread_t thread;
	bool run;
	bool contacts_inited;
	bool accounts_inited;
	struct message_lsnr *message;
	struct mqueue *mq;
	GApplication *app;
	GtkStatusIcon *status_icon;
	GtkWidget *app_menu;
	GtkWidget *contacts_menu;
	GtkWidget *accounts_menu;
	GtkWidget *status_menu;
	GSList *accounts_menu_group;
	struct dial_dialog *dial_dialog;
	GSList *call_windows;
	GSList *incoming_call_menus;
};

static struct gtk_mod mod_obj;

enum gtk_mod_events {
	MQ_POPUP,
	MQ_CONNECT,
	MQ_QUIT,
	MQ_ANSWER,
	MQ_HANGUP,
	MQ_SELECT_UA,
};

static void answer_activated(GSimpleAction *, GVariant *, gpointer);
static void reject_activated(GSimpleAction *, GVariant *, gpointer);
static void denotify_incoming_call(struct gtk_mod *, struct call *);

static GActionEntry app_entries[] = {
	{"answer", answer_activated, "x", NULL, NULL, {0} },
	{"reject", reject_activated, "x", NULL, NULL, {0} },
};

static struct call *get_call_from_gvariant(GVariant *param)
{
	gint64 call_ptr;
	struct call *call;
	struct list *calls = ua_calls(uag_current());
	struct le *le;

	call_ptr = g_variant_get_int64(param);
	call = GINT_TO_POINTER(call_ptr);

	for (le = list_head(calls); le; le = le->next)
		if (le->data == call)
			return call;

	return NULL;
}


static void menu_on_about(GtkMenuItem *menuItem, gpointer arg)
{
	(void)menuItem;
	(void)arg;

	gtk_show_about_dialog(NULL,
			"program-name", "baresip",
			"version", BARESIP_VERSION,
			"logo-icon-name", "call-start",
			"copyright", COPYRIGHT,
			"comments", COMMENTS,
			"website", WEBSITE,
			"license", LICENSE,
			NULL);
}


static void menu_on_quit(GtkMenuItem *menuItem, gpointer arg)
{
	struct gtk_mod *mod = arg;
	(void)menuItem;

	gtk_widget_destroy(GTK_WIDGET(mod->app_menu));
	g_object_unref(G_OBJECT(mod->status_icon));

	mqueue_push(mod->mq, MQ_QUIT, 0);
	info("quit from gtk\n");
}


static void menu_on_dial(GtkMenuItem *menuItem, gpointer arg)
{
	struct gtk_mod *mod = arg;
	(void)menuItem;
	if (!mod->dial_dialog)
		 mod->dial_dialog = dial_dialog_alloc(mod);
	dial_dialog_show(mod->dial_dialog);
}


static void menu_on_dial_contact(GtkMenuItem *menuItem, gpointer arg)
{
	struct gtk_mod *mod = arg;
	const char *uri = gtk_menu_item_get_label(menuItem);
	/* Queue dial from the main thread */
	mqueue_push(mod->mq, MQ_CONNECT, (char *)uri);
}


static void init_contacts_menu(struct gtk_mod *mod)
{
	struct contacts *contacts = baresip_contacts();
	struct le *le;
	GtkWidget *item;
	GtkMenuShell *contacts_menu = GTK_MENU_SHELL(mod->contacts_menu);

	/* Add contacts to submenu */
	for (le = list_head(contact_list(contacts)); le; le = le->next) {
		struct contact *c = le->data;
		item = gtk_menu_item_new_with_label(contact_str(c));
		gtk_menu_shell_append(contacts_menu, item);
		g_signal_connect(G_OBJECT(item), "activate",
				G_CALLBACK(menu_on_dial_contact), mod);
	}
}


static void menu_on_account_toggled(GtkCheckMenuItem *menu_item,
		struct gtk_mod *mod)
{
	struct ua *ua = g_object_get_data(G_OBJECT(menu_item), "ua");
	if (menu_item->active)
		mqueue_push(mod->mq, MQ_SELECT_UA, ua);
}


static void menu_on_presence_set(GtkMenuItem *item, struct gtk_mod *mod)
{
	struct le *le;
	void *type = g_object_get_data(G_OBJECT(item), "presence");
	enum presence_status status = GPOINTER_TO_UINT(type);
	(void)mod;

	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		ua_presence_status_set(ua, status);
	}
}


#ifdef USE_NOTIFICATIONS
static void menu_on_incoming_call_answer(GtkMenuItem *menuItem,
		struct gtk_mod *mod)
{
	struct call *call = g_object_get_data(G_OBJECT(menuItem), "call");
	denotify_incoming_call(mod, call);
	mqueue_push(mod->mq, MQ_ANSWER, call);
}


static void menu_on_incoming_call_reject(GtkMenuItem *menuItem,
		struct gtk_mod *mod)
{
	struct call *call = g_object_get_data(G_OBJECT(menuItem), "call");
	denotify_incoming_call(mod, call);
	mqueue_push(mod->mq, MQ_HANGUP, call);
}
#endif


static GtkMenuItem *accounts_menu_add_item(struct gtk_mod *mod,
		struct ua *ua)
{
	GtkMenuShell *accounts_menu = GTK_MENU_SHELL(mod->accounts_menu);
	GtkWidget *item;
	GSList *group = mod->accounts_menu_group;
	struct ua *ua_current = uag_current();
	char buf[256];

	re_snprintf(buf, sizeof buf, "%s%s", ua_aor(ua),
			ua_isregistered(ua) ? " (OK)" : "");
	item = gtk_radio_menu_item_new_with_label(group, buf);
	group = gtk_radio_menu_item_get_group(
			GTK_RADIO_MENU_ITEM (item));
	if (ua == ua_current)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
				TRUE);
	g_object_set_data(G_OBJECT(item), "ua", ua);
	g_signal_connect(item, "toggled",
			G_CALLBACK(menu_on_account_toggled), mod);
	gtk_menu_shell_append(accounts_menu, item);
	mod->accounts_menu_group = group;

	return GTK_MENU_ITEM(item);
}


static GtkMenuItem *accounts_menu_get_item(struct gtk_mod *mod,
		struct ua *ua)
{
	GtkMenuItem *item;
	GtkMenuShell *accounts_menu = GTK_MENU_SHELL(mod->accounts_menu);
	GList *items = accounts_menu->children;

	for (; items; items = items->next) {
		item = items->data;
		if (ua == g_object_get_data(G_OBJECT(item), "ua"))
			return item;
	}

	/* Add new account not yet in menu */
	return accounts_menu_add_item(mod, ua);
}


static void update_current_accounts_menu_item(struct gtk_mod *mod)
{
	GtkMenuItem *item = accounts_menu_get_item(mod,
			uag_current());
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
}


static void update_ua_presence(struct gtk_mod *mod)
{
	GtkCheckMenuItem *item = 0;
	enum presence_status cur_status;
	void *status;
	GtkMenuShell *status_menu = GTK_MENU_SHELL(mod->status_menu);
	GList *items = status_menu->children;

	cur_status = ua_presence_status(uag_current());

	for (; items; items = items->next) {
		item = items->data;
		status = g_object_get_data(G_OBJECT(item), "presence");
		if (cur_status == GPOINTER_TO_UINT(status))
			break;
	}
	if (!item)
		return;

	gtk_check_menu_item_set_active(item, TRUE);
}


static const char *ua_event_reg_str(enum ua_event ev)
{
	switch (ev) {

	case UA_EVENT_REGISTERING:      return "registering";
	case UA_EVENT_REGISTER_OK:      return "OK";
	case UA_EVENT_REGISTER_FAIL:    return "ERR";
	case UA_EVENT_UNREGISTERING:    return "unregistering";
	default: return "?";
	}
}


static void accounts_menu_set_status(struct gtk_mod *mod,
		struct ua *ua, enum ua_event ev)
{
	GtkMenuItem *item = accounts_menu_get_item(mod, ua);
	char buf[256];
	re_snprintf(buf, sizeof buf, "%s (%s)", ua_aor(ua),
			ua_event_reg_str(ev));
	gtk_menu_item_set_label(item, buf);
}


#ifdef USE_NOTIFICATIONS
static void notify_incoming_call(struct gtk_mod *mod,
		struct call *call)
{
	static const char *title = "Incoming call";
	const char *msg = call_peeruri(call);
	GtkWidget *call_menu;
	GtkWidget *menu_item;
#if defined(USE_LIBNOTIFY)
	NotifyNotification *notification;

	if (!notify_is_initted())
		return;
	notification = notify_notification_new(title, msg, "baresip");
	notify_notification_set_urgency(notification, NOTIFY_URGENCY_CRITICAL);
	notify_notification_show(notification, NULL);
	g_object_unref(notification);

#elif GLIB_CHECK_VERSION(2,40,0)
	char id[64];
	GVariant *target;
	GNotification *notification = g_notification_new(title);

	re_snprintf(id, sizeof id, "incoming-call-%p", call);
	id[sizeof id - 1] = '\0';

#if GLIB_CHECK_VERSION(2,42,0)
	g_notification_set_priority(notification,
			G_NOTIFICATION_PRIORITY_URGENT);
#else
	g_notification_set_urgent(notification, TRUE);
#endif

	target = g_variant_new_int64(GPOINTER_TO_INT(call));
	g_notification_set_body(notification, msg);
	g_notification_add_button_with_target_value(notification,
			"Answer", "app.answer", target);
	g_notification_add_button_with_target_value(notification,
			"Reject", "app.reject", target);
	g_application_send_notification(mod->app, id, notification);
	g_object_unref(notification);

#else
	(void)msg;
	(void)title;
#endif

	/* Add incoming call to the app menu */
	call_menu = gtk_menu_new();
	menu_item = gtk_menu_item_new_with_mnemonic("_Incoming call");
	g_object_set_data(G_OBJECT(menu_item), "call", call);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item),
			call_menu);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(mod->app_menu), menu_item);
	mod->incoming_call_menus = g_slist_append(mod->incoming_call_menus,
			menu_item);

	menu_item = gtk_menu_item_new_with_label(call_peeruri(call));
	gtk_widget_set_sensitive(menu_item, FALSE);
	gtk_menu_shell_append(GTK_MENU_SHELL(call_menu), menu_item);

	menu_item = gtk_menu_item_new_with_mnemonic("_Accept");
	g_object_set_data(G_OBJECT(menu_item), "call", call);
	g_signal_connect(G_OBJECT(menu_item), "activate",
			G_CALLBACK(menu_on_incoming_call_answer), mod);
	gtk_menu_shell_append(GTK_MENU_SHELL(call_menu), menu_item);

	menu_item = gtk_menu_item_new_with_mnemonic("_Reject");
	g_object_set_data(G_OBJECT(menu_item), "call", call);
	g_signal_connect(G_OBJECT(menu_item), "activate",
			G_CALLBACK(menu_on_incoming_call_reject), mod);
	gtk_menu_shell_append(GTK_MENU_SHELL(call_menu), menu_item);
}
#endif


static void denotify_incoming_call(struct gtk_mod *mod, struct call *call)
{
	GSList *item, *next;

#if GLIB_CHECK_VERSION(2,40,0)
	char id[64];

	re_snprintf(id, sizeof id, "incoming-call-%p", call);
	id[sizeof id - 1] = '\0';
	g_application_withdraw_notification(mod->app, id);
#endif

	/* Remove call submenu */
	for (item = mod->incoming_call_menus; item; item = next) {
		GtkWidget *menu_item = item->data;
		next = item->next;

		if (call == g_object_get_data(G_OBJECT(menu_item), "call")) {
			gtk_widget_destroy(menu_item);
			mod->incoming_call_menus =
				g_slist_delete_link(mod->incoming_call_menus,
						    item);
		}
	}
}


static void answer_activated(GSimpleAction *action, GVariant *parameter,
		gpointer arg)
{
	struct gtk_mod *mod = arg;
	struct call *call = get_call_from_gvariant(parameter);
	(void)action;

	if (call) {
		denotify_incoming_call(mod, call);
		mqueue_push(mod->mq, MQ_ANSWER, call);
	}
}


static void reject_activated(GSimpleAction *action, GVariant *parameter,
		gpointer arg)
{
	struct gtk_mod *mod = arg;
	struct call *call = get_call_from_gvariant(parameter);
	(void)action;

	if (call) {
		denotify_incoming_call(mod, call);
		mqueue_push(mod->mq, MQ_HANGUP, call);
	}
}


static struct call_window *new_call_window(struct gtk_mod *mod,
		struct call *call)
{
	struct call_window *win = call_window_new(call, mod);
	if (call) {
		mod->call_windows = g_slist_append(mod->call_windows, win);
	}
	return win;
}


static struct call_window *get_call_window(struct gtk_mod *mod,
		struct call *call)
{
	GSList *wins;

	for (wins = mod->call_windows; wins; wins = wins->next) {
		struct call_window *win = wins->data;
		if (call_window_is_for_call(win, call))
			return win;
	}
	return NULL;
}


static struct call_window *get_create_call_window(struct gtk_mod *mod,
		struct call *call)
{
	struct call_window *win = get_call_window(mod, call);
	if (!win)
		win = new_call_window(mod, call);
	return win;
}


void gtk_mod_call_window_closed(struct gtk_mod *mod, struct call_window *win)
{
	if (!mod)
		return;
	mod->call_windows = g_slist_remove(mod->call_windows, win);
}


static void ua_event_handler(struct ua *ua,
		enum ua_event ev,
		struct call *call,
		const char *prm,
		void *arg )
{
	struct gtk_mod *mod = arg;
	struct call_window *win;

	gdk_threads_enter();

	switch (ev) {

	case UA_EVENT_REGISTERING:
	case UA_EVENT_UNREGISTERING:
	case UA_EVENT_REGISTER_OK:
	case UA_EVENT_REGISTER_FAIL:
		accounts_menu_set_status(mod, ua, ev);
		break;

#ifdef USE_NOTIFICATIONS
	case UA_EVENT_CALL_INCOMING:
		notify_incoming_call(mod, call);
		break;
#endif

	case UA_EVENT_CALL_CLOSED:
		win = get_call_window(mod, call);
		if (win)
			call_window_closed(win, prm);
		else
			denotify_incoming_call(mod, call);
		break;

	case UA_EVENT_CALL_RINGING:
		win = get_create_call_window(mod, call);
		if (win)
			call_window_ringing(win);
		break;

	case UA_EVENT_CALL_PROGRESS:
		win = get_create_call_window(mod, call);
		if (win)
			call_window_progress(win);
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		win = get_create_call_window(mod, call);
		if (win)
			call_window_established(win);
		break;

	case UA_EVENT_CALL_TRANSFER_FAILED:
		win = get_create_call_window(mod, call);
		if (win)
			call_window_transfer_failed(win, prm);
		break;

	default:
		break;
	}

	gdk_threads_leave();
}


#ifdef USE_NOTIFICATIONS
static void message_handler(struct ua *ua,
			    const struct pl *peer, const struct pl *ctype,
			    struct mbuf *body, void *arg)
{
	struct gtk_mod *mod = arg;
	char title[128];
	char msg[512];

#if GLIB_CHECK_VERSION(2,40,0)
	GNotification *notification;
#elif defined(USE_LIBNOTIFY)
	NotifyNotification *notification;
#endif

	(void)ua;
	(void)ctype;


	/* Display notification of chat */

	re_snprintf(title, sizeof title, "Chat from %r", peer);
	title[sizeof title - 1] = '\0';

	re_snprintf(msg, sizeof msg, "%b",
			mbuf_buf(body), mbuf_get_left(body));

#if GLIB_CHECK_VERSION(2,40,0)
	notification = g_notification_new(title);
	g_notification_set_body(notification, msg);
	g_application_send_notification(mod->app, NULL, notification);
	g_object_unref(notification);

#elif defined(USE_LIBNOTIFY)
	(void)mod;

	if (!notify_is_initted())
		return;
	notification = notify_notification_new(title, msg, "baresip");
	notify_notification_show(notification, NULL);
	g_object_unref(notification);
#endif
}
#endif


static void popup_menu(struct gtk_mod *mod, GtkMenuPositionFunc position,
		gpointer position_arg, guint button, guint32 activate_time)
{
	if (!mod->contacts_inited) {
		init_contacts_menu(mod);
		mod->contacts_inited = TRUE;
	}

	/* Update things that may have been changed through another UI */
	update_current_accounts_menu_item(mod);
	update_ua_presence(mod);

	gtk_widget_show_all(mod->app_menu);

	gtk_menu_popup(GTK_MENU(mod->app_menu), NULL, NULL,
			position, position_arg,
			button, activate_time);
}


static gboolean status_icon_on_button_press(GtkStatusIcon *status_icon,
		GdkEventButton *event,
		struct gtk_mod *mod)
{
	popup_menu(mod, gtk_status_icon_position_menu, status_icon,
			event->button, event->time);
	return TRUE;
}


void gtk_mod_connect(struct gtk_mod *mod, const char *uri)
{
	if (!mod)
		return;
	mqueue_push(mod->mq, MQ_CONNECT, (char *)uri);
}


static void warning_dialog(const char *title, const char *fmt, ...)
{
	va_list ap;
	char msg[512];
	GtkWidget *dialog;

	va_start(ap, fmt);
	(void)re_vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);

	dialog = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE, "%s", title);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
			"%s", msg);
	g_signal_connect_swapped(G_OBJECT(dialog), "response",
			G_CALLBACK(gtk_widget_destroy), dialog);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_widget_show(dialog);
}


static void mqueue_handler(int id, void *data, void *arg)
{
	struct gtk_mod *mod = arg;
	const char *uri;
	struct call *call;
	int err;
	struct ua *ua = uag_current();
	(void)mod;

	switch ((enum gtk_mod_events)id) {

	case MQ_POPUP:
		gdk_threads_enter();
		popup_menu(mod, NULL, NULL, 0, GPOINTER_TO_UINT(data));
		gdk_threads_leave();
		break;

	case MQ_CONNECT:
		uri = data;
		err = ua_connect(ua, &call, NULL, uri, VIDMODE_ON);
		if (err) {
			gdk_threads_enter();
			warning_dialog("Call failed",
					"Connecting to \"%s\" failed.\n"
					"Error: %m", uri, err);
			gdk_threads_leave();
			break;
		}
		gdk_threads_enter();
		err = new_call_window(mod, call) == NULL;
		gdk_threads_leave();
		if (err) {
			ua_hangup(ua, call, 500, "Server Error");
		}
		break;

	case MQ_HANGUP:
		call = data;
		ua_hangup(ua, call, 0, NULL);
		break;

	case MQ_QUIT:
		ua_stop_all(false);
		break;

	case MQ_ANSWER:
		call = data;
		err = ua_answer(ua, call);
		if (err) {
			gdk_threads_enter();
			warning_dialog("Call failed",
					"Answering the call "
					"from \"%s\" failed.\n"
					"Error: %m",
					call_peername(call), err);
			gdk_threads_leave();
			break;
		}

		gdk_threads_enter();
		err = new_call_window(mod, call) == NULL;
		gdk_threads_leave();
		if (err) {
			ua_hangup(ua, call, 500, "Server Error");
		}
		break;

	case MQ_SELECT_UA:
		ua = data;
		uag_current_set(ua);
		break;
	}
}


static void *gtk_thread(void *arg)
{
	struct gtk_mod *mod = arg;
	GtkMenuShell *app_menu;
	GtkWidget *item;
	GError *err = NULL;
	struct le *le;

	gdk_threads_init();
	gtk_init(0, NULL);

	g_set_application_name("baresip");
	mod->app = g_application_new ("com.creytiv.baresip",
			G_APPLICATION_FLAGS_NONE);

	g_application_register (G_APPLICATION (mod->app), NULL, &err);
	if (err != NULL) {
		warning ("Unable to register GApplication: %s",
				err->message);
		g_error_free (err);
		err = NULL;
	}

#ifdef USE_LIBNOTIFY
	notify_init("baresip");
#endif

	mod->status_icon = gtk_status_icon_new_from_icon_name("call-start");
	gtk_status_icon_set_tooltip_text (mod->status_icon, "baresip");

	g_signal_connect(G_OBJECT(mod->status_icon),
			"button_press_event",
			G_CALLBACK(status_icon_on_button_press), mod);
	gtk_status_icon_set_visible(mod->status_icon, TRUE);

	mod->contacts_inited = false;
	mod->dial_dialog = NULL;
	mod->call_windows = NULL;
	mod->incoming_call_menus = NULL;

	/* App menu */
	mod->app_menu = gtk_menu_new();
	app_menu = GTK_MENU_SHELL(mod->app_menu);

	/* Account submenu */
	mod->accounts_menu = gtk_menu_new();
	mod->accounts_menu_group = NULL;
	item = gtk_menu_item_new_with_mnemonic("_Account");
	gtk_menu_shell_append(app_menu, item);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
			mod->accounts_menu);

	/* Add accounts to submenu */
	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		accounts_menu_add_item(mod, ua);
	}

	/* Status submenu */
	mod->status_menu = gtk_menu_new();
	item = gtk_menu_item_new_with_mnemonic("_Status");
	gtk_menu_shell_append(GTK_MENU_SHELL(app_menu), item);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), mod->status_menu);

	/* Open */
	item = gtk_radio_menu_item_new_with_label(NULL, "Open");
	g_object_set_data(G_OBJECT(item), "presence",
			GINT_TO_POINTER(PRESENCE_OPEN));
	g_signal_connect(item, "activate",
			G_CALLBACK(menu_on_presence_set), mod);
	gtk_menu_shell_append(GTK_MENU_SHELL(mod->status_menu), item);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);

	/* Closed */
	item = gtk_radio_menu_item_new_with_label_from_widget(
			GTK_RADIO_MENU_ITEM(item), "Closed");
	g_object_set_data(G_OBJECT(item), "presence",
			GINT_TO_POINTER(PRESENCE_CLOSED));
	g_signal_connect(item, "activate",
			G_CALLBACK(menu_on_presence_set), mod);
	gtk_menu_shell_append(GTK_MENU_SHELL(mod->status_menu), item);

	gtk_menu_shell_append(app_menu, gtk_separator_menu_item_new());

	/* Dial */
	item = gtk_menu_item_new_with_mnemonic("_Dial...");
	gtk_menu_shell_append(app_menu, item);
	g_signal_connect(G_OBJECT(item), "activate",
			G_CALLBACK(menu_on_dial), mod);

	/* Dial contact */
	mod->contacts_menu = gtk_menu_new();
	item = gtk_menu_item_new_with_mnemonic("Dial _contact");
	gtk_menu_shell_append(app_menu, item);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
			mod->contacts_menu);

	gtk_menu_shell_append(app_menu, gtk_separator_menu_item_new());

	/* About */
	item = gtk_menu_item_new_with_mnemonic("A_bout");
	g_signal_connect(G_OBJECT(item), "activate",
			G_CALLBACK(menu_on_about), mod);
	gtk_menu_shell_append(app_menu, item);

	gtk_menu_shell_append(app_menu, gtk_separator_menu_item_new());

	/* Quit */
	item = gtk_menu_item_new_with_mnemonic("_Quit");
	g_signal_connect(G_OBJECT(item), "activate",
			G_CALLBACK(menu_on_quit), mod);
	gtk_menu_shell_append(app_menu, item);

	g_action_map_add_action_entries(G_ACTION_MAP(mod->app),
			app_entries, G_N_ELEMENTS(app_entries), mod);

	info("gtk_menu starting\n");

	uag_event_register( ua_event_handler, mod );
	mod->run = true;
	gtk_main();
	mod->run = false;
	uag_event_unregister(ua_event_handler);

	if (mod->dial_dialog) {
		mem_deref(mod->dial_dialog);
		mod->dial_dialog = NULL;
	}

	return NULL;
}


static void vu_enc_destructor(void *arg)
{
	struct vumeter_enc *st = arg;

	list_unlink(&st->af.le);
}


static void vu_dec_destructor(void *arg)
{
	struct vumeter_dec *st = arg;

	list_unlink(&st->af.le);
}


static int16_t calc_avg_s16(const int16_t *sampv, size_t sampc)
{
	int32_t v = 0;
	size_t i;

	if (!sampv || !sampc)
		return 0;

	for (i=0; i<sampc; i++)
		v += abs(sampv[i]);

	return v/sampc;
}


static int vu_encode_update(struct aufilt_enc_st **stp, void **ctx,
			    const struct aufilt *af, struct aufilt_prm *prm,
			    const struct audio *au)
{
	struct vumeter_enc *st;
	(void)ctx;
	(void)prm;
	(void)au;

	if (!stp || !af)
		return EINVAL;

	if (*stp)
		return 0;

	if (prm->fmt != AUFMT_S16LE) {
		warning("vumeter: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), vu_enc_destructor);
	if (!st)
		return ENOMEM;

	gdk_threads_enter();
	call_window_got_vu_enc(st);
	gdk_threads_leave();

	*stp = (struct aufilt_enc_st *)st;

	return 0;
}


static int vu_decode_update(struct aufilt_dec_st **stp, void **ctx,
			    const struct aufilt *af, struct aufilt_prm *prm,
			    const struct audio *au)
{
	struct vumeter_dec *st;
	(void)ctx;
	(void)prm;
	(void)au;

	if (!stp || !af)
		return EINVAL;

	if (*stp)
		return 0;

	if (prm->fmt != AUFMT_S16LE) {
		warning("vumeter: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), vu_dec_destructor);
	if (!st)
		return ENOMEM;

	gdk_threads_enter();
	call_window_got_vu_dec(st);
	gdk_threads_leave();

	*stp = (struct aufilt_dec_st *)st;

	return 0;
}


static int vu_encode(struct aufilt_enc_st *st, void *sampv, size_t *sampc)
{
	struct vumeter_enc *vu = (struct vumeter_enc *)st;

	vu->avg_rec = calc_avg_s16(sampv, *sampc);
	vu->started = true;

	return 0;
}


static int vu_decode(struct aufilt_dec_st *st, void *sampv, size_t *sampc)
{
	struct vumeter_dec *vu = (struct vumeter_dec *)st;

	vu->avg_play = calc_avg_s16(sampv, *sampc);
	vu->started = true;

	return 0;
}


static struct aufilt vumeter = {
	LE_INIT, "gtk_vumeter",
	vu_encode_update, vu_encode,
	vu_decode_update, vu_decode
};


static int cmd_popup_menu(struct re_printf *pf, void *unused)
{
	(void)pf;
	(void)unused;

	mqueue_push(mod_obj.mq, MQ_POPUP, GUINT_TO_POINTER(GDK_CURRENT_TIME));

	return 0;
}


static const struct cmd cmdv[] = {
	{"gtk", 0,   0, "Pop up GTK+ menu",         cmd_popup_menu       },
};


static int module_init(void)
{
	int err = 0;

	err = mqueue_alloc(&mod_obj.mq, mqueue_handler, &mod_obj);
	if (err)
		return err;

	aufilt_register(baresip_aufiltl(), &vumeter);

#ifdef USE_NOTIFICATIONS
	err = message_listen(&mod_obj.message, baresip_message(),
			     message_handler, &mod_obj);
	if (err) {
		warning("gtk: message_init failed (%m)\n", err);
		return err;
	}
#endif

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	if (err)
		return err;

	/* start the thread last */
	err = pthread_create(&mod_obj.thread, NULL, gtk_thread,
			     &mod_obj);
	if (err)
		return err;

	return err;
}


static int module_close(void)
{
	cmd_unregister(baresip_commands(), cmdv);
	if (mod_obj.run) {
		gdk_threads_enter();
		gtk_main_quit();
		gdk_threads_leave();
	}
	if (mod_obj.thread)
		pthread_join(mod_obj.thread, NULL);
	mod_obj.mq = mem_deref(mod_obj.mq);
	aufilt_unregister(&vumeter);
	mod_obj.message = mem_deref(mod_obj.message);

#ifdef USE_LIBNOTIFY
	if (notify_is_initted())
		notify_uninit();
#endif

	g_slist_free(mod_obj.accounts_menu_group);
	g_slist_free(mod_obj.call_windows);
	g_slist_free(mod_obj.incoming_call_menus);

	uag_event_unregister(ua_event_handler);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(gtk) = {
	"gtk",
	"application",
	module_init,
	module_close,
};
