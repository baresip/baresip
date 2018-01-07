/**
 * @file omx.h     Raspberry Pi VideoCoreIV OpenMAX interface
 *
 * Copyright (C) 2016 - 2017 Creytiv.com
 * Copyright (C) 2016 - 2017 Jonathan Sieber
 */

#ifdef RASPBERRY_PI
#include <IL/OMX_Core.h>
#include <IL/OMX_Video.h>
#include <IL/OMX_Broadcom.h>
#else
#include <OMX_Core.h>
#include <OMX_Component.h>
#include <OMX_Video.h>

#undef OMX_VERSION
#define OMX_VERSION 0x01010101
#define OMX_ERROR_NONE 0
#endif

#include <pthread.h>
#include <stdint.h>
#include <string.h>

/* Needed for usleep to appear */
#define _BSD_SOURCE
#include <unistd.h>

struct omx_state {
	OMX_HANDLETYPE video_render;
	OMX_BUFFERHEADERTYPE** buffers;
	int num_buffers;
	int current_buffer;
};

int omx_init(struct omx_state* st);
void omx_deinit(struct omx_state* st);

int omx_display_input_buffer(struct omx_state* st,
	void** pbuf, uint32_t* plen);
int omx_display_flush_buffer(struct omx_state* st);

int omx_display_enable(struct omx_state *st,
	int width, int height, int stride);
void omx_display_disable(struct omx_state *st);
