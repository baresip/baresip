/* static.c - manually updated */
#include <re_types.h>
#include <re_mod.h>

extern const struct mod_export exports_account;
extern const struct mod_export exports_auloop;
extern const struct mod_export exports_contact;
extern const struct mod_export exports_debug_cmd;
extern const struct mod_export exports_dshow;
extern const struct mod_export exports_g711;
extern const struct mod_export exports_httpd;
extern const struct mod_export exports_ice;
extern const struct mod_export exports_menu;
extern const struct mod_export exports_stun;
extern const struct mod_export exports_turn;
extern const struct mod_export exports_uuid;
extern const struct mod_export exports_vidloop;
extern const struct mod_export exports_vumeter;
extern const struct mod_export exports_wincons;
extern const struct mod_export exports_winwave;


const struct mod_export *mod_table[] = {
	&exports_account,
	&exports_auloop,
	&exports_contact,
	&exports_debug_cmd,
	&exports_dshow,
	&exports_g711,
	&exports_httpd,
	&exports_ice,
	&exports_menu,
	&exports_stun,
	&exports_turn,
	&exports_uuid,
	&exports_vidloop,
	&exports_vumeter,
	&exports_wincons,
	&exports_winwave,
	NULL
};
