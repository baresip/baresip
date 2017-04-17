/* static.c - manually updated */
#include <re_types.h>
#include <re_mod.h>

extern const struct mod_export exports_cons;
extern const struct mod_export exports_wincons;
extern const struct mod_export exports_g711;
extern const struct mod_export exports_winwave;
extern const struct mod_export exports_dshow;
extern const struct mod_export exports_account;
extern const struct mod_export exports_contact;
extern const struct mod_export exports_menu;
extern const struct mod_export exports_auloop;
extern const struct mod_export exports_vidloop;
extern const struct mod_export exports_uuid;
extern const struct mod_export exports_stun;
extern const struct mod_export exports_turn;
extern const struct mod_export exports_ice;
extern const struct mod_export exports_vumeter;


const struct mod_export *mod_table[] = {
	&exports_wincons,
	&exports_g711,
	&exports_winwave,
	&exports_dshow,
	&exports_account,
	&exports_contact,
	&exports_menu,
	&exports_auloop,
	&exports_vidloop,
	&exports_uuid,
	&exports_stun,
	&exports_turn,
	&exports_ice,
	&exports_vumeter,
	NULL
};
