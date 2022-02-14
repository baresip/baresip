/**
 * @file src/descr.c  RTC Session Description
 *
 * Copyright (C) 2020 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>


enum { HASH_SIZE = 4 };


int session_description_encode(struct odict **odp,
			       enum sdp_type type, struct mbuf *sdp)
{
	struct odict *od = NULL;
	char *str = NULL;
	int err;

	if (!odp || !sdp)
		return EINVAL;

	info("descr: encode: type='%s'\n", sdptype_name(type));

	err = mbuf_strdup(sdp, &str, sdp->end);
	if (err)
		goto out;

	err = odict_alloc(&od, HASH_SIZE);
	if (err)
		goto out;

	err |= odict_entry_add(od, "type", ODICT_STRING, sdptype_name(type));
	err |= odict_entry_add(od, "sdp", ODICT_STRING, str);
	if (err)
		goto out;

 out:
	mem_deref(str);
	if (err)
		mem_deref(od);
	else
		*odp = od;

	return err;
}


int session_description_decode(struct session_description *sd,
			       struct mbuf *mb)
{
	const char *type, *sdp;
	struct odict *od;
	enum {MAX_DEPTH = 2};
	int err;

	if (!sd || !mb)
		return EINVAL;

	memset(sd, 0, sizeof(*sd));

	err = json_decode_odict(&od, HASH_SIZE, (char *)mbuf_buf(mb),
				mbuf_get_left(mb), MAX_DEPTH);
	if (err) {
		warning("descr: could not decode json (%m)\n", err);
		return err;
	}

	type = odict_string(od, "type");
	sdp  = odict_string(od, "sdp");
	if (!type || !sdp) {
		warning("descr: missing json fields\n");
		err = EPROTO;
		goto out;
	}

	if (0 == str_casecmp(type, "offer"))
		sd->type = SDP_OFFER;
	else if (0 == str_casecmp(type, "answer"))
		sd->type = SDP_ANSWER;
	else if (0 == str_casecmp(type, "rollback"))
		sd->type = SDP_ROLLBACK;
	else {
		warning("descr: invalid type %s\n", type);
		err = EPROTO;
		goto out;
	}

	sd->sdp = mbuf_alloc(512);
	if (!sd->sdp) {
		err = ENOMEM;
		goto out;
	}

	mbuf_write_str(sd->sdp, sdp);
	sd->sdp->pos = 0;

	info("descr: decode: type='%s'\n", type);

 out:
	mem_deref(od);

	return err;
}


void session_description_reset(struct session_description *sd)
{
	if (!sd)
		return;

	sd->type = -1;
	sd->sdp = mem_deref(sd->sdp);
}


const char *sdptype_name(enum sdp_type type)
{
	switch (type) {

	case SDP_OFFER:    return "offer";
	case SDP_ANSWER:   return "answer";
	case SDP_ROLLBACK: return "rollback";
	default: return "?";
	}
}
