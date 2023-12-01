/**
 * @file pulse.h  Pulseaudio sound driver (asynchronous API)
 *
 * Copyright (C) 2021 Commend.com - h.ramoser@commend.com
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
	bool shutdown;

	pa_stream *stream;
	pa_sample_spec ss;
	pa_buffer_attr attr;
	pa_stream_direction_t direction;

	struct {
		size_t overrun;
		size_t underrun;
	} stats;
};


/*player.c*/
int pulse_player_init(struct auplay *ap);
int pulse_player_alloc(struct auplay_st **stp, const struct auplay *ap,
	struct auplay_prm *prm, const char *device,
	auplay_write_h *wh, void *arg);
void stream_write_cb(pa_stream *s, size_t len, void *arg);


/*recorder.c*/
int pulse_recorder_init(struct ausrc *as);
int pulse_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
	struct ausrc_prm *prm, const char *dev, ausrc_read_h *rh,
	ausrc_error_h *errh, void *arg);
void stream_read_cb(pa_stream *s, size_t len, void *arg);


/*pulse.c*/
struct paconn_st *paconn_get(void);
int pulse_set_available_devices(struct list *dev_list,
	pa_operation *(get_dev_info_cb)(pa_context *, struct list*));


/*pastream.c*/
int pastream_alloc(struct pastream_st **bptr, const char *dev,
	const char *pname, const char *sname, pa_stream_direction_t dir,
	uint32_t srate, uint8_t ch, uint32_t ptime, int fmt);
int pastream_start(struct pastream_st *st, void *arg);


