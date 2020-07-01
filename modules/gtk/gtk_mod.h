/**
 * @file gtk/gtk_mod.h GTK+ UI module -- internal API
 *
 * Copyright (C) 2015 Charles E. Lehner
 */

struct gtk_mod;
struct call_window;
struct dial_dialog;
struct transfer_dialog;

struct vumeter_enc {
	struct aufilt_enc_st af;  /* inheritance */
	int16_t avg_rec;
	volatile bool started;
};

struct vumeter_dec {
	struct aufilt_dec_st af;  /* inheritance */
	int16_t avg_play;
	volatile bool started;
};

/* Main menu */
void gtk_mod_connect(struct gtk_mod *, const char *uri);
void gtk_mod_call_window_closed(struct gtk_mod *, struct call_window *);

/* Call Window */
struct call_window *call_window_new(struct call *call, struct gtk_mod *mod);
void call_window_got_vu_dec(struct vumeter_dec *);
void call_window_got_vu_enc(struct vumeter_enc *);
void call_window_transfer(struct call_window *, const char *uri);
void call_window_closed(struct call_window *, const char *reason);
void call_window_ringing(struct call_window *);
void call_window_progress(struct call_window *);
void call_window_established(struct call_window *);
void call_window_transfer_failed(struct call_window *, const char *reason);
bool call_window_is_for_call(struct call_window *, struct call *);

/* Dial Dialog */
struct dial_dialog *dial_dialog_alloc(struct gtk_mod *);
void dial_dialog_show(struct dial_dialog *);

/* Call transfer dialog */
struct transfer_dialog *transfer_dialog_alloc(struct call_window *);
void transfer_dialog_show(struct transfer_dialog *);
void transfer_dialog_fail(struct transfer_dialog *, const char *reason);

/* URI entry combo box */
GtkWidget *uri_combo_box_new(void);
void uri_combo_box_set_text(GtkComboBox *box, char* str, int length);
const char *uri_combo_box_get_text(GtkComboBox *box);

/* Helper functions */
bool gtk_mod_clean_number(struct gtk_mod *mod);
