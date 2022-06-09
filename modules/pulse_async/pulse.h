/**
 * @file player.c  Pulseaudio sound driver - player (asynchronous API)
 *
 * Copyright (C) 2021 Commend.com - h.ramoser@commend.com,
 *                                  c.spielberger@commend.com
 *                                  c.huber@commend.com
 */


/**
 * Pulseaudio connection struct
 *
 */
struct paconn_st {
	pa_threaded_mainloop *mainloop;
	pa_context *context;
};


/**
 * Pulseaudio stream struct
 *
 */
struct pastream_st {
	char pname[256];
	char device[256];
	char sname[256];

	struct ausrc_prm src_prm;
	struct auplay_prm play_prm;
	auplay_write_h *wh;
	ausrc_read_h *rh;

	bool shutdown;

	pa_stream *stream;
	pa_sample_spec ss;
	pa_buffer_attr attr;

	size_t sampsz;
	size_t sz;
	size_t sampc;
	size_t samps;
	void *sampv;

	pa_stream_direction_t direction;

	void *arg;
};


/*player.c*/
int pulse_async_player_init(struct auplay *ap);
int pulse_async_player_alloc(struct auplay_st **stp, const struct auplay *ap,
	struct auplay_prm *prm, const char *device,
	auplay_write_h *wh, void *arg);

/*recorder.c*/
int pulse_async_recorder_init(struct ausrc *as);
int pulse_async_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
	struct ausrc_prm *prm, const char *dev, ausrc_read_h *rh,
	ausrc_error_h *errh, void *arg);

/*pulse.c*/
struct paconn_st *paconn_get(void);

int pulse_async_set_available_devices(struct list *dev_list,
	pa_operation *(get_dev_info_cb)(pa_context *, struct list*));


/*pastream.c*/
int pastream_alloc(struct pastream_st **bptr, struct auplay_prm *prm,
	const char *dev, const char *pname, const char *sname,
	pa_stream_direction_t dir, void *arg);
int pastream_start(struct pastream_st *st);
void pastream_set_writehandler(struct pastream_st *st, auplay_write_h *wh);
void pastream_set_readhandler(struct pastream_st *st, ausrc_read_h *rh);

void stream_write_cb(pa_stream *s, size_t len, void *arg);
void stream_read_cb(pa_stream *s, size_t len, void *arg);
