/* static.c - manually updated */
#include <re_types.h>
#include <re_mod.h>

extern const struct mod_export exports_cons;
extern const struct mod_export exports_wincons;
extern const struct mod_export exports_g711;
extern const struct mod_export exports_winwave;
extern const struct mod_export exports_dshow;
extern const struct mod_export exports_avcodec;

const struct mod_export *mod_table[] = {
	//&exports_cons,
	&exports_avcodec,
	&exports_wincons,
	&exports_g711,
	&exports_winwave,
	&exports_dshow,
	NULL
};
