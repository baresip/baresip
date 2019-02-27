/**
 * @file omx.c     Raspberry Pi VideoCoreIV OpenMAX interface
 *
 * Copyright (C) 2016 - 2017 Creytiv.com
 * Copyright (C) 2016 - 2017 Jonathan Sieber
 */

#define _POSIX_C_SOURCE 199309L

#include "omx.h"

#include <re.h>
#include <rem.h>
#include <baresip.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Avoids a VideoCore header warning about clock_gettime() */
#include <time.h>
#include <sys/time.h>

#ifdef RASPBERRY_PI
#include <bcm_host.h>
#endif


/**
 * @defgroup omx omx
 *
 * Raspberry Pi VideoCoreIV OpenMAX interface
 *
 * TODO:
 *  * Proper sync OMX events across threads, instead of busy waiting
 */

#ifdef RASPBERRY_PI
static const int VIDEO_RENDER_PORT = 90;
#else
static const int VIDEO_RENDER_PORT = 0;
#endif

/*
static void setHeader(OMX_PTR header, OMX_U32 size) {
  OMX_VERSIONTYPE* ver = (OMX_VERSIONTYPE*)(header + sizeof(OMX_U32));
  *((OMX_U32*)header) = size;

  ver->s.nVersionMajor = VERSIONMAJOR;
  ver->s.nVersionMinor = VERSIONMINOR;
  ver->s.nRevision = VERSIONREVISION;
  ver->s.nStep = VERSIONSTEP;
}
* */


static OMX_ERRORTYPE EventHandler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
	OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2,
	OMX_PTR pEventData)
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


static OMX_ERRORTYPE EmptyBufferDone(OMX_HANDLETYPE hComponent,
				     OMX_PTR pAppData,
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
	EventHandler,
	EmptyBufferDone,
	&FillBufferDone
};


int omx_init(struct omx_state *st)
{
	OMX_ERRORTYPE err;

#ifdef RASPBERRY_PI
	bcm_host_init();
#endif

	st->buffers = NULL;

	err = OMX_Init();
#ifdef RASPBERRY_PI
	err |= OMX_GetHandle(&st->video_render,
		"OMX.broadcom.video_render", 0, &callbacks);
#else
	err |= OMX_GetHandle(&st->video_render,
		"OMX.st.video.xvideosink", 0, &callbacks);
#endif

	if (!st->video_render || err != 0) {
		warning("omx: Failed to create OMX video_render component\n");
		return ENOENT;
	}
	else {
		info("omx: created video_render component\n");
		return 0;
	}
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
			sys_usleep(10000);
		}
	}
}


void omx_deinit(struct omx_state *st)
{
	if (!st)
		return;

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


void omx_display_disable(struct omx_state *st)
{
#ifdef RASPBERRY_PI
	OMX_ERRORTYPE err;
	OMX_CONFIG_DISPLAYREGIONTYPE config;

	if (!st)
		return;

	memset(&config, 0, sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
	config.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
	config.nVersion.nVersion = OMX_VERSION;
	config.nPortIndex = VIDEO_RENDER_PORT;
	config.fullscreen = 0;
	config.set = OMX_DISPLAY_SET_FULLSCREEN;

	err = OMX_SetParameter(st->video_render,
		OMX_IndexConfigDisplayRegion, &config);

	if (err != 0) {
		warning("omx_display_disable command failed");
	}
#else
	(void)st;
#endif
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
			OMX_IndexParamPortDefinition, &portdef);
		if (r != OMX_ErrorNone) {
			warning("block_until_port_changed: OMX_GetParameter "
				" failed with Result=%d\n", r);
		}
		if (portdef.bEnabled != bEnabled) {
			sys_usleep(10000);
		}
	}
}


int omx_display_enable(struct omx_state* st,
	int width, int height, int stride)
{
	unsigned int i;
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
#ifdef RASPBERRY_PI
	OMX_CONFIG_DISPLAYREGIONTYPE config;
#endif
	OMX_ERRORTYPE err = 0;

	info("omx_update_size %d %d\n", width, height);

	#ifdef RASPBERRY_PI
	memset(&config, 0, sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
	config.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
	config.nVersion.nVersion = OMX_VERSION;
	config.nPortIndex = VIDEO_RENDER_PORT;
	config.fullscreen = 1;
	config.set = OMX_DISPLAY_SET_FULLSCREEN;

	err |= OMX_SetParameter(st->video_render,
		OMX_IndexConfigDisplayRegion, &config);

	#endif

	memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = VIDEO_RENDER_PORT;

	/* specify buffer requirements */
	err |= OMX_GetParameter(st->video_render,
		OMX_IndexParamPortDefinition, &portdef);
	if (err != 0) {
		warning("omx_display_enable: couldn't retrieve port def\n");
		err = ENOMEM;
		goto exit;
	}

	info("omx port definition: h=%d w=%d s=%d sh=%d\n",
		portdef.format.video.nFrameWidth,
		portdef.format.video.nFrameHeight,
		portdef.format.video.nStride,
		portdef.format.video.nSliceHeight);

	portdef.format.video.nFrameWidth = width;
	portdef.format.video.nFrameHeight = height;
	portdef.format.video.nStride = stride;
	portdef.format.video.nSliceHeight = height;
	portdef.bEnabled = 1;

	err |= OMX_SetParameter(st->video_render,
		OMX_IndexParamPortDefinition, &portdef);

	if (err) {
		warning("omx_display_enable: could not set port definition\n");
	}
	block_until_port_changed(st->video_render, VIDEO_RENDER_PORT, true);

	err |= OMX_GetParameter(st->video_render,
		OMX_IndexParamPortDefinition, &portdef);

	if (err != 0 || !portdef.bEnabled) {
		warning("omx_display_enable: failed to set up video port\n");
		err = ENOMEM;
		goto exit;
	}

	/* HACK: This state-change sometimes hangs for unknown reasons,
	 *       so we just send the state command and wait 50 ms */
	/* block_until_state_changed(st->video_render, OMX_StateIdle); */

	OMX_SendCommand(st->video_render, OMX_CommandStateSet,
		OMX_StateIdle, NULL);
	sys_usleep(50000);

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
				warning("OMX_AllocateBuffer failed: %d\n",
					err);
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
	return err;
}


int omx_display_input_buffer(struct omx_state* st,
	void** pbuf, uint32_t* plen)
{
	if (!st->buffers) return EINVAL;

	*pbuf = st->buffers[0]->pBuffer;
	*plen = st->buffers[0]->nAllocLen;

	st->buffers[0]->nFilledLen = *plen;
	st->buffers[0]->nOffset = 0;

	return 0;
}


int omx_display_flush_buffer(struct omx_state* st)
{
	if (OMX_EmptyThisBuffer(st->video_render, st->buffers[0])
		!= OMX_ErrorNone) {
		warning("OMX_EmptyThisBuffer error");
	}

	return 0;
}
