/**
 * @file multicast.h  Private multicast interface
 *
 * Copyright (c) 2020 Commend.com - c.huber@commend.com
 */


/* Multicast */
enum {
	MAX_SRATE	= 48000,              /* Maximum sample rate in [Hz] */
	MAX_CHANNELS	= 2,                  /* Maximum number of channels  */
	MAX_PTIME	= 60,                 /* Maximum packet time in [ms] */

	STREAM_PRESZ	= RTP_HEADER_SIZE + 4,/* same as RTP_HEADER_SIZE */

	AUDIO_SAMPSZ	= MAX_SRATE * MAX_CHANNELS * MAX_PTIME / 1000,
	PTIME		= 20,
};


uint8_t multicast_callprio(void);
uint8_t multicast_ttl(void);
uint32_t multicast_fade_time(void);


/* Sender */
typedef int (mcsender_send_h)(size_t ext_len, bool marker, uint32_t rtp_ts,
	struct mbuf *mb, void *arg);

int  mcsender_alloc(struct sa *addr, const struct aucodec *codec);
void mcsender_stopall(void);
void mcsender_stop(struct sa *addr);
void mcsender_enable(bool enable);

void mcsender_print(struct re_printf *pf);

/* Receiver */
int mcreceiver_alloc(struct sa *addr, uint8_t prio);
void mcreceiver_unregall(void);
void mcreceiver_unreg(struct sa *addr);
int mcreceiver_chprio(struct sa *addr, uint32_t prio);
void mcreceiver_enprio(uint32_t prio);
void mcreceiver_enrangeprio(uint32_t priol, uint32_t prioh, bool en);
int  mcreceiver_prioignore(uint32_t prio);
int  mcreceiver_mute(uint32_t prio);
void mcreceiver_enable(bool enable);

void mcreceiver_print(struct re_printf *pf);

/* Player <exchangable player> */
int mcplayer_start(const struct aucodec *ac);
void mcplayer_stop(void);
void mcplayer_fadeout(void);
void mcplayer_fadein(bool restart);
bool mcplayer_fadeout_done(void);
int mcplayer_decode(const struct rtp_header *hdr, struct mbuf *mb, bool drop);

int  mcplayer_init(void);
void mcplayer_terminate(void);

/* Source <exchangable source> */
struct mcsource;
int mcsource_start(struct mcsource **srcp, const struct aucodec *ac,
	mcsender_send_h *sendh, void *arg);
void mcsource_stop(struct mcsource *src);

int  mcsource_init(void);
void mcsource_terminate(void);
