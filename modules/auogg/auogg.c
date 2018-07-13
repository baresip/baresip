/**
 * @file auogg.c ogg/speex Audio Source
 *
 * Copyright (C) 2018 Commend.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>
#include <speex/speex_header.h>
#include <speex/speex.h>

#include <re_atomic.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * Ogg unpack status structure
 */
struct ogg_dec {
	int packet_count;
	int stream_init;
	int serialno;

	ogg_sync_state oy;
	ogg_page       og;
	ogg_packet     op;
	ogg_stream_state os;
};


enum codec_en {
	CO_NOCODEC = 0,
	CO_SPEEX,
};


/**
 * @defgroup aufile aufile
 *
 * Audio module for using an OGG-file encoded with speex as audio input
 */
struct ausrc_st {
	const struct ausrc *as;         /**< Base class                      */
	struct ausrc_prm *prm;          /**< Audio src parameter             */
	const struct aucodec *ac;       /**< Current audio decoder           */
	struct audec_state *dec;        /**< Audio decoder state (optional)  */
	FILE* fin;                      /**< File descriptor of audio file   */
	bool run;                       /**< Read thread running flag        */
	bool started;                   /**< Read thread started flag        */
	thrd_t thread;
	bool eof;                       /**< End of File flag                */
	const char *error;              /**< Error message                   */
	struct tmr tmr;                 /**< Timer calling error handler     */
	int err;                        /**< Error code for error handler    */

	struct aubuf *aubuf;
	size_t sampc;                   /**< Sample count for ptime          */
	size_t maxsz;                   /**< Maximum aubuf size in [bytes]   */
	struct config_audio cfg;        /**< Audio configuration             */

	void *sampv;                    /**< Sample buffer                   */
	uint32_t sampv_sz;              /**< Size of sample buffer           */

	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;

	uint32_t aubuf_underrun;

	struct ogg_dec ogg;
	bool packet_done;
};


static struct ausrc *ausrc;


static void destructor(void *arg)
{
	struct ausrc_st *st = arg;
	struct ogg_dec *ogg = &st->ogg;

	debug("auogg: Going to be destroyed.\n");

	if (st->started) {
		st->run = false;
		thrd_join(st->thread, NULL);
	}

	if (st->fin)
		fclose(st->fin);

	mem_deref(st->aubuf);
	mem_deref(st->dec);
	mem_deref(st->sampv);
	tmr_cancel(&st->tmr);

	if (ogg->stream_init)
		ogg_stream_clear(&ogg->os);
	ogg_sync_clear(&ogg->oy);

	debug("auogg: Destroyed object.\n");
}


static int init_aubuf(struct ausrc_st *st)
{
	uint32_t ptime;

	if (!st || !st->ac)
		return EINVAL;

	ptime = st->prm->ptime;
	if (ptime == 0)
		ptime = 20;

	st->sampc =  st->ac->srate * st->ac->ch * ptime / 1000;

	st->aubuf = mem_deref(st->aubuf);

	st->sampv_sz = 2 * st->sampc;
	st->sampv = mem_zalloc(st->sampv_sz*2, NULL);

	/* If ptime == 0 pass data to output immediately. We need no aubuf. */
	if (st->prm->ptime == 0)
		return 0;

	st->maxsz = 2 * 8 * st->sampc;
	return aubuf_alloc(&st->aubuf, 0, 0);
}


static int set_aucodec(struct ausrc_st *st, const struct aucodec *ac, int mode)
{
	int err;
	char fmtp[1];

	*fmtp = (char) mode;

	st->dec = mem_deref(st->dec);

	st->ac = ac;
	if (ac->decupdh) {
		err = ac->decupdh(&st->dec, ac, fmtp);
		if (err) {
			warning("auogg: alloc decoder: %m\n", err);
			return err;
		}
	}

	return init_aubuf(st);
}


static int process_speex_header(struct ausrc_st *st, uint32_t *rate,
		uint8_t *channels, int *mode, int *extra_headers)
{
   SpeexHeader *header;
   struct ogg_dec *ogg = &st->ogg;

   header = speex_packet_to_header((char*)ogg->op.packet, ogg->op.bytes);
   if (!header) {
	  st->error = "Cannot read speex header.";
      return EINVAL;
   }

   *rate = header->rate;
   /* Adjust rate if --force-* options are used */

   *channels = header->nb_channels;

   *extra_headers = header->extra_headers;
   *mode = header->mode;

   free(header);
   return 0;
}


static int push_thread(void *arg);


static int decode_packet(struct ausrc_st *st, const uint8_t *data,
			 size_t bytes)
{
	int err = 0;
	int dec_fmt = st->cfg.dec_fmt;
	int play_fmt = st->cfg.play_fmt;
	size_t sampc = st->sampv_sz / 2;
	struct auframe auframe;

	/* No decoder set */
	if (!st->ac) {
		warning("auogg: no decoder set\n");
		return EINVAL;
	}

	if (!bytes)
		return EINVAL;

	/* decode */
	err = st->ac->dech(st->dec, dec_fmt, st->sampv, &sampc, false, data,
			   bytes);
	if (err) {
		st->error = "Decode error";
		warning("auogg: %s codec=%s %u bytes: (%m)\n", st->error,
			st->ac->name, bytes, err);
		goto out;
	}

	auframe_init(&auframe, AUFMT_S16LE, st->sampv, sampc,
		     st->ac->srate, st->ac->ch);
	if (!st->aubuf) {
		/* Only for aufileinfo:
		 * if we have no aubuf we pass data immediately */
		if (st->run)
			st->rh(&auframe, st->arg);

		goto out;
	}

	/* pass to aubuf */
	if (play_fmt == dec_fmt && sampc) {
		err = aubuf_write_auframe(st->aubuf, &auframe);
		if (err)
			goto out;
	}
	else {
		warning("auogg: invalid sample formats (%s -> %s)\n",
			aufmt_name(dec_fmt),
			aufmt_name(play_fmt));
	}

 out:
	return err;
}


static int process_packets(struct ausrc_st *st)
{
	struct ogg_dec *ogg = &st->ogg;
	int extra_headers = 0;
	int err = 0;
	int mode = 0;

	/*Extract all available packets*/
	if (!st->eof) {

		if (!st->run)
			return 0;

		if (ogg_stream_packetout(&ogg->os, &ogg->op) != 1) {
			st->packet_done = true;
			return 0;
		}

		ogg->serialno = ogg->os.serialno;
		/* Select codec. */

		/* If no codec set, break here. */
		if (ogg->serialno == -1) {
			st->error = "Error in ogg header.";
			err = EINVAL;
			warning("auogg: %s.\n", st->error);
			return err;
		}

		/*If first packet, process header. */
		if (ogg->packet_count == 0) {
			enum codec_en codec = CO_NOCODEC;
			const char* codecname = NULL;

			if (ogg->op.bytes >= 5 &&
					!memcmp(ogg->op.packet, "Speex", 5)) {
				codecname = "speex";
				codec = CO_SPEEX;
			}

			switch (codec) {
				case CO_SPEEX:
					err = process_speex_header(st,
							&st->prm->srate,
							&st->prm->ch, &mode,
							&extra_headers);
					st->error = "";
					break;
				default:
					err = EINVAL;
					break;
			}
			if (err) {
				st->error = "Error in ogg header. Codec not "
						"supported";
				err = EINVAL;
				warning("auogg: %s.\n", st->error);
				return err;
			}

			st->ac = aucodec_find(baresip_aucodecl(), codecname,
					st->prm->srate, st->prm->ch);
			if (!st->ac) {
				st->error = "Could not find decoder";
				err = EINVAL;
				warning("auogg: %s %s/%d/%u\n", st->error,
						codecname, st->prm->srate,
						st->prm->ch);
				err = EINVAL;
				return err;
			}

			info("auogg: found codec %s:%d:%d\n", codecname,
					st->prm->srate, st->prm->ch);

			err = set_aucodec(st, st->ac, mode);
			if (err)
				return err;

		}
		else if (ogg->packet_count==1)
		{
		}
		else if (ogg->packet_count<=1+extra_headers)
		{
			/* Ignore extra headers */
		}
		else {
			/*End of stream condition*/
			if (ogg->op.e_o_s &&
					ogg->os.serialno == ogg->serialno) {
				debug("auogg: ogg end of stream.\n");
				st->eof = true;
				st->packet_done = true;
			}

			if (ogg->op.bytes) {
				err = decode_packet(st, ogg->op.packet,
						ogg->op.bytes);
				if (err)
					return err;
			}
			else {
				warning("auogg: empty ogg packet");
				st->eof = true;
				st->packet_done = true;
			}
		}

		ogg->packet_count++;
	}

	return err;
}


static int process_page(struct ausrc_st *st)
{
	struct ogg_dec *ogg = &st->ogg;
	int err = 0;

	if (st->eof)
		return 0;

	if (!st->run)
		return 0;

	/* process next page */
	if (st->packet_done) {
		st->packet_done = false;
		if (ogg_sync_pageout(&ogg->oy, &ogg->og) != 1)
			return 0;

		if (ogg->stream_init == 0) {
			ogg_stream_init(&ogg->os, ogg_page_serialno(&ogg->og));
			ogg->stream_init = 1;
		}
		if (ogg_page_serialno(&ogg->og) != ogg->os.serialno) {
			/* so all streams are read. */
			ogg_stream_reset_serialno(&ogg->os,
						  ogg_page_serialno(&ogg->og));
		}
		/*Add page to the bitstream*/
		ogg_stream_pagein(&ogg->os, &ogg->og);
	}

	err = process_packets(st);
	if (err)
		return err;

	return err;
}


static void tmr_errh(void *arg)
{
	struct ausrc_st *st = arg;

	/* error handler must be called from re_main thread */
	if (st->run)
		tmr_start(&st->tmr, 1000, tmr_errh, st);
	else if (st->errh)
		st->errh(st->err, st->error, st->arg);
}


static void prepare_read(struct ausrc_st *st)
{
	struct ogg_dec *ogg = &st->ogg;

	if (!st || !st->fin) {
		warning("auogg: File not open.\n");
		return;
	}
	ogg->serialno = -1;

	/*Init Ogg data struct*/
	ogg_sync_init(&ogg->oy);
	st->packet_done = true;
}


static int do_read(struct ausrc_st *st)
{
	struct ogg_dec *ogg = &st->ogg;
	int err;

	if (st->run) {
		char *data;
		size_t n;

		if (st->eof && st->aubuf) {
			if (aubuf_cur_size(st->aubuf)==0) {

				/* Exit loop here. For replay the caller has
				 * to alloc a new instance. */
				return EOF;
			}

			return 0;
		}

		if (st->packet_done) {
			/* Get the ogg buffer for writing */
			data = ogg_sync_buffer(&ogg->oy, 200);
			n = fread(data, sizeof(char), 200, st->fin);
			if (!n) {
				if (feof(st->fin)) {
					debug("auogg: end of file.\n");
				}
				else {
					warning("auogg: file read error.\n");
				}

				st->eof = true;
				return 0;
			}

			ogg_sync_wrote(&ogg->oy, n);
		}

		err = process_page(st);
		return err;
	}

	return 0;
}


static int push_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct ausrc_st *st = arg;
	int16_t *sampv;
	uint32_t ptime;
	int err, cnt = 0;

	st->started = true;
	if (!st->rh)
		return EINVAL;

	/* read some data */
	prepare_read(st);
	err = do_read(st);
	if (err) {
		warning("aufile: do_read failure %i - aborting\n", err);
		return err;
	}

	ptime = st->prm->ptime;
	sampv = mem_alloc(st->sampc * 2 + 1, NULL);
	if (!sampv)
		return ENOMEM;

	while (st->run) {

		struct auframe af = {
			.fmt   = AUFMT_S16LE,
			.sampv = sampv,
			.sampc = st->sampc,
			.timestamp = ts * 1000
		};

		now = tmr_jiffies();

		if (!st->aubuf) {
			err = do_read(st);
			if (err) {
				warning("aufile: do_read failure %i\n", err);
				break;
			}

			if (st->eof)
				break;
			else
				continue;
		}

		if (st->eof && aubuf_cur_size(st->aubuf) == 0)
			break;

		/* make sure that aubuf is filled
		 * (even if there is no time left) */
		if ( (ts > now + ptime / 4 ||
			aubuf_cur_size(st->aubuf) < st->sampc * 2) &&
			!st->eof &&
			aubuf_cur_size(st->aubuf) < st->maxsz) {
			/* some time is left - read some samples */
			err = do_read(st);
			if (err) {
				warning("aufile: do_read failure %i\n", err);
				break;
			}

			continue;
		}
		else if (ts > now) {
			sys_msleep(ts - now);
			continue;
		}

#if 1
		if (now > ts + 100) {
			warning("aufile: cpu lagging behind (%llu ms)\n",
			      now - ts);
		}
#endif

		if (!st->eof && aubuf_cur_size(st->aubuf)/2 < st->sampc) {
			++st->aubuf_underrun;

			warning("auogg: push thread aubuf_underrun "
					"(total %lu)\n",
					st->aubuf_underrun);
		}

		aubuf_read_auframe(st->aubuf, &af);

		if (st->run)
			st->rh(&af, st->arg);

		ts += ptime;
		++cnt;
	}

	st->run = false;
	mem_deref(sampv);
	debug("auogg: push thread exited (%i frames)\n", cnt);
	return 0;
}


static int alloc_handler(struct ausrc_st **stp, const struct ausrc *as,
			 struct ausrc_prm *prm, const char *dev,
			 ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	bool join = false;
	int err = 0;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("auogg: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	info("auogg: loading input file '%s'\n", dev);

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->as    = as;
	st->prm   = prm;
	st->rh    = rh;
	st->errh  = errh;
	st->arg   = arg;
	st->cfg   = conf_config()->audio;

	/* ptime == 0 means blocking mode */
	join = st->prm->ptime == 0;

	st->fin = fopen(dev, "rb");
	if (!st->fin) {
		err = errno;
		warning("auogg: Failed to open file '%s' (%m)\n", dev, err);
		goto out;
	}

	tmr_init(&st->tmr);
	tmr_start(&st->tmr, 1000, tmr_errh, st);

	st->run = true;
	if (!st->thread) {
		err = thread_create_name(&st->thread, "auogg_src",
					 push_thread, st);
		if (err)
			warning("auogg: Could not start push thread. "
				"(%m)\n", err);
	}

	if (join) {
		thrd_join(st->thread, NULL);
		st->errh(st->err, st->error, st->arg);
	}
 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int module_init(void)
{
	return ausrc_register(&ausrc, baresip_ausrcl(),
			      "auogg", alloc_handler);
}


static int module_close(void)
{
	ausrc = mem_deref(ausrc);

	info("auogg: Module closed.\n");
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(auogg) = {
	"auogg",
	"ausrc",
	module_init,
	module_close
};
