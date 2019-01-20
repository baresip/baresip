/**
 * @file audiounit.h  AudioUnit sound driver -- Internal interface
 *
 * Copyright (C) 2010 Creytiv.com
 */


AudioComponent audiounit_comp_io;
AudioComponent audiounit_comp_conv;


struct audiosess;
struct audiosess_st;
struct conv_buf;


typedef void (audiosess_int_h)(bool start, void *arg);

int  audiosess_alloc(struct audiosess_st **stp,
		     audiosess_int_h *inth, void *arg);
void audiosess_interrupt(bool interrupted);


int conv_buf_alloc(struct conv_buf **bufp, size_t framesz);
int  get_nb_frames(struct conv_buf *buf, uint32_t *nb_frames);
OSStatus init_data_write(struct conv_buf *buf, void **data,
			 size_t framesz, uint32_t nb_frames);
OSStatus init_data_read(struct conv_buf *buf, void **data,
			size_t framesz, uint32_t nb_frames);


int audiounit_player_alloc(struct auplay_st **stp, const struct auplay *ap,
			   struct auplay_prm *prm, const char *device,
			   auplay_write_h *wh, void *arg);
int audiounit_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			     struct media_ctx **ctx,
			     struct ausrc_prm *prm, const char *device,
			     ausrc_read_h *rh, ausrc_error_h *errh, void *arg);


uint32_t audiounit_aufmt_to_formatflags(enum aufmt fmt);
