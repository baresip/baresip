/**
 * @file session.h  GNU ZRTP: Session class
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
 */
#ifndef __SESSION_H
#define __SESSION_H


#include "stream.h"


class Stream;
class ZRTPConfig;

class Session {
public:
	Session(const ZRTPConfig& config);

	~Session();

	Stream *create_stream(const ZRTPConfig& config,
	                      udp_sock *rtpsock,
	                      udp_sock *rtcpsock,
	                      uint32_t local_ssrc,
	                      StreamMediaType media_type);

	int start_stream(Stream *stream);
	int id() const { return m_id; }

	bool request_master(Stream *stream);
	void on_secure(Stream *stream);

	static int cmd_verify_sas(struct re_printf *pf, void *arg);
	static int cmd_unverify_sas(struct re_printf *pf, void *arg);
	static int cmd_sas(bool verify, struct re_printf *pf, void *arg);

private:
	static std::vector<Session *> s_sessl;

	const bool m_start_parallel;
	int m_id;
	std::vector<Stream *> m_streams;
	Stream *m_master;
	unsigned int m_encrypted;
};


#endif // __SESSION_H

