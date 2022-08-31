/**
 * @file metric.c  Metrics for media transmit/receive
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <re.h>
#include <baresip.h>
#include "core.h"

/*
 * Metric
 */

struct metric {
	/* internal stuff: */
	struct tmr tmr;
	mtx_t lock;
	uint64_t ts_start;
	bool started;

	/* counters: */
	uint32_t n_packets;
	uint32_t n_bytes;
	uint32_t n_err;

	/* bitrate calculation */
	uint32_t cur_bitrate;
	uint64_t ts_last;
	uint32_t n_bytes_last;
};

enum {TMR_INTERVAL = 3};
static void tmr_handler(void *arg)
{
	struct metric *metric = arg;
	const uint64_t now = tmr_jiffies();
	uint32_t diff;

	tmr_start(&metric->tmr, TMR_INTERVAL * 1000, tmr_handler, metric);

	mtx_lock(&metric->lock);

	if (!metric->started)
		goto out;

	if (now <= metric->ts_last)
		goto out;

	if (metric->ts_last) {
		uint32_t bytes = metric->n_bytes - metric->n_bytes_last;
		diff = (uint32_t)(now - metric->ts_last);
		metric->cur_bitrate = 1000 * 8 * bytes / diff;
	}

	/* Update counters */
	metric->ts_last = now;
	metric->n_bytes_last = metric->n_bytes;

out:
	mtx_unlock(&metric->lock);
}


static void metric_start(struct metric *metric)
{
	if (metric->started)
		return;

	metric->ts_start = tmr_jiffies();

	metric->started = true;
}


int metric_init(struct metric *metric)
{
	int err;

	if (!metric)
		return EINVAL;

	err = mtx_init(&metric->lock, mtx_plain) != thrd_success;
	if (err)
		return ENOMEM;

	tmr_start(&metric->tmr, 100, tmr_handler, metric);

	return 0;
}


void metric_reset(struct metric *metric)
{
	if (!metric)
		return;

	tmr_cancel(&metric->tmr);
	mtx_destroy(&metric->lock);
}


static void destructor(void *arg)
{
	metric_reset(arg);
}


struct metric *metric_alloc(void)
{
	struct metric *m;

	m = mem_zalloc(sizeof(*m), destructor);
	return m;
}


/*
 * NOTE: may be called from any thread
 */
void metric_add_packet(struct metric *metric, size_t packetsize)
{
	if (!metric)
		return;

	mtx_lock(&metric->lock);

	if (!metric->started)
		metric_start(metric);

	metric->n_bytes += (uint32_t)packetsize;
	metric->n_packets++;

	mtx_unlock(&metric->lock);
}


double metric_avg_bitrate(const struct metric *metric)
{
	int diff;

	if (!metric || !metric->ts_start)
		return 0;

	diff = (int)(tmr_jiffies() - metric->ts_start);

	return 1000.0 * 8 * (double)metric->n_bytes / (double)diff;
}


uint32_t metric_n_packets(struct metric *metric)
{
	uint32_t n;
	if (!metric)
		return 0;

	mtx_lock(&metric->lock);
	n = metric->n_packets;
	mtx_unlock(&metric->lock);
	return n;
}


uint32_t metric_n_bytes(struct metric *metric)
{
	uint32_t n;
	if (!metric)
		return 0;

	mtx_lock(&metric->lock);
	n = metric->n_bytes;
	mtx_unlock(&metric->lock);
	return n;
}


uint32_t metric_n_err(struct metric *metric)
{
	uint32_t n;
	if (!metric)
		return 0;

	mtx_lock(&metric->lock);
	n = metric->n_err;
	mtx_unlock(&metric->lock);
	return n;
}


uint32_t metric_bitrate(struct metric *metric)
{
	uint32_t n;
	if (!metric)
		return 0;

	mtx_lock(&metric->lock);
	n = metric->cur_bitrate;
	mtx_unlock(&metric->lock);
	return n;
}


void     metric_inc_err(struct metric *metric)
{
	if (!metric)
		return;

	mtx_lock(&metric->lock);
	++metric->n_err;
	mtx_unlock(&metric->lock);
}
