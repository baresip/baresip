/**
 * @file rst.h MP3/ICY HTTP AV Source
 *
 * Copyright (C) 2011 Creytiv.com
 */


/* Shared AV state */
struct rst;

int  rst_alloc(struct rst **rstp, const char *dev);
void rst_set_audio(struct rst *rst, struct ausrc_st *st);
void rst_set_video(struct rst *rst, struct vidsrc_st *st);


/* Audio */
void rst_audio_feed(struct ausrc_st *st, const uint8_t *buf, size_t sz);
int  rst_audio_init(void);
void rst_audio_close(void);


/* Video */
void rst_video_update(struct vidsrc_st *st, const char *name,
		      const char *meta);
int  rst_video_init(void);
void rst_video_close(void);
