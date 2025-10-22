/**
 * @file sndfile.c  Audio dumper using libsndfile
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 * Modified by Andreas Granig for RT Start
 */
#include <sndfile.h>
#include <time.h>
#include <sys/time.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


enum {
	SNDFILE_NAME_LEN = 256
};


/**
 * @defgroup sndfile-rt-start sndfile-rt-start	
 *
 * Audio filter that writes audio samples to WAV-file
 *
 * Example Configuration:
 \verbatim
  snd_path					/tmp/
 \endverbatim
 */


struct sndfile_enc {
	struct aufilt_enc_st af;  /* base class */
	SNDFILE *encf;
	int err;
	const struct audio *audio;
	char *filename;
};

struct sndfile_dec {
	struct aufilt_dec_st af;  /* base class */
	SNDFILE *decf;
	int err;
	const struct audio *audio;
	char *filename;
};

static char file_path[512] = ".";

static int timestamp_print_usec(struct re_printf *pf, const struct timeval *tv)
{
	long long usec;

	if (!tv)
		return 0;

	usec = (long long)((tv->tv_sec) * 1000000) + tv->tv_usec;
	return re_hprintf(pf, "%lld", usec);
}


static void enc_destructor(void *arg)
{
	struct sndfile_enc *st = arg;

	if (st->encf) {
		sf_close(st->encf);
		module_event("sndfile-rt-start", "close_enc", NULL, NULL, "%s",
			     st->filename);
	}

	list_unlink(&st->af.le);
	mem_deref(st->filename);
}


static void dec_destructor(void *arg)
{
	struct sndfile_dec *st = arg;

	if (st->decf) {
		sf_close(st->decf);
		module_event("sndfile-rt-start", "close_dec", NULL, NULL, "%s",
			     st->filename);
	}

	list_unlink(&st->af.le);
	mem_deref(st->filename);
}


static int get_format(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:  return SF_FORMAT_PCM_16;
	case AUFMT_FLOAT:  return SF_FORMAT_FLOAT;
	default:           return 0;
	}
}


static int filename_alloc(char **filenamep,
			  const struct stream *strm,
			  bool enc)
{
	char *filename;
	int err;
	struct timeval tv;

	const char *cname = stream_cname(strm);
	const char *peer = stream_peer(strm);

	if (gettimeofday(&tv, NULL)) {
		warning("sndfile-rt-start: could not get time\n");
		return EINVAL;
	}
	err = re_sdprintf(&filename,
			  "%s/dump-%s=>%s-%H-%s.wav",
			  file_path,
			  cname, peer,
			  timestamp_print_usec, &tv, enc ? "enc" : "dec");
	if (err)
		return err;

	info("sndfile-rt-start: dumping %s audio to %s\n",
	     enc ? "encode" : "decode", filename);

	module_event("sndfile-rt-start", "dump", NULL, NULL, "%s", filename);

	*filenamep = filename;
	return 0;
}


static int openfile(SNDFILE **sfp, const char *filename,
			 const struct aufilt_prm *prm,
			 bool enc)
{
	SF_INFO sfinfo;
	SNDFILE *sf;
	int format;

	format = get_format(prm->fmt);
	if (!format) {
		warning("sndfile-rt-start: sample format not supported (%s)\n",
			aufmt_name(prm->fmt));
		return EINVAL;
	}

	sfinfo.samplerate = prm->srate;
	sfinfo.channels   = prm->ch;
	sfinfo.format     = SF_FORMAT_WAV | format;

	sf = sf_open(filename, SFM_WRITE, &sfinfo);
	if (!sf) {
		warning("sndfile-rt-start: could not open: %s\n", filename);
		puts(sf_strerror(NULL));
		return EIO;
	}

	info("sndfile-rt-start: dumping %s audio to %s\n",
	     enc ? "encode" : "decode", filename);

	module_event("sndfile-rt-start", "dump", NULL, NULL, "%s", filename);

	*sfp = sf;
	return 0;
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct sndfile_enc *st;
	(void)ctx;
	(void)af;
	(void)prm;

	if (!stp || !au)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	st->audio = au;
	*stp = (struct aufilt_enc_st *)st;

	return 0;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct sndfile_dec *st;
	(void)ctx;
	(void)af;
	(void)prm;

	if (!stp || !au)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	st->audio = au;
	*stp = (struct aufilt_dec_st *)st;

	return 0;
}


static int encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct sndfile_enc *sf = (struct sndfile_enc *)st;
	size_t num_bytes;

	if (!st || !af)
		return EINVAL;

	if (sf->err)
		return sf->err;

	if (!sf->encf) {
		const struct stream *strm = audio_strm(sf->audio);
		struct aufilt_prm prm = {af->srate, af->ch, af->fmt};

		/* Allocate filename on first packet */
		if (!sf->filename) {
			sf->err = filename_alloc(&sf->filename, strm, true);
			if (sf->err)
				return sf->err;
		}

		sf->err = openfile(&sf->encf, sf->filename, &prm, true);
		if (sf->err)
			return sf->err;
	}

	num_bytes = auframe_size(af);

	sf_write_raw(sf->encf, af->sampv, num_bytes);

	return 0;
}


static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct sndfile_dec *sf = (struct sndfile_dec *)st;
	size_t num_bytes;

	if (!st || !af)
		return EINVAL;

	if (sf->err)
		return sf->err;

	if (!sf->decf) {
		const struct stream *strm = audio_strm(sf->audio);
		struct aufilt_prm prm = {af->srate, af->ch, af->fmt};

		/* Allocate filename on first packet */
		if (!sf->filename) {
			sf->err = filename_alloc(&sf->filename, strm, false);
			if (sf->err)
				return sf->err;
		}

		sf->err = openfile(&sf->decf, sf->filename, &prm, false);
		if (sf->err)
			return sf->err;
	}

	num_bytes = auframe_size(af);

	sf_write_raw(sf->decf, af->sampv, num_bytes);

	return 0;
}


static struct aufilt sndfile = {
	.name    = "sndfile-rt-start",
	.encupdh = encode_update,
	.ench    = encode,
	.decupdh = decode_update,
	.dech    = decode
};


static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &sndfile);

	conf_get_str(conf_cur(), "snd_path", file_path, sizeof(file_path));

	info("sndfile-rt-start: saving files in %s\n", file_path);

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&sndfile);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(sndfile) = {
	"sndfile-rt-start",
	"filter",
	module_init,
	module_close
};
