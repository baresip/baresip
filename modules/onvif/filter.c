/**
 * @file filter.c
 *
 * This file contains the code to process the preprocessed data from the
 * @idlepipe module. The two main tasks are to process the audio data throuth
 * a codec and write the data to the network and the other way around.
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "filter.h"
#include "rtspd.h"

#define DEBUG_MODULE "onvif_filter"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


enum {
	AUDIO_SAMPSZ   = 3 * 1920,
};


static bool onvif_aupipe_src_en = true;
static bool onvif_aupipe_play_en = true;

static void onvif_ua_event_handler(struct ua *ua, enum ua_event ev,
	struct call *call, const char *prm, void *arg);


static void send_event(const char *obj, const char *ev, const char *detail)
{
	ua_event(NULL, UA_EVENT_CUSTOM, NULL, "%s:%s:%s", obj, ev, detail);
}


struct filter_mixer {
	bool is_call_running;           /**< call flag                       */

	struct aubuf *aubuf;            /**< aubuf shared between dec-enc    */
	size_t aubuf_maxsz;             /**< maximal audio buffer            */
	const struct aucodec *incodec;  /**< incoming codec                  */

	struct auresamp resamp;         /**< resampler                       */
	uint32_t orate;                 /**< output sample rate              */
	unsigned och;                   /**< output channel count            */
	void *sampv;                    /**< resampler buffer                */
	void *sampvre;                  /**< resampler buffer                */
};


struct filter_resamp {
	struct auresamp resamp;     /**< resampler        */
	void *sampvre;              /**< resample buffer  */
};


struct onvif_filter_stream {
	struct le le;

	bool active;                        /**< stream active flag          */
	const struct aucodec *codec;        /**< stream codec (usually G711) */
	struct auenc_state *auenc_state;    /**< encoder status struct       */
	struct audec_state *audec_state;    /**< decoder status struct       */
	enum aufmt fmt;                     /**< stream sample format        */

	struct jbuf *jbuf;                  /**< jbufer for incoming streams */
	struct aubuf *aubuf;                /**< aubuf for decoded data      */
	size_t aubuf_maxsz;                 /**< max size of aubuf           */
	void *sampv;                        /**< sample buffer               */

	struct rtp_sock *rtpsock;           /**< RTP send socket             */
	struct sa addr;                     /**< address to send             */

	uint32_t ssrc;                      /**< RTP ssrc field              */
	uint32_t timestamp;                 /**< timestamp counter           */
};


struct filter_st {
	struct lock *lock;             /**< streams list lock */
	struct list streams;           /**< streams list      */
	struct aufilt_prm prm;         /**< streams parameter */
	enum aufmt fmt;                /**< used format       */

	struct filter_mixer *mixer;    /**< enc<->dec mixer   */
	struct filter_resamp *aresamp; /**< announcement mixer*/
};
static struct filter_st *incoming_st = NULL;    /*Ref. for the dec. status*/
static struct filter_st *outgoing_st = NULL;    /*Ref. for the enc. status*/


struct enc_st {
	struct aufilt_enc_st af;    /**< base class                          */
	struct filter_st *st;       /**< onvif filter extension              */
	bool marker;                /**< marker bit                          */
};


struct dec_st {
	struct aufilt_dec_st af;    /**< base class            */
	struct filter_st *st;       /**< onvif filter extension*/
};


/*-------------------------------------------------------------------------- */
static void enc_destructor(void *arg) {
	struct enc_st *st = arg;

	list_unlink(&st->af.le);
	st->st = mem_deref(st->st);
}


static void dec_destructor(void *arg) {
	struct dec_st *st = arg;

	list_unlink(&st->af.le);
	st->st = mem_deref(st->st);
}


static void filter_st_destructor(void *arg) {
	struct filter_st *st = arg;

	lock_write_get(st->lock);
	list_flush(&st->streams);
	lock_rel(st->lock);

	st->lock = mem_deref(st->lock);
	st->mixer = mem_deref(st->mixer);
	st->aresamp = mem_deref(st->aresamp);
}


static void filter_stream_destructor(void *arg) {
	struct onvif_filter_stream *fs = arg;

	list_unlink(&fs->le);
	fs->jbuf = mem_deref(fs->jbuf);
	fs->aubuf =mem_deref(fs->aubuf);
	fs->sampv = mem_deref(fs->sampv);
	fs->rtpsock = mem_deref(fs->rtpsock);
}


static void filter_mixer_destructor(void *arg) {
	struct filter_mixer *mixer = arg;

	uag_event_unregister(onvif_ua_event_handler);
	mixer->aubuf = mem_deref(mixer->aubuf);
	mixer->sampv = mem_deref(mixer->sampv);
	mixer->sampvre = mem_deref(mixer->sampvre);
}


static void filter_resamp_destructor(void *arg) {
	struct filter_resamp *aresamp = arg;

	aresamp->sampvre = mem_deref(aresamp->sampvre);
}


/*-------------------------------------------------------------------------- */

/**
 * user agent event handler. detects established and closed calles
 *
 * @param ua		user agent
 * @param ev		user agent event
 * @param call		current call
 * @param prm		parameter of the call
 * @param arg		filter mixer
 */
static void onvif_ua_event_handler(struct ua *ua, enum ua_event ev,
	struct call *call, const char *prm, void *arg)
{
	struct filter_mixer *mixer = arg;
	int err = 0;
	(void) ua;
	(void) prm;

	if (!call)
		return;

	mixer->incodec = audio_codec_getstruct(call_audio(call));
	if (mixer->incodec)
		err = auresamp_setup(&mixer->resamp, mixer->incodec->srate,
			mixer->incodec->ch, mixer->orate, mixer->och);

	if (err) {
		warning("%s: could't setup the resampler (%m)\n", DEBUG_MODULE,
			err);
		return;
	}

	switch (ev) {
		case UA_EVENT_CALL_INCOMING:
			if (!list_isempty(&incoming_st->streams)) {
				call_hangup(call, 486, "Rejected");
				break;
			}
		/*fallthrough*/
		case UA_EVENT_CALL_RINGING:
		case UA_EVENT_CALL_PROGRESS:
		case UA_EVENT_CALL_OUTGOING:
		case UA_EVENT_CALL_ESTABLISHED:
		case UA_EVENT_VU_TX:
		case UA_EVENT_VU_RX:
			mixer->is_call_running = true;
			break;

		case UA_EVENT_CALL_CLOSED:
			if (!list_isempty(&incoming_st->streams))
				break;
			mixer->is_call_running = false;
			aubuf_flush(mixer->aubuf);
			break;

		default:
			break;
	}
}


/**
 * setter for onvif audio filter src enable / disable
 *
 * @param a
 */
void onvif_set_aufilter_src_en(bool a)
{
	onvif_aupipe_src_en = a;
}


/**
 * setter for onvif audio filter play enable / disable
 *
 * @param a
 */
void onvif_set_aufilter_play_en(bool a)
{
	onvif_aupipe_play_en = a;
}


/*-------------------------------------------------------------------------- */
/**
 * RTP receive handler.
 *
 * @param src		source address
 * @param hdr		RTP header
 * @param mb		memory buffer containing the data
 * @param arg		handler argument (filter stream info struct)
 *
 * @return			0 if success, error code otherwise
 *
 */
static void rtp_recvhandler(const struct sa *src, const struct rtp_header *hdr,
	struct mbuf *mb, void *arg)
{
	struct onvif_filter_stream *fs = arg;

	(void) src;

	if (hdr->ssrc != fs->ssrc) {
		fs->ssrc = hdr->ssrc;
		jbuf_flush(fs->jbuf);
	}

	jbuf_put(fs->jbuf, hdr, mb);
	return;
}


/**
 * rtsp receiver handler wrapper for RTP packages
 * read from rtsp IL -> RTP receivehandler
 *
 * the rtp_decode function needs a rtp_sock struct in form of a pointer.
 * however, the function only makes a nullptr check and will do nothing else
 * with this struct. So use a dummy ptr (0xdeadbeef) to nothing meaning full.
 *
 * @param mb            memory buffer to the RTP package
 * @param arg           handler argument
 *
 * @return              0 if success, error code otherwise
 */
void onvif_aufilter_rtsp_wrapper(struct mbuf *mb, void *arg)
{
	struct rtp_header hdr;
	int err = 0;

	err = rtp_decode((struct rtp_sock*)0xdeadbeef, mb, &hdr);
	if (err) {
		warning("%s: Not able to decode the RTP package (%m)\n",
			DEBUG_MODULE, err);
		return;
	}

	rtp_recvhandler(NULL, &hdr, mb, arg);
}


/**
 * decode one RTP packge out of a jbuffer
 *
 * @param fs	onvif filter steam element
 * @param hdr	RTP header
 * @param mb	RTP payload
 * @param wsampc   wish sample count
 * @return int	0 if success, errorcode otherwise
 */
static int handle_rtp(struct onvif_filter_stream *fs,
	const struct rtp_header *hdr, struct mbuf *mb, size_t wsampc)
{
	int err = 0;
	struct filter_resamp *aresamp = incoming_st->aresamp;
	size_t bufsize = mbuf_get_left(mb);
	size_t num_bytes = 0;
	size_t new_sampc = 0;
	size_t sampc = AUDIO_SAMPSZ / 2;
	void *sampv_s = NULL;
	const struct config *cfg = conf_config();

	(void) hdr;

	if (!fs->sampv) {
		fs->sampv = mem_zalloc(sampc * aufmt_sample_size(fs->fmt),
				       NULL);
		if (!fs->sampv)
			return ENOMEM;
	}

	if (bufsize) {
		err = fs->codec->dech(fs->audec_state, fs->fmt, fs->sampv,
				      &sampc, hdr->m, mbuf_buf(mb), bufsize);
	}
	else if (fs->codec->plch && fs->fmt == AUFMT_S16LE) {
		sampc = wsampc;
		err = fs->codec->plch(fs->audec_state, fs->fmt, fs->sampv,
				      &sampc, mbuf_buf(mb), bufsize);
	}
	else {
		sampc = 0;
	}

	if (err)
		return err;

	sampv_s = fs->sampv;
	if (aresamp->resamp.ratio) {
		new_sampc = sampc * aresamp->resamp.ratio;
		if (!aresamp->sampvre) {
			int tmp_num_bytes = new_sampc *
				aufmt_sample_size(fs->fmt) * 2;
			aresamp->sampvre = mem_zalloc(tmp_num_bytes, NULL);
			if (!aresamp->sampvre) {
				err = ENOMEM;
				return err;
			}
		}

		err = auresamp(&aresamp->resamp, aresamp->sampvre, &new_sampc,
			fs->sampv, sampc);
		if (err) {
			warning("%s: announcement resampling (%m)",
				DEBUG_MODULE, err);
			return err;
		}

		sampv_s = aresamp->sampvre;
		sampc = new_sampc;
	}

	num_bytes = sampc *aufmt_sample_size(fs->fmt);
	if (!fs->aubuf) {
		err = aubuf_alloc(&fs->aubuf,
				num_bytes, num_bytes * fs->aubuf_maxsz);

		aubuf_set_mode(fs->aubuf, cfg->audio.adaptive ?
			       AUBUF_ADAPTIVE : AUBUF_FIXED);
		aubuf_set_silence(fs->aubuf, cfg->audio.silence);
		return err;
	}

	err = aubuf_write(fs->aubuf, sampv_s, num_bytes);
	if (err)
		warning ("%s: could not write %d into %p (%m)\n", DEBUG_MODULE,
			 sampc, fs->aubuf, err);

	return err;
}


/**
 * decode a stream on demand
 *
 * @param fs	onvif filter stream element
 * @param wsampc   wish sample count
 * @return int	0 if success, errorcode otherwise
 */
static int stream_decode(struct onvif_filter_stream *fs, size_t wsampc)
{
	struct rtp_header hdr;
	void *mb;

	if (!fs->jbuf)
		return ENOENT;

	if (jbuf_get(fs->jbuf, &hdr, &mb))
		return ENOENT;

	if (hdr.ext && hdr.x.len && mb)
		return ENOTSUP;

	handle_rtp(fs, &hdr, mb, wsampc);
	mem_deref(mb);

	return 0;
}


static int mixer_resize(struct filter_mixer *mixer, struct auframe *af)
{
	size_t aubuf_maxsz = 0;
	const struct config *cfg = conf_config();
	size_t num_bytes;
	uint32_t maxsz;
	int err;

	if (!mixer || !af)
		return EINVAL;

	if (mixer->aubuf)
		return 0;

	num_bytes = auframe_size(af);
	conf_get_u32(conf_cur(), "audio_aubufmaxsize_tx", &maxsz);
	aubuf_maxsz = max(cfg->audio.buffer.max, maxsz);
	mixer->aubuf_maxsz = num_bytes * aubuf_maxsz;

	err = aubuf_alloc(&mixer->aubuf, num_bytes * 1, mixer->aubuf_maxsz);
	if (err)
		return err;

	mixer->sampv = mem_deref(mixer->sampv);
	mixer->sampv = mem_zalloc(num_bytes, NULL);
	if (!mixer->sampv)
		return ENOMEM;

	mixer->sampvre = mem_deref(mixer->sampvre);
	mixer->sampvre = mem_zalloc(num_bytes, NULL);
	if (!mixer->sampvre)
		return ENOMEM;

	return 0;
}


/*-------------------------------------------------------------------------- */
/**
 * encoding handler
 * View of this filter:
 * get the data from the idlepipe
 * MIC-> <ANY FILTER BY IDLEPIPE> -> ONVIF
 * -> <ANY FILTER BY IDLEPIPE> ->NETWORK)
 *
 * @param st        pointer to audio filter encoding struct
 * @param sampv     sample buffer
 * @param sampc     sample counter
 *
 *  return
 */
static int encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct enc_st *est = (struct enc_st *) st;
	struct filter_st *sp = est->st;
	struct onvif_filter_stream *fs = NULL;
	struct le *le = NULL;
	struct mbuf *buf = NULL;
	int16_t *s1;
	int err = 0;

	size_t n = af->sampc;
	size_t num_bytes = auframe_size(af);
	size_t new_sampc = n;
	size_t buf_len = 0;
	size_t i = 0;
	bool marker = est->marker;

	if (!sp->mixer->aubuf)
		err = mixer_resize(sp->mixer, af);

	if (sp->mixer->is_call_running) {
		aubuf_read(sp->mixer->aubuf, sp->mixer->sampv, num_bytes);
		s1 = (int16_t *)sp->mixer->sampv;
		for (i = 0; i < n; i++)
			s1[i] += ((int16_t *) af->sampv)[i];

		if (sp->mixer->resamp.ratio) {
			err = auresamp(&sp->mixer->resamp,
				sp->mixer->sampvre, &new_sampc,
				sp->mixer->sampv, n);

			if (err)
				goto out;

			s1 = (int16_t *)sp->mixer->sampvre;
		}
	}
	else if (sp->aresamp->resamp.ratio && !list_isempty(&sp->streams)) {
		if (!sp->aresamp->sampvre) {
			sp->aresamp->sampvre = mem_zalloc(num_bytes, NULL);
			if (!sp->aresamp->sampvre) {
				err = ENOMEM;
				goto out;
			}
		}
		err = auresamp(&sp->aresamp->resamp,
			sp->aresamp->sampvre, &new_sampc,
			af->sampv, n);
		if (err)
			goto out;

		s1 = (int16_t *)sp->aresamp->sampvre;
	}
	else {
		s1 = (int16_t *)af->sampv;
	}

	LIST_FOREACH(&sp->streams, le) {
		fs = le->data;

		if (!fs->active)
			continue;

		if (!fs->auenc_state && fs->codec->encupdh)
			err = fs->codec->encupdh(&fs->auenc_state, fs->codec,
						 NULL, NULL);

		if (err)
			goto out;

		buf = mbuf_alloc(new_sampc + RTP_HEADER_SIZE);
		if (!buf) {
			warning("%s: out of memory for encode buffer",
				DEBUG_MODULE);
			err = ENOMEM;
			goto out;
		}

		mbuf_set_end(buf, buf->size);
		mbuf_advance(buf, RTP_HEADER_SIZE);
		buf_len = mbuf_get_left(buf);

		if (!onvif_aupipe_src_en) {
			memset(mbuf_buf(buf), 0, mbuf_get_left(buf));
		}
		else {
			err = fs->codec->ench(fs->auenc_state,
				&marker, mbuf_buf(buf),
				&buf_len, sp->fmt, s1, new_sampc);

			if (err) {
				warning("%s: Error while encoding the data."
					" (%m)", DEBUG_MODULE, err);
				goto out;
			}
		}

		err = rtp_send(fs->rtpsock, &fs->addr, false, false, 0,
			fs->timestamp, buf);
		if (err) {
			warning("%s: Could not send audio stream via RTP."
				" (%m)", DEBUG_MODULE, err);
			goto out;
		}

		fs->timestamp += new_sampc;
		buf = mem_deref(buf);
	}

  out:
	mem_deref(buf);
	est->marker = false;

	return err;
}

/**
 * decoding handler
 * View of this filter:
 * get the data from the network socket
 * NETWORK -> ONVIF -> <ANY FILTER BY IDLEPIPE> ->SPEAKER
 *
 * @param st        pointer to audio filter decoding struct
 * @param sampv     sample buffer
 * @param sampc     sample counter
 *
 * @return			0 if success, error code otherwise
 */
static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct dec_st *dst = (struct dec_st *) st;
	struct filter_st *sp = dst->st;
	struct onvif_filter_stream *fs;
	size_t num_bytes = auframe_size(af);
	int err = 0;

	err = lock_read_try(sp->lock);
	if (err) {
		memset(af->sampv, 0, num_bytes);
		return 0;
	}

	if (sp->mixer->is_call_running)
		aubuf_write(sp->mixer->aubuf,(uint8_t *) af->sampv, num_bytes);

	fs = list_ledata(list_head(&sp->streams));
	lock_rel(sp->lock);

	if (!fs)
		return 0;

	if (onvif_aupipe_play_en) {
		while (!err && aubuf_cur_size(fs->aubuf) < num_bytes)
			err = stream_decode(fs, af->sampc);

		aubuf_read(fs->aubuf, (uint8_t *) af->sampv, num_bytes);
	}

	return err;
}


/*-------------------------------------------------------------------------- */
/**
 * allocate a mixer struct
 *
 * @param mp	filter mixer struct pointer
 * @param prm	audio filter parameter
 * @param fmt	audio sample format
 * @return int	0 if success, errorcode otherwise
 */
static int filter_mixer_alloc(struct filter_mixer **mp, struct aufilt_prm *prm,
	enum aufmt fmt)
{
	int err = 0;
	struct filter_mixer *mixer = NULL;
	(void) prm;
	(void) fmt;

	mixer = mem_zalloc(sizeof(*mixer), filter_mixer_destructor);
	if (!mixer) {
		err = ENOMEM;
		goto out;
	}

	/*output is fixed at 8000/1 because of onvif G711 only*/
	/*can be resetted by decoder or encoder if codec changes*/
	mixer->orate = 8000;
	mixer->och = 1;
	auresamp_init(&mixer->resamp);

  out:
	if (err)
		mixer = mem_deref(mixer);
	else
		*mp = mixer;

	return err;
}


/**
 * init a resampler for the decoding side
 *
 * @param frp		filter resampler pointer
 * @param irate	input sample rate
 * @param ich		input channles
 * @param orate		output sample rate
 * @param och		output channles
 * @param fmt		audio sample format
 * @return int		0 if success, errorcase otherwise
 */
static int filter_resamp_alloc(struct filter_resamp **frp, uint32_t irate,
	unsigned ich, uint32_t orate, unsigned och, enum aufmt fmt)
{
	int err = 0;
	struct filter_resamp *aresamp = NULL;
	(void) fmt;

	aresamp = mem_zalloc(sizeof(*aresamp), filter_resamp_destructor);
	if (!aresamp) {
		err = ENOMEM;
		goto out;
	}

	err = auresamp_setup(&aresamp->resamp, irate, ich, orate, och);

  out:
	if (err)
		aresamp = mem_deref(aresamp);
	else
		*frp = aresamp;

	return err;
}


/**
 * allocate the filter lists and locks
 * if not allocated -> allocate filter_mixer
 *
 * @param stp	filter list&lock struct
 * @param ctx	filter mixer struct
 * @param prm	filter parameter
 * @param is_encoder
 * @return int	0 if success, errorcode otherwise
 */
static int filter_alloc(struct filter_st **stp, void **ctx,
	struct aufilt_prm *prm, bool is_encoder)
{
	struct filter_st *st = NULL;
	const struct config *cfg = conf_config();
	int err = 0;

	if (!stp || !ctx || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), filter_st_destructor);
	if (!st)
		return ENOMEM;

	err = lock_alloc(&st->lock);
	if (err)
		goto out;

	if (cfg->audio.src_fmt != cfg->audio.play_fmt) {
		err = EINVAL;
		goto out;
	}

	st->fmt = cfg->audio.src_fmt;
	st->prm.srate = prm->srate;
	st->prm.ch = prm->ch;

	if (*ctx) {
		st->mixer = mem_ref(*ctx);
	}
	else if (!st->mixer){
		err = filter_mixer_alloc(&st->mixer, prm, st->fmt);
		if (err)
			goto out;

		err = uag_event_register(onvif_ua_event_handler, st->mixer);
		if (err)
			goto out;
	}
	else {
		warning("%s: mixer is set but the controlstruct is missung."
			"This is not possilbe");
		err = EINVAL;
		goto out;
	}

	if (!st->aresamp && !is_encoder) {
		/*hardcoded 8kHz as input for the resampler
		 * (onvif supports only G711)*/
		err = filter_resamp_alloc(&st->aresamp, 8000, 1,
			st->prm.srate, st->prm.ch, st->fmt);
		if (err)
			goto out;

	}
	else if (!st->aresamp && is_encoder) {
		/*hardcoded 8kHz as input for the resampler
		 * (onvif supports only G711)*/
		err = filter_resamp_alloc(&st->aresamp, st->prm.srate,
					  st->prm.ch, 8000, 1, st->fmt);
		if (err)
			goto out;

	}
	else {
		err = EINVAL;
		goto out;
	}

  out:
	if (err) {
		st = mem_deref(st);
	}
	else {
		*stp = st;
		*ctx = st->mixer;
	}

	return err;
}


/**
 * initialization function for a possible encoder filter path
 *
 * @param stp
 * @param ctx	controll struct (same for encoder & decoder)
 * @param af	audio filter
 * @param prm	audio filter parameter
 * @param au	audio struct (not used)
 *
 * @return			0 if success, error code otherwise
 */
static int encode_update(struct aufilt_enc_st **stp, void **ctx,
	const struct aufilt *af, struct aufilt_prm *prm,
	const struct audio *au)
{
	struct enc_st *st = NULL;
	int err = 0;

	(void) au;

	if (!stp || !ctx || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	if (outgoing_st)
		st->st = mem_ref(outgoing_st);
	else
		err = filter_alloc(&st->st, ctx, prm, true);

	if (err) {
		st = mem_deref(st);
	}
	else {
		*stp = (struct aufilt_enc_st *)st;
		if (!outgoing_st)
			outgoing_st = mem_ref(st->st);
	}

	return err;
}


/**
 * initialization function for a possible decode filter path
 *
 * @param stp
 * @param ctx	controll struct (same for encoder & decoder)
 * @param af	audio filter
 * @param prm	audio filter parameter
 * @param au	audio struct (not used)
 *
 * @return			0 if success, error code otherwise
 */
static int decode_update(struct aufilt_dec_st **stp, void **ctx,
	const struct aufilt *af, struct aufilt_prm *prm,
	const struct audio *au)
{
	struct dec_st *st;
	int err = 0;

	(void) au;

	if (!stp || !ctx || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	if (incoming_st)
		st->st = mem_ref(incoming_st);
	else
		err = filter_alloc(&st->st, ctx, prm, false);

	if (err) {
		st = mem_deref(st);
	}
	else {
		*stp = (struct aufilt_dec_st *)st;
		if (!incoming_st)
			incoming_st = mem_ref(st->st);
	}

	return err;
}


/*-------------------------------------------------------------------------- */
/**
 * create outgoing stream
 *
 * @param fs	onvif filter stream element
 * @param sa	target address
 * @param conn	optional rtsp connection
 * @param proto	protocol used (TCP->requires @conn!)
 * @return int	0 if success, errorcode otherwise
 */
int onvif_aufilter_audio_send_start(struct onvif_filter_stream *fs,
	const struct sa *sa, const struct rtsp_conn *conn, int proto)
{
	int err = 0;

	if (!fs || !sa || ((proto == IPPROTO_TCP) && !conn))
		return EINVAL;

	if (!outgoing_st)
		return EINVAL;

	sa_cpy(&fs->addr, sa);
	switch (proto) {
		case IPPROTO_TCP:
			err = rtp_over_tcp(&fs->rtpsock, &fs->addr,
				mem_ref((struct rtsp_conn *) conn));
			if (err)
				goto out;
			break;

		case IPPROTO_UDP:
			err =rtp_open(&fs->rtpsock, sa_af(&fs->addr));
			if (err)
				goto out;
			break;

		default:
			err =ENOTSUP;
			goto out;
	}

	lock_write_get(outgoing_st->lock);
	fs->fmt = outgoing_st->fmt;
	list_append(&outgoing_st->streams, &fs->le, fs);
	lock_rel(outgoing_st->lock);

	send_event("onvif", "start recording", "Start outgoing stream");

  out:
	return err;
}


/**
 * remove RTP-socket from onvif filter stream and unlink
 *
 * @param fs	onvif filter stream element
 */
void onvif_aufilter_audio_send_stop(struct onvif_filter_stream *fs)
{
	if (!outgoing_st)
		return;

	lock_write_get(outgoing_st->lock);
	list_unlink(&fs->le);
	lock_rel(outgoing_st->lock);
	fs->rtpsock = mem_deref(fs->rtpsock);

	send_event("onvif", "finished recording", "Stop outgoing stream");
}


/**
 * start RTP listener on @sa via @proto
 *
 * @param fs		filter stream element
 * @param sa		ip address + port to listen on
 * @param proto		used transport protocol
 * @return int		0 if success, errorcode otherwise
 */
int onvif_aufilter_audio_recv_start(struct onvif_filter_stream *fs,
	struct sa *sa, int proto)
{
	const struct config *cfg = conf_config();
	int err = 0;

	if (!fs || !sa)
		return EINVAL;

	if (!incoming_st)
		return EINVAL;

	sa_cpy(&fs->addr, sa);

	if (!fs->jbuf) {
		err = jbuf_alloc(&fs->jbuf,
			cfg->avt.jbuf_del.min, cfg->avt.jbuf_del.max);
		if (err)
			goto out;

		jbuf_set_type(fs->jbuf, cfg->avt.jbtype);
	}

	switch (proto) {
		case IPPROTO_TCP:
			break;

		case IPPROTO_UDP:
			err = rtp_listen(&fs->rtpsock, proto, sa,
					 sa_port(sa), sa_port(sa) +1, false,
					 rtp_recvhandler, NULL, fs);
			DEBUG_INFO("What port do i use %d (%m)\n", sa_port(sa),
				   err);
			if (err)
				goto out;
			break;

		default:
			err = ENOTSUP;
			goto out;
	}

	lock_write_get(incoming_st->lock);
	fs->fmt = incoming_st->fmt;
	list_append(&incoming_st->streams, &fs->le, fs);
	lock_rel(incoming_st->lock);
	send_event("onvif", "start announcement", "Start incoming stream");

  out:
	return err;
}


/**
 * remove RTP listener of onvif filter stream and unlink
 *
 * @param fs	onvif filter stream element
 */
void onvif_aufilter_audio_recv_stop(struct onvif_filter_stream *fs)
{
	if (!incoming_st)
		return;

	lock_write_get(incoming_st->lock);
	list_unlink(&fs->le);
	lock_rel(incoming_st->lock);
	fs->rtpsock = mem_deref(fs->rtpsock);
	send_event("onvif", "finished announcement", "Stop incoming stream");
}


/**
 * init and reset a stream element
 *
 * @param fs		filter stream element
 * @param srate		samplerate
 * @param ch		channel
 * @param codec		codec
 * @return int		0 if success, errorcode otherwise
 */
static int filter_stream_reset(struct onvif_filter_stream *fs,
	uint32_t srate, uint32_t ch, const char *codec)
{
	const struct config *cfg = conf_config();
	uint32_t maxsz;
	int err = 0;

	if (!fs || !codec)
		return EINVAL;

	fs->codec = aucodec_find(baresip_aucodecl(), codec, srate, ch);
	if (!fs->codec)
		return EINVAL;

	if (fs->aubuf)
		aubuf_flush(fs->aubuf);

	if (fs->jbuf)
		jbuf_flush(fs->jbuf);

	if (!fs->audec_state && fs->codec->decupdh)
		err = fs->codec->decupdh(&fs->audec_state, fs->codec, NULL);

	conf_get_u32(conf_cur(), "audio_aubufmaxsize_tx", &maxsz);

	fs->active = true;
	fs->aubuf_maxsz = max(cfg->audio.buffer.max, maxsz);
	fs->ssrc = 0;
	fs->timestamp = 0;

	return err;
}


/**
 * allocate a new stream element which can be inserted in encode or decode path
 *
 * @param fsp      onvif filter stream element pointer
 * @param srate    samplerate
 * @param ch       channels
 * @param codec    codec
 * @return int     0 if success, errorcode otherwise
 */
int onvif_aufilter_stream_alloc(struct onvif_filter_stream **fsp,
	uint32_t srate, uint8_t ch, const char *codec)
{
	struct onvif_filter_stream *fs = NULL;
	int err = 0;

	fs = mem_zalloc(sizeof(*fs), filter_stream_destructor);
	if (!fs)
		return ENOMEM;

	err = filter_stream_reset(fs, srate, ch, codec);

	if (err)
		fs = mem_deref(fs);
	else
		*fsp = fs;

	return err;
}


static struct aufilt onvif_filter = {
	LE_INIT, "onviffilter", encode_update, encode, decode_update, decode
};


/**
 * register onvif_filter as audio filter in baresip
 */
void register_onvif_filter(void)
{
	aufilt_register(baresip_aufiltl(), &onvif_filter);
}


/**
 * unregister onvif_filter as audio filter in baresip
 */
void unregister_onvif_filter(void)
{
	outgoing_st = mem_deref(outgoing_st);
	incoming_st = mem_deref(incoming_st);

	aufilt_unregister(&onvif_filter);
}
