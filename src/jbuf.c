/**
 * @file jbuf.c  Jitter Buffer implementation
 *
 * This is an adaptive jitter buffer implementation. See doc/jbuf for further
 * details!
 *
 * Copyright (C) 2010 Creytiv.com
 */

#undef RE_TRACE_ENABLED
#if JBUF_TRACE
#define RE_TRACE_ENABLED 1
#endif

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
	JBUF_PUT_TIMEOUT     = 10000,
	JBUF_LATE_TRESHOLD   = 3,
	JBUF_SPIKE_END       = 100
};

enum {
	JBUF_MODE_NORMAL,
	JBUF_MODE_SPIKE
};

#define SKEW_THRESHOLD 100 /* @TODO: needs dynamic frame time calculation */
#define SPIKE_THRESHOLD 50 /* [ms] */

/** Defines a packet frame */
struct packet {
	struct le le;           /**< Linked list element       */
	uint32_t playout_time;  /**< Playout time              */
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
	struct pl *id;
	struct rtp_sock *gnack_rtp; /**< Generic NACK RTP Socket             */
	struct list pooll;   /**< List of free packets in pool               */
	struct list packetl; /**< List of buffered packets                   */
	uint32_t n;          /**< [# packets] Current # of packets in buffer */
	uint32_t min;        /**< [# frames] Minimum # of frames to buffer   */
	uint32_t max;        /**< [# frames] Maximum # of frames to buffer   */
	uint32_t wish;       /**< [# frames] Wish size for adaptive mode     */
	uint16_t seq_put;    /**< Sequence number for last jbuf_put()        */
	uint16_t seq_get;    /**< Sequence number of last played frame       */
	uint32_t ssrc;       /**< Previous ssrc                              */
	struct {
		uint32_t offset;          /**< Offset                        */
		uint32_t jitter_offset;   /**< Offset                        */
		uint32_t delay_estimate;
		uint32_t active_delay;
		int mode;
		uint32_t last_transit;
		uint32_t last_last_transit;
		uint32_t spike;
		uint32_t jitter;
		uint16_t late_pkts;
		uint32_t srate;
		uint32_t last_delay;
	} p;                 /**< Playout specific values                    */
	uint64_t tr;         /**< Time of previous jbuf_put()                */
	int pt;              /**< Payload type                               */
	bool running;        /**< Jitter buffer is running                   */
	int32_t rdiff;       /**< Average out of order reverse diff          */

	mtx_t *lock;         /**< Makes jitter buffer thread safe            */
	enum jbuf_type jbtype;  /**< Jitter buffer type                      */
#if JBUF_STAT
	struct jbuf_stat stat; /**< Jitter buffer Statistics                 */
#endif
};


/** Calculate delay in ms from clock rate */
static inline int32_t delay_ms(uint32_t delay_clock, uint32_t srate)
{
	return (delay_clock * 1000) / srate;
}


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

		RE_TRACE_ID_INSTANT("jbuf", "overflow", jb->id);
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

	jbuf_flush(jb);

	/* Free all packets in the pool list */
	list_flush(&jb->pooll);
	mem_deref(jb->lock);
	mem_deref(jb->id);
}


/**
 * Allocate a new jitter buffer
 *
 * @param jbp    Pointer to returned jitter buffer
 * @param min    Minimum delay in [ms]
 * @param max    Maximum packets
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
	if (!rtp_seq_less(10, 20) || rtp_seq_less(20, 10) ||
	    !rtp_seq_less(65535, 0)) {
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

	DEBUG_INFO("alloc: delay=%u-%u packets\n", min, max);

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


void jbuf_set_srate(struct jbuf *jb, uint32_t srate)
{
	if (!jb)
		return;

	jb->p.srate = srate;
}


void jbuf_set_id(struct jbuf *jb, struct pl *id)
{
	if (!jb)
		return;

	jb->id = mem_ref(id);
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


static uint32_t adjust_due_to_jitter(struct jbuf *jb, struct packet *p)
{
	bool adapt = false;
	const int transit = (uint32_t)p->hdr.ts_arrive - p->hdr.ts;

	/* Check for first frame */
	if (!jb->p.last_transit) {
		jb->p.last_transit = transit;
		return 0;
	}

	int d = transit - jb->p.last_transit;
	if (d < 0)
		d = -d;

	jb->p.last_delay = d;

	jb->p.jitter += d - ((jb->p.jitter + 8) >> 4);

	if (delay_ms(d, jb->p.srate) > SPIKE_THRESHOLD) {
		jb->p.mode  = JBUF_MODE_SPIKE;
		jb->p.spike = 0;

		adapt = false;
		goto out;
	}

	/* During a network spike the adaption should be
	 * disabled to avoid wrong jitter assumptions */
	if (jb->p.mode == JBUF_MODE_SPIKE) {
		/* Slope estimation */
		jb->p.spike = jb->p.spike / 2;

		uint32_t lastd = abs((int)(transit - jb->p.last_last_transit));
		uint32_t delta = (d + lastd) / 8;
		jb->p.spike += delta;

		DEBUG_WARNING("SPIKE %lu < %d\n", jb->p.spike, JBUF_SPIKE_END);

		if (delay_ms(jb->p.spike, jb->p.srate) < JBUF_SPIKE_END)
			jb->p.mode = JBUF_MODE_NORMAL;

		adapt = false;
	}
	else {
		/* Only fatal events should trigger adaption */
		if (jb->p.late_pkts >= JBUF_LATE_TRESHOLD) {
			jb->p.late_pkts = 0;

			adapt = true;
		}
	}

out:
	jb->p.last_last_transit = jb->p.last_transit;
	jb->p.last_transit	= transit;

	if (adapt) {
		jb->p.jitter_offset = 3 * (jb->p.jitter >> 4);
		RE_TRACE_ID_INSTANT_I("jbuf", "adjust_due_to_jitter",
				      jb->p.jitter_offset, jb->id);
	}

	return jb->p.jitter_offset;
}


static int32_t adjust_due_to_skew(struct jbuf *jb, struct packet *p)
{
	uint32_t delay = (uint32_t)p->hdr.ts_arrive - p->hdr.ts;

	if (!jb->p.active_delay) {
		jb->p.active_delay   = delay;
		jb->p.delay_estimate = delay;
		return 0;
	}
	else {
		jb->p.delay_estimate =
			(31LL * jb->p.delay_estimate + delay) / 32;
	}

	int32_t diff = jb->p.active_delay - jb->p.delay_estimate;

	RE_TRACE_ID_INSTANT_I("jbuf", "clock_skew", diff, jb->id);

	if (diff > SKEW_THRESHOLD) {
		jb->p.offset	   = 0; /* needs testing */
		jb->p.active_delay = jb->p.delay_estimate;
		return SKEW_THRESHOLD;
	}

	if (diff < -SKEW_THRESHOLD) {
		jb->p.offset	   = 0; /* needs testing */
		jb->p.active_delay = jb->p.delay_estimate;
		return -SKEW_THRESHOLD;
	}

	return 0;
}


static inline uint32_t offset_min(uint32_t a, uint32_t b)
{
	/* Works only if the difference between a and b is not greater than
	 * UINT32_MAX/2 */
	if ((a - b) & 0x80000000)
		return a; /* a < b */

	return b; /* b <= a */
}


static inline uint32_t offset(struct packet *p)
{
	return (uint32_t)p->hdr.ts_arrive - p->hdr.ts;
}


static uint32_t calc_playout_time(struct jbuf *jb, struct packet *p)
{
	uint32_t base_offset = 0;
	uint32_t play_time_base;

	/* Fragmented frames (like video) have equal playout_time.
	 * If a packet is missed here (late/reorder), playout time calculation
	 * should be fine too, since its based on same sender hdr.ts.
	 * This is also needed to prevent jitter miscalculations */
	if (p->le.prev) {
		struct packet *prevp = p->le.prev->data;
		if (prevp->hdr.ts == p->hdr.ts) {
			return prevp->playout_time;
		}
	}

	/* Compensating relative clock offset between sender and receiver */
	if (!jb->p.offset)
		jb->p.offset = offset(p);
	else
		jb->p.offset = offset_min(offset(p), jb->p.offset);

	/* Calculate base playout point */
	play_time_base = p->hdr.ts + jb->p.offset;

	if (jb->jbtype == JBUF_ADAPTIVE) {
		/* Add playout reference clock skew compensation */
		/* @TODO: trigger insert or remove frame */
		(void)adjust_due_to_skew(jb, p);

		/* Add Jitter compensation */
		base_offset += adjust_due_to_jitter(jb, p);
	}

	/* Check min/max latency requirements */
	/* @TODO: */
	base_offset += 960 * jb->min;

	RE_TRACE_ID_INSTANT_I("jbuf", "recv_delay",
			      delay_ms(jb->p.last_delay, jb->p.srate), jb->id);
	RE_TRACE_ID_INSTANT_I("jbuf", "play_delay",
			      delay_ms(base_offset, jb->p.srate), jb->id);

	return play_time_base + base_offset;
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
	struct le *le, *tail;
	uint16_t seq;
	uint64_t tr, dt;
	int err = 0;

	if (!jb || !hdr)
		return EINVAL;

	if (!jb->p.srate) {
		DEBUG_WARNING("no clock srate set!\n");
		return EINVAL;
	}

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
		if (jb->seq_get && rtp_seq_less(seq, jb->seq_get + 1)) {
			STAT_INC(n_late);
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
	if (rtp_seq_less(last_seq, seq)) {
		const int16_t seq_diff = seq - last_seq;

		if (jb->gnack_rtp && seq_diff > 1)
			send_gnack(jb, last_seq, seq_diff);

		list_append(&jb->packetl, &f->le, f);
		goto success;
	}

	/* Out-of-sequence, find right position */
	for (le = tail; le; le = le->prev) {
		const uint16_t seq_le = ((struct packet *)le->data)->hdr.seq;

		if (rtp_seq_less(seq_le, seq)) { /* most likely */
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
			RE_TRACE_ID_INSTANT("jbuf", "duplicate", jb->id);
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
	RE_TRACE_ID_INSTANT("jbuf", "out-of-sequence", jb->id);

success:
	/* Update last sequence */
	jb->running = true;
	jb->seq_put = seq;

	/* Success */
	f->hdr = *hdr;
	f->mem = mem_ref(mem);
	f->playout_time = calc_playout_time(jb, f);

	if (!f->playout_time)
		packet_deref(jb, f);

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
 * @return 0 if success, EAGAIN if it should be called again in order to avoid
 * a jitter buffer overflow, otherwise errorcode
 */
int jbuf_get(struct jbuf *jb, struct rtp_header *hdr, void **mem)
{
	struct packet *f;
	struct packet *nextp = NULL;

	int err = 0;

	if (!jb || !hdr || !mem)
		return EINVAL;

	mtx_lock(jb->lock);
	STAT_INC(n_get);

	RE_TRACE_ID_INSTANT_I("jbuf", "get", jb->n, jb->id);

	if (!jb->packetl.head) {
		jb->p.late_pkts++;
		RE_TRACE_ID_INSTANT("jbuf", "underrun", jb->id);
		err = ENOENT;
		goto out;
	}

	f = jb->packetl.head->data;

	uint32_t next_playout = (uint32_t)(tmr_jiffies() * jb->p.srate / 1000);

	/* Check playout time */
	if (f->playout_time > next_playout) {
		err = ENOENT;
		goto out;
	}

	*hdr = f->hdr;
	*mem = mem_ref(f->mem);

	if (f->le.next)
		nextp = f->le.next->data;

	packet_deref(jb, f);

	/* Check if next packet (maybe same frame) can also be played */
	if (nextp && nextp->playout_time <= next_playout)
		err = EAGAIN;

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

	//@TODO: reset playout specifc values

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
	jb->running = false;

	jb->seq_get = 0;
#if JBUF_STAT
	n_flush = STAT_INC(n_flush);
	memset(&jb->stat, 0, sizeof(jb->stat));
	jb->stat.n_flush = n_flush;
#endif
	RE_TRACE_ID_INSTANT("jbuf", "flush", jb->id);
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
 * Get jitter buffer next playout delay time
 *
 * @param jb Jitter buffer
 *
 * @return delay time in [ms] on success and -1 on error
 */
int32_t jbuf_next_play(const struct jbuf *jb)
{
	if (!jb || !jb->packetl.head)
		return -1;

	struct packet *p = jb->packetl.head->data;

	uint32_t current = (uint32_t)(tmr_jiffies() * jb->p.srate / 1000);

	if (p->playout_time <= current)
		return 0; /* already late */

	return delay_ms(p->playout_time - current, jb->p.srate);
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
	err |= mbuf_printf(mb, " min=%u cur=%u max=%u [packets]\n",
			  jb->min, jb->n, jb->max);
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
