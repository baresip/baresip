/**
 * @file omx.c     Raspberry Pi VideoCoreIV OpenMAX interface
 *
 * Copyright (C) 2016 - 2017 Creytiv.com
 * Copyright (C) 2016 - 2017 Jonathan Sieber
 */

#include "omx.h"

#include <re/re.h>
#include <rem/rem.h>
#include <baresip.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Avoids a VideoCore header warning about clock_gettime() */
#include <time.h>
#include <sys/time.h>

/**
 * @defgroup omx omx
 *
 * TODO:
 *  * Proper sync OMX events across threads, instead of busy waiting
 */

static const int VIDEO_RENDER_PORT = 90;

static int EventHandler(OMX_HANDLETYPE hComponent, void* pAppData,
	OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2,
	void* pEventData)
{
	(void) hComponent;
	switch (eEvent) {
		case OMX_EventCmdComplete:
			debug("omx.EventHandler: Previous command completed\n"
				"d1=%x\td2=%x\teventData=%p\tappdata=%p\n",
				nData1, nData2, pEventData, pAppData);
			/* TODO: Put these event into a multithreaded queue,
			 * properly wait for them in the issuing code */
			break;
		case OMX_EventError:
			warning("omx.EventHandler: Error event type "
				"data1=%x\tdata2=%x\n", nData1, nData2);
			break;
		default:
			warning("omx.EventHandler: Unknown event type %d\t"
				"data1=%x data2=%x\n", eEvent, nData1, nData2);
			return -1;
			break;
	}
	return 0;
}

static int EmptyBufferDone(OMX_HANDLETYPE hComponent, void* pAppData,
	OMX_BUFFERHEADERTYPE* pBuffer)
{
	(void) hComponent;
	(void) pAppData;
	(void) pBuffer;

	/* TODO: Wrap every call that can generate an event,
	 * and panic if an unexpected event arrives */
	return 0;
}

static OMX_ERRORTYPE FillBufferDone(OMX_HANDLETYPE hComponent,
	OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
	(void) hComponent;
	(void) pAppData;
	(void) pBuffer;
	debug("FillBufferDone\n");
	return 0;
}

static struct OMX_CALLBACKTYPE callbacks = {
	&EventHandler,
	&EmptyBufferDone,
	&FillBufferDone
};

int omx_init(struct omx_state* st)
{
	OMX_ERRORTYPE err;

	bcm_host_init();

	st->buffers = NULL;

   	pthread_mutex_init(&st->omx_mutex, 0);

	err = OMX_Init();
	err |= OMX_GetHandle(&st->video_render,
		"OMX.broadcom.video_render", 0, &callbacks);

	if (err != OMX_ERROR_NONE) {
		error("Failed to create OMX video_render component");
	}
	else {
		info("created video_render component");
	}

	return err;
}


/* Some busy loops to verify we're running in order */
static void block_until_state_changed(OMX_HANDLETYPE hComponent,
	OMX_STATETYPE wanted_eState)
{
	OMX_STATETYPE eState;
	unsigned int i = 0;
	while (i++ == 0 || eState != wanted_eState) {
		OMX_GetState(hComponent, &eState);
		if (eState != wanted_eState) {
			usleep(10000);
		}
	}
}


void omx_deinit(struct omx_state* st)
{
	info("omx_deinit");
	OMX_SendCommand(st->video_render,
		OMX_CommandStateSet, OMX_StateIdle, NULL);
	block_until_state_changed(st->video_render, OMX_StateIdle);
	OMX_SendCommand(st->video_render,
		OMX_CommandStateSet, OMX_StateLoaded, NULL);
	block_until_state_changed(st->video_render, OMX_StateLoaded);
	OMX_FreeHandle(st->video_render);
	OMX_Deinit();
}

void omx_display_disable(struct omx_state* st)
{
	OMX_ERRORTYPE err;
	OMX_CONFIG_DISPLAYREGIONTYPE config;
	memset(&config, 0, sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
	config.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
	config.nVersion.nVersion = OMX_VERSION;
	config.nPortIndex = VIDEO_RENDER_PORT;
	config.fullscreen = 0;
	config.set = OMX_DISPLAY_SET_FULLSCREEN;

	err = OMX_SetParameter(st->video_render,
		OMX_IndexConfigDisplayRegion, &config);

	if (err != OMX_ERROR_NONE) {
		warn("omx_display_disable command failed");
	}
}

static void block_until_port_changed(OMX_HANDLETYPE hComponent,
	OMX_U32 nPortIndex, OMX_BOOL bEnabled) {

	OMX_ERRORTYPE r;
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	OMX_U32 i = 0;

	memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = nPortIndex;

	while (i++ == 0 || portdef.bEnabled != bEnabled) {
		r = OMX_GetParameter(hComponent,
			OMX_IndexParamPortDefinition, &portdef));
		if (r != OMX_ErrorNone) {
			error("block_until_port_changed: OMX_GetParameter "
				" failed with Result=%d\n", r);
		}
		if (portdef.bEnabled != bEnabled) {
			usleep(10000);
		}
	}
}

int omx_display_enable(struct omx_state* st,
	int width, int height, int stride)
{
	unsigned int i;
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	OMX_CONFIG_DISPLAYREGIONTYPE config;
	OMX_ERRORTYPE err = OMX_ERROR_NONE;

	pthread_mutex_lock(&st->omx_mutex);
	info("omx_update_size %d %d\n", width, height);

	memset(&config, 0, sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
	config.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
	config.nVersion.nVersion = OMX_VERSION;
	config.nPortIndex = VIDEO_RENDER_PORT;
	config.fullscreen = 1;
	config.set = OMX_DISPLAY_SET_FULLSCREEN;

	err |= OMX_SetParameter(st->video_render,
		OMX_IndexConfigDisplayRegion, &config);

	memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = VIDEO_RENDER_PORT;

	/* specify buffer requirements */
	err |= OMX_GetParameter(st->video_render,
		OMX_IndexParamPortDefinition, &portdef);

	portdef.format.video.nFrameWidth = width;
	portdef.format.video.nFrameHeight = height;
	portdef.format.video.nStride = stride;
	portdef.format.video.nSliceHeight = height;
	portdef.bEnabled = 1;

	err |= OMX_SetParameter(st->video_render,
		OMX_IndexParamPortDefinition, &portdef);
	block_until_port_changed(st->video_render, VIDEO_RENDER_PORT, true);

	err |= OMX_GetParameter(st->video_render,
		OMX_IndexParamPortDefinition, &portdef);

	if (err != OMX_ERROR_NONE || !portdef.bEnabled) {
		error("omx_display_enable: failed to set up video port");
		err = ENOMEM;
		goto exit;
	}

	/* HACK: This state-change sometimes hangs for unknown reasons,
	 *       so we just send the state command and wait 50 ms */
	/* block_until_state_changed(st->video_render, OMX_StateIdle); */

	OMX_SendCommand(st->video_render, OMX_CommandStateSet,
		OMX_StateIdle, NULL);
	usleep(50000);

	if (!st->buffers) {
		st->buffers =
			malloc(portdef.nBufferCountActual * sizeof(void*));
		st->num_buffers = portdef.nBufferCountActual;
		st->current_buffer = 0;

		for (i = 0; i < portdef.nBufferCountActual; i++) {
			err = OMX_AllocateBuffer(st->video_render,
				&st->buffers[i], VIDEO_RENDER_PORT,
				st, portdef.nBufferSize);
			if (err) {
				error("OMX_AllocateBuffer failed: %d\n", err);
				err = ENOMEM;
				goto exit;
			}
		}
	}

	debug("omx_update_size: send to execute state");
	OMX_SendCommand(st->video_render, OMX_CommandStateSet,
		OMX_StateExecuting, NULL);
	block_until_state_changed(st->video_render, OMX_StateExecuting);

exit:
	pthread_mutex_unlock(&st->omx_mutex);
	return err;
}


int omx_display_input_buffer(struct omx_state* st,
	void** pbuf, uint32_t* plen)
{
	pthread_mutex_lock(&st->omx_mutex);

	assert(st->buffers);
	*pbuf = st->buffers[0]->pBuffer;
	*plen = st->buffers[0]->nAllocLen;

	st->buffers[0]->nFilledLen = *pl
	st->buffers[0]->nOffset = 0;

	pthread_mutex_unlock(&st->omx_mutex);

	return 0;
}

int omx_display_flush_buffer(struct omx_state* st)
{
	pthread_mutex_lock(&st->omx_mutex);
	if (OMX_EmptyThisBuffer(st->video_render, st->buffers[0])
		!= OMX_ErrorNone) {
		error("OMX_EmptyThisBuffer error");
	}

	pthread_mutex_unlock(&st->omx_mutex);

	return 0;
}
