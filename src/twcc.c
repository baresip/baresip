/**
 * @file twcc.c Transport-wide Congestion Control (TWCC) Receiver Status
 *
 * Copyright (C) 2025 Sebastian Reimers
 */

#include <re.h>
#include <baresip.h>


enum {
	TWCC_INTERVAL	= 100, /* ms */
	TWCC_MAX_CHUNKS = 400  /* Max. chunks per RTCP message */
};

static const char uri[] = "http://www.ietf.org/id/"
			  "draft-holmer-rmcat-transport-wide-cc-extensions-01";

struct twcc_packet {
	struct le le;
	uint16_t tseq;
	uint32_t delta;
};

struct twcc_status {
	mtx_t *mtx;
	struct stream *stream;
	struct list packets;
	struct mem_pool *pool;
	struct tmr tmr;
	int32_t ref_time;
};


static void send_feedback(void *arg)
{
	struct twcc_status *twccst = arg;

	mtx_lock(twccst->mtx);
	if (!twccst->packets.head)
		goto out;

	struct le *le;
	LIST_FOREACH(&twccst->packets, le)
	{
		struct mem_pool_entry *e = le->data;

		struct twcc_packet *p = mem_pool_member(e);
		if (!p)
			goto out;

		warning("RTCP TWCC -> %u\n", p->tseq);

		mem_pool_release(twccst->pool, e);
	}

out:
	tmr_start(&twccst->tmr, TWCC_INTERVAL, send_feedback, twccst);
	mtx_unlock(twccst->mtx);
}


static void twcc_destruct(void *arg)
{
	struct twcc_status *twccst = arg;

	tmr_cancel(&twccst->tmr);
	mem_deref(twccst->mtx);
}


int twcc_status_alloc(struct twcc_status **twccstp, struct stream *stream)
{
	if (!twccstp)
		return EINVAL;

	struct twcc_status *twccst =
		mem_zalloc(sizeof(struct twcc_status), twcc_destruct);
	if (!twccst)
		return ENOMEM;

	int err = mutex_alloc(&twccst->mtx);
	if (err)
		goto out;

	twccst->stream = stream;

	err = mem_pool_alloc(&twccst->pool, TWCC_MAX_CHUNKS,
			     sizeof(struct twcc_packet), NULL);
out:
	if (err)
		mem_deref(twccst);
	else {
		*twccstp = twccst;
		tmr_start(&twccst->tmr, TWCC_INTERVAL, send_feedback, twccst);
	}

	return 0;
}


static bool extmap_handler(const char *name, const char *value, void *arg)
{
	struct stream *strm = arg;
	struct sdp_extmap extmap;
	int err;
	(void)name;

	err = sdp_extmap_decode(&extmap, value);
	if (err) {
		warning("twcc: sdp_extmap_decode error (%m)\n", err);
		return false;
	}

	if (0 == pl_strcasecmp(&extmap.name, uri)) {
		err = sdp_media_set_lattr(stream_sdpmedia(strm), true,
					  "extmap", "%u %s", extmap.id, uri);
		if (err)
			false;

		stream_set_extmap_twcc(strm, extmap.id);

		return true;
	}

	return false;
}


void twcc_status_handle_extmap(struct stream *strm)
{
	sdp_media_rattr_apply(stream_sdpmedia(strm), "extmap", extmap_handler,
			      strm);
}


void twcc_status_append(struct twcc_status *twccst, uint16_t tseq)
{
	if (!twccst)
		return;

	struct mem_pool_entry *e = mem_pool_borrow_extend(twccst->pool);

	struct twcc_packet *p = mem_pool_member(e);
	if (!p)
		return;

	p->tseq = tseq;

	mtx_lock(twccst->mtx);
	list_append(&twccst->packets, &p->le, e);
	mtx_unlock(twccst->mtx);
}
