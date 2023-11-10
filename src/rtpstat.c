/**
 * @file rtpstat.c  RTP Statistics
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


/*
 * Reference:
 *
 * https://www.avm.de/de/Extern/files/x-rtp/xrtpv32.pdf
 */
int rtpstat_print(struct re_printf *pf, const struct call *call)
{
	struct audio *a = call_audio(call);
	const struct aucodec *ac_tx, *ac_rx;
	const struct rtcp_stats *rtcp;
	const struct stream *s;
	int srate_tx = 8000;
	int srate_rx = 8000;
	int err;

	if (!call || !a)
		return 0;

	s = audio_strm(a);
	rtcp = stream_rtcp_stats(s);

	if (!rtcp->tx.sent)
		return 1;

	ac_tx = audio_codec(a, true);
	ac_rx = audio_codec(a, false);

	if (ac_tx)
		srate_tx = ac_tx->srate;
	if (ac_rx)
		srate_rx = ac_rx->srate;

	err = re_hprintf(pf,
			 "EX=BareSip;"   /* Reporter Identifier	             */
			 "CS=%d;"        /* Call Setup in milliseconds       */
			 "CD=%d;"        /* Call Duration in seconds	     */
			 "PR=%u;PS=%u;"  /* Packets RX, TX                   */
			 "PL=%d,%d;"     /* Packets Lost RX, TX              */
			 "PD=%d,%d;"     /* Packets Discarded, RX, TX        */
			 "JI=%.1f,%.1f;" /* Jitter RX, TX in timestamp units */
			 "IP=%J,%J"      /* Local, Remote IPs                */
			 ,
			 call_setup_duration(call) * 1000,
			 call_duration(call),

			 stream_metric_get_rx_n_packets(s),
			 stream_metric_get_tx_n_packets(s),

			 rtcp->rx.lost, rtcp->tx.lost,

			 stream_metric_get_rx_n_err(s),
			 stream_metric_get_tx_n_err(s),

			 /* timestamp units (ie: 8 ts units = 1 ms @ 8KHZ) */
			 1.0 * rtcp->rx.jit/1000 * (srate_rx/1000),
			 1.0 * rtcp->tx.jit/1000 * (srate_tx/1000),

			 sdp_media_laddr(stream_sdpmedia(s)),
			 sdp_media_raddr(stream_sdpmedia(s))
			 );

	if (ac_tx)
		err |= re_hprintf(pf, ";EN=%s/%d", ac_tx->name, srate_tx);
	if (ac_rx)
		err |= re_hprintf(pf, ";DE=%s/%d", ac_rx->name, srate_rx);

	return err;
}
