/**
 * @file sndfile.c  Audio dumper using libsndfile
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <sndfile.h>
#include <time.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


enum {
	SNDFILE_NAME_LEN = 256
};


/**
 * @defgroup sndfile sndfile
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


static int timestamp_print(struct re_printf *pf, const struct tm *tm)
{
	if (!tm)
		return 0;

	return re_hprintf(pf, "%d-%02d-%02d-%02d-%02d-%02d",
			  1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
			  tm->tm_hour, tm->tm_min, tm->tm_sec);
}


static void enc_destructor(void *arg)
{
	struct sndfile_enc *st = arg;

	if (st->encf) {
		sf_close(st->encf);
		module_event("sndfile", "close_enc", NULL, NULL, "%s",
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
		module_event("sndfile", "close_dec", NULL, NULL, "%s",
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
	time_t tnow = time(0);
	struct tm *tm = localtime(&tnow);
	char *filename;
	int err;

	const char *cname = stream_cname(strm);
	const char *peer = stream_peer(strm);

	err = re_sdprintf(&filename,
			  "%s/dump-%s=>%s-%H-%s.wav",
			  file_path,
			  cname, peer,
			  timestamp_print, tm, enc ? "enc" : "dec");
	if (err)
		return err;

	info("sndfile: dumping %s audio to %s\n",
	     enc ? "encode" : "decode", filename);

	module_event("sndfile", "dump", NULL, NULL, "%s", filename);

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
		warning("sndfile: sample format not supported (%s)\n",
			aufmt_name(prm->fmt));
		return EINVAL;
	}

	sfinfo.samplerate = prm->srate;
	sfinfo.channels   = prm->ch;
	sfinfo.format     = SF_FORMAT_WAV | format;

	sf = sf_open(filename, SFM_WRITE, &sfinfo);
	if (!sf) {
		warning("sndfile: could not open: %s\n", filename);
		puts(sf_strerror(NULL));
		return EIO;
	}

	info("sndfile: dumping %s audio to %s\n",
	     enc ? "encode" : "decode", filename);

	module_event("sndfile", "dump", NULL, NULL, "%s", filename);

	*sfp = sf;
	return 0;
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct sndfile_enc *st;
	const struct stream *strm = audio_strm(au);
	int err;
	(void)ctx;
	(void)af;
	(void)prm;

	if (!stp || !au)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return EINVAL;

	err = filename_alloc(&st->filename, strm, true);
	if (err)
		goto error;

	st->audio = au;
	*stp = (struct aufilt_enc_st *)st;

	return 0;

error:
	mem_deref(st);
	return err;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct sndfile_dec *st;
	const struct stream *strm = audio_strm(au);
	int err;
	(void)ctx;
	(void)af;
	(void)prm;

	if (!stp || !au)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return EINVAL;

	err = filename_alloc(&st->filename, strm, false);
	if (err)
		goto error;

	st->audio = au;
	*stp = (struct aufilt_dec_st *)st;

	return 0;

error:
	mem_deref(st);
	return err;
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
		struct aufilt_prm prm = {af->srate, af->ch, af->fmt};
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
		struct aufilt_prm prm = {af->srate, af->ch, af->fmt};
		sf->err = openfile(&sf->decf, sf->filename, &prm, false);
		if (sf->err)
			return sf->err;
	}

	num_bytes = auframe_size(af);

	sf_write_raw(sf->decf, af->sampv, num_bytes);

	return 0;
}


static struct aufilt sndfile = {
	.name    = "sndfile",
	.encupdh = encode_update,
	.ench    = encode,
	.decupdh = decode_update,
	.dech    = decode
};


static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &sndfile);

	conf_get_str(conf_cur(), "snd_path", file_path, sizeof(file_path));

	info("sndfile: saving files in %s\n", file_path);

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&sndfile);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(sndfile) = {
	"sndfile",
	"filter",
	module_init,
	module_close
};
