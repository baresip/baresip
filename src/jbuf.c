/**
 * @file jbuf.c  Jitter Buffer implementation
 *
 * This is an adaptive jitter buffer implementation. See doc/jbuf for further
 * details!
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
	JBUF_RDIFF_EMA_COEFF = 1024,
	JBUF_RDIFF_UP_SPEED  = 512,
	JBUF_PUT_TIMEOUT     = 400,
};


/** Defines a packet frame */
struct packet {
	struct le le;           /**< Linked list element       */
	struct rtp_header hdr;  /**< RTP Header                */
	void *mem;              /**< Reference counted pointer */
};


/**
 * Defines a jitter buffer
 *
 * The jitter buffer is for incoming RTP packets, which are sorted by
 * sequence number.
 */
struct jbuf {
	struct rtp_sock *gnack_rtp; /**< Generic NACK RTP Socket             */
	struct list pooll;   /**< List of free packets in pool               */
	struct list packetl; /**< List of buffered packets                   */
	uint32_t n;          /**< [# packets] Current # of packets in buffer */
	uint32_t nf;         /**< [# frames] Current # of frames in buffer   */
	uint32_t min;        /**< [# frames] Minimum # of frames to buffer   */
	uint32_t max;        /**< [# frames] Maximum # of frames to buffer   */
	uint32_t wish;       /**< [# frames] Wish size for adaptive mode     */
	uint16_t seq_put;    /**< Sequence number for last jbuf_put()        */
	uint16_t seq_get;    /**< Sequence number of last played frame       */
	uint32_t ssrc;       /**< Previous ssrc                              */
	uint64_t tr;         /**< Time of previous jbuf_put()                */
	int pt;              /**< Payload type                               */
	bool running;        /**< Jitter buffer is running                   */
	int32_t rdiff;       /**< Average out of order reverse diff          */
	struct tmr tmr;      /**< Rdiff down timer                           */

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


/** Is x less than y? */
static inline bool seq_less(uint16_t x, uint16_t y)
{
	return ((int16_t)(x - y)) < 0;
}


#ifdef RE_JBUF_TRACE
static void plot_jbuf(struct jbuf *jb, uint64_t tr)
{
	uint32_t treal;
	uint32_t rdiff = (uint32_t)(jb->rdiff / (float)JBUF_RDIFF_EMA_COEFF);

	if (!jb->tr00)
		jb->tr00 = tr;

	treal = (uint32_t) (tr - jb->tr00);
	re_snprintf(jb->buf, sizeof(jb->buf),
		    "%s, 0x%p, %u, %u, %u, %u, %u",
			__func__,               /* row 1  - grep */
			jb,                     /* row 2  - grep optional */
			treal,                  /* row 3  - plot x-axis */
			rdiff,                  /* row 4  - plot */
			jb->wish,               /* row 5  - plot */
			jb->n,                  /* row 6  - plot */
			jb->nf);                /* row 7  - plot */
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
 * Get a frame from the pool
 */
static void packet_alloc(struct jbuf *jb, struct packet **f)
{
	struct le *le;

	le = jb->pooll.head;
	if (le) {
		list_unlink(le);
		++jb->n;
	}
	else {
		struct packet *f0;

		/* Steal an old frame */
		le = jb->packetl.head;
		f0 = le->data;

#if JBUF_STAT
		STAT_INC(n_overflow);
		DEBUG_WARNING("drop 1 old frame seq=%u (total dropped %u)\n",
			   f0->hdr.seq, jb->stat.n_overflow);
#else
		DEBUG_WARNING("drop 1 old frame seq=%u\n", f0->hdr.seq);
#endif

		if (le->next) {
			struct packet *f1 = list_ledata(le->next);
			if (f1->hdr.ts != f0->hdr.ts)
				--jb->nf;
		}

		plot_jbuf_event(jb, 'O');
		f0->mem = mem_deref(f0->mem);
		list_unlink(le);
	}

	*f = le->data;
}


/**
 * Release a packet, put it back in the pool
 */
static void packet_deref(struct jbuf *jb, struct packet *f)
{
	f->mem = mem_deref(f->mem);
	list_unlink(&f->le);
	list_append(&jb->pooll, &f->le, f);
	--jb->n;
}


static void jbuf_destructor(void *data)
{
	struct jbuf *jb = data;

	tmr_cancel(&jb->tmr);
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
 * @param max    Maximum delay in [packets]
 *
 * @return 0 if success, otherwise errorcode
 */
int jbuf_alloc(struct jbuf **jbp, uint32_t min, uint32_t max)
{
	struct jbuf *jb;
	uint32_t i;
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
	jb->wish = min;
	tmr_init(&jb->tmr);

	DEBUG_INFO("alloc: delay=%u-%u frames/packets\n", min, max);

	jb->pt = -1;
	err = mutex_alloc(&jb->lock);
	if (err)
		goto out;

	mem_destructor(jb, jbuf_destructor);

	/* Allocate all packets now */
	for (i=0; i<jb->max; i++) {
		struct packet *f = mem_zalloc(sizeof(*f), NULL);
		if (!f) {
			err = ENOMEM;
			break;
		}

		list_append(&jb->pooll, &f->le, f);
		DEBUG_INFO("alloc: adding to pool list %u\n", i);
	}

out:
	if (err)
		mem_deref(jb);
	else
		*jbp = jb;

	return err;
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
 * Set rtp socket for RTCP Generic NACK handling
 *
 * @param jb   The jitter buffer.
 * @param rtp  RTP Socket
 */
void jbuf_set_gnack(struct jbuf *jb, struct rtp_sock *rtp)
{
       if (!jb)
               return;

       mtx_lock(jb->lock);
       jb->gnack_rtp = rtp;
       mtx_unlock(jb->lock);
}


static void wish_down(void *arg)
{
	struct jbuf *jb = arg;

	if (jb->wish > jb->min) {
		DEBUG_INFO("wish size changed %u --> %u\n", jb->wish,
			   jb->wish - 1);
		--jb->wish;
	}
}


static void calc_rdiff(struct jbuf *jb, uint16_t seq)
{
	int32_t rdiff;
	int32_t adiff;
	int32_t s;                         /**< EMA coefficient              */
	float ratio = 1.0;                 /**< Frame packet ratio           */
	uint32_t wish;
	uint32_t max = jb->max;
	bool down = false;

	if (jb->jbtype != JBUF_ADAPTIVE)
		return;

	if (!jb->seq_get)
		return;

	if (jb->nf) {
		ratio = (float)jb->n / (float)jb->nf;
		max   = (uint32_t)(max / ratio);
	}

	rdiff = (int16_t)(jb->seq_put + 1 - seq);
	adiff = abs(rdiff * JBUF_RDIFF_EMA_COEFF);
	s = adiff > jb->rdiff ? JBUF_RDIFF_UP_SPEED :
		jb->wish > 2  ? 1 :
		jb->wish > 1  ? 2 : 3;
	jb->rdiff += (adiff - jb->rdiff) * s / JBUF_RDIFF_EMA_COEFF;

	wish = (uint32_t)(jb->rdiff / (float)JBUF_RDIFF_EMA_COEFF / ratio);
	if (wish < jb->min)
		wish = jb->min;

	if (max && wish >= max)
		wish = max - 1;

	if (wish > jb->wish) {
		DEBUG_INFO("wish size changed %u --> %u\n", jb->wish, wish);
		jb->wish = wish;
	}
	else if (wish < jb->wish) {
		uint32_t dt = wish + 1 == jb->wish ? 6000 : 1000;
		if (!tmr_isrunning(&jb->tmr) || tmr_get_expire(&jb->tmr) > dt)
			tmr_start(&jb->tmr, dt, wish_down, jb);

		down = true;
	}

	if (!down && tmr_isrunning(&jb->tmr))
		tmr_cancel(&jb->tmr);
}


static inline void send_gnack(struct jbuf *jb, uint16_t last_seq,
			     int16_t seq_diff)
{
	uint16_t pid = last_seq + 1;
	uint16_t blp = 0;

	for (int i = 0; i < seq_diff - 2; i++) {
		blp |= (1 << i);
	}

	debug("jbuf: RTCP_GNACK missing: %u diff: %d blp: %02X\n", pid,
		seq_diff, blp);

	rtcp_send_gnack(jb->gnack_rtp, jb->ssrc, pid, blp);
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
	struct packet *f;
	struct packet *fc;
	struct le *le, *tail;
	uint16_t seq;
	uint64_t tr, dt;
	bool equal;
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

		if (jb->jbtype == JBUF_ADAPTIVE)
			calc_rdiff(jb, seq);

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

	packet_alloc(jb, &f);

	tail = jb->packetl.tail;

	/* If buffer is empty -> append to tail */
	if (!tail) {
		list_append(&jb->packetl, &f->le, f);
		goto success;
	}

	uint16_t last_seq = ((struct packet *)tail->data)->hdr.seq;

	/* Frame is later than tail -> append to tail */
	if (seq_less(last_seq, seq)) {
		const int16_t seq_diff = seq - last_seq;

		if (jb->gnack_rtp && seq_diff > 1)
			send_gnack(jb, last_seq, seq_diff);

		list_append(&jb->packetl, &f->le, f);
		goto success;
	}

	/* Out-of-sequence, find right position */
	for (le = tail; le; le = le->prev) {
		const uint16_t seq_le = ((struct packet *)le->data)->hdr.seq;

		if (seq_less(seq_le, seq)) { /* most likely */
			DEBUG_PRINTF("put: out-of-sequence"
				   " - inserting after seq=%u (seq=%u)\n",
				   seq_le, seq);
			list_insert_after(&jb->packetl, le, &f->le, f);
			break;
		}
		else if (seq == seq_le) { /* less likely */
			/* Detect duplicates */
			DEBUG_INFO("duplicate: seq=%u\n", seq);
			STAT_INC(n_dups);
			plot_jbuf_event(jb, 'D');
			list_insert_after(&jb->packetl, le, &f->le, f);
			packet_deref(jb, f);
			err = EALREADY;
			goto out;
		}

		/* sequence number less than current seq, continue */
	}

	/* no earlier sequence found, put in head */
	if (!le) {
		DEBUG_PRINTF("put: out-of-sequence"
			   " - put in head (seq=%u)\n", seq);
		list_prepend(&jb->packetl, &f->le, f);
	}

	STAT_INC(n_oos);
	plot_jbuf_event(jb, 'S');

success:
	/* Update last sequence */
	jb->running = true;
	jb->seq_put = seq;

	/* Success */
	f->hdr = *hdr;
	f->mem = mem_ref(mem);

	equal = false;
	if (f->le.prev) {
		fc = f->le.prev->data;
		equal = (fc->hdr.ts == f->hdr.ts);
	}

	if (!equal && f->le.next) {
		fc = f->le.next->data;
		equal = (fc->hdr.ts == f->hdr.ts);
	}

	if (!equal)
		++jb->nf;

out:
#ifdef RE_JBUF_TRACE
	plot_jbuf(jb, tr);
#endif
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
 * @return 0 if success, EAGAIN if it should be called again in order to avoid
 * a jitter buffer overflow, otherwise errorcode
 */
int jbuf_get(struct jbuf *jb, struct rtp_header *hdr, void **mem)
{
	struct packet *f;
	int err = 0;

	if (!jb || !hdr || !mem)
		return EINVAL;

	mtx_lock(jb->lock);
	STAT_INC(n_get);

	if (jb->nf <= jb->wish || !jb->packetl.head) {
		DEBUG_INFO("not enough buffer packets - wait.. "
			   "(n=%u wish=%u)\n", jb->n, jb->wish);
		STAT_INC(n_underflow);
		plot_jbuf_event(jb, 'U');
		err = ENOENT;
		goto out;
	}

	/* When we get one packet P[i], check that the next packet P[i+1]
	   is present and have a seq no. of seq[i] + 1.
	   If not, we should consider that packet lost. */

	f = jb->packetl.head->data;

#if JBUF_STAT
	/* Check sequence of previously played packet */
	if (jb->seq_get) {
		const int16_t seq_diff = f->hdr.seq - jb->seq_get;
		if (seq_less(f->hdr.seq, jb->seq_get)) {
			DEBUG_WARNING("get: seq=%u too late\n", f->hdr.seq);
		}
		else if (seq_diff > 1) {
			STAT_ADD(n_lost, 1);
			plot_jbuf_event(jb, 'T');
			DEBUG_INFO("get: n_lost: diff=%d,seq=%u,seq_get=%u\n",
				   seq_diff, f->hdr.seq, jb->seq_get);
		}
	}
#endif

	/* Update sequence number for 'get' */
	jb->seq_get = f->hdr.seq;

	*hdr = f->hdr;
	*mem = mem_ref(f->mem);

	/* decrease not equal frames */
	if (f->le.next) {
		struct packet *next_f = f->le.next->data;

		if (f->hdr.ts != next_f->hdr.ts)
			--jb->nf;
	}
	else {
		--jb->nf;
	}

	packet_deref(jb, f);

	if (jb->nf > jb->wish) {
		DEBUG_INFO("reducing jitter buffer "
			   "(nf=%u min=%u wish=%u max=%u)\n",
			   jb->nf, jb->min, jb->wish, jb->max);
		err = EAGAIN;
	}

out:
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
	struct packet *f;
	int err = 0;

	if (!jb || !hdr || !mem)
		return EINVAL;

	mtx_lock(jb->lock);

	if (jb->n <= 0 || !jb->packetl.head) {
		err = ENOENT;
		goto out;
	}

	/* When we get one packet P[i], check that the next packet P[i+1]
	   is present and have a seq no. of seq[i] + 1.
	   If not, we should consider that packet lost. */

	f = jb->packetl.head->data;

	/* Update sequence number for 'get' */
	jb->seq_get = f->hdr.seq;

	*hdr = f->hdr;
	*mem = mem_ref(f->mem);

	/* decrease not equal frames */
	if (f->le.next) {
		struct packet *next_f = f->le.next->data;

		if (f->hdr.ts != next_f->hdr.ts)
			--jb->nf;
	}
	else {
		--jb->nf;
	}

	packet_deref(jb, f);

out:
	mtx_unlock(jb->lock);
	return err;
}

/**
 * Flush all frames in the jitter buffer
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
		DEBUG_INFO("flush: %u frames\n", jb->n);
	}

	/* put all buffered frames back in free list */
	for (le = jb->packetl.head; le; le = jb->packetl.head) {
		DEBUG_INFO(" flush frame: seq=%u\n",
			   ((struct packet *)(le->data))->hdr.seq);

		packet_deref(jb, le->data);
	}

	jb->n       = 0;
	jb->nf      = 0;
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
	err |= mbuf_printf(mb, " min=%u cur=%u/%u max=%u [frames/packets]\n",
			  jb->min, jb->nf, jb->n, jb->max);
	err |= mbuf_printf(mb, " seq_put=%u\n", jb->seq_put);

#if JBUF_STAT
	err |= mbuf_printf(mb, " Stat: put=%u", jb->stat.n_put);
	err |= mbuf_printf(mb, " get=%u", jb->stat.n_get);
	err |= mbuf_printf(mb, " oos=%u", jb->stat.n_oos);
	err |= mbuf_printf(mb, " dup=%u", jb->stat.n_dups);
	err |= mbuf_printf(mb, " late=%u", jb->stat.n_late);
	err |= mbuf_printf(mb, " or=%u", jb->stat.n_overflow);
	err |= mbuf_printf(mb, " ur=%u", jb->stat.n_underflow);
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
