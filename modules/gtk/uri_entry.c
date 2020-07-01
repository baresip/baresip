/**
 * @file uri_entry.c GTK+ URI entry combo box
 *
 * Copyright (C) 2015 Charles E. Lehner
 */

#include <re.h>
#include <baresip.h>
#include <gtk/gtk.h>
#include "gtk_mod.h"


/**
 * Create a URI combox box.
 *
 * The combo box has a menu of contacts, and a text entry for a URI.
 *
 * @return the combo box
 */
GtkWidget *uri_combo_box_new(void)
{
	struct contacts *contacts = baresip_contacts();
	struct le *le;
	GtkEntry *uri_entry;
	GtkWidget *uri_combobox;

	uri_combobox = gtk_combo_box_text_new_with_entry();
	uri_entry = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(uri_combobox)));
	gtk_entry_set_activates_default(uri_entry, TRUE);

	for (le = list_head(contact_list(contacts)); le; le = le->next) {
		struct contact *c = le->data;

		gtk_combo_box_text_append_text(
				       GTK_COMBO_BOX_TEXT(uri_combobox),
				       contact_str(c));
	}

	return uri_combobox;
}

void uri_combo_box_set_text(GtkComboBox *box, char* str, int length)
{
	gchar* number = g_strdup (str);
	GtkEntry *entry = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(box)));
	GtkEntryBuffer *buf = gtk_entry_get_buffer(entry);
	gtk_entry_buffer_set_text(buf, number, length);
}

const char *uri_combo_box_get_text(GtkComboBox *box)
{
	GtkEntry *entry = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(box)));
	GtkEntryBuffer *buf = gtk_entry_get_buffer(entry);

	return gtk_entry_buffer_get_text(buf);
}
