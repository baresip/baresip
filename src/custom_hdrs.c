#include <re.h>
#include <baresip.h>
#include "core.h"


static void hdr_destructor(void *arg)
{
	struct sip_hdr *hdr = arg;

	/*
	list_unlink(&hdr->le);
	hash_unlink(&hdr->he);
	*/
	mem_deref((char *)hdr->name.p);
	mem_deref((char *)hdr->val.p);
}

static void custom_hdrs_destructor(void *arg)
{
	struct list *hdrs = arg;
	list_flush(hdrs);
}

int custom_hdrs_alloc(struct list **hdrs)
{
	struct list *h;
	h = mem_zalloc(sizeof(*h), custom_hdrs_destructor);
	if (!h)
		return ENOMEM;
	list_init(h);
	*hdrs = h;
	return 0;
}

int custom_hdrs_add_pl(struct list *hdrs,
					   const struct pl *name,
					   const struct pl *val)
{
	int err;
	struct sip_hdr *hdr;

	hdr = mem_zalloc(sizeof(*hdr), hdr_destructor);
	if (!hdr)
		return ENOMEM;

	err = pl_dup(&hdr->name, name);
	if (err)
		goto err;

	err = pl_dup(&hdr->val, val);
	if (err)
		goto err;

	hdr->id = SIP_HDR_NONE;

	list_append(hdrs, &hdr->le, hdr);

	return 0;

err:
	mem_deref(hdr);
	return err;
}

int custom_hdrs_add(struct list *hdrs,
					const char *name,
					const char *val)
{
	struct pl name_pl;
	struct pl val_pl;

	pl_set_str(&name_pl, name);
	pl_set_str(&val_pl, val);

	return custom_hdrs_add_pl(hdrs, &name_pl, &val_pl);
}

int custom_hdrs_add_int(struct list *hdrs, const char *name, int val)
{
	char buf[21];
	re_snprintf(buf, sizeof(buf), "%d", val);
	return custom_hdrs_add(hdrs, name, buf);
}

int custom_hdrs_apply(const struct list *hdrs,
					  custom_hdrs_h *h,
					  void *arg)
{
	int err;
	struct le *le;
	for (le = list_head(hdrs); le; le = le->next) {
		struct sip_hdr * hdr = (struct sip_hdr *)(le->data);
		err = h(&hdr->name, &hdr->val, arg);
		if (err) {
			return err;
		}
	}
	return 0;
}

static int hdr_print_helper(const struct pl *name,
							const struct pl *val,
							void *arg)
{
	struct re_printf *pf = arg;
	int err = re_hprintf(pf, "%r: %r\r\n", name, val);
	return err;
}

int custom_hdrs_print(struct re_printf *pf,
					 const struct list *custom_hdrs)
{
	return custom_hdrs_apply(custom_hdrs, hdr_print_helper, pf);
}
