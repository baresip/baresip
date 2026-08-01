/**
 * @file datachannel_stub.c Disabled WebRTC data-channel API
 *
 * Copyright (C) 2026 The baresip project
 */

#include <errno.h>
#include <re.h>
#include <baresip.h>


int peerconnection_set_datachannel_handler(
	struct peer_connection *pc, peerconnection_datachannel_h *channelh,
	void *arg)
{
	(void)channelh;
	(void)arg;
	return pc ? ENOTSUP : EINVAL;
}


int peerconnection_create_datachannel(struct peer_connection *pc,
				      const char *label,
				      const struct data_channel_config *cfg,
				      struct data_channel **dcp)
{
	(void)cfg;
	return pc && label && dcp ? ENOTSUP : EINVAL;
}


int datachannel_set_handlers(struct data_channel *dc,
			     datachannel_message_h *messageh,
			     datachannel_state_h *stateh,
			     datachannel_buffered_low_h *buffered_lowh,
			     void *arg)
{
	(void)messageh;
	(void)stateh;
	(void)buffered_lowh;
	(void)arg;
	return dc ? ENOTSUP : EINVAL;
}


int datachannel_send(struct data_channel *dc,
		     enum data_channel_message_type type,
		     const uint8_t *buf, size_t len)
{
	(void)type;
	(void)buf;
	(void)len;
	return dc ? ENOTSUP : EINVAL;
}


int datachannel_close(struct data_channel *dc)
{
	return dc ? ENOTSUP : EINVAL;
}


const char *datachannel_label(const struct data_channel *dc)
{
	(void)dc;
	return NULL;
}


const char *datachannel_protocol(const struct data_channel *dc)
{
	(void)dc;
	return NULL;
}


int datachannel_id(const struct data_channel *dc)
{
	(void)dc;
	return -1;
}


enum data_channel_state datachannel_state(const struct data_channel *dc)
{
	(void)dc;
	return DATACHANNEL_CLOSED;
}


size_t datachannel_buffered_amount(const struct data_channel *dc)
{
	(void)dc;
	return 0;
}
