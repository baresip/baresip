/**
 * @file gtk/call_window.c GTK+ call window
 *
 * Copyright (C) 2015 Charles E. Lehner
 */

#include <re.h>
#include <baresip.h>
#include <gtk/gtk.h>
#include "gtk_mod.h"


struct call_window {
	struct gtk_mod *mod;
	struct call *call;

	/** for communicating from gtk thread to main thread */
	struct mqueue *mq;

	struct {
		struct vumeter_dec *dec;
		struct vumeter_enc *enc;
	} vu;
	struct transfer_dialog *transfer_dialog;
	GtkWidget *window;
	GtkLabel *status;
	GtkLabel *duration;
	struct {
		GtkWidget *hangup, *transfer, *hold, *mute;
	} buttons;
	struct {
		GtkProgressBar *enc, *dec;
	} progress;
	guint duration_timer_tag;
	guint vumeter_timer_tag;
	bool closed;
	int cur_key;
	struct play *play_dtmf_tone;
};

enum call_window_events {
	MQ_HANGUP,
	MQ_CLOSE,
	MQ_HOLD,
	MQ_MUTE,
	MQ_TRANSFER,
};

static struct call_window *last_call_win = NULL;
static struct vumeter_dec *last_dec = NULL;
static struct vumeter_enc *last_enc = NULL;


static void call_window_update_duration(struct call_window *win)
{
	gchar buf[32];

	const uint32_t dur = call_duration(win->call);
	const uint32_t sec = dur%60%60;
	const uint32_t min = dur/60%60;
	const uint32_t hrs = dur/60/60;

	re_snprintf(buf, sizeof buf, "%u:%02u:%02u", hrs, min, sec);
	gtk_label_set_text(win->duration, buf);
}


static void call_window_update_vumeters(struct call_window *win)
{
	double value;

	if (win->vu.enc && win->vu.enc->started) {
		value = min((double)win->vu.enc->avg_rec / 0x4000, 1);
		gtk_progress_bar_set_fraction(win->progress.enc, value);
	}
	if (win->vu.dec && win->vu.dec->started) {
		value = min((double)win->vu.dec->avg_play / 0x4000, 1);
		gtk_progress_bar_set_fraction(win->progress.dec, value);
	}
}


static gboolean call_timer(gpointer arg)
{
	struct call_window *win = arg;
	call_window_update_duration(win);
	return G_SOURCE_CONTINUE;
}


static gboolean vumeter_timer(gpointer arg)
{
	struct call_window *win = arg;
	call_window_update_vumeters(win);
	return G_SOURCE_CONTINUE;
}


static void vumeter_timer_start(struct call_window *win)
{
	if (!win->vumeter_timer_tag)
		win->vumeter_timer_tag =
			g_timeout_add(100, vumeter_timer, win);
	if (win->vu.enc)
		win->vu.enc->avg_rec = 0;
	if (win->vu.dec)
		win->vu.dec->avg_play = 0;
}


static void vumeter_timer_stop(struct call_window *win)
{
	if (win->vumeter_timer_tag) {
		g_source_remove(win->vumeter_timer_tag);
		win->vumeter_timer_tag = 0;
	}
	gtk_progress_bar_set_fraction(win->progress.enc, 0);
	gtk_progress_bar_set_fraction(win->progress.dec, 0);
}


static void call_window_set_vu_dec(struct call_window *win,
				   struct vumeter_dec *dec)
{
	mem_deref(win->vu.dec);
	win->vu.dec = mem_ref(dec);

	vumeter_timer_start(win);
}


static void call_window_set_vu_enc(struct call_window *win,
				   struct vumeter_enc *enc)
{
	mem_deref(win->vu.enc);
	win->vu.enc = mem_ref(enc);

	vumeter_timer_start(win);
}


/* This is a hack to associate a call with its vumeters */

void call_window_got_vu_dec(struct vumeter_dec *dec)
{
	if (last_call_win)
		call_window_set_vu_dec(last_call_win, dec);
	else
		last_dec = dec;
}


void call_window_got_vu_enc(struct vumeter_enc *enc)
{
	if (last_call_win)
		call_window_set_vu_enc(last_call_win, enc);
	else
		last_enc = enc;
}


static void got_call_window(struct call_window *win)
{
	if (last_enc)
		call_window_set_vu_enc(win, last_enc);
	if (last_dec)
		call_window_set_vu_dec(win, last_dec);
	if (!last_enc || !last_dec)
		last_call_win = win;
}


static void call_on_hangup(GtkToggleButton *btn, struct call_window *win)
{
	(void)btn;
	mqueue_push(win->mq, MQ_CLOSE, win);
}


static void call_on_hold_toggle(GtkToggleButton *btn, struct call_window *win)
{
	bool hold = gtk_toggle_button_get_active(btn);
	if (hold)
		vumeter_timer_stop(win);
	else
		vumeter_timer_start(win);
	mqueue_push(win->mq, MQ_HOLD, (void *)(size_t)hold);
}


static void call_on_mute_toggle(GtkToggleButton *btn, struct call_window *win)
{
	bool mute = gtk_toggle_button_get_active(btn);
	mqueue_push(win->mq, MQ_MUTE, (void *)(size_t)mute);
}


static void call_on_transfer(GtkToggleButton *btn, struct call_window *win)
{
	(void)btn;
	if (!win->transfer_dialog)
		win->transfer_dialog = transfer_dialog_alloc(win);
	else
		transfer_dialog_show(win->transfer_dialog);
}


static gboolean call_on_window_close(GtkWidget *widget, GdkEventAny *event,
				     struct call_window *win)
{
	(void)event;
	(void)widget;
	mqueue_push(win->mq, MQ_CLOSE, NULL);
	return TRUE;
}


static gboolean call_on_key_press(GtkWidget *window, GdkEvent *ev,
				  struct call_window *win)
{
	struct config *cfg;
	cfg = conf_config();
	gchar key = ev->key.string[0];
	(void)window;
	char wavfile[32];

	switch (key) {
	case '1': case '2': case '3':
	case '4': case '5': case '6':
	case '7': case '8': case '9': case '0':
		re_snprintf(wavfile, sizeof wavfile, "sound%c.wav", key);
		break;
	case '*':
		re_snprintf(wavfile, sizeof wavfile, "sound%s.wav", "star");
		break;
	case '#':
		re_snprintf(wavfile, sizeof wavfile, "sound%s.wav", "route");
		break;
	default:
		return FALSE;
	}
	(void)play_file(&win->play_dtmf_tone, baresip_player(),
		wavfile, -1, cfg->audio.alert_mod,
		cfg->audio.alert_dev);
	win->cur_key = key;
	call_send_digit(win->call, key);
	return TRUE;
}


static gboolean call_on_key_release(GtkWidget *window, GdkEvent *ev,
				    struct call_window *win)
{
	(void)window;

	if (win->cur_key && win->cur_key == ev->key.string[0]) {
		win->play_dtmf_tone = mem_deref(win->play_dtmf_tone);
		win->cur_key = KEYCODE_REL;
		call_send_digit(win->call, KEYCODE_REL);
		return TRUE;
	}

	return FALSE;
}


static void call_window_set_status(struct call_window *win,
				   const char *status)
{
	gtk_label_set_text(win->status, status);
}


static void mqueue_handler(int id, void *data, void *arg)
{
	struct call_window *win = arg;

	switch ((enum call_window_events)id) {

	case MQ_HANGUP:
		ua_hangup(uag_current(), win->call, 0, NULL);
		break;

	case MQ_CLOSE:
		if (!win->closed) {
			ua_hangup(uag_current(), win->call, 0, NULL);
			win->closed = true;
		}
		mem_deref(win);
		break;

	case MQ_MUTE:
		audio_mute(call_audio(win->call), (size_t)data);
		break;

	case MQ_HOLD:
		call_hold(win->call, (size_t)data);
		break;

	case MQ_TRANSFER:
		call_transfer(win->call, data);
		break;
	}
}


static void call_window_destructor(void *arg)
{
	struct call_window *window = arg;

	gdk_threads_enter();
	gtk_mod_call_window_closed(window->mod, window);
	gtk_widget_destroy(window->window);
	mem_deref(window->transfer_dialog);
	gdk_threads_leave();

	mem_deref(window->call);
	mem_deref(window->mq);
	mem_deref(window->vu.enc);
	mem_deref(window->vu.dec);

	if (window->duration_timer_tag)
		g_source_remove(window->duration_timer_tag);
	if (window->vumeter_timer_tag)
		g_source_remove(window->vumeter_timer_tag);

	/* TODO: avoid race conditions here */
	last_call_win = NULL;
}


struct call_window *call_window_new(struct call *call, struct gtk_mod *mod)
{
	struct call_window *win;
	GtkWidget *window, *label, *status, *button, *progress, *image;
	GtkWidget *button_box, *vbox, *hbox;
	GtkWidget *duration;
	int err = 0;

	win = mem_zalloc(sizeof(*win), call_window_destructor);
	if (!win)
		return NULL;

	err = mqueue_alloc(&win->mq, mqueue_handler, win);
	if (err)
		goto out;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), call_peeruri(call));
	gtk_window_set_type_hint(GTK_WINDOW(window),
			GDK_WINDOW_TYPE_HINT_DIALOG);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	/* Peer name and URI */
	label = gtk_label_new(call_peername(call));
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	label = gtk_label_new(call_peeruri(call));
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	/* Call duration */
	duration = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(vbox), duration, FALSE, FALSE, 0);

	/* Status */
	status = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(vbox), status, FALSE, FALSE, 0);

	/* Progress bars */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_set_spacing(GTK_BOX(hbox), 6);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	/* Encoding vumeter */
	image = gtk_image_new_from_icon_name("audio-input-microphone",
			GTK_ICON_SIZE_BUTTON);
	progress = gtk_progress_bar_new();
	win->progress.enc = GTK_PROGRESS_BAR(progress);
	gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), progress, FALSE, FALSE, 0);

	/* Decoding vumeter */
	image = gtk_image_new_from_icon_name("audio-headphones",
			GTK_ICON_SIZE_BUTTON);
	progress = gtk_progress_bar_new();
	win->progress.dec = GTK_PROGRESS_BAR(progress);
	gtk_box_pack_end(GTK_BOX(hbox), progress, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), image, FALSE, FALSE, 0);

	/* Buttons */
	button_box = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box),
			GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(button_box), 6);
	gtk_container_set_border_width(GTK_CONTAINER(button_box), 5);
	gtk_box_pack_end(GTK_BOX(vbox), button_box, FALSE, TRUE, 0);

	/* Hang up */
	button = gtk_button_new_with_label("Hangup");
	win->buttons.hangup = button;
	gtk_box_pack_end(GTK_BOX(button_box), button, FALSE, TRUE, 0);
	g_signal_connect(button, "clicked",
			G_CALLBACK(call_on_hangup), win);
	image = gtk_image_new_from_icon_name("call-stop",
			GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button), image);

	/* Transfer */
	button = gtk_button_new_with_label("Transfer");
	win->buttons.transfer = button;
	gtk_box_pack_end(GTK_BOX(button_box), button, FALSE, TRUE, 0);
	g_signal_connect(button, "clicked", G_CALLBACK(call_on_transfer), win);
	image = gtk_image_new_from_icon_name("forward", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button), image);

	/* Hold */
	button = gtk_toggle_button_new_with_label("Hold");
	win->buttons.hold = button;
	gtk_box_pack_end(GTK_BOX(button_box), button, FALSE, TRUE, 0);
	g_signal_connect(button, "toggled",
			G_CALLBACK(call_on_hold_toggle), win);
	image = gtk_image_new_from_icon_name("player_pause",
			GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button), image);

	/* Mute */
	button = gtk_toggle_button_new_with_label("Mute");
	win->buttons.mute = button;
	gtk_box_pack_end(GTK_BOX(button_box), button, FALSE, TRUE, 0);
	g_signal_connect(button, "toggled",
			G_CALLBACK(call_on_mute_toggle), win);
	image = gtk_image_new_from_icon_name("microphone-sensitivity-muted",
			GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button), image);

	gtk_widget_show_all(window);
	gtk_window_present(GTK_WINDOW(window));

	g_signal_connect(window, "delete_event",
			G_CALLBACK(call_on_window_close), win);
	g_signal_connect(window, "key-press-event",
			G_CALLBACK(call_on_key_press), win);
	g_signal_connect(window, "key-release-event",
			G_CALLBACK(call_on_key_release), win);

	win->call = mem_ref(call);
	win->mod = mod;
	win->window = window;
	win->transfer_dialog = NULL;
	win->status = GTK_LABEL(status);
	win->duration = GTK_LABEL(duration);
	win->closed = false;
	win->duration_timer_tag = 0;
	win->vumeter_timer_tag = 0;
	win->vu.enc = NULL;
	win->vu.dec = NULL;

	got_call_window(win);

out:
	if (err)
		mem_deref(win);

	return win;
}


void call_window_transfer(struct call_window *win, const char *uri)
{
	mqueue_push(win->mq, MQ_TRANSFER, (char *)uri);
}


void call_window_closed(struct call_window *win, const char *reason)
{
	char buf[256];
	const char *status;

	if (!win)
		return;

	vumeter_timer_stop(win);
	if (win->duration_timer_tag) {
		g_source_remove(win->duration_timer_tag);
		win->duration_timer_tag = 0;
	}
	gtk_widget_set_sensitive(win->buttons.transfer, FALSE);
	gtk_widget_set_sensitive(win->buttons.hold, FALSE);
	gtk_widget_set_sensitive(win->buttons.mute, FALSE);

	if (reason && reason[0]) {
		re_snprintf(buf, sizeof buf, "closed: %s", reason);
		status = buf;
	}
	else {
		status = "closed";
	}

	call_window_set_status(win, status);
	win->transfer_dialog = mem_deref(win->transfer_dialog);
	win->closed = true;
}


void call_window_ringing(struct call_window *win)
{
	call_window_set_status(win, "ringing");
}


void call_window_progress(struct call_window *win)
{
	if (!win)
		return;

	win->duration_timer_tag = g_timeout_add_seconds(1, call_timer, win);
	last_call_win = win;
	call_window_set_status(win, "progress");
}


void call_window_established(struct call_window *win)
{
	if (!win)
		return;

	call_window_update_duration(win);

	if (!win->duration_timer_tag) {
		win->duration_timer_tag = g_timeout_add_seconds(1, call_timer,
								win);
	}

	last_call_win = win;
	call_window_set_status(win, "established");
}


void call_window_transfer_failed(struct call_window *win, const char *reason)
{
	if (!win)
		return;

	if (win->transfer_dialog) {
		transfer_dialog_fail(win->transfer_dialog, reason);
	}
}


bool call_window_is_for_call(struct call_window *win, struct call *call)
{
	if (!win)
		return false;

	return win->call == call;
}
