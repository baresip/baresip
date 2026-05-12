/**
 * @file sndfile.c  Audio dumper using libsndfile
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 * Modified by Andreas Granig for RT Start
 */
#include <sndfile.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
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
	uint64_t t0_us;
	uint64_t nsamp_written;
	uint32_t srate;
	uint32_t ch;
	enum aufmt fmt;
};

struct sndfile_dec {
	struct aufilt_dec_st af;  /* base class */
	SNDFILE *decf;
	int err;
	const struct audio *audio;
	char *filename;
	uint64_t t0_us;
	uint64_t nsamp_written;
	uint32_t srate;
	uint32_t ch;
	enum aufmt fmt;
};

static char file_path[512] = ".";
static bool dump_wallclock = true;        /* RX (decoder) pacing */
static bool dump_wallclock_enc = false;   /* TX (encoder) pacing - off */
static bool dump_wallclock_drop_ahead = true;

static uint32_t dump_wallclock_ahead_tolerance_ms = 50;
static uint32_t dump_wallclock_max_silence_ms = 200;

static inline uint64_t now_usec_monotonic(void)
{
	struct timespec ts;
	if (0 != clock_gettime(CLOCK_MONOTONIC, &ts))
		return 0;
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static size_t bytes_per_sample(enum aufmt fmt)
{
	switch (fmt) {
	case AUFMT_S16LE: return 2;
	case AUFMT_FLOAT: return 4;
	default:          return 0;
	}
}

static void write_silence(SNDFILE *sf, enum aufmt fmt, uint32_t ch,
			  uint64_t nsamp)
{
	uint8_t zbuf[4096];
	size_t bps;
	uint64_t nbytes;

	if (!sf || !nsamp)
		return;

	bps = bytes_per_sample(fmt);
	if (!bps || !ch)
		return;

	memset(zbuf, 0, sizeof(zbuf));

	nbytes = nsamp * (uint64_t)ch * (uint64_t)bps;

	while (nbytes) {
		size_t chunk = (size_t)min(nbytes, (uint64_t)sizeof(zbuf));
		sf_write_raw(sf, zbuf, chunk);
		nbytes -= chunk;
	}
}

static bool pacing_should_drop(uint64_t nsamp_written, uint64_t exp,
			       uint32_t srate)
{
	uint64_t tol;

	if (!dump_wallclock_drop_ahead || !srate)
		return false;

	tol = ((uint64_t)dump_wallclock_ahead_tolerance_ms * (uint64_t)srate) /
	      1000ULL;

	return nsamp_written > exp + tol;
}

static uint64_t pacing_cap_silence(uint64_t nsamp, uint32_t srate)
{
	uint64_t cap;
	if (!srate)
		return 0;

	cap = ((uint64_t)dump_wallclock_max_silence_ms * (uint64_t)srate) /
	      1000ULL;

	return min(nsamp, cap);
}

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
	size_t bps;
	uint64_t nsamp;

	if (!st || !af)
		return EINVAL;

	if (sf->err)
		return sf->err;

	if (sf->encf && (sf->srate != af->srate || sf->ch != af->ch ||
			 sf->fmt != af->fmt)) {
		/* Rotate file on format change */
		sf_close(sf->encf);
		module_event("sndfile-rt-start", "close_enc", NULL, NULL, "%s",
			     sf->filename);
		sf->encf = NULL;
		sf->filename = mem_deref(sf->filename);
		sf->t0_us = 0;
		sf->nsamp_written = 0;
	}

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

		sf->srate = af->srate;
		sf->ch    = af->ch;
		sf->fmt   = af->fmt;
	}

	num_bytes = auframe_size(af);
	bps = bytes_per_sample(af->fmt);
	nsamp = bps && af->ch ? (uint64_t)num_bytes / ((uint64_t)bps *
						      (uint64_t)af->ch) : 0;

	if (dump_wallclock_enc && nsamp && af->srate) {
		uint64_t t = now_usec_monotonic();
		uint64_t exp = 0;

		if (!sf->t0_us) {
			sf->t0_us = t;
			sf->nsamp_written = 0;
		}
		else if (t > sf->t0_us) {
			exp = ((t - sf->t0_us) * (uint64_t)af->srate) /
			      1000000ULL;
			if (exp > sf->nsamp_written) {
				uint64_t gap = exp - sf->nsamp_written;
				gap = pacing_cap_silence(gap, af->srate);
				write_silence(sf->encf, af->fmt, af->ch, gap);
				sf->nsamp_written += gap;
			}
		}

		if (pacing_should_drop(sf->nsamp_written, exp, af->srate))
			return 0;
	}

	sf_write_raw(sf->encf, af->sampv, num_bytes);
	sf->nsamp_written += nsamp;

	return 0;
}


static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct sndfile_dec *sf = (struct sndfile_dec *)st;
	size_t num_bytes;
	size_t bps;
	uint64_t nsamp;

	if (!st || !af)
		return EINVAL;

	if (sf->err)
		return sf->err;

	if (sf->decf && (sf->srate != af->srate || sf->ch != af->ch ||
			 sf->fmt != af->fmt)) {
		/* Rotate file on format change */
		sf_close(sf->decf);
		module_event("sndfile-rt-start", "close_dec", NULL, NULL, "%s",
			     sf->filename);
		sf->decf = NULL;
		sf->filename = mem_deref(sf->filename);
		sf->t0_us = 0;
		sf->nsamp_written = 0;
	}

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

		sf->srate = af->srate;
		sf->ch    = af->ch;
		sf->fmt   = af->fmt;
	}

	num_bytes = auframe_size(af);
	bps = bytes_per_sample(af->fmt);
	nsamp = bps && af->ch ? (uint64_t)num_bytes / ((uint64_t)bps *
						      (uint64_t)af->ch) : 0;

	if (dump_wallclock && nsamp && af->srate) {
		uint64_t t = now_usec_monotonic();
		uint64_t exp = 0;

		if (!sf->t0_us) {
			sf->t0_us = t;
			sf->nsamp_written = 0;
		}
		else if (t > sf->t0_us) {
			exp = ((t - sf->t0_us) * (uint64_t)af->srate) /
			      1000000ULL;
			if (exp > sf->nsamp_written) {
				uint64_t gap = exp - sf->nsamp_written;
				gap = pacing_cap_silence(gap, af->srate);
				write_silence(sf->decf, af->fmt, af->ch, gap);
				sf->nsamp_written += gap;
			}
		}

		if (pacing_should_drop(sf->nsamp_written, exp, af->srate))
			return 0;
	}

	sf_write_raw(sf->decf, af->sampv, num_bytes);
	sf->nsamp_written += nsamp;

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
	(void)conf_get_bool(conf_cur(), "snd_dump_wallclock", &dump_wallclock);
	(void)conf_get_bool(conf_cur(), "snd_dump_wallclock_enc",
			    &dump_wallclock_enc);
	(void)conf_get_bool(conf_cur(), "snd_dump_wallclock_drop_ahead",
			    &dump_wallclock_drop_ahead);
	(void)conf_get_u32(conf_cur(), "snd_dump_wallclock_ahead_tol_ms",
			   &dump_wallclock_ahead_tolerance_ms);
	(void)conf_get_u32(conf_cur(), "snd_dump_wallclock_max_silence_ms",
			   &dump_wallclock_max_silence_ms);

	info("sndfile-rt-start: saving files in %s\n", file_path);
	info("sndfile-rt-start: dump wallclock pacing dec=%s enc=%s\n",
	     dump_wallclock ? "enabled" : "disabled",
	     dump_wallclock_enc ? "enabled" : "disabled");
	info("sndfile-rt-start: dump wallclock drop-ahead %s (tol=%ums)\n",
	     dump_wallclock_drop_ahead ? "enabled" : "disabled",
	     dump_wallclock_ahead_tolerance_ms);
	info("sndfile-rt-start: dump wallclock max silence %ums\n",
	     dump_wallclock_max_silence_ms);

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
