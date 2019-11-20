/**
 * @file i2s.c freeRTOS I2S audio driver module
 *
 * Copyright (C) 2019 cspiel.at
 */
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "driver/i2s.h"
#include "i2s.h"


/**
 * @defgroup i2s i2s
 *
 * I2S audio driver module for freeRTOS (ESP32 Espressif)
 *
 * This module adds an audio source for I2S MEMs microphone (mono/stereo) and
 * an audio player for I2S class D amplifiers. It was tested with:
 *
 * - ESP32-WROOM from Espressif
 * - Sparkfun I2S Audio Breakout - MAX98357A SF14809 - CLASS D stereo amplifier
 * - Adafruit I2S MEMS Microphone Breakout - SPH0645LM4H
 */


static struct ausrc *ausrc = NULL;
static struct auplay *auplay = NULL;
static enum I2SOnMask _i2s_on = I2O_NONE;


static int i2s_init(void)
{
	int err;

	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      "i2s", i2s_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       "i2s", i2s_play_alloc);


	return err;
}


int i2s_start_bus(uint32_t srate, enum I2SOnMask playrec, uint8_t channels)
{
	esp_err_t err;
	bool start = _i2s_on == I2O_NONE;
	_i2s_on = _i2s_on | playrec;

	if (srate * 4 % DMA_SIZE) {
		warning("i2s: srate*4 % DMA_SIZE != 0\n");
		return EINVAL;
	}

	info("%s start with _i2s_on=%d", __func__, _i2s_on);
	if (start) {
		i2s_config_t i2s_config = {
			.mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX,
			.sample_rate =  srate,
			.bits_per_sample = 32,
			.communication_format = I2S_COMM_FORMAT_I2S |
				I2S_COMM_FORMAT_I2S_MSB,
			.channel_format = channels ? I2S_CHANNEL_FMT_ONLY_RIGHT
			    : I2S_CHANNEL_FMT_RIGHT_LEFT,
			.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
			.dma_buf_count = 2,
			.dma_buf_len = DMA_SIZE,
			.use_apll = 0                       /* disables APLL */
		};

		/* install and start i2s driver */
		err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
		if (err) {
			warning("i2s: could not install i2s driver (%s)",
					esp_err_to_name(err));
			return EINVAL;
		}
		i2s_pin_config_t pins = {
			.bck_io_num = 26,
			.ws_io_num = 25,
			.data_out_num = 22,
			.data_in_num = 23
		};
		i2s_set_pin(I2S_PORT, &pins);

		i2s_zero_dma_buffer(I2S_PORT);
	}

	return 0;
}


void i2s_stop_bus(enum I2SOnMask playrec)
{
	_i2s_on &= (~playrec);

	info("%s _i2s_on=%d", __func__, _i2s_on);
	if (_i2s_on == I2O_NONE)
		i2s_driver_uninstall(I2S_PORT);
}


static int i2s_close(void)
{
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);
	return 0;
}


const struct mod_export DECL_EXPORTS(i2s) = {
	"i2s",
	"sound",
	i2s_init,
	i2s_close
};
