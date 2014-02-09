/**
 * @file mod.cpp  Module wrapper for Symbian OS
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <e32def.h>
#include <e32std.h>
#include <re_types.h>
#include <re_mod.h>

extern "C" {
	extern const struct mod_export exports;
}


EXPORT_C void *NewModule()
{
	return (void *)&exports;
}
