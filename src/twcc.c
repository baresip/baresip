/**
 * @file twcc.c Transport-wide Congestion Control (TWCC) Receiver Status
 *
 * Copyright (C) 2025 Sebastian Reimers
 */

#include <re.h>
#include <baresip.h>


enum {
	TWCC_INTERVAL	= 100,	/**< ms                                */
	TWCC_MAX_PAKETS = 100,	/**< Limit pakets per Feedback message */
	TWCC_PKT_SIZE	= 1280, /**< max. Packet size in bytes         */
};

enum packet_state {
	PACKET_NOT_RECEIVED = 0,
	PACKET_RECEIVED,
	PACKET_LARGE_DELAY /* Packet received, large or negative delta */
};

static const char uri[] = "http://www.ietf.org/id/"
			  "draft-holmer-rmcat-transport-wide-cc-extensions-01";

struct twcc_packet {
	struct le le;
	uint16_t tseq;
	uint32_t delta;
	enum packet_state state;
};

struct twcc_status {
	mtx_t *mtx;
	struct stream *stream;
	struct list packets;
	struct list status;
	struct mem_pool *pool;
	struct tmr tmr;
	uint16_t last_tseq;
	uint8_t fb_count;
	struct mbuf *chunks;
	struct mbuf *delays;
};

/** Is x less than y? */
static inline bool seq_less(uint16_t x, uint16_t y)
{
	return ((int16_t)(x - y)) < 0;
}


static void send_feedback(void *arg)
{
	struct twcc_status *twccst = arg;

	uint64_t delay = TWCC_INTERVAL;

	mtx_lock(twccst->mtx);
	if (!twccst->packets.head)
		goto out;

	struct le *le = twccst->packets.head;

	struct mem_pool_entry *e = le->data;
	struct twcc_packet *p	 = mem_pool_member(e);

	uint16_t base_seq  = p->tseq;
	uint16_t pkt_count = 1;

	++twccst->fb_count;

	while (le) {
		uint16_t chunk = 0;

		e  = le->data;
		le = le->next;

		p = mem_pool_member(e);
		if (!p)
			goto out;

		warning("RTCP TWCC -> %u %u\n", p->tseq, pkt_count);

		++pkt_count;

		list_move(&p->le, &twccst->status);

		mem_pool_release(twccst->pool, e);
		if (pkt_count > TWCC_MAX_PAKETS) {
			delay = 0;
			break;
		}
	}

	/* Run Length chunk */
	/* Status Vector Chunk - 14 bit "packet received" and NOT */
	/* Status Vector Chunk - 7 bit */

	/* Send RTCP */

out:
	tmr_start(&twccst->tmr, delay, send_feedback, twccst);
	mtx_unlock(twccst->mtx);
}


void twcc_status_send_feedback(struct twcc_status *twccst)
{
	send_feedback(twccst);
}


static void twcc_destruct(void *arg)
{
	struct twcc_status *twccst = arg;

	tmr_cancel(&twccst->tmr);
	mem_deref(twccst->mtx);
	mem_deref(twccst->pool);
	mem_deref(twccst->chunks);
	mem_deref(twccst->delays);
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

	err = mem_pool_alloc(&twccst->pool, TWCC_MAX_PAKETS,
			     sizeof(struct twcc_packet), NULL);
	if (err)
		goto out;

	twccst->chunks = mbuf_alloc(TWCC_PKT_SIZE);
	if (!twccst->chunks) {
		err = ENOMEM;
		goto out;
	}

	twccst->delays = mbuf_alloc(TWCC_PKT_SIZE);
	if (!twccst->delays) {
		err = ENOMEM;
		goto out;
	}

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

	/* already late - and reported */
	if (seq_less(tseq, twccst->last_tseq + 1))
		return;

	struct mem_pool_entry *e = mem_pool_borrow_extend(twccst->pool);

	struct twcc_packet *p = mem_pool_member(e);
	if (!p)
		return;

	p->tseq	 = tseq;
	p->state = PACKET_RECEIVED;

	int16_t seq_diff = p->tseq - twccst->last_tseq;
	while (seq_diff-- > 1) {
		struct mem_pool_entry *ef =
			mem_pool_borrow_extend(twccst->pool);

		struct twcc_packet *pf = mem_pool_member(ef);
		if (!pf)
			return;
		pf->state = PACKET_NOT_RECEIVED;
		pf->tseq  = p->tseq - seq_diff;
		mtx_lock(twccst->mtx);
		list_append(&twccst->packets, &pf->le, ef);
		mtx_unlock(twccst->mtx);
	}

	twccst->last_tseq = p->tseq;

	/* calculate delay */
	mtx_lock(twccst->mtx);
	list_append(&twccst->packets, &p->le, e);
	mtx_unlock(twccst->mtx);
}


struct mbuf *twcc_status_chunks(struct twcc_status *twccst)
{
	return twccst ? twccst->chunks : NULL;
}


struct mbuf *twcc_status_delays(struct twcc_status *twccst)
{
	return twccst ? twccst->delays : NULL;
}
