/**
 * @file twcc.c Transport-wide Congestion Control (TWCC) Receiver Status
 *
 * https://datatracker.ietf.org/doc/html/
 * draft-holmer-rmcat-transport-wide-cc-extensions-01
 *
 * Copyright (C) 2026 Sebastian Reimers
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


enum {
	TWCC_INTERVAL = 100,	/**< ms                                */
	TWCC_PKT_SIZE = 1280,	/**< max. Packet size in bytes         */
	TWCC_HDR_SIZE = 20,	/**< RTCP FB + TWCC header in bytes    */
	TWCC_RUN_MAX  = 0x1fff, /**< max. Run Length (13 bits)         */
};

static const char uri[] = "http://www.ietf.org/id/"
			  "draft-holmer-rmcat-transport-wide-cc-extensions-01";

static const char rtcp_fb[] = "* transport-cc";

struct twcc_status {
	mtx_t *mtx;
	struct stream *stream;
	struct list packets;
	struct list status;
	size_t status_cnt; /**< Number of pending status entries  */
	size_t status_sz;  /**< Delta bytes of pending entries    */
	struct mem_pool *pool;
	struct tmr tmr;
	bool started;
	uint32_t fbtotal; /**< Number of feedbacks sent          */
	uint16_t last_tseq;
	enum twcc_packet_state max_state;
	enum twcc_packet_state last_state;
	bool equal_state;
	uint64_t last_ts;
	struct twcc msg;
};


static inline size_t delta_size(enum twcc_packet_state state)
{
	switch (state) {
	case TWCC_PK_RECEIVED:
		return 1;
	case TWCC_PK_LARGE_DELTA:
		return 2;
	default:
		return 0;
	}
}


static inline void append_delay(struct twcc_status *twccst,
				struct rtcp_twcc_packet *p)
{
	switch (p->state) {
	case TWCC_PK_RECEIVED:
		mbuf_write_u8(twccst->msg.deltas, p->delta);
		break;
	case TWCC_PK_LARGE_DELTA:
		mbuf_write_u16(twccst->msg.deltas, htons(p->delta));
		break;
	default:
		break;
	}
}


static void reset_status(struct twcc_status *twccst)
{
	twccst->equal_state = true;
	twccst->max_state   = TWCC_PK_NOT_RECEIVED;
	twccst->status_cnt  = 0;
	twccst->status_sz   = 0;
}


static uint16_t handle_run_chunk(struct twcc_status *twccst)
{
	struct le *le		     = list_head(&twccst->status);
	struct mem_pool_entry *e     = le->data;
	struct rtcp_twcc_packet *p   = mem_pool_member(e);
	enum twcc_packet_state state = p->state;

	int cnt = 0;
	while (le && cnt < TWCC_RUN_MAX) {
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


static void flush_runs(struct twcc_status *twccst)
{
	/* Write all pending status entries as Run Length Chunks */
	while (twccst->status.head) {
		uint16_t chunk = handle_run_chunk(twccst);
		if (!chunk)
			break;

		mbuf_write_u16(twccst->msg.chunks, htons(chunk));
	}

	reset_status(twccst);
}


static void handle_vector_chunk(struct twcc_status *twccst)
{
	size_t cnt = twccst->status_cnt;
	unsigned bits;
	uint16_t chunk;

	if (twccst->equal_state)
		return;

	if (twccst->max_state <= TWCC_PK_RECEIVED && cnt == 14) {
		/* Add Status Vector Chunk - 14 x 1 bit */
		chunk = 0x8000;
		bits  = 1;
	}
	else if (twccst->max_state >= TWCC_PK_LARGE_DELTA && cnt == 7) {
		/* Add Status Vector Chunk - 7 x 2 bits */
		chunk = 0xc000;
		bits  = 2;
	}
	else {
		return;
	}

	struct le *le = list_head(&twccst->status);
	while (le) {
		struct mem_pool_entry *e   = le->data;
		struct rtcp_twcc_packet *p = mem_pool_member(e);

		le = le->next;

		chunk |= p->state << (--cnt * bits);

		append_delay(twccst, p);
		list_unlink(&p->le);
		mem_pool_release(twccst->pool, e);
	}

	mbuf_write_u16(twccst->msg.chunks, htons(chunk));

	reset_status(twccst);
}


static void send_feedback(void *arg)
{
	struct twcc_status *twccst = arg;

	uint64_t tmr_delay = TWCC_INTERVAL;

	mtx_lock(twccst->mtx);
	if (!twccst->packets.head)
		goto out;

	struct le *le = twccst->packets.head;

	struct mem_pool_entry *e   = le->data;
	struct rtcp_twcc_packet *p = mem_pool_member(e);

	twccst->msg.seq = p->tseq;
	twccst->msg.fbcount += 1;
	twccst->msg.count = 0;

	mbuf_rewind(twccst->msg.chunks);
	mbuf_rewind(twccst->msg.deltas);

	/* The reference time is based on the first received packet,
	   lost packets have no timestamp */
	for (struct le *lef = le; lef; lef = lef->next) {
		p = mem_pool_member(lef->data);
		if (p->state == TWCC_PK_NOT_RECEIVED)
			continue;

		twccst->last_ts = p->ts;
		break;
	}

	twccst->msg.reftime = (uint32_t)(twccst->last_ts / 64);
	twccst->last_ts	    = (uint64_t)twccst->msg.reftime * 64;

	reset_status(twccst);

	while (le) {
		enum twcc_packet_state state;
		int64_t delta = 0;

		e = le->data;
		p = mem_pool_member(e);
		if (!p)
			break;

		state = p->state;

		/* The sequence numbers of a feedback must be contiguous */
		if (twccst->msg.count &&
		    p->tseq !=
			    (uint16_t)(twccst->msg.seq + twccst->msg.count)) {
			tmr_delay = 0;
			break;
		}

		if ((TWCC_HDR_SIZE + twccst->msg.chunks->pos +
		     twccst->msg.deltas->pos + twccst->status_sz +
		     delta_size(TWCC_PK_LARGE_DELTA) + sizeof(uint16_t)) >=
			    TWCC_PKT_SIZE ||
		    twccst->msg.count == UINT16_MAX) {
			tmr_delay = 0;
			break;
		}

		if (state != TWCC_PK_NOT_RECEIVED) {
			delta = ((int64_t)p->ts - (int64_t)twccst->last_ts) *
				1000 / 250;
			if (delta < INT16_MIN || delta > INT16_MAX) {
				/* If the delta exceeds 16-bit, a new
				 feedback message must be used, where the
				 24-bit base receive delta can cover very
				 large gaps. */
				tmr_delay = 0;
				break;
			}

			state = (delta < 0 || delta > UINT8_MAX)
					? TWCC_PK_LARGE_DELTA
					: TWCC_PK_RECEIVED;
		}

		/* Pending entries that can not share a Status Vector Chunk
		   with this packet are written as Run Length Chunks */
		if (twccst->status_cnt >= 7 &&
		    ((twccst->equal_state && state != twccst->last_state) ||
		     (state == TWCC_PK_LARGE_DELTA &&
		      twccst->max_state < TWCC_PK_LARGE_DELTA)))
			flush_runs(twccst);

		p->state = state;
		p->delta = (int32_t)delta;

		if (state > twccst->max_state)
			twccst->max_state = state;

		twccst->equal_state =
			!twccst->status_cnt ||
			(twccst->equal_state && state == twccst->last_state);

		twccst->last_state = state;
		if (state != TWCC_PK_NOT_RECEIVED)
			twccst->last_ts = p->ts;

		le = le->next;

		twccst->msg.count++;
		list_move(&p->le, &twccst->status);
		twccst->status_cnt++;
		twccst->status_sz += delta_size(state);

		handle_vector_chunk(twccst);
	}

	flush_runs(twccst);

	mbuf_set_pos(twccst->msg.chunks, 0);
	mbuf_set_pos(twccst->msg.deltas, 0);

	if (!twccst->msg.count) {
		tmr_delay = TWCC_INTERVAL;
		goto out;
	}

	/* Send RTCP */
	if (!twccst->stream)
		goto out;

	uint32_t ssrc_media;
	int err = stream_ssrc_rx(twccst->stream, &ssrc_media);
	if (err)
		goto out;

	err = rtcp_send_twcc(stream_rtp_sock(twccst->stream), ssrc_media,
			     &twccst->msg);
	if (err)
		debug("rtcp_send_twcc: error %m\n", err);
	else
		++twccst->fbtotal;

out:
	tmr_start(&twccst->tmr, tmr_delay, send_feedback, twccst);
	mtx_unlock(twccst->mtx);
}


void twcc_status_send_feedback(struct twcc_status *twccst)
{
	send_feedback(twccst);
}


/**
 * Detach a stream that is about to be destroyed
 *
 * @param twccst TWCC Status object
 * @param stream Stream object
 */
void twcc_status_detach(struct twcc_status *twccst,
			const struct stream *stream)
{
	if (!twccst)
		return;

	mtx_lock(twccst->mtx);
	if (twccst->stream == stream)
		twccst->stream = NULL;
	mtx_unlock(twccst->mtx);
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

	err = mem_pool_alloc(&twccst->pool, 100,
			     sizeof(struct rtcp_twcc_packet), NULL);
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

	return err;
}


static bool extmap_handler(const char *name, const char *value, void *arg)
{
	struct sdp_extmap extmap;
	uint8_t *id = arg;
	int err;
	(void)name;

	err = sdp_extmap_decode(&extmap, value);
	if (err) {
		debug("twcc: sdp_extmap_decode error (%m)\n", err);
		return false;
	}

	if (0 != pl_strcasecmp(&extmap.name, uri))
		return false;

	if (id)
		*id = (uint8_t)extmap.id;

	return true;
}


static bool rtcp_fb_handler(const char *name, const char *value, void *arg)
{
	(void)name;
	(void)arg;

	return 0 == str_casecmp(value, rtcp_fb);
}


/**
 * Offer Transport-wide Congestion Control for a stream
 *
 * @param strm Stream object
 *
 * @return 0 if success, otherwise errorcode
 */
int twcc_status_offer(struct stream *strm)
{
	struct sdp_media *m = stream_sdpmedia(strm);
	uint8_t id;
	int err;

	if (!strm)
		return EINVAL;

	id = stream_generate_extmap_id(strm);
	if (!id)
		return ERANGE;

	err = sdp_media_set_lattr(m, false, "extmap", "%u %s", id, uri);
	if (err)
		return err;

	return sdp_media_set_lattr(m, false, "rtcp-fb", "%s", rtcp_fb);
}


void twcc_status_handle_extmap(struct stream *strm)
{
	struct sdp_media *m = stream_sdpmedia(strm);
	uint8_t id = 0;
	int err;

	if (!sdp_media_rattr_apply(m, "extmap", extmap_handler, &id)) {
		stream_set_extmap_twcc(strm, 0);
		return;
	}

	/* NOTE: other extmap attributes (e.g. BUNDLE mid) must be kept */
	if (!sdp_media_lattr_apply(m, "extmap", extmap_handler, NULL)) {
		err = sdp_media_set_lattr(m, false, "extmap", "%u %s", id,
					  uri);
		if (err)
			return;
	}

	if (!sdp_media_lattr_apply(m, "rtcp-fb", rtcp_fb_handler, NULL)) {
		err = sdp_media_set_lattr(m, false, "rtcp-fb", "%s", rtcp_fb);
		if (err)
			return;
	}

	stream_set_extmap_twcc(strm, id);
}


/* A reordered packet is still received, if it was not reported yet */
static bool late_packet(struct twcc_status *twccst, uint16_t tseq, uint64_t ts)
{
	for (struct le *le = twccst->packets.tail; le; le = le->prev) {
		struct rtcp_twcc_packet *p = mem_pool_member(le->data);

		if (rtp_seq_less(p->tseq, tseq))
			break;

		if (p->tseq != tseq)
			continue;

		if (p->state != TWCC_PK_NOT_RECEIVED)
			break;

		p->state = TWCC_PK_RECEIVED;
		p->ts	 = ts;

		return true;
	}

	return false;
}


void twcc_status_append(struct twcc_status *twccst, uint16_t tseq, uint64_t ts)
{
	uint16_t gap = 0;

	if (!twccst)
		return;

	mtx_lock(twccst->mtx);

	if (twccst->started) {
		if (tseq == twccst->last_tseq ||
		    rtp_seq_less(tseq, twccst->last_tseq)) {
			if (!late_packet(twccst, tseq, ts)) {
				/* duplicate or already reported as lost */
				debug("twcc_status_append: duplicate or late "
				      "%u <= %u\n",
				      tseq, twccst->last_tseq);
			}
			goto out;
		}

		gap = tseq - twccst->last_tseq - 1;
		if (gap > TWCC_RUN_MAX) {
			debug("twcc_status_append: skip large gap %u\n", gap);
			gap = 0;
		}
	}

	for (; gap > 0; gap--) {
		struct mem_pool_entry *ef =
			mem_pool_borrow_extend(twccst->pool);

		struct rtcp_twcc_packet *pf = mem_pool_member(ef);
		if (!pf) {
			debug("twcc_status_append: no mem pool member\n");
			goto out;
		}

		pf->state = TWCC_PK_NOT_RECEIVED;
		pf->tseq  = tseq - gap;
		pf->ts	  = 0;
		list_append(&twccst->packets, &pf->le, ef);

		twccst->last_tseq = pf->tseq;
	}

	struct mem_pool_entry *e = mem_pool_borrow_extend(twccst->pool);

	struct rtcp_twcc_packet *p = mem_pool_member(e);
	if (!p) {
		debug("twcc_status_append: no mem pool member\n");
		goto out;
	}

	p->tseq	 = tseq;
	p->state = TWCC_PK_RECEIVED;
	p->ts	 = ts;
	list_append(&twccst->packets, &p->le, e);

	twccst->last_tseq = tseq;
	twccst->started	  = true;

out:
	mtx_unlock(twccst->mtx);
}


struct twcc *twcc_status_msg(struct twcc_status *twccst)
{
	return twccst ? &twccst->msg : NULL;
}


int twcc_status_debug(struct re_printf *pf, struct twcc_status *twccst)
{
	int err;

	if (!twccst)
		return 0;

	mtx_lock(twccst->mtx);
	err = re_hprintf(pf,
			 " twcc: feedbacks=%u last_seq=%u last_count=%u"
			 " pending=%u\n",
			 twccst->fbtotal, twccst->msg.seq, twccst->msg.count,
			 list_count(&twccst->packets));
	mtx_unlock(twccst->mtx);

	return err;
}
