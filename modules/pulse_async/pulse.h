/**
 * @file player.c  Pulseaudio sound driver - player (asynchronous API)
 *
 * Copyright (C) 2021 Commend.com - h.ramoser@commend.com,
 *                                  c.spielberger@commend.com
 *                                  c.huber@commend.com
 */


/*player.c*/
int pulse_async_player_init(struct auplay *ap);


/*recorder.c*/
int pulse_async_recorder_init(struct ausrc *as);


/*pulse.c*/
int pulse_async_set_available_devices(struct list *dev_list,
	pa_operation *(get_dev_info_cb)(pa_context *, struct list*));