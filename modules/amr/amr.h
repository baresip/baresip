/**
 * @file amr/amr.h AMR module -- internal interface
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */


int  amr_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		  bool offer, void *arg);
bool amr_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg);
