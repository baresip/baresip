/**
 * @file audiocore.c  Commend Acoustic Echo Cancellation and Noise Reduction
 *
 * Copyright (C) 2018 Commend International
 */
#include "audiocore.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <audiocore_cwrapper.h>
#include <re/re.h>
#include <rem/rem.h>
#include <baresip.h>



#define send_event(A, B, C, ...) \
	ua_event(NULL, UA_EVENT_CUSTOM, NULL, A " " B " " C, __VA_ARGS__)


/**
 * @defgroup commend_audiocore_aec commend_audiocore_aec
 *
 * Acoustic Echo Cancellation (AEC) from Commend International
 */

enum eqFilter {
	EQ_FILTER_PEAK,
	EQ_FILTER_HIGH_SHELV,
	EQ_FILTER_LOW_SHELV,
};

struct audiocore_st {
	int size_delay;
	int playback_delay;
	int framesize;
	int filterlength;
	int samplerate;
	int samplerate_prev;
	int ref;
	bool echo_cancellation;
	int noise_suppression;
	bool noise_suppression_enabled;
	int noise_suppression_g_min;
	bool noise_gate_enabled;
	bool postgain_enabled;
	bool rec_enabled;
	bool debug_enable;
	int out;
	int delay_ms;
	int tail_length_ms;
	int startup;
	int skipcount;
	compressor_parameter ls_compressor;
	float mic_compressor_gain;
	compressor_parameter mic_compressor;
	float mic_post_gain;
	float noise_suppression_rec_scale;
	noisegate_parameter ng_param;
	postgain_parameter pg_param;
	bool bypass;
	bool ivc_enabled;
	int volume_level;
	ac_handle audiocore;
	uint8_t ac_usage;

	int am_enabled;
	unsigned int am_spl_threshold;
	unsigned int am_spl_threshold_time;
	int am_mic_sensitivity;

	int lspl_enabled;
	int idleAudio_enabled;

	bool ws_filter_enabled;
	bool et962_filter_enabled;
	eq_config_handle mic_eq_config;
	eq_config_handle ls_eq_config;
	unsigned int ls_mic_retryinterval;
	int ls_mic_noise_volume;

	bool lm_enabled;
	bool lm_use100V;
	unsigned int lm_measurement_interval;
	unsigned int lm_reference_impedance;
	unsigned int lm_impedance_tolerance;
	AC_LINEMONITORING_STATION lm_station;
	AC_LINEMONITORING_INPUT lm_input;

	uint32_t call_count;
};

struct enc_st {
	struct aufilt_enc_st af;  /* base class */
	struct audiocore_st *st;
};

struct dec_st {
	struct aufilt_dec_st af;  /* base class */
	struct audiocore_st *st;
};

struct audiocore_st *d = NULL;
static int audiocore_init(struct audiocore_st* st);

/**
 * @defgroup commend_commands commend_commands
 *
 * Commend specific commands
 *
 * This module must be loaded if you want to use commend specific commands
 * used by bct-inp to communicate with baresip.
 */
static const char *lm_errorstr(AC_LINEMONITORING_ERROR error)
{
	if (error == AC_LM_ERROR_OK)
		return "no error";
	else if (error & AC_LM_ERROR_INTERRUPTION)
		return "interruption";
	else if (error & AC_LM_ERROR_IMPEDANCE_HIGH)
		return "impedance high";
	else if (error & AC_LM_ERROR_IMPEDANCE_LOW)
		return "impedance low";
	else if (error & AC_LM_ERROR_SHORT_CIRCUIT)
		return "short circuit";
	else if (error & AC_LM_ERROR_GROUND_FAULT)
		return "ground fault";
	else if (error & AC_LM_ERROR_AMP_FAULT)
		return "amp fault";
	else
		return "unknown error";
}


static const char *lm_inputstr(AC_LINEMONITORING_INPUT input)
{
	switch (input) {
	case AC_LM_INPUT_DEFAULT:
		return "default";
		break;
	case AC_LM_INPUT_CURRENT:
		return "isens";
		break;
	case AC_LM_INPUT_VOLTAGE_1:
		return "usensp";
		break;
	case AC_LM_INPUT_VOLTAGE_2:
		return "usensm";
		break;
	case AC_LM_INPUT_NONE:
	default:
		return "none";
		break;
	}
}
/**
 * Set audiocore echo cancellation
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_set_echo_cancellation(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0) {
			d->echo_cancellation = value;
		}
		else {
			err = -1;
		}
	}
	else {
		re_hprintf(pf, "echo cancellation is %d",
				d->echo_cancellation);
	}

	/* Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setEchoCancellation(d->audiocore,
				d->echo_cancellation);

	if (err) {
		warning("audiocore: setting echo cancellation failed: %m\n",
				err);
	}
	else {
		debug("audiocore: echo cancellation set to %d\n",
				d->echo_cancellation);
	}

	return err;
}


/**
 * Enable audiocore noise suppression
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_en_noise_suppression(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0)
			d->noise_suppression_enabled = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "noise suppression enabled is %d",
				d->noise_suppression_enabled);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setNoiseSuppression(d->audiocore,
				d->noise_suppression_enabled);

	if (err) {
		warning("audiocore: enabling noise suppression failed: %m\n",
				err);
	}
	else {
		debug("audiocore: noise suppression enabled set to %d\n",
				d->noise_suppression_enabled);
	}

	return err;
}


/**
 * Set audiocore intelligent volume control
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_set_ivc(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0)
			d->ivc_enabled = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "IVC is %d", d->ivc_enabled);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setIVC(d->audiocore, d->ivc_enabled);

	if (err) {
		warning("audiocore: setting IVC failed: %m\n", err);
	}
	else {
		debug("audiocore: IVC set to %d\n",
				d->ivc_enabled);
	}

	return err;
}


/**
 * Enable audiocore noise gate
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_en_noise_gate(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0)
			d->noise_gate_enabled = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "noise gate enabled is %d",
				d->noise_gate_enabled);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		enableNoiseGate(d->audiocore,
				d->noise_gate_enabled);

	if (err) {
		warning("audiocore: enabling noise gate failed: %m\n", err);
	}
	else {
		debug("audiocore: noise gate enabled set to %d\n",
				d->noise_gate_enabled);
	}

	return err;
}


/**
 * Enable audiocore postgain
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_en_postgain(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0)
			d->postgain_enabled = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "postgain enabled is %d",
				d->postgain_enabled);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		enablePostgain(d->audiocore,
				d->postgain_enabled);

	if (err) {
		warning("audiocore: enabling postgain failed: %m\n", err);
	}
	else {
		debug("audiocore: postgain enabled set to %d\n",
				d->postgain_enabled);
	}

	return err;
}


static int com_en_rec(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0)
			d->rec_enabled = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "rec enabled is %d",
				d->rec_enabled);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		enableREC(d->audiocore,
				d->rec_enabled);

	if (err) {
		warning("audiocore: enabling rec failed: %m\n", err);
	}
	else {
		debug("audiocore: rec enabled set to %d\n",
				d->rec_enabled);
	}

	return err;
}


/**
 * Enable audiocore debug mode
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_set_debug_mode(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0)
			d->debug_enable = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "debug mode is %d",
				d->debug_enable);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		enableDebugMode(d->audiocore,
				d->debug_enable);

	if (err) {
		warning("audiocore: enabling debug mode failed: %m\n", err);
	}
	else {
		debug("audiocore: debug mode set to %d\n",
				d->debug_enable);
	}

	return err;
}


/**
 * Set audiocore volume level
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given argument to int
 *			EINVAL	audiocore not initialized
 */
static int com_set_volume_level(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	long value = LONG_MIN;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = strtol(carg->prm, NULL, 10);
		if (value >= INT_MIN && value <= INT_MAX)
			d->volume_level = (int)value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "volume level is %d",
				d->volume_level);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		notifyVolumeLevel(d->audiocore,
				d->volume_level);

	if (err) {
		warning("audiocore: setting volume level failed: %m\n", err);
	}
	else {
		debug("audiocore: volume level set to %d\n",
				d->volume_level);
	}

	return err;
}


/**
 * Set audiocore noise suppression
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given argument to int
 *			EINVAL	audiocore not initialized
 */
static int com_set_noise_suppression(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	long value = LONG_MIN;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = strtol(carg->prm, NULL, 10);
		if (value >= INT_MIN && value <= INT_MAX)
			d->noise_suppression = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "noise suppression is %d",
				d->noise_suppression);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setNoiseSuppressionParameter(d->audiocore,
				d->noise_suppression);

	if (err) {
		warning("audiocore: setting noise suppression failed: %m\n",
				err);
	}
	else {
		debug("audiocore: noise suppression set to %d\n",
				d->noise_suppression);
	}

	return err;
}


/**
 * Set audiocore noise suppression recording scale
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given argument to float
 *			EINVAL	audiocore not initialized
 */
static int com_set_noise_suppression_rec_scale(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	float value = HUGE_VALF;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = strtof(carg->prm, NULL);
		if (value != HUGE_VALF)
			d->noise_suppression_rec_scale = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "noise suppression rec scale is %f",
				d->noise_suppression_rec_scale);
	}

	/* Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setNoiseSuppressionRecScaling(d->audiocore,
				d->noise_suppression_rec_scale);

	if (err) {
		warning("audiocore: setting noise suppression failed: %m\n",
				err);
	}
	else {
		debug("audiocore: noise suppression rec scale set to %f\n",
				d->noise_suppression_rec_scale);
	}

	return err;
}


/**
 * Set audiocore microphone compressor gain
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given argument to int
 *			EINVAL	audiocore not initialized
 */
static int com_set_microphone_compressor_gain(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	float value = LONG_MIN;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = strtof(carg->prm, NULL);
		if (value != HUGE_VALF)
			d->mic_compressor.gain = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "microphone compressor gain is %f",
				d->mic_compressor.gain);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setMicCompressorGain(d->audiocore,
				d->mic_compressor.gain);

	if (err) {
		warning("audiocore: setting microphone compressor gain failed "
				"(%m)\n", err);
	}
	else {
		debug("audiocore: microphone compressor gain set to %d\n",
				d->mic_compressor.gain);
	}

	return err;
}


/**
 * Set audiocore microphone compressor
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_microphone_compressor(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	compressor_parameter comp;
	compressor_parameter *audiocoreMC;
	char useNg[8] = {0};
	int err = 0;
	int cnt;

	if (d == NULL)
		return EINVAL;

	audiocoreMC = &d->mic_compressor;
	if (str_isset(carg->prm)) {
		memset(&comp, 0, (sizeof(comp)));

		cnt = sscanf(carg->prm, "%f %f %f %f %7s",
				&comp.gain, &comp.thresh_lo, &comp.thresh_hi,
				&comp.noise_gain, useNg);

		if (cnt == 5) {
			if (str_bool(&audiocoreMC->use_noise_gain, useNg)) {
				err = EINVAL;
			}
			else {
				audiocoreMC->gain = comp.gain;
				audiocoreMC->thresh_lo = comp.thresh_lo;
				audiocoreMC->thresh_hi = comp.thresh_hi;
				audiocoreMC->noise_gain = comp.noise_gain;
				err = 0;
			}
		}
		else {
			err = EINVAL;
		}
	}
	else {
		re_hprintf(pf,	"mc_gain is %f mc_thresh_lo is %f "
				"mc_thresh_hi is %f mc_noise_gain is %f "
				"mc_use_noise_gain is %d",
				audiocoreMC->gain,
				audiocoreMC->thresh_lo,
				audiocoreMC->thresh_hi,
				audiocoreMC->noise_gain,
				audiocoreMC->use_noise_gain);
	}

	/* Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setMicCompressor(d->audiocore,
				audiocoreMC->gain,
				audiocoreMC->thresh_lo,
				audiocoreMC->thresh_hi,
				audiocoreMC->use_noise_gain,
				audiocoreMC->noise_gain);

	if (err) {
		warning("audiocore: setting microphone compressor failed "
				"(%m)\n", err);
	}
	else {
		debug("audiocore: microphone compressor set to mc_gain is %f "
				"mc_thresh_lo is %f "
				"mc_thresh_hi is %f mc_noise_gain is %f "
				"mc_use_noise_gain is %d\n",
				audiocoreMC->gain,
				audiocoreMC->thresh_lo,
				audiocoreMC->thresh_hi,
				audiocoreMC->noise_gain,
				audiocoreMC->use_noise_gain);
	}

	return err;
}


/**
 * Set audiocore microphone post gain
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given argument to int
 *			EINVAL	audiocore not initialized
 */
static int com_set_microphone_post_gain(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	float value = HUGE_VALF;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = strtof(carg->prm, NULL);
		if (value != HUGE_VALF)
			d->mic_post_gain = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "microphone post gain is %f",
				d->mic_post_gain);
	}

	/* Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setPostMicGainDb(d->audiocore,
				d->mic_post_gain);

	if (err) {
		warning("audiocore: setting microphone post gain failed: %m\n",
				err);
	}
	else {
		debug("audiocore: microphone post gain set to %d\n",
				d->mic_post_gain);
	}

	return err;
}

/**
 * Set audiocore loudspeaker compressor gain
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given argument to int
 *			EINVAL	audiocore not initialized
 */
static int com_set_loudspeaker_compressor_gain(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	float value = LONG_MIN;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = strtof(carg->prm, NULL);
		if (value != HUGE_VALF)
			d->ls_compressor.gain = value;
		else
			err = -1;
	}
	else {
		re_hprintf(pf, "loudspeaker compressor gain is %f",
				d->ls_compressor.gain);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setLsCompressorGain(d->audiocore,
				d->ls_compressor.gain);

	if (err) {
		warning("audiocore: setting loudspeaker compressor gain failed"
				" (%m)\n", err);
	}
	else {
		debug("audiocore: loudspeaker compressor gain set to %d\n",
				d->ls_compressor.gain);
	}

	return err;
}


/**
 * Set audiocore loudspeaker compressor
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_set_loudspeaker_compressor(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	compressor_parameter comp;
	compressor_parameter *audiocoreLC;
	char useNg[8] = {0};
	int err = 0;
	int cnt;

	if (d == NULL)
		return EINVAL;

	audiocoreLC = &d->ls_compressor;
	if (str_isset(carg->prm)) {
		memset(&comp, 0, (sizeof(comp)));

		cnt = sscanf(carg->prm, "%f %f %f %f %7s",
				&comp.gain, &comp.thresh_lo, &comp.thresh_hi,
				&comp.noise_gain, useNg);

		if (cnt == 5) {
			if (str_bool(&audiocoreLC->use_noise_gain, useNg)) {
				err = EINVAL;
			}
			else {
				audiocoreLC->gain = comp.gain;
				audiocoreLC->thresh_lo = comp.thresh_lo;
				audiocoreLC->thresh_hi = comp.thresh_hi;
				audiocoreLC->noise_gain = comp.noise_gain;
				err = 0;
			}
		}
		else {
			err = EINVAL;
		}
	}
	else {
		re_hprintf(pf,	"lc_gain is %f lc_thresh_lo is %f "
				"lc_thresh_hi is %f lc_noise_gain is %f "
				"lc_use_noise_gain is %d",
				audiocoreLC->gain,
				audiocoreLC->thresh_lo,
				audiocoreLC->thresh_hi,
				audiocoreLC->noise_gain,
				audiocoreLC->use_noise_gain);
	}

	/* Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setLsCompressor(d->audiocore,
				audiocoreLC->gain,
				audiocoreLC->thresh_lo,
				audiocoreLC->thresh_hi,
				audiocoreLC->use_noise_gain,
				audiocoreLC->noise_gain);

	if (err) {
		warning("audiocore: setting loudspeaker compressor failed "
				"(%m)\n", err);
	}
	else {
		debug("audiocore: loudspeaker compressor set to lc_gain is %f "
				"lc_thresh_lo is %f "
				"lc_thresh_hi is %f lc_noise_gain is %f "
				"lc_use_noise_gain is %d\n",
				audiocoreLC->gain,
				audiocoreLC->thresh_lo,
				audiocoreLC->thresh_hi,
				audiocoreLC->noise_gain,
				audiocoreLC->use_noise_gain);
	}

	return err;
}

/**
 * Set audiocore noise gate
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_noise_gate(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	noisegate_parameter ng;
	noisegate_parameter *audiocoreNG;
	int err = 0;
	int cnt;

	if (d == NULL)
		return EINVAL;

	audiocoreNG = &d->ng_param;
	if (str_isset(carg->prm)) {
		memset(&ng, 0, (sizeof(ng)));

		cnt = sscanf(carg->prm, "%f %f %f %f %f %f",
				&ng.release_time, &ng.attack_time,
				&ng.hold_time, &ng.closed_gain,
				&ng.open_threshold, &ng.close_threshold);

		if (cnt == 6) {
			audiocoreNG->release_time = ng.release_time;
			audiocoreNG->attack_time = ng.attack_time;
			audiocoreNG->hold_time = ng.hold_time;
			audiocoreNG->closed_gain = ng.closed_gain;
			audiocoreNG->open_threshold = ng.open_threshold;
			audiocoreNG->close_threshold = ng.close_threshold;
			err = 0;
		}
		else {
			err = EINVAL;
		}
	}
	else {
		re_hprintf(pf,	"ng_release_time is %f ng_attack_time is %f "
				"ng_hold_time is %f ng_closed_gain is %f "
				"ng_open_threshold is %f "
				"ng_close_threshold is %f",
				audiocoreNG->release_time,
				audiocoreNG->attack_time,
				audiocoreNG->hold_time,
				audiocoreNG->closed_gain,
				audiocoreNG->open_threshold,
				audiocoreNG->close_threshold);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setupNoiseGate(d->audiocore,
				audiocoreNG->release_time,
				audiocoreNG->attack_time,
				audiocoreNG->hold_time,
				audiocoreNG->closed_gain,
				audiocoreNG->open_threshold,
				audiocoreNG->close_threshold);

	if (err) {
		warning("audiocore: setting noise gate failed: %m\n",
				err);
	}
	else {
		debug("audiocore: noise gate set to ng_release_time is %f "
				"ng_attack_time is %f "
				"ng_hold_time is %f ng_closed_gain is %f "
				"ng_open_threshold is %f "
				"ng_close_threshold is %f\n",
				audiocoreNG->release_time,
				audiocoreNG->attack_time,
				audiocoreNG->hold_time,
				audiocoreNG->closed_gain,
				audiocoreNG->open_threshold,
				audiocoreNG->close_threshold);
	}

	return err;
}


/**
 * Set audiocore postgain
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_postgain(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	postgain_parameter pg;
	postgain_parameter *audiocorePG;
	int err = 0;
	int cnt;

	if (d == NULL)
		return EINVAL;

	audiocorePG = &d->pg_param;
	if (str_isset(carg->prm)) {
		memset(&pg, 0, (sizeof(pg)));

		cnt = sscanf(carg->prm, "%f %f %f %f %f %f %f %f",
				&pg.release_time, &pg.attack_time,
				&pg.hold_open_time,
				&pg.hold_closed_time, &pg.closed_gain,
				&pg.open_threshold,
				&pg.close_threshold, &pg.tau);

		if (cnt == 8) {
			audiocorePG->release_time = pg.release_time;
			audiocorePG->attack_time = pg.attack_time;
			audiocorePG->hold_open_time = pg.hold_open_time;
			audiocorePG->hold_closed_time = pg.hold_closed_time;
			audiocorePG->closed_gain = pg.closed_gain;
			audiocorePG->open_threshold = pg.open_threshold;
			audiocorePG->close_threshold = pg.close_threshold;
			audiocorePG->tau = pg.tau;
			err = 0;
		}
		else {
			err = EINVAL;
		}
	}
	else {
		re_hprintf(pf,	"pg_release_time is %f pg_attack_time is %f "
				"pg_hold_open_time is %f "
				"hold_closed_time is %f "
				"pg_closed_gain is %f pg_open_threshold is %f "
				"pg_close_threshold is %f pg_tau is %f",
				audiocorePG->release_time,
				audiocorePG->attack_time,
				audiocorePG->hold_open_time,
				audiocorePG->hold_closed_time,
				audiocorePG->closed_gain,
				audiocorePG->open_threshold,
				audiocorePG->close_threshold,
				audiocorePG->tau);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setupPostgain(d->audiocore,
				audiocorePG->release_time,
				audiocorePG->attack_time,
				audiocorePG->hold_open_time,
				audiocorePG->hold_closed_time,
				audiocorePG->closed_gain,
				audiocorePG->open_threshold,
				audiocorePG->close_threshold,
				audiocorePG->tau);

	if (err) {
		warning("audiocore: setting noise gate failed: %m\n",
				err);
	}
	else {
		debug("audiocore: postgain set to pg_release_time is %f "
				"pg_attack_time is %f "
				"pg_hold_open_time is %f "
				"hold_closed_time is %f "
				"pg_closed_gain is %f pg_open_threshold is %f "
				"pg_close_threshold is %f pg_tau is %f\n",
				audiocorePG->release_time,
				audiocorePG->attack_time,
				audiocorePG->hold_open_time,
				audiocorePG->hold_closed_time,
				audiocorePG->closed_gain,
				audiocorePG->open_threshold,
				audiocorePG->close_threshold,
				audiocorePG->tau);
	}

	return err;
}


/**
 * Adding a filter to equalizer
 *
 * @param	eq		handler of equalizer
 * @param	filter	type of filter
 * @param	fc		frequence
 * @param	gain	gain
 * @param	q		q factor
 *
 * @return	 0		if success
 *			-1		unkown filter type
 */
static int addFilter(eq_config_handle eq, int filter,
		float fc, float gain, float q)
{
	int err;

	switch (filter) {
	case EQ_FILTER_PEAK:
		err = addPeakFilter(eq, fc, gain, q);
		break;
	case EQ_FILTER_HIGH_SHELV:
		err = addShelvingFilter(eq, false, fc, gain, q, 0.0);
		break;
	case EQ_FILTER_LOW_SHELV:
		err = addShelvingFilter(eq, true, fc, gain, q, -0.0);
		break;
	default:
		err = -1;
		break;
	}

	return err;
}


/**
 * Parse the equalizer config string and add the filters
 *
 * FilterType,frequency,gain,q
 * The parameter string looks like this
 * LS,500,10,5|PK,1000,10,5|PK,2000,10,5|PK,4000,10,5|HS,6000,10,5"
 * Means: low shelv filter	500Hz	1.0dB	0.5
 *			   peak filter	1000Hz	1.0dB	0.5
 *			   ...
 *
 * @param	eq		handle to equalizer
 * @param	param	config string for filters
 *
 * @return	 0		on success
 *			-1		unknown or missing filter type
 *			-2		parsing prameter failed
 *			EINVAL	eq or param is NULL pointer
 */
static int parseEqParameter(eq_config_handle eq, const char *param)
{
	int err;
	const char *ptr;
	float fc;
	float gain;
	float q;

	if (param == NULL)
		return EINVAL;

	err = 0;
	ptr = param;

	while (ptr && !err) {
		int filter = -1;

		/*Parse filter type */
		if (!strncmp(ptr, "PK", 2))
			filter = EQ_FILTER_PEAK;
		else if (!strncmp(ptr, "HS", 2))
			filter = EQ_FILTER_HIGH_SHELV;
		else if (!strncmp(ptr, "LS", 2))
			filter = EQ_FILTER_LOW_SHELV;
		else
			return -1;

		ptr = strchr(ptr, ',');
		if (!ptr)
			return -2;

		ptr += 1;
		fc = atoi(ptr);

		ptr = strchr(ptr, ',');
		if (!ptr)
			return -2;

		ptr += 1;
		gain = (atoi(ptr) / 10.0);

		ptr = strchr(ptr, ',');
		if (!ptr)
			return -2;

		ptr += 1;
		q = (atoi(ptr) / 10.0);

		ptr = strchr(ptr, '|');
		if (ptr)
			ptr += 1;

		err = addFilter(eq, filter, fc, gain, q);
	}


	return err;
}


/**
 * Set audiocore microphone filter for WS devices
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_ws_filter(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0) {
			d->ws_filter_enabled = value;
		}
		else {
			err = -1;
		}
	}
	else {
		re_hprintf(pf, "WS microphone filter is %d",
				d->ws_filter_enabled);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		err = enableWsMicEqualizer(d->audiocore,
				d->ws_filter_enabled);

	if (err) {
		warning("audiocore: setting WS microphone filter failed: %m\n",
				err);
	}
	else {
		debug("audiocore: WS microphone filter set to %d\n",
				d->ws_filter_enabled);
	}

	return err;
}


/**
 * Set audiocore microphone filter for ET962/ET970 devices
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_et962_filter(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0) {
			d->et962_filter_enabled = value;
		}
		else {
			err = -1;
		}
	}
	else {
		re_hprintf(pf, "ET962 microphone filter is %d",
				d->et962_filter_enabled);
	}

	/* Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		err = enableET962HLsEqualizer(d->audiocore,
				d->et962_filter_enabled);

	if (err) {
		warning("audiocore: setting ET962 microphone filter failed "
				"(%m)\n", err);
	}
	else {
		debug("audiocore: ET962 microphone filter set to %d\n",
				d->et962_filter_enabled);
	}

	return err;
}

/**
 * Set audiocore microphone equalizer
 *
 * An empty string disables the equalizer.
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			 1		disable equalizer
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_mic_equalizer(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;

	(void)pf;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (d->mic_eq_config) {
			destroyEqualizerConfiguration(
					d->mic_eq_config);
		}

		d->mic_eq_config = createEqualizerConfiguration();
		err = parseEqParameter(d->mic_eq_config,
				carg->prm);
		if (err)
			return -1;
	}
	else {
		if (d->audiocore)
			enableMicEqualizer(d->audiocore, false);
		destroyEqualizerConfiguration(d->mic_eq_config);
		d->mic_eq_config = 0;
		info("audiocore: disable microphone equalizer\n");
		return 1;
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore) {
		err = updateMicEqualizerConfig(d->audiocore,
				d->mic_eq_config);
		if (!err)
			err = enableMicEqualizer(d->audiocore,
					(d->mic_eq_config != 0));
	}

	if (err) {
		warning("audiocore: setting microphone equalizer failed: %m\n",
				err);
	}
	else {
		debug("audiocore: microphone equalizer set\n");
	}

	return err;
}


/**
 * Set audiocore loudspeaker equalizer
 *
 * An empty string disables the equalizer.
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			 1		disable equalizer
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_ls_equalizer(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;

	(void)pf;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (d->ls_eq_config) {
			destroyEqualizerConfiguration(
					d->ls_eq_config);
		}
		d->ls_eq_config = createEqualizerConfiguration();
		err = parseEqParameter(
				d->ls_eq_config, carg->prm);
		if (err)
			return -1;
	}
	else {
		if (d->audiocore)
			enableLsEqualizer(d->audiocore, false);
		destroyEqualizerConfiguration(d->ls_eq_config);
		d->ls_eq_config = 0;
		info("audiocore: disable loudspeaker equalizer\n");
		return 1;
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore) {
		err = updateLsEqualizerConfig(d->audiocore,
				d->ls_eq_config);
		if (!err)
			err = enableLsEqualizer(d->audiocore,
					(d->ls_eq_config != 0));
	}

	if (err) {
		warning("audiocore: setting loudspeaker equalizer failed "
				"(%m)\n", err);
	}
	else {
		debug("audiocore: loudspeaker equalizer set\n");
	}

	return err;
}


/**
 * Set audiocore idle audio (bypass mic to ls) enable/disable
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument, cmd_arg->prm should be 1 or 0
 *
 * @return	 0		if success
 *			EINVAL	audiocore or argument not initialized
 */
static int com_set_idle_audio(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool enable = 0;
	int result = 0;
	(void) pf;

	if (!d || !carg)
		return EINVAL;

	if (!str_isset(carg->prm))
		return EINVAL;

	result = str_bool(&enable, carg->prm);
	if (result) {
		return EINVAL;
	}

	d->idleAudio_enabled = enable;
	if (d->audiocore) {
		if (enable && !d->call_count) {
			result = enableIdleAudio(d->audiocore,
					true);
		}
		else {
			result = enableIdleAudio(d->audiocore,
					false);
		}
	}

	return result ? EINVAL : 0;
}


/**
 * Set audiocore live sound pressure level measurement enable/disable
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument, cmd_arg->prm should be 1 or 0
 *
 * @return	 0		if success
 *			EINVAL	audiocore or argument not initialized
 */
static int com_set_live_sound_pressure_level(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool enable = 0;
	int result = 0;
	(void) pf;

	if (!d || !carg)
		return EINVAL;

	if (!str_isset(carg->prm))
		return EINVAL;

	result = str_bool(&enable, carg->prm);
	if (result) {
		return EINVAL;
	}

	d->lspl_enabled = enable;
	if (d->audiocore) {
		if (enable && !d->call_count) {
			result = enableAudioMonitoringMeasurement(
					d->audiocore, true);
		}
		else if (!enable) {
			result = enableAudioMonitoringMeasurement(
					d->audiocore, false);
		}
	}

	return result ? EINVAL : 0;
}


/**
 * Get current audiocore live sound pressure level
 *
 * @param pf		Print handler for debug output
 * @param arg		unused
 *
 * @return	 0		if success
 *			EINVAL	audiocore or argument not initialized
 */
static int com_get_live_sound_pressure_level(struct re_printf *pf, void *arg)
{
	unsigned int level = 0;
	unsigned int max = 0;
	int result = EINVAL;
	(void) arg;

	if (!d)
		return EINVAL;

	if (d->audiocore) {
		result = getAudioMonitoringSPL(d->audiocore,
				&level, &max);
	}

	if (!result)
		re_hprintf(pf, "Live SPL is %u max %u\n", level, max);
	else
		re_hprintf(pf, "No live SPL available\n");

	return result ? EINVAL : 0;
}

/**
 * Set audiocore audio monitoring enable/disable
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument, cmd_arg->prm should be 1 or 0
 *
 * @return	 0		if success
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_audiomonitoring(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool enable = 0;
	int result = 0;
	(void) pf;

	if (!d || !carg)
		return EINVAL;

	if (!str_isset(carg->prm))
		return EINVAL;

	result = str_bool(&enable, carg->prm);
	if (result) {
		return -1;
	}

	d->am_enabled = enable;
	if (d->audiocore) {
		if (enable && !d->call_count) {
			result = enableAudioMonitoringAlarm(
					d->audiocore, true);
		}
		else if (!enable) {
			result = enableAudioMonitoringAlarm(
					d->audiocore, false);
		}
	}

	return result ? EINVAL : 0;
}


/**
 * Set audiocore audio monitoring sound pressure level threshold
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument, cmd_arg->prm should be 1 or 0
 *
 * @return	 0		if success
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_spl_threshold(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	unsigned int value = 0;
	int result = 0;
	(void) pf;

	if (!d || !carg)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = (unsigned int)atoi(carg->prm);
		d->am_spl_threshold = value;
		if (d->audiocore)
			result = setAudioMonitoringSPLThreshold(
					d->audiocore, value);
	}

	return result ? EINVAL : 0;
}


/**
 * Set audiocore audio monitoring activation time for sound pressure level
 * threshold
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument, cmd_arg->prm should be 1 or 0
 *
 * @return	 0		if success
 *			-1		failed to cast given arguments
 *			EINVAL	audiocore not initialized
 */
static int com_set_spl_threshold_time(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	unsigned int value = 0;
	int result = 0;
	(void) pf;

	if (!d || !carg)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = (unsigned int)atoi(carg->prm);
		d->am_spl_threshold_time = value;
		if (d->audiocore)
			result = setAudioMonitoringSPLThresholdTime(
					d->audiocore, value);
	}

	return result ? EINVAL : 0;
}


/**
 * Set audiocore audio monitoring microphone sensitivity
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument, cmd_arg->prm 0 or higher
 *
 * @return	0	if success
 *		EINVAL	audiocore not initialized
 */
static int com_set_mic_sensitivity(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int value = 0;
	int result = 0;
	(void) pf;

	if (!d || !carg)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = atoi(carg->prm);
		d->am_mic_sensitivity = value;
		if (d->audiocore)
			result = setAudioMonitoringMicSensitivity(
					d->audiocore, value);
	}

	return result ? EINVAL : 0;
}


/**
 * Enable audiocore lsmic surveillance
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_en_lsmic(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;
	(void) pf;

	if (!d || !d->audiocore)
		return EINVAL;

	if (!str_isset(carg->prm))
		return EINVAL;

	err = str_bool(&value, carg->prm);
	if (err)
		return -1;

	if (value && !d->call_count) {
		info("enable lsmic\n");
		err = enableLSMic(d->audiocore, true);
	}
	else if (!value) {
		info("disable lsmic\n");
		err = enableLSMic(d->audiocore, false);
	}

	if (err)
		warning("audiocore: %s lsmic surveillance failed: %m\n",
				value ? "enable" : "disable", err);

	return err;
}


/**
 * Set audiocore lsmic surveillance fault retry delay
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_set_lsmic_retryinterval(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;
	unsigned int value;
	(void) pf;

	if (!d)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (sscanf(carg->prm, "%u", &value) != 1) {
			warning("audiocore: setting lsmic retry interval "
					"failed\n");
			return EINVAL;
		}

		info("set lsmic retry interval: %i\n", value);
		d->ls_mic_retryinterval = value;
		if (d->audiocore)
			err = setLSMicRetryInterval(d->audiocore,
					d->ls_mic_retryinterval);

		if (err)
			warning("audiocore: setting lsmic retry interval "
					"failed (%m)\n", err);
	}

	return err;
}


/**
 * Set audiocore lsmic surveillance noise playback volume
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_set_lsmic_noise_volume(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;
	int value;
	(void) pf;

	if (!d)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (sscanf(carg->prm, "%i", &value) != 1) {
			warning("audiocore: setting lsmic surveillance noise"
					" volume failed\n");
			return EINVAL;
		}

		info("set lsmic noise volume: %i\n", value);
		d->ls_mic_noise_volume = value;
		if (d->audiocore)
			err = setLSMicNoiseVolume(d->audiocore,
					d->ls_mic_noise_volume);

		if (err)
			warning("audiocore: setting lsmic surveillance noise"
					" volume failed: %m\n", err);
	}

	return err;
}

/**
 * Set audiocore line monitoring
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *		-1		failed to cast given argument to boolean
 *			EINVAL	audiocore not initialized
 */
static int com_set_line_monitoring(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	bool value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (str_bool(&value, carg->prm) == 0) {
			d->lm_enabled = value;
		}
		else {
			err = -1;
		}
	}
	else {
		re_hprintf(pf, "Line Monitoring is %d",
				d->lm_enabled);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		enableLineMonitoring(d->audiocore,
				d->lm_enabled);

	if (err) {
		warning("audiocore: enable line monitoring failed: %m\n", err);
	}
	else {
		debug("audiocore: line monitoring set to %d\n",
				d->lm_enabled);
	}

	return err;
}


/**
 * Get current state of audiocore line monitoring
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_is_line_monitoring_enabled(struct re_printf *pf, void *arg)
{
	(void)arg;

	if (d == NULL)
		return EINVAL;

	re_hprintf(pf, "Audiocore Line Monitoring is %d",
			isLineMonitoringEnabled(d->audiocore));

	return 0;
}


/**
 * Start one measurement of audiocore line monitoring
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_start_line_monitoring_single_shot(struct re_printf *pf,
		void *arg)
{
	(void)arg;

	if (d == NULL)
		return EINVAL;

	if (isLineMonitoringEnabled(d->audiocore)) {
		startLineMonitoringMeasurement(d->audiocore);
		re_hprintf(pf, "Started Audiocore Line Monitoring");
	}
	else {
		re_hprintf(pf, "Audiocore Line Monitoring is disabled");
	}

	return 0;
}

/**
 * Start only one measurement of audiocore line monitoring
 * without any retry on error.
 * This should be used only to initiate the values.
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_start_line_monitoring_one_shot(struct re_printf *pf, void *arg)
{
	(void)arg;

	if (d == NULL)
		return EINVAL;

	if (isLineMonitoringEnabled(d->audiocore)) {
		startLineMonitoringConfigurationMeasurement(
				d->audiocore);
		re_hprintf(pf, "Started Audiocore one shot Line Monitoring");
	}
	else {
		re_hprintf(pf, "Audiocore Line Monitoring is disabled");
	}

	return 0;
}

/**
 * Set audiocore line monitoring measurement interval in seconds
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_set_line_monitoring_measurement_interval(struct re_printf *pf,
		void *arg)
{
	const struct cmd_arg *carg = arg;
	unsigned int value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = (unsigned int)atoi(carg->prm);
		d->lm_measurement_interval = value;
	}
	else {
		re_hprintf(pf, "Line Monitoring Measurement Interval is %u",
				d->lm_measurement_interval);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setLineMonitoringMeasurementInterval(d->audiocore,
				d->lm_measurement_interval);

	if (err) {
		warning("audiocore: setting line monitoring measurement "
				"interval failed: %m\n", err);
	}
	else {
		debug("audiocore: line monitoring measurement interval set "
				"to %u\n",
				d->lm_measurement_interval);
	}

	return err;
}


/**
 * Set audiocore line monitoring station
 * @param pf		Print handler for debug output
 * @param arg		Command argument as station use100V
 *					station: AF50H, AF125H, AF250H
 *					use100V: on/off or 1/0
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_set_line_monitoring_station(struct re_printf *pf,
		void *arg)
{
	const struct cmd_arg *carg = arg;
	int cnt;
	bool value;
	char station[20] = {0};
	char use100V[10] = {0};
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		cnt = sscanf(carg->prm, "%s %s", station, use100V);

		if (cnt == 2) {
			if (str_bool(&value, use100V) == 0) {
				d->lm_use100V = value;

				if (!strcmp(station, "AF50H")) {
					d->lm_station = AC_LM_AF50;
				}
				else if (!strcmp(station, "AF125H")) {
					d->lm_station = AC_LM_AF125;
				}
				else if (!strcmp(station, "AF250H")) {
					d->lm_station = AC_LM_AF250;
				}
				else if (!strcmp(station, "AF500H")) {
					d->lm_station = AC_LM_AF500;
				}
				else {
					err = EINVAL;
				}
			}
		}
		else {
			err = EINVAL;
		}
	}
	else {
		re_hprintf(pf, "Line Monitoring Station is %u use100V %d",
				d->lm_station, d->lm_use100V);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setLineMonitoringStation(d->audiocore,
				d->lm_station, d->lm_use100V);

	if (err) {
		warning("audiocore: setting line monitoring station failed "
				"(%m)\n", err);
	}
	else {
		debug("audiocore: line monitoring station set to %u "
				"use100V %d\n",
				d->lm_station, d->lm_use100V);
	}

	return err;
}


/**
 * Set audiocore line monitoring input
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument (default, none, isens, usensp, usensm)
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_set_line_monitoring_input(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		if (!strcmp(carg->prm, "none")) {
			d->lm_input = AC_LM_INPUT_NONE;
		}
		else if (!strcmp(carg->prm, "isens")) {
			d->lm_input = AC_LM_INPUT_CURRENT;
		}
		else if (!strcmp(carg->prm, "usensp")) {
			d->lm_input = AC_LM_INPUT_VOLTAGE_1;
		}
		else if (!strcmp(carg->prm, "usensm")) {
			d->lm_input = AC_LM_INPUT_VOLTAGE_2;
		}
		else { /*default */
			d->lm_input = AC_LM_INPUT_DEFAULT;
		}
	}
	else {
		re_hprintf(pf, "Line Monitoring Input is %u",
				d->lm_input);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setLineMonitoringInput(d->audiocore,
				d->lm_input);

	if (err) {
		warning("audiocore: setting line monitoring input failed "
				"(%m)\n", err);
	}
	else {
		debug("audiocore: line monitoring input set to %u\n",
				d->lm_input);
	}

	return err;
}

/**
 * Set audiocore line monitoring reference impedance
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_set_line_monitoring_reference_impedance(struct re_printf *pf,
		void *arg)
{
	const struct cmd_arg *carg = arg;
	unsigned int value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = (unsigned int)atoi(carg->prm);
		d->lm_reference_impedance = value;
	}
	else {
		re_hprintf(pf, "Line Monitoring Reference Impedance is %u",
				d->lm_reference_impedance);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setLineMonitoringReferenceImpedance(d->audiocore,
				d->lm_reference_impedance);

	if (err) {
		warning("audiocore: setting line monitoring reference "
				"impedance failed: %m\n", err);
	}
	else {
		debug("audiocore: line monitoring reference impedance "
				"set to %u\n",
				d->lm_reference_impedance);
	}

	return err;
}


/**
 * Set audiocore line monitoring impedance tolerance
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_set_line_monitoring_impedance_tolerance(struct re_printf *pf,
		void *arg)
{
	const struct cmd_arg *carg = arg;
	unsigned int value;
	int err = 0;

	if (d == NULL)
		return EINVAL;

	if (str_isset(carg->prm)) {
		value = (unsigned int)atoi(carg->prm);
		d->lm_impedance_tolerance = value;
	}
	else {
		re_hprintf(pf, "Line Monitoring Impedance Tolerance is %u",
				d->lm_impedance_tolerance);
	}

	/*Set value on the fly if audiocore is active */
	if (!err && d->audiocore)
		setLineMonitoringImpedanceTolerance(d->audiocore,
				d->lm_impedance_tolerance != 0);

	if (err) {
		warning("audiocore: setting line monitoring impedance "
				"tolerance failed: %m\n", err);
	}
	else {
		debug("audiocore: line monitoring impedance tolerance "
				"set to %u\n",
				d->lm_impedance_tolerance);
	}

	return err;
}


/**
 * Measured audiocore line monitoring impedance
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_get_line_monitoring_measured_impedance(struct re_printf *pf,
		void *arg)
{
	unsigned int value;
	int err = 0;
	(void)arg;

	if (d == NULL)
		return EINVAL;

	err = getLineMonitoringMeasuredImpedance(d->audiocore, &value);

	if (!err) {
		re_hprintf(pf, "Line Monitoring Measured Impedance is %u",
				value);
	}
	else {
		re_hprintf(pf, "Line Monitoring Measure Impedance failed");
	}

	return err;
}


/**
 * Measured audiocore line monitoring resistance against ground
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_get_line_monitoring_ground_fault_resistance(
		struct re_printf *pf, void *arg)
{
	unsigned int value;
	int err = 0;
	(void)arg;

	if (d == NULL)
		return EINVAL;

	err = getLineMonitoringMeasuredGroundfaultResistance
		(d->audiocore, &value);

	if (!err) {
		re_hprintf(pf, "Line Monitoring Measured Ground Fault "
				"Resistance is %u", value);
	}
	else {
		re_hprintf(pf, "Line Monitoring Measure Ground Fault "
				"Resistance failed");
	}

	return err;
}

/**
 * Get input requested by audiocore
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			EINVAL	audiocore not initialized
 */
static int com_get_line_monitoring_requested_input(struct re_printf *pf
		, void *arg)
{
	AC_LINEMONITORING_INPUT input;
	int err = 0;
	(void)arg;

	if (d == NULL)
		return EINVAL;

	err = getLineMonitoringRequestedInput
		(d->audiocore, &input);

	if (!err) {
		re_hprintf(pf, "Line Monitoring requested input is %s\n",
				lm_inputstr(input));
	}
	else {
		re_hprintf(pf, "Line Monitoring request input failed\n");
	}

	return err;
}

static const struct cmd cmdv[] = {

	{"com_ac_set_ec", 0, CMD_PRM, "Set audiocore echo cancellation",
		com_set_echo_cancellation},
	{"com_ac_en_ns", 0, CMD_PRM,"Enable audiocore noise suppression",
		com_en_noise_suppression},
	{"com_ac_set_ivc", 0, CMD_PRM,"Set audiocore IVC",
		com_set_ivc},
	{"com_ac_en_ng", 0, CMD_PRM,"Enable audiocore noise gate",
		com_en_noise_gate},
	{"com_ac_en_pg", 0, CMD_PRM,"Enable audiocore postgain",
		com_en_postgain},
	{"com_ac_en_rec", 0, CMD_PRM,"Enable audiocore REC",
		com_en_rec},
	{"com_ac_en_dbg", 0, CMD_PRM,"Enable audiocore debug mode",
		com_set_debug_mode},
	{"com_ac_set_vl", 0, CMD_PRM,"Set audiocore volume level",
		com_set_volume_level},
	{"com_ac_set_ns", 0, CMD_PRM,"Set audiocore noise suppression",
		com_set_noise_suppression},
	{"com_ac_set_ns_rs",0, CMD_PRM,
		"Set audiocore noise suppression rec scale",
		com_set_noise_suppression_rec_scale},
	{"com_ac_set_mc", 0, CMD_PRM,"Set audiocore microphone compressor",
		com_set_microphone_compressor},
	{"com_ac_set_mcg", 0, CMD_PRM,"Set audiocore microphone compressor "
		"gain",  com_set_microphone_compressor_gain},
	{"com_ac_set_mpg", 0, CMD_PRM,"Set audiocore microphone post gain",
		com_set_microphone_post_gain},
	{"com_ac_set_lc", 0, CMD_PRM,"Set audiocore loudspeaker compressor",
		com_set_set_loudspeaker_compressor},
	{"com_ac_set_lcg", 0, CMD_PRM,"Set audiocore loudspeaker compressor "
		"gain", com_set_loudspeaker_compressor_gain},
	{"com_ac_set_ng", 0, CMD_PRM,"Set audiocore noise gain",
		com_set_noise_gate},
	{"com_ac_set_pg", 0, CMD_PRM,"Set audiocore postgain",
		com_set_postgain},
	{"com_ac_ws_filter", 0, CMD_PRM,"Set the WS microphone filter",
		com_set_ws_filter},
	{"com_ac_et962_filter", 0, CMD_PRM,"Set the ET962 microphone filter",
		com_set_et962_filter},
	{"com_ac_set_mic_eq", 0, CMD_PRM,"Set the microphone equalizer",
		com_set_mic_equalizer},
	{"com_ac_set_ls_eq", 0, CMD_PRM,"Set the loudspeaker equalizer",
		com_set_ls_equalizer},
	{"com_ac_set_am", 0, CMD_PRM,"Set audiocore audio monitoring",
		com_set_audiomonitoring},
	{"com_ac_set_am_th",0,  CMD_PRM,"Set audiocore am spl threshold",
		com_set_spl_threshold},
	{"com_ac_set_am_time", 0, CMD_PRM,"Set audiocore am spl th time",
		com_set_spl_threshold_time},
	{"com_ac_set_mic_sens", 0, CMD_PRM,"Set microphone sensitivity",
		com_set_mic_sensitivity},
	{"com_ac_set_lspl", 0, CMD_PRM,"Set audiocore live sound pressure "
		"level", com_set_live_sound_pressure_level},
	{"com_ac_get_lspl", 0, 0, "Get current live sound pressure level",
		com_get_live_sound_pressure_level},
	{"com_ac_set_idle", 0, CMD_PRM,"Set idle audio (audio bypassing)",
		com_set_idle_audio},
	{"com_ac_en_lsmic", 0, CMD_PRM,"Enable lsmic surveillance",
		com_en_lsmic},
	{"com_ac_set_lsmic_ri", 0, CMD_PRM,"Set lsmic retry interval",
		com_set_lsmic_retryinterval},
	{"com_ac_set_lsmic_nv", 0, CMD_PRM,"Set lsmic noise volume",
		com_set_lsmic_noise_volume},
	{"com_ac_en_lm",  0, CMD_PRM,"Enable line monitoring",
		com_set_line_monitoring},
	{"com_ac_lm_en",  0, 0,  "Is LM enabled",
		com_is_line_monitoring_enabled},
	{"com_ac_lm_single", 0, 0,  "LM single shot measure",
		com_start_line_monitoring_single_shot},
	{"com_ac_lm_oneshot", 0, 0,  "LM only one measure",
		com_start_line_monitoring_one_shot},
	{"com_ac_set_lm_inter", 0, CMD_PRM,"Set LM interval",
		com_set_line_monitoring_measurement_interval},
	{"com_ac_set_lm_station",0, CMD_PRM,"Set LM station",
		com_set_line_monitoring_station},
	{"com_ac_set_lm_inp", 0, CMD_PRM,"Set LM input",
		com_set_line_monitoring_input},
	{"com_ac_lm_ref_imp", 0, CMD_PRM,"Set LM reference impedance",
		com_set_line_monitoring_reference_impedance},
	{"com_ac_lm_imp_tol", 0, CMD_PRM,"Set LM impedance tolerance",
		com_set_line_monitoring_impedance_tolerance},
	{"com_ac_get_lm_imp", 0, 0,  "Measure LM impedance",
		com_get_line_monitoring_measured_impedance},
	{"com_ac_get_lm_gft", 0, 0,  "Measure LM ground fault",
		com_get_line_monitoring_ground_fault_resistance},
	{"com_ac_get_lm_rin", 0, 0,  "Get LM requested input",
		com_get_line_monitoring_requested_input},


};


static void audio_detected (unsigned int spl_peek, unsigned int peek_duration)
{
	send_event("audiocore", "audio_detected", "spl=%u, duration=%u",
			spl_peek, peek_duration);
}


static void lsmic_result (bool detected)
{
	send_event("audiocore", "lsmic_result", "%u", detected ? 1 : 0);
}


static void lm_input_cb (AC_LINEMONITORING_INPUT input)
{
	send_event("audiocore", "linemonitoring input", "%s",
			lm_inputstr(input));
}


static void lm_cb(AC_LINEMONITORING_ERROR error)
{
	if (error == AC_LM_ERROR_OK) {
		send_event("audiocore", "linemonitoring error", "%s",
				lm_errorstr(error));
	}
	else {
		if (error & AC_LM_ERROR_INTERRUPTION)
			send_event("audiocore", "linemonitoring error",
					"%s", lm_errorstr(error &
						AC_LM_ERROR_INTERRUPTION));
		if (error & AC_LM_ERROR_IMPEDANCE_HIGH)
			send_event("audiocore", "linemonitoring error",
					"%s", lm_errorstr(error &
						AC_LM_ERROR_IMPEDANCE_HIGH));
		if (error & AC_LM_ERROR_IMPEDANCE_LOW)
			send_event("audiocore", "linemonitoring error",
					"%s", lm_errorstr(error &
						AC_LM_ERROR_IMPEDANCE_LOW));
		if (error & AC_LM_ERROR_SHORT_CIRCUIT)
			send_event("audiocore", "linemonitoring error",
					"%s", lm_errorstr(error &
						AC_LM_ERROR_SHORT_CIRCUIT));
		if (error & AC_LM_ERROR_GROUND_FAULT)
			send_event("audiocore", "linemonitoring error",
					"%s", lm_errorstr(error &
						AC_LM_ERROR_GROUND_FAULT));
		if (error & AC_LM_ERROR_AMP_FAULT)
			send_event("audiocore", "linemonitoring error",
					"%s", lm_errorstr(error &
						AC_LM_ERROR_AMP_FAULT));
	}
}

static void enc_destructor(void *arg)
{
	struct enc_st *st = (struct enc_st *)arg;

	info("audiocore: enc_destructor\n");

	if (st->st && st->st->audiocore)
		stopAudiocore(st->st->audiocore);

	list_unlink(&st->af.le);
}


static void dec_destructor(void *arg)
{
	struct dec_st *st = (struct dec_st *)arg;

	info("audiocore: dec_destructor\n");

	if (st->st && st->st->audiocore)
		stopAudiocore(st->st->audiocore);
	list_unlink(&st->af.le);
}


static void audiocore_st_destructor(void *arg)
{
	struct audiocore_st *st = (struct audiocore_st *)arg;

	info("audiocore: audiocore_st_destructor\n");
	st->size_delay = 0;

	destroyAudioCore(st->audiocore);
	st->audiocore = 0;
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
		struct call *call, const char *prm, void *arg)
{
	uint32_t cnt;
	struct audiocore_st *state = (struct audiocore_st *)arg;

	(void) ua;
	(void) call;
	(void) prm;

	cnt = uag_call_count();
	switch (ev) {
	case UA_EVENT_CALL_CLOSED:
		if (cnt == 1) {
			/* clear JitterBuffers */
			jbReset(state->audiocore);
			/* start audio monitoring */
			if (state->am_enabled && state->audiocore)
				enableAudioMonitoringAlarm(
						state->audiocore, true);
			if (state->lspl_enabled && state->audiocore)
				enableAudioMonitoringMeasurement(
						state->audiocore, true);
			if (state->idleAudio_enabled && state->audiocore)
				enableIdleAudio(state->audiocore, true);
		}
		state->call_count = cnt - 1;
		break;
	case UA_EVENT_CALL_INCOMING:
	case UA_EVENT_CALL_OUTGOING:
	case UA_EVENT_CALL_RINGING:
		if (!state->call_count && state->audiocore)
			jbReset(state->audiocore);
		break;
	case UA_EVENT_CALL_PROGRESS:
	case UA_EVENT_CALL_ESTABLISHED:
		if (!state->call_count && state->audiocore)
			jbReset(state->audiocore);
		if (cnt > 0 && !state->call_count && state->audiocore) {
			/* stop idle audio monitoring */
			enableLSMic(state->audiocore, false);
			enableAudioMonitoringAlarm(state->audiocore, false);
			enableAudioMonitoringMeasurement(
					state->audiocore, false);
			enableIdleAudio(state->audiocore, false);
		}
		state->call_count = cnt;
		break;
	default:
		break;
	}
}


static int audiocore_init(struct audiocore_st* st)
{
	int err;

	info("audiocore: audiocore_init\n");

	if (st->samplerate != st->samplerate_prev) {
		destroyAudioCore(st->audiocore);
		info("audiocore: create audiocore with samplerate=%d\n",
				st->samplerate);
		st->audiocore =
			createAudioCore(st->samplerate, st->samplerate / 2,
				(float)st->framesize / (float)st->samplerate,
				(float)st->tail_length_ms / 1000.0f,
				st->noise_suppression,
				st->echo_cancellation,
				st->noise_suppression_enabled,
				st->noise_gate_enabled,
				st->postgain_enabled,
				st->mic_compressor.gain,
				st->ls_compressor.gain,
				st->debug_enable, st->volume_level,
				st->mic_eq_config,
				st->ls_eq_config, st->mic_post_gain);
		if (!st->audiocore) {
			err = ENOMEM;
			return err;
		}
		st->samplerate_prev = st->samplerate;
	}

	/* set callback functions for audio monitoring and ls-mic surv */
	err = setLSMicCallback(st->audiocore, lsmic_result);
	err |= setLSMicRetryInterval(st->audiocore, st->ls_mic_retryinterval);
	err |= setLSMicNoiseVolume(st->audiocore, st->ls_mic_noise_volume);
	err |= setAudioMonitoringCallback(st->audiocore, audio_detected);
	err |= setAudioMonitoringSPLThreshold(st->audiocore,
			st->am_spl_threshold);
	err |= setAudioMonitoringMicSensitivity(st->audiocore,
			st->am_mic_sensitivity);
	if (st->am_enabled && !st->call_count)
		enableAudioMonitoringAlarm(d->audiocore,	true);
	err |= setAudioMonitoringSPLThresholdTime(st->audiocore,
			st->am_spl_threshold_time);
	if (st->lspl_enabled && !st->call_count)
		enableAudioMonitoringMeasurement(d->audiocore, true);
	if (st->idleAudio_enabled && !st->call_count)
		enableIdleAudio(d->audiocore, true);

	err |= setLineMonitoringInputCallback(st->audiocore, lm_input_cb);
	err |= setLineMonitoringCallback(st->audiocore, lm_cb);
	setLineMonitoringMeasurementInterval(st->audiocore,
			st->lm_measurement_interval);
	setLineMonitoringStation(st->audiocore, st->lm_station,
			st->lm_use100V);
	setLineMonitoringReferenceImpedance(st->audiocore,
			st->lm_reference_impedance);
	setLineMonitoringImpedanceTolerance(st->audiocore,
			st->lm_impedance_tolerance);

	if (err)
		warning("audiocore: could not set callback handler\n");

	setNoiseSuppressionParameter(d->audiocore,
			d->noise_suppression);
	setNoiseSuppression(d->audiocore,
			d->noise_suppression_enabled);
	setNoiseSuppressionRecScaling(d->audiocore,
			d->noise_suppression_rec_scale);
	setEchoCancellation(d->audiocore,
			d->echo_cancellation);
	enableNoiseGate(d->audiocore,
			d->noise_gate_enabled);
	enablePostgain(d->audiocore,
			d->postgain_enabled);
	setMicCompressor(d->audiocore,
			d->mic_compressor.gain,
			d->mic_compressor.thresh_lo,
			d->mic_compressor.thresh_hi,
			d->mic_compressor.use_noise_gain,
			d->mic_compressor.noise_gain);
	setLsCompressor(d->audiocore,
			d->ls_compressor.gain,
			d->ls_compressor.thresh_lo,
			d->ls_compressor.thresh_hi,
			d->ls_compressor.use_noise_gain,
			d->ls_compressor.noise_gain);
	enableDebugMode(d->audiocore, d->debug_enable);
	notifyVolumeLevel(d->audiocore, d->volume_level);
	enableWsMicEqualizer(d->audiocore, st->ws_filter_enabled);
	enableET962HLsEqualizer(d->audiocore, st->et962_filter_enabled);
	enableMicEqualizer(st->audiocore, (st->mic_eq_config != 0));
	enableLsEqualizer(st->audiocore, (st->ls_eq_config != 0));
	enableREC(d->audiocore,
			d->rec_enabled);

	/*Set IVC because it is not in the init function */
	setIVC(st->audiocore, st->ivc_enabled);

	return 0;
}


static int aec_alloc(struct audiocore_st *st, struct aufilt_prm *prm)
{
	int err = 0;

	if (!prm)
		return EINVAL;

	if (!st)
		return EINVAL;

	info("audiocore: aec_alloc\n");

	st->samplerate = prm->srate;
	err = audiocore_init(st);
	if (err)
		return err;

	return startAudiocore(st->audiocore);
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
		const struct aufilt *af, struct aufilt_prm *prm,
		const struct audio *au)
{
	struct enc_st *st;
	int err;
	(void)au;

	if (!stp || !ctx || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	if (!d)
		return EINVAL;

	info("audiocore: encode_update\n");

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	err = aec_alloc(d, prm);
	if (err) {
		mem_deref(st);
	}
	else {
		st->st = d;
		*stp = (struct aufilt_enc_st *)st;
	}
	return err;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
		const struct aufilt *af, struct aufilt_prm *prm,
		const struct audio *au)
{
	struct dec_st *st;
	int err;
	(void)au;

	if (!stp || !ctx || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	if (!d)
		return EINVAL;

	info("audiocore: decode_update\n");

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	err = aec_alloc(d, prm);
	if (err) {
		mem_deref(st);
	}
	else {
		st->st = d;
		*stp = (struct aufilt_dec_st *)st;
	}

	return err;
}


static int encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct enc_st *est = (struct enc_st *)st;
	struct audiocore_st *state = est->st;

	if (!st || !af)
		return EINVAL;

	/*Bypass mode: Just feed audio through */
	if (state->bypass)
		return 0;

	if (!state->audiocore)
		return 0;


	if (af->sampc)
		jbProcessBz(state->audiocore, af->sampv, af->sampc);

	return 0;
}


static int decode(struct aufilt_dec_st *st,  struct auframe *af)
{
	struct dec_st *dst = (struct dec_st *)st;
	struct audiocore_st *state = dst->st;

	if (!st || !af)
		return EINVAL;

	/*Bypass mode: Just feed audio through */
	if (state->bypass)
		return 0;

	if (!state->audiocore)
		return 0;

	if (af->sampc)
		jbProcessBx(state->audiocore, af->sampv, af->sampc);

	return 0;
}


static struct aufilt audiocore_aec = {
	LE_INIT, "audiocore_aec", encode_update, encode, decode_update, decode
};


static int module_init(void)
{
	struct aufilt_prm prm;
	int err;

	info("audiocore: module_init\n");
	prm.srate = 16000;
	prm.ch = 2;

	if (!d)
		d = mem_zalloc(sizeof(*d),
				audiocore_st_destructor);

	/* Set default values for audiocore */
	if (!d) {
		warning("audiocore: could not allocate audiocore state\n");
		return ENOMEM;
	}

	d->samplerate=prm.srate;
	d->framesize=256;
	d->tail_length_ms=200;
	d->echo_cancellation = true;
	d->noise_suppression = 4;
	d->noise_gate_enabled = false;
	d->startup = 0;
	d->ls_compressor.gain = 0.0f;
	d->ls_compressor.thresh_lo = -60.0f;
	d->ls_compressor.thresh_hi = -30.0f;
	d->ls_compressor.noise_gain = 0.0f;
	d->ls_compressor.use_noise_gain = false;
	d->mic_compressor.gain = 6.0f;
	d->mic_compressor.thresh_lo = -60.0f;
	d->mic_compressor.thresh_hi = -30.0f;
	d->mic_compressor.noise_gain = 0.0f;
	d->mic_compressor.use_noise_gain = false;
	d->mic_post_gain = 0.0f;
	d->noise_suppression_rec_scale = 1.0f;
	d->skipcount = 0;
	d->debug_enable = false;
	d->bypass = false;
	d->volume_level = 8;
	d->audiocore = 0;
	d->ws_filter_enabled = false;
	d->et962_filter_enabled = false;
	d->mic_eq_config = 0;
	d->ls_eq_config = 0;
	d->call_count = uag_call_count();
	d->am_enabled = 0;
	d->am_spl_threshold = 0;
	d->am_spl_threshold_time = 0;
	d->am_mic_sensitivity = 0;
	d->lspl_enabled = 0;
	d->idleAudio_enabled = 0;
	d->rec_enabled = true;
	d->lm_enabled = false;
	d->lm_use100V = false;
	d->lm_measurement_interval = 0;
	d->lm_reference_impedance = 0;
	d->lm_impedance_tolerance = 0;
	d->lm_station = AC_LM_AF50;
	d->lm_input = AC_LM_INPUT_DEFAULT;

	uag_event_register(ua_event_handler, d);

	/* register audio filter */
	aufilt_register(baresip_aufiltl(), &audiocore_aec);

	/* register commands */
	err  = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	if (!err)
		err = audiocore_init(d);

	return err;
}


static int module_close(void)
{
	info("audiocore: module_close\n");
	cmd_unregister(baresip_commands(), cmdv);
	aufilt_unregister(&audiocore_aec);
	uag_event_unregister(ua_event_handler);
	if (d) {
		destroyEqualizerConfiguration(d->mic_eq_config);
		destroyEqualizerConfiguration(d->ls_eq_config);
		mem_deref(d);
	}

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(audiocore) = {
	"audiocore",
	"filter",
	module_init,
	module_close
};
