/**
 * @file amr/amr.h AMR module -- internal interface
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */

struct amr_aucodec {
	struct aucodec ac;
	bool aligned;
	uint8_t *be_dec_arr;
};

bool amr_octet_align(const char *fmtp);
int  amr_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		  bool offer, void *arg);
