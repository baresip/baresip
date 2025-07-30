/**
 * @file gtk/gtk_mod.c GTK+ UI module
 *
 * Copyright (C) 2015 Charles E. Lehner
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */
#include <re.h>
#include <rem.h>
#include <time.h>
#include <baresip.h>
#include <stdlib.h>
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
#define COPYRIGHT " Copyright (C) 2010 - 2021 Alfred E. Heggestad et al."
#define COMMENTS "A modular SIP User-Agent with audio and video support"
#define WEBSITE "https://github.com/baresip/baresip"
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
	thrd_t thread;
	bool run;
	bool contacts_inited;
	struct mqueue *mq;
	int call_history_length;
	GApplication *app;
	GtkStatusIcon *status_icon;
	GtkWidget *app_menu;
	GtkWidget *contacts_menu;
	GtkWidget *accounts_menu;
	GtkWidget *history_menu;
	GtkWidget *status_menu;
	GtkWidget *menu_window;
	GtkWidget *menu_button;
	GSList *accounts_menu_group;
	struct dial_dialog *dial_dialog;
	GSList *call_windows;
	GSList *incoming_call_menus;
	bool clean_number;
	bool use_status_icon;
	bool use_window;
	struct ua *ua_cur;
	bool icon_call_missed;
	bool icon_call_outgoing;
	bool icon_call_incoming;
};

static struct gtk_mod mod_obj;

enum gtk_mod_events {
	MQ_CONNECT,
	MQ_CONNECTATTENDED,
	MQ_QUIT,
	MQ_ANSWER,
	MQ_HANGUP,
	MQ_SELECT_UA,
};

static void answer_activated(GSimpleAction *, GVariant *, gpointer);
static void reject_activated(GSimpleAction *, GVariant *, gpointer);
static void denotify_incoming_call(struct gtk_mod *, struct call *);
static int module_close(void);

static GActionEntry app_entries[] = {
	{"answer", answer_activated, "s", NULL, NULL, {0} },
	{"reject", reject_activated, "s", NULL, NULL, {0} },
};


static void gtk_current_ua_set(struct ua *ua)
{
	mod_obj.ua_cur = ua;
}


static struct ua *gtk_current_ua(void)
{
	if (!mod_obj.ua_cur)
		mod_obj.ua_cur = list_ledata(list_head(uag_list()));

	return mod_obj.ua_cur;
}


static struct call *get_call_from_gvariant(GVariant *param)
{
	struct list *calls = ua_calls(gtk_current_ua());
	const gchar *call_ptr = g_variant_get_string(param, NULL);

	return call_find_id(calls, call_ptr);
}


static void menu_on_about(GtkMenuItem *menuItem, gpointer arg)
{
	(void)menuItem;
	(void)arg;

	gtk_show_about_dialog(NULL,
			"program-name", "baresip",
			      "version", baresip_version(),
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

	mqueue_push(mod->mq, MQ_QUIT, 0);
	info("quit from gtk\n");
}


static void menu_on_dial(GtkMenuItem *menuItem, gpointer arg)
{
	struct gtk_mod *mod = arg;
	(void)menuItem;
	if (!mod->dial_dialog)
		 mod->dial_dialog = dial_dialog_alloc(mod, NULL);
	dial_dialog_show(mod->dial_dialog);
}


static void menu_on_dial_contact(GtkMenuItem *menuItem, gpointer arg)
{
	struct gtk_mod *mod = arg;
	const char *uri = gtk_menu_item_get_label(menuItem);
	/* Queue dial from the main thread */
	gtk_mod_connect(mod, uri);
}


static void menu_on_dial_history(GtkMenuItem *menuItem, gpointer arg)
{
	struct gtk_mod *mod = arg;
	const char *label = gtk_menu_item_get_label(menuItem);
	char *label_1;
	char buf[256];
	char *uri;

	str_ncpy(buf, label, sizeof(buf));
	label_1 = strchr(buf, '[');
	if (!label_1)
		return;

	label_1 = label_1 + 1;

	uri = strtok(label_1, "]");
	gtk_mod_connect(mod, uri);
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


static void add_history_menu_item(struct gtk_mod *mod, const char *uri,
					int call_type, const char *info)
{
	GtkWidget *item, *history_item;
	GtkMenuShell *history_menu = GTK_MENU_SHELL(mod->history_menu);
	char buf[256];
	time_t rawtime = time(NULL);
	struct tm *ptm = localtime(&rawtime);
	GList *list;

	if (mod->call_history_length < 20) {
		mod->call_history_length++;
	}
	else {
		/* Remove old call history */
		list = gtk_container_get_children(GTK_CONTAINER(history_menu));
		history_item = GTK_WIDGET(list->data);
		gtk_widget_destroy(history_item);

	}

	re_snprintf(buf, sizeof buf,
			"%s [%s]\n%04d-%02d-%02d %02d:%02d:%02d",
			info, uri, ptm->tm_year + 1900, ptm->tm_mon + 1,
			ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);

	item = gtk_image_menu_item_new_with_label(buf);
	switch (call_type) {
		case CALL_INCOMING:
			gtk_image_menu_item_set_image(
				GTK_IMAGE_MENU_ITEM(item),
				gtk_image_new_from_icon_name(
                                        (mod->icon_call_incoming) ?
					"call-incoming-symbolic" : "go-next",
					GTK_ICON_SIZE_MENU));
			break;
		case CALL_OUTGOING:
			gtk_image_menu_item_set_image(
				GTK_IMAGE_MENU_ITEM(item),
				gtk_image_new_from_icon_name(
					(mod->icon_call_outgoing) ?
					"call-outgoing-symbolic"
						 : "go-previous",
				GTK_ICON_SIZE_MENU));
			break;
		case CALL_MISSED:
			gtk_image_menu_item_set_image(
				GTK_IMAGE_MENU_ITEM(item),
				gtk_image_new_from_icon_name(
					(mod->icon_call_missed) ?
					"call-missed-symbolic" : "call-stop",
					GTK_ICON_SIZE_MENU));
			break;
		case CALL_REJECTED:
			gtk_image_menu_item_set_image(
				GTK_IMAGE_MENU_ITEM(item),
				gtk_image_new_from_icon_name(
					"window-close", GTK_ICON_SIZE_MENU));
			break;
		default:
			gtk_image_menu_item_set_image(
				GTK_IMAGE_MENU_ITEM(item),
				gtk_image_new_from_icon_name(
					"call-start", GTK_ICON_SIZE_MENU));
			break;
	}
	gtk_menu_shell_append(history_menu, item);
	g_signal_connect(G_OBJECT(item), "activate",
		G_CALLBACK(menu_on_dial_history), mod);
}


static void menu_on_account_toggled(GtkCheckMenuItem *menu_item,
				    struct gtk_mod *mod)
{
	struct ua *ua = g_object_get_data(G_OBJECT(menu_item), "ua");
	if (gtk_check_menu_item_get_active(menu_item))
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
	add_history_menu_item(mod,call_peeruri(call), CALL_REJECTED,
						call_peername(call));
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
	struct ua *ua_current = gtk_current_ua();
	char buf[256];

	re_snprintf(buf, sizeof buf, "%s%s", account_aor(ua_account(ua)),
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
	GtkContainer *accounts_menu_cont = GTK_CONTAINER(mod->accounts_menu);
	GList *items = gtk_container_get_children(accounts_menu_cont);

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
	GtkMenuItem *item = accounts_menu_get_item(mod, gtk_current_ua());

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
}


static void update_ua_presence(struct gtk_mod *mod)
{
	GtkCheckMenuItem *item = 0;
	enum presence_status cur_status;
	void *status;
	GtkContainer *status_menu_cont = GTK_CONTAINER(mod->status_menu);
	GList *items = gtk_container_get_children(status_menu_cont);

	cur_status = ua_presence_status(gtk_current_ua());

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


static const char *ua_event_reg_str(enum bevent_ev ev)
{
	switch (ev) {

	case BEVENT_REGISTERING:      return "registering";
	case BEVENT_REGISTER_OK:      return "OK";
	case BEVENT_REGISTER_FAIL:    return "ERR";
	case BEVENT_UNREGISTERING:    return "unregistering";
	default: return "?";
	}
}


static void accounts_menu_set_status(struct gtk_mod *mod,
					struct ua *ua, enum bevent_ev ev)
{
	GtkMenuItem *item = accounts_menu_get_item(mod, ua);
	char buf[256];
	re_snprintf(buf, sizeof buf, "%s (%s)", account_aor(ua_account(ua)),
			ua_event_reg_str(ev));
	gtk_menu_item_set_label(item, buf);
}


#ifdef USE_NOTIFICATIONS
static void notify_incoming_call(struct gtk_mod *mod,
		struct call *call)
{
	char title[128];
	const char *msg = call_peeruri(call);
	GtkWidget *call_menu;
	GtkWidget *menu_item;

#if defined(USE_LIBNOTIFY)
	NotifyNotification *notification;
#elif GLIB_CHECK_VERSION(2,40,0)
	char id[64];
	GVariant *target;
	GNotification *notification;
#endif

	re_snprintf(title, sizeof title, "Incoming call from %s",
					call_peername(call));

#if defined(USE_LIBNOTIFY)

	if (!notify_is_initted())
		return;
	notification = notify_notification_new(title, msg, "baresip");
	notify_notification_set_urgency(notification, NOTIFY_URGENCY_CRITICAL);
	notify_notification_show(notification, NULL);
	g_object_unref(notification);

#elif GLIB_CHECK_VERSION(2,40,0)
	notification = g_notification_new(title);

	re_snprintf(id, sizeof id, "incoming-call-%p", call);
	id[sizeof id - 1] = '\0';

#if GLIB_CHECK_VERSION(2,42,0)
	g_notification_set_priority(notification,
			G_NOTIFICATION_PRIORITY_URGENT);
#else
	g_notification_set_urgent(notification, TRUE);
#endif

	target = g_variant_new_string(call_id(call));
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
		add_history_menu_item(mod,call_peeruri(call), CALL_REJECTED,
					call_peername(call));
		mqueue_push(mod->mq, MQ_HANGUP, call);
	}
}


static struct call_window *new_call_window(struct gtk_mod *mod,
						struct call *call)
{
	struct call_window *win = call_window_new(call, mod, NULL);
	if (call) {
		mod->call_windows = g_slist_append(mod->call_windows, win);
	}
	return win;
}


static struct call_window *new_call_transfer_window(struct gtk_mod *mod,
						struct call *call,
						struct call *attended_call)
{
	struct call_window *win = call_window_new(call, mod, attended_call);
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


static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct gtk_mod *mod = arg;
	struct call_window *win;
	struct ua   *ua   = bevent_get_ua(event);
	struct call *call = bevent_get_call(event);
	const char  *txt  = bevent_get_text(event);

	gdk_threads_enter();

	switch (ev) {

	case BEVENT_REGISTERING:
	case BEVENT_UNREGISTERING:
	case BEVENT_REGISTER_OK:
	case BEVENT_REGISTER_FAIL:
		accounts_menu_set_status(mod, ua, ev);
		break;

#ifdef USE_NOTIFICATIONS
	case BEVENT_CALL_INCOMING:
		notify_incoming_call(mod, call);
		break;
#endif

	case BEVENT_CALL_CLOSED:
		win = get_call_window(mod, call);
		if (win)
			call_window_closed(win, txt);
		denotify_incoming_call(mod, call);
		if (!call_is_outgoing(call)
			&& call_state(call) != CALL_STATE_TERMINATED
			&& call_state(call) != CALL_STATE_ESTABLISHED) {
			add_history_menu_item(mod,
				call_peeruri(call),
				CALL_MISSED, call_peername(call));

			if (mod->use_status_icon)
				gtk_status_icon_set_from_icon_name(
					mod->status_icon,
					(mod->icon_call_missed) ?
					"call-missed-symbolic" : "call-stop");

			if (mod->use_window)
				gtk_button_set_image(
					GTK_BUTTON(mod->menu_button),
					gtk_image_new_from_icon_name(
						(mod->icon_call_missed) ?
					"call-missed-symbolic" : "call-stop",
					GTK_ICON_SIZE_SMALL_TOOLBAR));
		}
		break;

	case BEVENT_CALL_RINGING:
		win = get_create_call_window(mod, call);
		if (win)
			call_window_ringing(win);
		break;

	case BEVENT_CALL_PROGRESS:
		win = get_create_call_window(mod, call);
		if (win)
			call_window_progress(win);
		break;

	case BEVENT_CALL_ESTABLISHED:
		win = get_create_call_window(mod, call);
		if (win)
			call_window_established(win);
		denotify_incoming_call(mod, call);
		break;

	case BEVENT_CALL_TRANSFER_FAILED:
		win = get_create_call_window(mod, call);
		if (win)
			call_window_transfer_failed(win, txt);
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


static void popup_menu(struct gtk_mod *mod, GdkEventButton *event)
{
	(void)event;
	if (!mod->contacts_inited) {
		init_contacts_menu(mod);
		mod->contacts_inited = TRUE;
	}

	/* Update things that may have been changed through another UI */
	update_current_accounts_menu_item(mod);
	update_ua_presence(mod);

	gtk_widget_show_all(mod->app_menu);

	gtk_menu_popup_at_pointer(GTK_MENU(mod->app_menu), NULL);
}


static gboolean status_icon_on_button_press(GtkStatusIcon *status_icon,
						GdkEventButton *event,
						struct gtk_mod *mod)
{
	popup_menu(mod, event);
	gtk_status_icon_set_from_icon_name(status_icon, "call-start");
	return TRUE;
}

static gboolean menu_button_on_button_press(GtkWidget *button,
						GdkEventButton *event,
						struct gtk_mod *mod)
{
	popup_menu(mod, event);
	gtk_button_set_image(GTK_BUTTON(button),
		gtk_image_new_from_icon_name("call-start",
		GTK_ICON_SIZE_SMALL_TOOLBAR));
	return TRUE;
}


int gtk_mod_connect(struct gtk_mod *mod, const char *uri)
{
	char *uric = NULL;
	struct pl url_pl;
	int err = 0;

	pl_set_str(&url_pl, uri);
	if (!mod)
		return ENOMEM;

	err = account_uri_complete_strdup(ua_account(mod->ua_cur),
							&uric, &url_pl);
	if (err)
		goto out;

	err = mqueue_push(mod->mq, MQ_CONNECT, uric);

out:
	return err;
}


int gtk_mod_connect_attended(struct gtk_mod *mod, const char *uri,
					struct call *attended_call)
{
	struct attended_transfer_store *ats;
	char *uric = NULL;
	struct pl url_pl;
	int err = 0;

	pl_set_str(&url_pl, uri);
	if (!mod)
		return ENOMEM;

	ats = mem_zalloc(sizeof(struct attended_transfer_store), NULL);
	if (!ats)
		return ENOMEM;

	err = account_uri_complete_strdup(ua_account(mod->ua_cur),
							&uric, &url_pl);
	if (err)
		goto out;

	ats->uri = (char *)uric;
	ats->attended_call = attended_call;

	err = mqueue_push(mod->mq, MQ_CONNECTATTENDED, ats);

out:
	return err;
}


bool gtk_mod_clean_number(struct gtk_mod *mod)
{
	if (!mod)
		return false;
	return mod->clean_number;
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
	struct attended_transfer_store *ats;
	int err;
	struct ua *ua = gtk_current_ua();

	switch ((enum gtk_mod_events)id) {

	case MQ_CONNECT:
		uri = data;
		err = ua_connect(ua, &call, NULL, uri, VIDMODE_ON);
		add_history_menu_item(mod, uri, CALL_OUTGOING, "");
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
		mem_deref(data);
		break;

	case MQ_CONNECTATTENDED:
		ats = data;
		err = ua_connect(ua, &call, NULL, ats->uri, VIDMODE_ON);
		add_history_menu_item(mod, ats->uri, CALL_OUTGOING, "");
		if (err) {
			gdk_threads_enter();
			warning_dialog("Call failed",
				       "Connecting to \"%s\" failed.\n"
				       "Error: %m", ats->uri, err);
			gdk_threads_leave();
			break;
		}
		gdk_threads_enter();
		err = new_call_transfer_window(mod, call,
						ats->attended_call) == NULL;
		gdk_threads_leave();
		if (err) {
			ua_hangup(ua, call, 500, "Server Error");
		}
		mem_deref(ats->uri);
		mem_deref(data);
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
		err = ua_answer(ua, call, VIDMODE_ON);
		add_history_menu_item(mod, call_peeruri(call),
				CALL_INCOMING, call_peername(call));
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
		gtk_current_ua_set(ua);
		break;
	}
}


static int gtk_thread(void *arg)
{
	struct gtk_mod *mod = arg;
	GtkMenuShell *app_menu;
	GtkWidget *item;
	GError *err = NULL;
	struct le *le;
	GtkIconTheme *theme;

	gdk_threads_init();
	gtk_init(0, NULL);

	g_set_application_name("baresip");
	mod->app = g_application_new("com.github.baresip",
					G_APPLICATION_FLAGS_NONE);

	g_application_register(G_APPLICATION (mod->app), NULL, &err);
	if (err != NULL) {
		warning ("Unable to register GApplication: %s",
				err->message);
		g_error_free(err);
		err = NULL;
	}

#ifdef USE_LIBNOTIFY
	notify_init("baresip");
#endif

	if (mod->use_window)
	{
		mod->menu_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title(
			GTK_WINDOW(mod->menu_window), "BareSIP GTK");
		gtk_window_set_default_size(
			GTK_WINDOW(mod->menu_window), 350, 50);
		gtk_window_set_default_icon_name(
			"call-start");

		mod->menu_button = gtk_button_new_from_icon_name(
			"call-start", GTK_ICON_SIZE_BUTTON);
		g_signal_connect(G_OBJECT(mod->menu_button),
				"button_press_event",
				G_CALLBACK(menu_button_on_button_press), mod);
		gtk_container_add(
			GTK_CONTAINER(mod->menu_window), mod->menu_button);

		gtk_widget_show_all(mod->menu_window);
		g_signal_connect(mod->menu_window, "destroy",
					G_CALLBACK(menu_on_quit), mod);
	}

	if (mod->use_status_icon)
	{
		mod->status_icon = NULL;
		mod->status_icon =
			gtk_status_icon_new_from_icon_name("call-start");

		if (!gtk_status_icon_get_visible(mod->status_icon)) {
			info("gtk status icon is not supported. ");
			info("Disable gtk_use_status_icon in the settings\n");
			module_close();
			return 1;
		}

		gtk_status_icon_set_tooltip_text(
			mod->status_icon, "baresip");

		g_signal_connect(G_OBJECT(mod->status_icon),
				"button_press_event",
				G_CALLBACK(status_icon_on_button_press), mod);
		gtk_status_icon_set_visible(mod->status_icon, TRUE);
	}

	mod->contacts_inited = false;
	mod->dial_dialog = NULL;
	mod->call_windows = NULL;
	mod->incoming_call_menus = NULL;
	mod->call_history_length = 0;

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

	/* Caller history */
	mod->history_menu = gtk_menu_new();
	item = gtk_menu_item_new_with_mnemonic("Call _history");
	gtk_menu_shell_append(app_menu, item);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
			mod->history_menu);

	gtk_menu_shell_append(app_menu, gtk_separator_menu_item_new());

	theme = gtk_icon_theme_get_default();
	mod->icon_call_incoming = gtk_icon_theme_has_icon(theme,
						"call-incoming-symbolic");
	mod->icon_call_outgoing = gtk_icon_theme_has_icon(theme,
						"call-outgoing-symbolic");
	mod->icon_call_missed = gtk_icon_theme_has_icon(theme,
						"call-missed-symbolic");

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

	bevent_register(event_handler, mod);
	mod->run = true;
	gtk_main();
	mod->run = false;
	bevent_unregister(event_handler);

	mod->dial_dialog = mem_deref(mod->dial_dialog);

	return 0;
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
				const struct aufilt *af,
				struct aufilt_prm *prm,
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


static int vu_decode_update(struct aufilt_dec_st **stp,
				void **ctx,
				const struct aufilt *af,
				struct aufilt_prm *prm,
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


static int vu_encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct vumeter_enc *vu = (struct vumeter_enc *)st;

	vu->avg_rec = calc_avg_s16(af->sampv, af->sampc);
	vu->started = true;

	return 0;
}


static int vu_decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct vumeter_dec *vu = (struct vumeter_dec *)st;

	vu->avg_play = calc_avg_s16(af->sampv, af->sampc);
	vu->started = true;

	return 0;
}


static struct aufilt vumeter = {
	.name    = "gtk_vumeter",
	.encupdh = vu_encode_update,
	.ench    = vu_encode,
	.decupdh = vu_decode_update,
	.dech    = vu_decode
};


static int module_init(void)
{
	int err;

	mod_obj.clean_number = false;
	mod_obj.use_status_icon = false;
	mod_obj.use_window = true;

	conf_get_bool(conf_cur(),
		"gtk_clean_number", &mod_obj.clean_number);
	conf_get_bool(conf_cur(),
		"gtk_use_status_icon", &mod_obj.use_status_icon);
	conf_get_bool(conf_cur(),
		"gtk_use_window", &mod_obj.use_window);

	err = mqueue_alloc(&mod_obj.mq, mqueue_handler, &mod_obj);
	if (err)
		return err;

	aufilt_register(baresip_aufiltl(), &vumeter);

#ifdef USE_NOTIFICATIONS
	err = message_listen(baresip_message(),
				message_handler, &mod_obj);
	if (err) {
		warning("gtk: message_init failed (%m)\n", err);
		return err;
	}
#endif

	/* start the thread last */
	err = thread_create_name(&mod_obj.thread, "gtk", gtk_thread,
				&mod_obj);
	if (err)
		return err;

	return 0;
}


static int module_close(void)
{
	if (mod_obj.run) {
		gdk_threads_enter();
		gtk_main_quit();
		gdk_threads_leave();
	}
	if (mod_obj.thread)
		thrd_join(mod_obj.thread, NULL);
	mod_obj.mq = mem_deref(mod_obj.mq);
	aufilt_unregister(&vumeter);
	message_unlisten(baresip_message(), message_handler);

#ifdef USE_LIBNOTIFY
	if (notify_is_initted())
		notify_uninit();
#endif

	g_slist_free(mod_obj.accounts_menu_group);
	g_slist_free(mod_obj.call_windows);
	g_slist_free(mod_obj.incoming_call_menus);

	bevent_unregister(event_handler);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(gtk) = {
	"gtk",
	"application",
	module_init,
	module_close,
};
