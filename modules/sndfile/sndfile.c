/**
 * @file sndfile.c  Audio dumper using libsndfile
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <sndfile.h>
#include <time.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup sndfile sndfile
 *
 * Audio filter that writes audio samples to WAV-file
 *
 * Example Configuration:
 \verbatim
  snd_path 					/tmp/
 \endverbatim
 */


struct sndfile_enc {
	struct aufilt_enc_st af;  /* base class */
	SNDFILE *enc;
	enum aufmt fmt;
};

struct sndfile_dec {
	struct aufilt_dec_st af;  /* base class */
	SNDFILE *dec;
	enum aufmt fmt;
};

static char file_path[256] = ".";


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

	if (st->enc)
		sf_close(st->enc);

	list_unlink(&st->af.le);
}


static void dec_destructor(void *arg)
{
	struct sndfile_dec *st = arg;

	if (st->dec)
		sf_close(st->dec);

	list_unlink(&st->af.le);
}


static int get_format(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:  return SF_FORMAT_PCM_16;
	case AUFMT_FLOAT:  return SF_FORMAT_FLOAT;
	default:           return 0;
	}
}


static SNDFILE *openfile(const struct aufilt_prm *prm, bool enc)
{
	char filename[128];
	SF_INFO sfinfo;
	time_t tnow = time(0);
	struct tm *tm = localtime(&tnow);
	SNDFILE *sf;
	int format;

	(void)re_snprintf(filename, sizeof(filename),
			  "%s/dump-%H-%s.wav",
				file_path,
			  timestamp_print, tm, enc ? "enc" : "dec");

	format = get_format(prm->fmt);
	if (!format) {
		warning("sndfile: sample format not supported (%s)\n",
			aufmt_name(prm->fmt));
		return NULL;
	}

	sfinfo.samplerate = prm->srate;
	sfinfo.channels   = prm->ch;
	sfinfo.format     = SF_FORMAT_WAV | format;

	sf = sf_open(filename, SFM_WRITE, &sfinfo);
	if (!sf) {
		warning("sndfile: could not open: %s\n", filename);
		puts(sf_strerror(NULL));
		return NULL;
	}

	info("sndfile: dumping %s audio to %s\n",
	     enc ? "encode" : "decode", filename);

	return sf;
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct sndfile_enc *st;
	int err = 0;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return EINVAL;

	st->fmt = prm->fmt;

	st->enc = openfile(prm, true);
	if (!st->enc)
		err = ENOMEM;

	if (err)
		mem_deref(st);
	else
		*stp = (struct aufilt_enc_st *)st;

	return err;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct sndfile_dec *st;
	int err = 0;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return EINVAL;

	st->fmt = prm->fmt;

	st->dec = openfile(prm, false);
	if (!st->dec)
		err = ENOMEM;

	if (err)
		mem_deref(st);
	else
		*stp = (struct aufilt_dec_st *)st;

	return err;
}


static int encode(struct aufilt_enc_st *st, void *sampv, size_t *sampc)
{
	struct sndfile_enc *sf = (struct sndfile_enc *)st;
	size_t num_bytes;

	if (!st || !sampv || !sampc)
		return EINVAL;

	num_bytes = *sampc * aufmt_sample_size(sf->fmt);

	sf_write_raw(sf->enc, sampv, num_bytes);

	return 0;
}


static int decode(struct aufilt_dec_st *st, void *sampv, size_t *sampc)
{
	struct sndfile_dec *sf = (struct sndfile_dec *)st;
	size_t num_bytes;

	if (!st || !sampv || !sampc)
		return EINVAL;

	num_bytes = *sampc * aufmt_sample_size(sf->fmt);

	sf_write_raw(sf->dec, sampv, num_bytes);

	return 0;
}


static struct aufilt sndfile = {
	LE_INIT, "sndfile", encode_update, encode, decode_update, decode
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
