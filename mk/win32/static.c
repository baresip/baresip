/* static.c - manually updated */
#include <re_types.h>
#include <re_mod.h>

extern const struct mod_export exports_cons;
extern const struct mod_export exports_g711;
extern const struct mod_export exports_winwave;

const struct mod_export *mod_table[] = {
	&exports_cons,
	&exports_g711,
	&exports_winwave,
	NULL
};
