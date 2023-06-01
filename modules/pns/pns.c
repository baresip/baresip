/**
 * @file pns.c Push Notification Service Module for SIP
 *
 * Copyright (C) 2023 Cody T.-H. Chiu
 */

#include <re.h>
#include <baresip.h>
#include <string.h>


/**
 * @defgroup pns pns
 *
 * Push Notification Service Module for SIP
 */


/** Defines PNS parameters  */
struct pns {
	char *pn_provider;	/**< Push provider */
	char *pn_prid;		/**< Push resource ID */
	char *pn_param;		/**< Push provider optional param */
};


static void destructor(void *data)
{
	struct pns *pns = data;

	mem_deref(pns->pn_provider);
	mem_deref(pns->pn_prid);
	mem_deref(pns->pn_param);
}


/**
 * Allocate a Push Notification Service
 *
 * @param pnsp     	Pointer to allocated Push Notification Service
 * @param provider	Push provider
 * @param prid    	Push resource ID
 * @param param 	Push provider optional parameter
 *
 * @return 0 if success, otherwise errorcode
 */
static int pns_alloc(struct pns **pnsp, const struct pl *provider,
		const struct pl *prid, const struct pl *param)
{
	struct pns *pns;
	int err = 0;

	if (!pl_isset(provider) || !pl_isset(prid)) {
		warning("pns: provider and prid are required\n");
		return EINVAL;
	}

	pns = mem_zalloc(sizeof(*pns), destructor);
	if (!pns)
		return ENOMEM;

	err |= pl_strdup(&pns->pn_provider, provider);
	err |= pl_strdup(&pns->pn_prid, prid);
	if (pl_isset(param))
		err |= pl_strdup(&pns->pn_param, param);

	*pnsp = pns;
	return err;
}


/* Encode push notification service into URI parameters */
static int pns_encode(char **params, struct pns *pns) {
	return re_sdprintf(params, "pn-provider=%s;pn-prid=%s%s%s;",
		pns->pn_provider,
		pns->pn_prid,
		str_isset(pns->pn_param) ? ";pn-param=" : "",
		str_isset(pns->pn_param) ? pns->pn_param : "");
}


/* Decode comma separated parameters into push notification service */
static int pns_decode(struct pns **pns, struct pl *data) {
	struct pl provider, prid, param;
	int err;

	err = re_regex(data->p, data->l, "[^,]*,[^,]*[,]*[~]*",
		&provider, &prid, NULL, &param);

	if (err)
		return err;

	return pns_alloc(pns, &provider, &prid, &param);
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
		struct call *call, const char *prm, void *arg) {

	(void)call;
	(void)arg;

	int err;
	struct pl module, event, data;
	struct pns *pns = NULL;
	char *ua_cparam = NULL;

	if (ev != UA_EVENT_MODULE)
		return;

	if (re_regex(prm, strlen(prm), "[^,]*,[^,]*,[~]*",
			&module, &event, &data))
		return;

	if (pl_strcmp(&module, "pns") || pl_strcmp(&event, "config_update"))
		return;

	err = pns_decode(&pns, &data);
	if (err) {
		if (err == EINVAL)
			err |= ua_set_contact_params(ua, NULL);
		else
			goto error;
	}

	if (pns) {
		err |= pns_encode(&ua_cparam, pns);
		err |= ua_set_contact_params(ua, ua_cparam);
	}

	mem_deref(pns);
	mem_deref(ua_cparam);

error:
	if (err)
		warning("pns: error updating pns config\n");
}


static int module_init(void)
{
	return uag_event_register(ua_event_handler, NULL);
}


static int module_close(void)
{
	uag_event_unregister(ua_event_handler);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(pns) = {
	"pns",
	"sipext",
	module_init,
	module_close,
};
