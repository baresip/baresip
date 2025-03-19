/**
 * @file twcc.c Transport-wide Congestion Control (TWCC) Receiver Status
 *
 * https://tools.ietf.org/
 * draft-holmer-rmcat-transport-wide-cc-extensions-01
 *
 * Copyright (C) 2025 Sebastian Reimers
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


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
	int32_t delta;
	enum packet_state state;
	uint64_t ts;
};

struct twcc_status {
	mtx_t *mtx;
	struct stream *stream;
	struct list packets;
	struct list status;
	struct mem_pool *pool;
	struct tmr tmr;
	uint16_t last_tseq;
	enum packet_state max_state;
	enum packet_state last_state;
	bool equal_state;
	uint64_t last_ts;
	struct twcc msg;
};

/** Is x less than y? */
static inline bool seq_less(uint16_t x, uint16_t y)
{
	return ((int16_t)(x - y)) < 0;
}


static inline void append_delay(struct twcc_status *twccst,
				struct twcc_packet *p)
{
	switch (p->state) {
	case PACKET_RECEIVED:
		mbuf_write_u8(twccst->msg.deltas, p->delta);
		break;
	case PACKET_LARGE_DELAY:
		mbuf_write_u16(twccst->msg.deltas, htons(p->delta));
		break;
	default:
		break;
	}
}


static uint16_t handle_run_chunk(struct twcc_status *twccst)
{
	struct le *le		 = list_head(&twccst->status);
	struct mem_pool_entry *e = le->data;
	struct twcc_packet *p	 = mem_pool_member(e);
	enum packet_state state	 = p->state;

	int cnt = 0;
	while (le) {
		e  = le->data;
		le = le->next;
		p  = mem_pool_member(e);
		if (!p || (p->state != state))
			break;

		++cnt;

		append_delay(twccst, p);
		list_unlink(&p->le);
		mem_pool_release(twccst->pool, e);
	}

	uint16_t chunk = 0;
	if (cnt) {
		chunk |= state << 13;
		chunk |= cnt;
	}

	return chunk;
}


static void handle_chunks(struct twcc_status *twccst, bool run_finish)
{
	struct le *le;
	size_t status_cnt = list_count(&twccst->status);
	uint16_t chunk;

	if (run_finish && twccst->equal_state && status_cnt >= 7) {
		chunk = handle_run_chunk(twccst);
	}
	else if (!twccst->equal_state &&
		 twccst->max_state <= PACKET_RECEIVED && status_cnt == 14) {
		/* Add Status Vector Chunk - 14 bits */
		chunk = 0x8000;

		size_t cnt = status_cnt;

		le = list_head(&twccst->status);
		while (le) {
			struct mem_pool_entry *e = le->data;
			struct twcc_packet *p	 = mem_pool_member(e);

			le = le->next;

			chunk |= p->state << --cnt;

			append_delay(twccst, p);
			list_unlink(&p->le);
			mem_pool_release(twccst->pool, e);
		}
	}
	else if (!twccst->equal_state &&
		 twccst->max_state >= PACKET_LARGE_DELAY && status_cnt == 7) {
		/* Add Status Vector Chunk - 7 bits */
		chunk = 0xc000;

		size_t cnt = status_cnt;

		le = list_head(&twccst->status);
		while (le) {
			struct mem_pool_entry *e = le->data;
			struct twcc_packet *p	 = mem_pool_member(e);

			le = le->next;

			chunk |= p->state << (--cnt * 2);

			append_delay(twccst, p);
			list_unlink(&p->le);
			mem_pool_release(twccst->pool, e);
		}
	}
	else {
		return;
	}

	mbuf_write_u16(twccst->msg.chunks, htons(chunk));

	/* reset states */
	le = twccst->packets.head;
	if (le) {
		struct mem_pool_entry *e = le->data;
		struct twcc_packet *p	 = mem_pool_member(e);
		twccst->last_state	 = p->state;
	}
	twccst->equal_state = true;
	twccst->max_state   = PACKET_NOT_RECEIVED;
	list_clear(&twccst->status);
}


static void send_feedback(void *arg)
{
	struct twcc_status *twccst = arg;

	uint64_t tmr_delay = TWCC_INTERVAL;
	uint16_t pkt_count = 0;

	mtx_lock(twccst->mtx);
	if (!twccst->packets.head)
		goto out;

	struct le *le = twccst->packets.head;

	struct mem_pool_entry *e = le->data;
	struct twcc_packet *p	 = mem_pool_member(e);

	twccst->msg.seq	    = p->tseq;
	twccst->msg.reftime = (uint32_t)(p->ts / 64);
	twccst->msg.fbcount += 1;

	mbuf_rewind(twccst->msg.chunks);
	mbuf_rewind(twccst->msg.deltas);

	twccst->equal_state = true;
	twccst->last_state  = p->state;
	twccst->last_ts	    = twccst->msg.reftime * 64;


	while (le) {
		e  = le->data;
		le = le->next;

		p = mem_pool_member(e);
		if (!p)
			goto out;

		if (p->state > twccst->max_state)
			twccst->max_state = p->state;

		p->delta = (int32_t)(p->ts - twccst->last_ts) * 1000LL / 250;
		if (p->delta < INT16_MIN || p->delta > INT16_MAX) {
			/* If the delta exceeds 16-bit, a new
			 feedback message must be used, where the 24-bit base
			 receive delta can cover very large gaps. */
			tmr_delay = 0;
			break;
		}

		if (p->ts && (p->delta < 0 || p->delta > 255))
			p->state = PACKET_LARGE_DELAY;

		if (++pkt_count > TWCC_MAX_PAKETS) {
			tmr_delay = 0;
			break;
		}

		list_move(&p->le, &twccst->status);

		if (twccst->equal_state)
			twccst->equal_state = p->state == twccst->last_state;
		twccst->last_state = p->state;
		if (p->ts)
			twccst->last_ts = p->ts;

		debug("  RTCP TWCC -> %u %u state:%i equal:%d delta:%d\n",
		      p->tseq, pkt_count, p->state, twccst->equal_state,
		      p->delta);

		if (le) {
			struct twcc_packet *p_next = mem_pool_member(le->data);
			handle_chunks(twccst, p->state != p_next->state);
		}
		else {

			handle_chunks(twccst, true);
		}
	}

	while (twccst->status.head) {
		uint16_t chunk = handle_run_chunk(twccst);
		mbuf_write_u16(twccst->msg.chunks, htons(chunk));
	}

	mbuf_set_pos(twccst->msg.chunks, 0);
	mbuf_set_pos(twccst->msg.deltas, 0);

	/* Send RTCP */
	if (!twccst->stream)
		goto out;

	uint32_t ssrc_media;
	int err = stream_ssrc_rx(twccst->stream, &ssrc_media);
	if (err)
		goto out;

	err = rtcp_send_twcc(stream_rtp_sock(twccst->stream), ssrc_media,
			     &twccst->msg);
	if (err) {
		debug("rtcp_send_twcc: error %m\n", err);
	}

out:
	tmr_start(&twccst->tmr, tmr_delay, send_feedback, twccst);
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
	mem_deref(twccst->msg.chunks);
	mem_deref(twccst->msg.deltas);
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

	twccst->msg.chunks = mbuf_alloc(TWCC_PKT_SIZE);
	if (!twccst->msg.chunks) {
		err = ENOMEM;
		goto out;
	}

	twccst->msg.deltas = mbuf_alloc(TWCC_PKT_SIZE);
	if (!twccst->msg.deltas) {
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
		debug("twcc: sdp_extmap_decode error (%m)\n", err);
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


void twcc_status_append(struct twcc_status *twccst, uint16_t tseq, uint64_t ts)
{
	if (!twccst)
		return;

	/* already late - and reported */
	if (seq_less(tseq, twccst->last_tseq)) {
		debug("twcc_status_append: already late %u < %u\n", tseq,
			twccst->last_tseq);
		return;
	}

	struct mem_pool_entry *e = mem_pool_borrow_extend(twccst->pool);

	struct twcc_packet *p = mem_pool_member(e);
	if (!p) {
		debug("twcc_status_append: no mem pool member\n");
		return;
	}

	p->tseq	 = tseq;
	p->state = PACKET_RECEIVED;
	p->ts	 = ts;

	int16_t seq_diff = p->tseq - twccst->last_tseq;
	while (seq_diff-- > 1) {
		struct mem_pool_entry *ef =
			mem_pool_borrow_extend(twccst->pool);

		struct twcc_packet *pf = mem_pool_member(ef);
		if (!pf)
			return;
		pf->state = PACKET_NOT_RECEIVED;
		pf->tseq  = p->tseq - seq_diff;
		pf->ts	  = 0;
		mtx_lock(twccst->mtx);
		list_append(&twccst->packets, &pf->le, ef);
		mtx_unlock(twccst->mtx);
	}

	twccst->last_tseq = p->tseq;

	mtx_lock(twccst->mtx);
	list_append(&twccst->packets, &p->le, e);
	mtx_unlock(twccst->mtx);
}


struct twcc *twcc_status_msg(struct twcc_status *twccst)
{
	return twccst ? &twccst->msg : NULL;
}
