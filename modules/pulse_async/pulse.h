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

	bool shutdown;

	pa_stream *stream;
	pa_sample_spec ss;
	pa_buffer_attr attr;
	pa_stream_direction_t direction;
};


/*player.c*/
int pulse_async_player_init(struct auplay *ap);


/*recorder.c*/
int pulse_async_recorder_init(struct ausrc *as);


/*pulse.c*/
struct paconn_st *paconn_get(void);

int pulse_async_set_available_devices(struct list *dev_list,
	pa_operation *(get_dev_info_cb)(pa_context *, struct list*));


// void stream_write_cb(pa_stream *s, size_t len, void *arg);
// void stream_read_cb(pa_stream *s, size_t len, void *arg);