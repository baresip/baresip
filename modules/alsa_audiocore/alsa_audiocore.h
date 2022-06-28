/**
 * @file alsa_audiocore.h Acoustic Echo Cancellation and Noise Reduction
 *
 * Copyright (C) 2022 Commend.com - h.ramoser@commend.com
 */

#ifndef alsa_audiocore_h
#define alsa_audiocore_h

struct audiocore_st;

struct alsa_play_st {
	snd_pcm_t *write;
	void *sampv;
	size_t sampc;
	size_t num_frames;
	struct auplay_prm prm;
	char *device;
};

struct alsa_src_st {
	snd_pcm_t *read;
	size_t sampc;
	size_t num_frames;
	struct ausrc_prm prm;
	char *device;
};

int alsa_ac_reset(snd_pcm_t *pcm, uint32_t srate, uint32_t ch,
	       uint32_t num_frames, snd_pcm_format_t pcmfmt);
snd_pcm_format_t alsa_ac_aufmt_to_alsaformat(enum aufmt fmt);

int alsa_ac_src_alloc(struct alsa_src_st **stp, struct ausrc_prm *prm,
	const char *device);
void alsa_ac_src_read(struct alsa_src_st *st, void* sampv);
void alsa_ac_src_read_frames(struct alsa_src_st *st, void* sampv,
	size_t num_frames);

int alsa_ac_play_alloc(struct alsa_play_st **stp, struct auplay_prm *prm,
	const char *device);
void alsa_ac_play_write(struct alsa_play_st *st, void *sampv);
void alsa_ac_play_write_frames(struct alsa_play_st *st, void *sampv,
	size_t num_frames);

int audiocore_init(void);
void audiocore_close(void);
void audiocore_process_bx(void *sampv, size_t sampc);
void audiocore_process_bz(void *sampv, size_t sampc);
void audiocore_enable(bool enable);
// signals call start
void audiocore_start(void);
// signals call end
void audiocore_stop(void);

#endif
