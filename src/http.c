/**
 * @file http.c  HTTP functions
 *
 * Copyright (C) 2020 - 2022 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>


const char *http_extension_to_mimetype(const char *ext)
{
	if (0 == str_casecmp(ext, "html")) return "text/html";
	if (0 == str_casecmp(ext, "js"))   return "text/javascript";

	return "application/octet-stream";  /* default */
}
