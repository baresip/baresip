/**
* @file src/custom_hdrs.c  Custom headers control
*
* Copyright (C) 2010 Creytiv.com
*/
#include <re.h>
#include <baresip.h>
#include "core.h"


static void hdr_destructor(void *arg)
{
	struct sip_hdr *hdr = arg;

	mem_deref((char *)hdr->name.p);
	mem_deref((char *)hdr->val.p);
}


int custom_hdrs_add(struct list *hdrs, const char *name,
		    const char *fmt, ...)
{
	struct pl temp_pl = { NULL, 0 };
	struct sip_hdr *hdr;
	char *value = NULL;
	va_list ap;
	int err = 0;

	va_start(ap, fmt);
	err = re_vsdprintf(&value, fmt, ap);
	va_end(ap);

	hdr = mem_zalloc(sizeof(*hdr), hdr_destructor);
	if (!hdr || !value)
		goto error;

	pl_set_str(&temp_pl, name);
	err = pl_dup(&hdr->name, &temp_pl);
	if (err)
		goto error;

	pl_set_str(&hdr->val, value);

	hdr->id = SIP_HDR_NONE;

	list_append(hdrs, &hdr->le, hdr);

	return 0;

error:
	mem_deref(hdr);
	return err;
}


int custom_hdrs_apply(const struct list *hdrs, custom_hdrs_h *h, void *arg)
{
	struct le *le;
	int err;

	LIST_FOREACH(hdrs, le) {
		struct sip_hdr *hdr = le->data;

		err = h(&hdr->name, &hdr->val, arg);
		if (err)
			return err;
	}

	return 0;
}


static int hdr_print_helper(const struct pl *name, const struct pl *val,
			    void *arg)
{
	struct re_printf *pf = arg;

	return re_hprintf(pf, "%r: %r\r\n", name, val);
}


int custom_hdrs_print(struct re_printf *pf, const struct list *custom_hdrs)
{
	return custom_hdrs_apply(custom_hdrs, hdr_print_helper, pf);
}
