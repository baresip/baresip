/**
 * @file aptx.c aptX Audio Codec
 *
 * Copyright (C) 2019 Hessischer Rundfunk
 */

#include <re.h>
#include <baresip.h>
#include "aptx.h"

/**
 * @defgroup aptx aptx
 *
 * aptX audio codec (Standard and HD variant)
 *
 * Supported version:
 *    libopenaptx 0.1.0 or later
 *
 * References:
 *    RFC 7310  RTP Payload Format for Standard apt-X
 *              and Enhanced apt-X Codecs
 *
 * TODOs:
 *    - Code check, cleanup and error handling
 *    - Add SDP fmtp negotiation & config preconfiguration
 *    - Check and implement other sampling rates and channel modes
 *    - Add optional real 24 bit audio I/O support
 *
 */

static struct aucodec aptx = {
	.name = "aptx",
	.srate = APTX_SRATE,
	.crate = APTX_SRATE,
	.ch = APTX_CHANNELS,
	.pch = APTX_CHANNELS,
	.ptime = 4,
	.encupdh = aptx_encode_update,
	.ench = aptx_encode_frm,
	.decupdh = aptx_decode_update,
	.dech = aptx_decode_frm,
	.fmtp_ench = aptx_fmtp_enc,
	.fmtp_cmph = aptx_fmtp_cmp,
};


static int module_init(void)
{
	aucodec_register(baresip_aucodecl(), &aptx);

	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&aptx);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aptx) = {
	"aptx",
	"audio codec",
	module_init,
	module_close,
};
