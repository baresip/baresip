/**
 * @file mod_avformat.h  libavformat media-source -- internal interface
 *
 * Copyright (C) 2010 - 2020 Creytiv.com
 */


struct shared {
	const char *id;
	struct ausrc_st *ausrc_st;    /* pointer */
	struct vidsrc_st *vidsrc_st;  /* pointer */
	struct lock *lock;
	AVFormatContext *ic;
	pthread_t thread;
	bool is_realtime;
	bool run;

	struct stream {
		AVRational time_base;
		AVCodecContext *ctx;
		int idx;
	} au, vid;
};


int avformat_shared_alloc(struct shared **shp, const char *dev,
			  const double fps, const struct vidsz *size,
			  bool video);
void avformat_shared_set_audio(struct shared *sh, struct ausrc_st *st);
void avformat_shared_set_video(struct shared *sh, struct vidsrc_st *st);


int  avformat_audio_alloc(struct ausrc_st **stp, const struct ausrc *as,
			  struct media_ctx **ctx,
			  struct ausrc_prm *prm, const char *dev,
			  ausrc_read_h *readh, ausrc_error_h *errh, void *arg);
void avformat_audio_decode(struct shared *st, AVPacket *pkt);


int  avformat_video_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
			  struct media_ctx **ctx, struct vidsrc_prm *prm,
			  const struct vidsz *size, const char *fmt,
			  const char *dev, vidsrc_frame_h *frameh,
			  vidsrc_error_h *errorh, void *arg);
void avformat_video_decode(struct shared *st, AVPacket *pkt);
