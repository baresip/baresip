/**
 * @file metric.c  Metrics for media transmit/receive
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


enum {TMR_INTERVAL = 3};
static void tmr_handler(void *arg)
{
	struct metric *metric = arg;
	const uint64_t now = tmr_jiffies();
	uint32_t diff;

	tmr_start(&metric->tmr, TMR_INTERVAL * 1000, tmr_handler, metric);

	lock_write_get(metric->lock);

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
	lock_rel(metric->lock);
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

	err = lock_alloc(&metric->lock);
	if (err)
		return err;

	tmr_start(&metric->tmr, 100, tmr_handler, metric);

	return 0;
}


void metric_reset(struct metric *metric)
{
	if (!metric)
		return;

	tmr_cancel(&metric->tmr);
	metric->lock = mem_deref(metric->lock);
}


/*
 * NOTE: may be called from any thread
 */
void metric_add_packet(struct metric *metric, size_t packetsize)
{
	if (!metric)
		return;

	lock_write_get(metric->lock);

	if (!metric->started)
		metric_start(metric);

	metric->n_bytes += (uint32_t)packetsize;
	metric->n_packets++;

	lock_rel(metric->lock);
}


double metric_avg_bitrate(const struct metric *metric)
{
	int diff;

	if (!metric || !metric->ts_start)
		return 0;

	diff = (int)(tmr_jiffies() - metric->ts_start);

	return 1000.0 * 8 * (double)metric->n_bytes / (double)diff;
}
