/**
 * @file transfer_dialog.c GTK+ call transfer dialog
 *
 * Copyright (C) 2015 Charles E. Lehner
 */
#include <re.h>
#include <baresip.h>
#include <gtk/gtk.h>
#include "gtk_mod.h"


struct transfer_dialog {
	struct call_window *call_win;
	GtkWidget *dialog;
	GtkComboBox *uri_combobox;
	GtkLabel *status_label;
	GtkWidget *spinner;
};

static const char *status_progress = "progress";


static void set_status(struct transfer_dialog *td, const char *status)
{
	if (status == status_progress) {
		gtk_widget_show(td->spinner);
		gtk_spinner_start(GTK_SPINNER(td->spinner));
		gtk_label_set_text(td->status_label, NULL);
	}
	else {
		gtk_widget_hide(td->spinner);
		gtk_spinner_stop(GTK_SPINNER(td->spinner));
		gtk_label_set_text(td->status_label, status);
	}
}


static void on_dialog_response(GtkDialog *dialog, gint response_id,
			       struct transfer_dialog *win)
{
	char *uri;

	if (response_id == GTK_RESPONSE_ACCEPT) {
		uri = (char *)uri_combo_box_get_text(win->uri_combobox);
		set_status(win, status_progress);
		call_window_transfer(win->call_win, uri);
	}
	else {
		set_status(win, NULL);
		gtk_widget_hide(GTK_WIDGET(dialog));
	}
}


static void destructor(void *arg)
{
	struct transfer_dialog *td = arg;

	gtk_widget_destroy(td->dialog);
}


struct transfer_dialog *transfer_dialog_alloc(struct call_window *call_win)
{
	struct transfer_dialog *win;
	GtkWidget *dialog, *content, *button, *image, *hbox, *spinner, *label;
	GtkWidget *uri_combobox;

	win = mem_zalloc(sizeof(*win), destructor);
	if (!win)
	    return NULL;

	dialog = gtk_dialog_new_with_buttons("Transfer", NULL, 0,
	        GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT, NULL);

	/* Transfer button */
	button = gtk_button_new_with_label("Transfer");
	image = gtk_image_new_from_icon_name("forward",
			GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button), image);
	gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button,
			GTK_RESPONSE_ACCEPT);
	gtk_widget_set_can_default(button, TRUE);

	gtk_dialog_set_default_response(GTK_DIALOG(dialog),
			GTK_RESPONSE_ACCEPT);
	/* Label */
	content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	label = gtk_label_new("Transfer call to:");
	gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);

	/* URI entry */
	uri_combobox = uri_combo_box_new();
	gtk_box_pack_start(GTK_BOX(content), uri_combobox, FALSE, FALSE, 5);

	g_signal_connect(dialog, "response",
			 G_CALLBACK(on_dialog_response), win);
	g_signal_connect(dialog, "delete-event",
			 G_CALLBACK(gtk_widget_hide_on_delete), win);

	/* Spinner and status */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 0);

	spinner = gtk_spinner_new();
	gtk_box_pack_start(GTK_BOX(hbox), spinner, TRUE, TRUE, 0);

	label = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
	win->status_label = GTK_LABEL(label);

	win->dialog = dialog;
	win->uri_combobox = GTK_COMBO_BOX(uri_combobox);
	win->call_win = call_win;
	win->spinner = spinner;

	gtk_widget_show_all(dialog);
	gtk_widget_hide(spinner);

	return win;
}


void transfer_dialog_show(struct transfer_dialog *td)
{
	if (!td)
		return;

	gtk_window_present(GTK_WINDOW(td->dialog));
	gtk_widget_grab_focus(gtk_bin_get_child(GTK_BIN(td->uri_combobox)));
	set_status(td, NULL);
}


void transfer_dialog_fail(struct transfer_dialog *td, const char *reason)
{
	char buf[256];

	if (!td)
		return;

	re_snprintf(buf, sizeof buf, "Transfer failed: %s", reason);
	set_status(td, buf);
}
