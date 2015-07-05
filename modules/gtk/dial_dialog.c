/**
 * @file gtk/dial_dialog.c  GTK+ dial dialog
 *
 * Copyright (C) 2015 Charles E. Lehner
 */

#include <re.h>
#include <baresip.h>
#include <stdlib.h>
#include <pthread.h>
#include <gtk/gtk.h>
#include "gtk_mod.h"

struct dial_dialog {
	struct gtk_mod *mod;
	GtkWidget *dialog;
	GtkComboBox *uri_combobox;
};

static void dial_dialog_on_response(GtkDialog *dialog, gint response_id,
		gpointer arg)
{
	struct dial_dialog *dd = arg;
	char *uri;

	if (response_id == GTK_RESPONSE_ACCEPT) {
		uri = (char *)uri_combo_box_get_text(dd->uri_combobox);
		gtk_mod_connect(dd->mod, uri);
	}

	gtk_widget_hide(GTK_WIDGET(dialog));
}


static void destructor(void *arg)
{
	struct dial_dialog *dd = arg;

	gtk_widget_destroy(dd->dialog);
}

struct dial_dialog *dial_dialog_alloc(struct gtk_mod *mod)
{
	struct dial_dialog *dd;
	GtkWidget *dial;
	GtkWidget *content, *button, *image;
	GtkWidget *uri_combobox;

	dd = mem_zalloc(sizeof(*dd), destructor);
	if (!dd)
		return NULL;

	dial = gtk_dialog_new_with_buttons("Dial", NULL, 0, NULL);

	/* Cancel */
	button = gtk_button_new_with_label("Cancel");
	image = gtk_image_new_from_icon_name("call-stop",
			GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button), image);
	gtk_dialog_add_action_widget(GTK_DIALOG(dial), button,
			GTK_RESPONSE_REJECT);

	/* Call */
	button = gtk_button_new_with_label("Call");
	image = gtk_image_new_from_icon_name("call-start",
			GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button), image);
	gtk_dialog_add_action_widget(GTK_DIALOG(dial), button,
			GTK_RESPONSE_ACCEPT);
	gtk_widget_set_can_default (button, TRUE);

	gtk_dialog_set_default_response(GTK_DIALOG(dial),
			GTK_RESPONSE_ACCEPT);
	uri_combobox = uri_combo_box_new();

	content = gtk_dialog_get_content_area(GTK_DIALOG(dial));
	gtk_box_pack_start(GTK_BOX(content), uri_combobox, FALSE, FALSE, 5);
	gtk_widget_show_all(content);

	g_signal_connect(G_OBJECT(dial), "response",
			G_CALLBACK(dial_dialog_on_response), dd);
	g_signal_connect(G_OBJECT(dial), "delete-event",
			G_CALLBACK(gtk_widget_hide_on_delete), dd);

	dd->dialog = dial;
	dd->uri_combobox = GTK_COMBO_BOX(uri_combobox);
	dd->mod = mod;

	return dd;
}

void dial_dialog_show(struct dial_dialog *dd)
{
	gtk_window_present(GTK_WINDOW(dd->dialog));
	gtk_widget_grab_focus(gtk_bin_get_child(GTK_BIN(dd->uri_combobox)));
}
