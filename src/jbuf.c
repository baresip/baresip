/**
 * @file jbuf.c  Jitter Buffer implementation
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <stdint.h>
#include <re.h>
#include <baresip.h>

#include <stdlib.h>

#define DEBUG_MODULE "jbuf"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


#ifndef RELEASE
#define JBUF_STAT 1  /**< Jitter buffer statistics */
#endif


#if JBUF_STAT
#define STAT_ADD(var, value)  (jb->stat.var) += (value) /**< Stats add */
#define STAT_INC(var)         ++(jb->stat.var)          /**< Stats inc */
#else
#define STAT_ADD(var, value)
#define STAT_INC(var)
#endif

enum {
	JBUF_PUT_TIMEOUT     = 400,
	JBUF_EMA_FAC         = 1024,
	JBUF_EMA_COEFF       = 128,
	JBUF_UP_SPEED        = 64
};


/** Defines a packet */
struct packet {
	struct le le;           /**< Linked list element       */
	struct rtp_header hdr;  /**< RTP Header                */
	void *mem;              /**< Reference counted pointer */
};


/**
 * Defines a jitter buffer
 *
 * The jitter buffer is for incoming RTP packets
 *
 * Features:
 * - re-order out-of-order packets
 * - ensure frame completeness
 * - specify min/max number of frames
 */
struct jbuf {
	struct list pooll;   /**< List of free packets in pool               */
	struct list packetl; /**< List of buffered packets                   */
	uint32_t n;          /**< [# packets] Current # of packets in buffer */
	uint32_t nf;         /**< [# frames] Current # of frames in buffer   */
	uint32_t ncf;        /**< [# frames] # of complete frames in buffer  */
	uint32_t min;        /**< [# frames] Minimum # of frames to buffer   */
	uint32_t max;        /**< [# frames] Maximum # of frames to buffer   */
	uint32_t packets;    /**< [# packets] Size of the packet pool        */
	uint16_t seq_put;    /**< Sequence number for last jbuf_put()        */
	uint16_t seq_get;    /**< Sequence number of last played packet      */
	struct le *end;      /**< End of complete sequence                   */
	uint32_t ssrc;       /**< Previous ssrc                              */
	uint64_t tr;         /**< Time of previous jbuf_put()                */
	int pt;              /**< Payload type                               */
	bool running;        /**< Jitter buffer is running                   */
	uint32_t wish;       /**< [# frames] Wish ncf after underrun         */
	bool newframe;       /**< Flag for pass one frame                    */
	int32_t nfa;         /**< Moving average nf - ncf + min              */

	mtx_t *lock;         /**< Makes jitter buffer thread safe            */
	enum jbuf_type jbtype;  /**< Jitter buffer type                      */
#if JBUF_STAT
	struct jbuf_stat stat; /**< Jitter buffer Statistics                 */
#endif
#ifdef RE_JBUF_TRACE
	uint64_t tr00;       /**< Arrival of first packet                    */
	char buf[136];       /**< Buffer for trace                           */
#endif
};


/**
 * Update frame counter and sequence end list element pointer before oldest
 * packet is removed
 *
 * @param jb  Jitter buffer
 */
static void jbuf_update_nf(struct jbuf *jb)
{
	struct le *le = jb->packetl.head;

	if (!le)
		return;

	struct le *n = le->next;

	if (n) {
		struct packet *p  = le->data;
		struct packet *pn = n->data;

		if (p->hdr.ts != pn->hdr.ts) {
			if (jb->nf)
				--jb->nf;

			if (jb->ncf)
				--jb->ncf;
		}
	}
	else if (!le->prev) {
		jb->nf = 0;
	}

	if (jb->end == le) {
		jb->end = NULL;
		jb->ncf = 0;
	}
}


/** Is x less than y? */
static inline bool seq_less(uint16_t x, uint16_t y)
{
	return ((int16_t)(x - y)) < 0;
}


#ifdef RE_JBUF_TRACE
static void plot_jbuf(struct jbuf *jb, uint64_t tr)
{
	uint32_t treal;

	if (!jb->tr00)
		jb->tr00 = tr;

	treal = (uint32_t) (tr - jb->tr00);
	re_snprintf(jb->buf, sizeof(jb->buf),
		    "%s, 0x%p, %u, %u, %u, %u, %f",
			__func__,               /* row 1  - grep */
			jb,                     /* row 2  - grep optional */
			treal,                  /* row 3  - plot x-axis */
			jb->n,                  /* row 4  - plot */
			jb->nf,                 /* row 5  - plot */
			jb->ncf,                /* row 6  - plot */
			((float)jb->nfa)/JBUF_EMA_FAC);    /* row 7  - plot */
	re_trace_event("jbuf", "plot", 'P', NULL, 0, RE_TRACE_ARG_STRING_COPY,
		       "line", jb->buf);
}


static void plot_jbuf_event(struct jbuf *jb, char ph)
{
	uint32_t treal;
	uint64_t tr;

	tr = tmr_jiffies();
	if (!jb->tr00)
		jb->tr00 = tr;

	treal = (uint32_t) (tr - jb->tr00);
	re_snprintf(jb->buf, sizeof(jb->buf), "%s, 0x%p, %u, %i",
			__func__,               /* row 1  - grep */
			jb,                     /* row 2  - grep optional */
			treal,                  /* row 3  - plot x-axis */
			1);                     /* row 4  - plot */
	re_trace_event("jbuf", "plot", ph, NULL, 0, RE_TRACE_ARG_STRING_COPY,
		       "line", jb->buf);
}
#else
static void plot_jbuf_event(struct jbuf *jb, char ph)
{
	(void)jb;
	(void)ph;
}
#endif


/**
 * Get a packet from the pool
 */
static void packet_alloc(struct jbuf *jb, struct packet **pp)
{
	struct le *le;

	le = jb->pooll.head;
	if (le) {
		list_unlink(le);
		++jb->n;
	}
	else {
		struct packet *p0;

		/* Steal an old packet */
		le = jb->packetl.head;
		p0 = le->data;
		jbuf_update_nf(jb);

#if JBUF_STAT
		STAT_INC(n_overflow);
		DEBUG_WARNING("drop 1 old packet seq=%u (total dropped %u)\n",
			   p0->hdr.seq, jb->stat.n_overflow);
#else
		DEBUG_WARNING("drop 1 old packet seq=%u\n", p0->hdr.seq);
#endif

		plot_jbuf_event(jb, 'O');
		p0->mem = mem_deref(p0->mem);
		list_unlink(le);
	}

	*pp = le->data;
}


/**
 * Release a packet, put it back in the pool
 */
static void packet_deref(struct jbuf *jb, struct packet *p)
{
	p->mem = mem_deref(p->mem);
	list_unlink(&p->le);
	list_append(&jb->pooll, &p->le, p);
	--jb->n;
}


static void jbuf_destructor(void *data)
{
	struct jbuf *jb = data;

	jbuf_flush(jb);

	/* Free all packets in the pool list */
	list_flush(&jb->pooll);
	mem_deref(jb->lock);
}


/**
 * Allocate a new jitter buffer
 *
 * @param jbp    Pointer to returned jitter buffer
 * @param min    Minimum delay in [frames]
 * @param max    Maximum delay in [frames]
 *
 * @return 0 if success, otherwise errorcode
 */
int jbuf_alloc(struct jbuf **jbp, uint32_t min, uint32_t max)
{
	struct jbuf *jb;
	int err = 0;

	if (!jbp || ( min > max))
		return EINVAL;

	/* self-test: x < y (also handle wrap around) */
	if (!seq_less(10, 20) || seq_less(20, 10) || !seq_less(65535, 0)) {
		DEBUG_WARNING("seq_less() is broken\n");
		return ENOSYS;
	}

	jb = mem_zalloc(sizeof(*jb), NULL);
	if (!jb)
		return ENOMEM;

	list_init(&jb->pooll);
	list_init(&jb->packetl);

	jb->jbtype = JBUF_FIXED;
	jb->min  = min;
	jb->max  = max;
	jb->nfa  = 3*JBUF_EMA_FAC;

	DEBUG_PRINTF("alloc: delay=%u-%u [frames]\n", min, max);

	jb->pt = -1;
	err = mutex_alloc(&jb->lock);
	if (err)
		goto out;

	mem_destructor(jb, jbuf_destructor);

	/* Allocate all packets now */
	err = jbuf_resize(jb, max + 1);

out:
	if (err)
		mem_deref(jb);
	else
		*jbp = jb;

	return err;
}


/**
 * Resize the packet pool
 *
 * @param jb      The jitter buffer
 * @param packets Size of the packet pool in [packets]. Default: jb->max
 *
 * @return 0 if success, otherwise errorcode
 */
int  jbuf_resize(struct jbuf *jb, uint32_t packets)
{
	if (!jb)
		return EINVAL;

	if (packets <= jb->packets)
		return 0;

	for (uint32_t i=jb->packets; i<packets; i++) {
		struct packet *p = mem_zalloc(sizeof(*p), NULL);
		if (!p)
			return ENOMEM;

		list_append(&jb->pooll, &p->le, p);
		DEBUG_INFO("alloc: adding to pool list %u\n", i);
	}

	jb->packets = packets;
	return 0;
}


/**
 * Set jitter buffer type.
 *
 * @param jb      The jitter buffer.
 * @param jbtype  The jitter buffer type.
 *
 * @return 0 if success, otherwise errorcode
 */
int jbuf_set_type(struct jbuf *jb, enum jbuf_type jbtype)
{
	if (!jb)
		return EINVAL;

	jb->jbtype = jbtype;

	return 0;
}


/**
 * Moves end of complete sequence and counts complete frames. If `end` is NULL,
 * a search starts from the head which points to the oldest packet
 *
 * @param jb   Jitter buffer
 * @param cur  Current inserted list element
 */
static void jbuf_move_end(struct jbuf *jb, struct le *cur)
{
	struct le *end;

	if (!jb->end) {
		cur = jb->packetl.head;
		if (!cur)
			return;

		struct packet *pm = cur->data;
		if (!jb->seq_get || jb->seq_get + 1 == pm->hdr.seq) {
			jb->end = cur;
			cur = cur->next;
		}

		if (!cur)
			return;
	}

	end = cur->prev;
	if (!end)
		return;

	/* update only if missing packet was inserted right now */
	if (jb->end != end)
		return;

	for (; end->next; end = end->next) {
		struct packet *pm = end->data;
		struct packet *pn = end->next->data;

		if (pm->hdr.seq + 1 != pn->hdr.seq)
			break;

		if (pm->hdr.ts != pn->hdr.ts)
			++jb->ncf;

		if (jb->ncf > jb->nf)
			DEBUG_WARNING("ncf > nf\n");
	}

	jb->end = end;
}


static bool jbuf_frame_ready(struct jbuf *jb)
{
	if (!jb->packetl.head)
		return false;

	if (jb->nf < jb->min)
		return false;

	if (jb->nf > jb->max)
		return true;

	if (!jb->end)
		return false;

	return jb->ncf > jb->wish;
}


/**
 * Put one packet into the jitter buffer
 *
 * @param jb   Jitter buffer
 * @param hdr  RTP Header
 * @param mem  Memory pointer - will be referenced
 *
 * @return 0 if success, otherwise errorcode
 */
int jbuf_put(struct jbuf *jb, const struct rtp_header *hdr, void *mem)
{
	struct packet *p;
	struct le *le, *tail;
	uint16_t seq;
	uint64_t tr, dt;
	int err = 0;

	if (!jb || !hdr)
		return EINVAL;

	seq = hdr->seq;
	if (jb->pt == -1)
		jb->pt = hdr->pt;

	if (jb->ssrc && jb->ssrc != hdr->ssrc) {
		DEBUG_INFO("ssrc changed %u %u\n", jb->ssrc, hdr->ssrc);
		jbuf_flush(jb);
	}

	tr = tmr_jiffies();
	dt = tr - jb->tr;
	if (jb->tr && dt > JBUF_PUT_TIMEOUT) {
		DEBUG_INFO("put timeout %lu ms, marker %d\n", dt, hdr->m);
		if (hdr->m)
			jbuf_flush(jb);
	}

	jb->tr = tr;

	mtx_lock(jb->lock);
	jb->ssrc = hdr->ssrc;

	if (jb->running) {
		/* Packet arrived too late to be put into buffer */
		if (jb->seq_get && seq_less(seq, jb->seq_get + 1)) {
			STAT_INC(n_late);
			plot_jbuf_event(jb, 'L');
			DEBUG_INFO("packet too late: seq=%u "
				   "(seq_put=%u seq_get=%u)\n",
				   seq, jb->seq_put, jb->seq_get);
			err = ETIMEDOUT;
			goto out;
		}

	}

	STAT_INC(n_put);

	packet_alloc(jb, &p);

	tail = jb->packetl.tail;

	/* If buffer is empty -> append to tail
	   Frame is later than tail -> append to tail
	*/
	if (!tail || seq_less(((struct packet *)tail->data)->hdr.seq, seq)) {
		list_append(&jb->packetl, &p->le, p);
		goto success;
	}

	/* Out-of-sequence, find right position */
	for (le = tail; le; le = le->prev) {
		const uint16_t seq_le = ((struct packet *)le->data)->hdr.seq;

		if (seq_less(seq_le, seq)) { /* most likely */
			DEBUG_PRINTF("put: out-of-sequence"
				   " - inserting after seq=%u (seq=%u)\n",
				   seq_le, seq);
			list_insert_after(&jb->packetl, le, &p->le, p);
			break;
		}
		else if (seq == seq_le) { /* less likely */
			/* Detect duplicates */
			DEBUG_INFO("duplicate: seq=%u\n", seq);
			STAT_INC(n_dups);
			plot_jbuf_event(jb, 'D');
			list_insert_after(&jb->packetl, le, &p->le, p);
			packet_deref(jb, p);
			err = EALREADY;
			goto out;
		}

		/* sequence number less than current seq, continue */
	}

	/* no earlier sequence found, put in head */
	if (!le) {
		DEBUG_PRINTF("put: out-of-sequence"
			   " - put in head (seq=%u)\n", seq);
		list_prepend(&jb->packetl, &p->le, p);
	}

	STAT_INC(n_oos);
	plot_jbuf_event(jb, 'S');

success:
	/* Update last sequence */
	jb->running = true;
	jb->seq_put = seq;

	/* Success */
	p->hdr = *hdr;
	p->mem = mem_ref(mem);
	uint32_t nf = jb->nf;
	if (p->le.prev || p->le.next) {
		struct packet *pprev = NULL;
		struct packet *pnext = NULL;

		if (p->le.prev)
			pprev = p->le.prev->data;

		if (p->le.next)
			pnext = p->le.next->data;

		if ((!pprev || pprev->hdr.ts != hdr->ts) &&
		    (!pnext || pnext->hdr.ts != hdr->ts)) {
			++jb->nf;
		}
	}
	else {
		jb->nf = 1;
	}


	/* check frame completeness */
	jbuf_move_end(jb, &p->le);

	if (jb->nf > nf)
		jb->newframe = true;

	if (jb->ncf >= jb->wish)
		jb->wish = 0;

out:
	mtx_unlock(jb->lock);
	return err;
}


/**
 * Get one packet from the jitter buffer
 *
 * @param jb   Jitter buffer
 * @param hdr  Returned RTP Header
 * @param mem  Pointer to memory object storage - referenced on success
 *
 * @return 0 if success, EAGAIN if it should be called again, otherwise
 * errorcode
 */
int jbuf_get(struct jbuf *jb, struct rtp_header *hdr, void **mem)
{
	struct packet *p;
	int err = 0;

	if (!jb || !hdr || !mem)
		return EINVAL;

	mtx_lock(jb->lock);
	STAT_INC(n_get);

	if (!jbuf_frame_ready(jb) || !jb->newframe) {
		if (jb->newframe) {
			DEBUG_INFO("no frame ready - wait.. "
			   "(nf=%u ncf=%u min=%u nfa=%d wish=%u)\n",
			   jb->nf, jb->ncf, jb->min, jb->nfa, jb->wish);
			STAT_INC(n_underrun);
			jb->wish = jb->nf > 2 ? jb->nf : 2;
			int32_t nfa = (int32_t) (jb->nf + 1) * JBUF_EMA_FAC;
			if (nfa > jb->nfa)
				jb->nfa  = nfa;

			plot_jbuf_event(jb, 'U');
		}

		err = ENOENT;
		goto out;
	}

	p = jb->packetl.head->data;
#if JBUF_STAT
	/* Check sequence of previously played packet */
	if (jb->seq_get) {
		const int16_t seq_diff = p->hdr.seq - jb->seq_get;
		if (seq_less(p->hdr.seq, jb->seq_get)) {
			DEBUG_WARNING("get: seq=%u too late\n", p->hdr.seq);
		}
		else if (seq_diff > 1) {
			STAT_ADD(n_lost, 1);
			plot_jbuf_event(jb, 'T');
			DEBUG_INFO("get: n_lost: diff=%d,seq=%u,seq_get=%u\n",
				   seq_diff, p->hdr.seq, jb->seq_get);
		}
	}
#endif

	/* Update sequence number for 'get' */
	jb->seq_get = p->hdr.seq;

	*hdr = p->hdr;
	*mem = mem_ref(p->mem);

	jbuf_update_nf(jb);
	packet_deref(jb, p);
	jb->newframe = false;

	int32_t nfa = ((int32_t) jb->nf - jb->ncf + jb->min) * JBUF_EMA_FAC;
	int32_t s = nfa > jb->nfa ? JBUF_UP_SPEED : 1;

	jb->nfa += (nfa - jb->nfa) * s / JBUF_EMA_COEFF;
	if (jb->nfa < (int32_t) jb->min)
		jb->nfa = (int32_t) jb->min;

	if (jbuf_frame_ready(jb)) {
		p = jb->packetl.head->data;
		if (p->hdr.ts == hdr->ts || jb->nf > jb->max) {
			err = EAGAIN;
			jb->newframe = true;
		}
		else if (!jb->wish &&
			 (uint32_t) jb->nfa <= jb->nf*JBUF_EMA_FAC) {
			jb->nfa += JBUF_EMA_FAC;
			jb->newframe = true;
			err = EAGAIN;
		}
	}

out:
#ifdef RE_JBUF_TRACE
	plot_jbuf(jb, jb->tr);
#endif
	mtx_unlock(jb->lock);
	return err;
}


/**
 * Get one packet from the jitter buffer, even if it becomes depleted
 *
 * @param jb   Jitter buffer
 * @param hdr  Returned RTP Header
 * @param mem  Pointer to memory object storage - referenced on success
 *
 * @return 0 if success, otherwise errorcode
 */
int jbuf_drain(struct jbuf *jb, struct rtp_header *hdr, void **mem)
{
	struct packet *p;
	int err = 0;

	if (!jb || !hdr || !mem)
		return EINVAL;

	mtx_lock(jb->lock);

	if (jb->n <= 0 || !jb->packetl.head) {
		err = ENOENT;
		goto out;
	}

	p = jb->packetl.head->data;

	/* Update sequence number for 'get' */
	jb->seq_get = p->hdr.seq;

	*hdr = p->hdr;
	*mem = mem_ref(p->mem);

	jbuf_update_nf(jb);
	packet_deref(jb, p);

out:
	mtx_unlock(jb->lock);
	return err;
}

/**
 * Flush all packets in the jitter buffer
 *
 * @param jb   Jitter buffer
 */
void jbuf_flush(struct jbuf *jb)
{
	struct le *le;
#if JBUF_STAT
	uint32_t n_flush;
#endif

	if (!jb)
		return;

	mtx_lock(jb->lock);
	if (jb->packetl.head) {
		DEBUG_INFO("flush: %u packets\n", jb->n);
	}

	/* put all buffered packets back in free list */
	for (le = jb->packetl.head; le; le = jb->packetl.head) {
		DEBUG_PRINTF(" flush packet: seq=%u\n",
			   ((struct packet *)(le->data))->hdr.seq);

		packet_deref(jb, le->data);
	}

	jb->n       = 0;
	jb->nf      = 0;
	jb->ncf     = 0;
	jb->end     = NULL;
	jb->running = false;

	jb->seq_get = 0;
#if JBUF_STAT
	n_flush = STAT_INC(n_flush);
	memset(&jb->stat, 0, sizeof(jb->stat));
	jb->stat.n_flush = n_flush;
	plot_jbuf_event(jb, 'F');
#endif
	mtx_unlock(jb->lock);
}


/**
 * Get number of current packets
 *
 * @param jb Jitter buffer
 *
 * @return number of packets
 */
uint32_t jbuf_packets(const struct jbuf *jb)
{
	if (!jb)
		return 0;

	mtx_lock(jb->lock);
	uint32_t n = jb->n;
	mtx_unlock(jb->lock);

	return n;
}


/**
 * Get number of current frames
 *
 * @param jb Jitter buffer
 *
 * @return number of frames
 */
uint32_t jbuf_frames(const struct jbuf *jb)
{
       if (!jb)
               return 0;

       mtx_lock(jb->lock);
       uint32_t n = jb->nf;
       mtx_unlock(jb->lock);

       return n;
}


/**
 * Get number of complete frames
 *
 * @param jb Jitter buffer
 *
 * @return number of frames
 */
uint32_t jbuf_complete_frames(const struct jbuf *jb)
{
       if (!jb)
               return 0;

       mtx_lock(jb->lock);
       uint32_t n = jb->ncf;
       mtx_unlock(jb->lock);

       return n;
}


/**
 * Get jitter buffer statistics
 *
 * @param jb    Jitter buffer
 * @param jstat Pointer to statistics storage
 *
 * @return 0 if success, otherwise errorcode
 */
int jbuf_stats(const struct jbuf *jb, struct jbuf_stat *jstat)
{
	if (!jb || !jstat)
		return EINVAL;

#if JBUF_STAT
	mtx_lock(jb->lock);
	*jstat = jb->stat;
	mtx_unlock(jb->lock);

	return 0;
#else
	return ENOSYS;
#endif
}


/**
 * Debug the jitter buffer. This function is thread safe with short blocking
 *
 * @param pf Print handler
 * @param jb Jitter buffer
 *
 * @return 0 if success, otherwise errorcode
 */
int jbuf_debug(struct re_printf *pf, const struct jbuf *jb)
{
	int err = 0;

	if (!jb)
		return 0;

	struct mbuf *mb = mbuf_alloc(512);
	if (!mb)
		return ENOMEM;

	err |= mbuf_printf(mb, "--- jitter buffer debug---\n");

	mtx_lock(jb->lock);
	err |= mbuf_printf(mb, " running=%d", jb->running);
	err |= mbuf_printf(mb, " min=%u cur=%u complete=%u max=%u [frames]\n",
			   jb->min, jb->nf, jb->ncf, jb->max);
	err |= mbuf_printf(mb, " seq_put=%u\n", jb->seq_put);

#if JBUF_STAT
	err |= mbuf_printf(mb, " Stat: put=%u", jb->stat.n_put);
	err |= mbuf_printf(mb, " get=%u", jb->stat.n_get);
	err |= mbuf_printf(mb, " oos=%u", jb->stat.n_oos);
	err |= mbuf_printf(mb, " dup=%u", jb->stat.n_dups);
	err |= mbuf_printf(mb, " late=%u", jb->stat.n_late);
	err |= mbuf_printf(mb, " or=%u", jb->stat.n_overflow);
	err |= mbuf_printf(mb, " underrun=%u", jb->stat.n_underrun);
	err |= mbuf_printf(mb, " flush=%u", jb->stat.n_flush);
	err |= mbuf_printf(mb, "       put/get_ratio=%u%%", jb->stat.n_get ?
			  100*jb->stat.n_put/jb->stat.n_get : 0);
	err |= mbuf_printf(mb, " lost=%u (%u.%02u%%)\n",
			  jb->stat.n_lost,
			  jb->stat.n_put ?
			  100*jb->stat.n_lost/jb->stat.n_put : 0,
			  jb->stat.n_put ?
			  10000*jb->stat.n_lost/jb->stat.n_put%100 : 0);
#endif
	mtx_unlock(jb->lock);

	if (err)
		goto out;

	err = re_hprintf(pf, "%b", mb->buf, mb->pos);

out:
	mem_deref(mb);
	return err;
}
