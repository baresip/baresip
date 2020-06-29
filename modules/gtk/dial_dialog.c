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

static int clean_number(char* str)
{
	/* only clean numeric numbers
	 * In other cases trust the user input
	 */
	int err = re_regex(str, sizeof(str), "[A-Za-z]");
	if (err == 0)
		return -1;

	/* remove (0) which is in some mal-formated numbers
	 * but only if trailed by another character
	 */
	int i = 0, k = 0;
	if (str[0] == '+' || (str[0] == '0' && str[1] == '0'))
		while (str[i]) {
			if (str[i] == '('
			 && str[i+1] == '0'
			 && str[i+2] == ')'
			 && (str[i+3] == ' '
				 || (str[i+3] >= '0' && str[i+3] <= '9')
			    )
			) {
				str[i+1] = ' ';
				break;
			}
			++i;
		}
	i = 0;
	while (str[i]) {
		if (str[i] == ' '
		 || str[i] == '.'
		 || str[i] == '-'
		 || str[i] == '/'
		 || str[i] == '('
		 || str[i] == ')')
			++i;
		else
			str[k++] = str[i++];
	}
	str[k] = '\0';
	return k;
}

static void dial_dialog_on_response(GtkDialog *dialog, gint response_id,
				    gpointer arg)
{
	struct dial_dialog *dd = arg;
	char *uri;

	if (response_id == GTK_RESPONSE_ACCEPT) {
		uri = (char *)uri_combo_box_get_text(dd->uri_combobox);
		if (gtk_mod_clean_number(dd->mod)) {
			int length = clean_number(uri);
			if (length >= 0)
				uri_combo_box_set_text(dd->uri_combobox,
					uri, length);
		}
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
	if (!dd)
		return;

	gtk_window_present(GTK_WINDOW(dd->dialog));
	gtk_widget_grab_focus(gtk_bin_get_child(GTK_BIN(dd->uri_combobox)));
}
