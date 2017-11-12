/**
 * @file stream.h  GNU ZRTP: Stream class
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
 */
#ifndef __STREAM_H
#define __STREAM_H


#include <libzrtpcpp/ZRtp.h>


enum StreamMediaType {
	MT_UNKNOWN = 0,
	MT_AUDIO,
	MT_VIDEO,
	MT_TEXT,
	MT_APPLICATION,
	MT_MESSAGE
};


class ZRTPConfig {
public:
	ZRTPConfig(const struct conf *conf, const char *conf_dir);
private:
	friend class Stream;
	friend class Session;

	ZrtpConfigure zrtp;

	char client_id[CLIENT_ID_SIZE + 1];
	char zid_filename[256];

	bool start_parallel;
};


class Stream;

class SRTPStat {
public:
	SRTPStat(const Stream *st, bool srtcp, uint64_t threshold);
	void update(int ret_code, bool quiet = false);
	void reset();
	uint64_t ok() { return m_ok; }
private:
	const Stream *m_stream;
	const bool m_control;
	const uint64_t m_threshold;
	uint64_t m_ok, m_decode, m_auth, m_replay;
	uint64_t m_decode_burst, m_auth_burst, m_replay_burst;
};


class Session;
class Srtp;

class Stream : public ZrtpCallback {
public:
	Stream(int& err, const ZRTPConfig& config, Session *session,
	       udp_sock *rtpsock, udp_sock *rtcpsock,
	       uint32_t local_ssrc, StreamMediaType media_type);

	virtual ~Stream();

	int start(Stream *master);
	void stop();
	bool started() { return m_started; }

	int sdp_encode(struct sdp_media *sdpm);
	int sdp_decode(const struct sdp_media *sdpm);

	const char *media_name() const;

	const char *get_sas() const { return m_sas.c_str(); }
	const char *get_ciphers() const { return m_ciphers.c_str(); }
	bool sas_verified();
	void verify_sas(bool verify);

private:
	static void zrtp_timer_cb(void *arg);
	static bool udp_helper_send_cb(int *err, struct sa *src,
	                               struct mbuf *mb, void *arg);
	static bool udp_helper_recv_cb(struct sa *src, struct mbuf *mb,
	                               void *arg);

	bool udp_helper_send(int *err, struct sa *src, struct mbuf *mb);
	bool udp_helper_recv(struct sa *src, struct mbuf *mb);
	bool recv_zrtp(struct mbuf *mb);

	void print_message(GnuZrtpCodes::MessageSeverity severity,
	                   int32_t subcode);

	Session *m_session;
	ZRtp *m_zrtp;
	bool m_started;
	struct tmr m_zrtp_timer;
	pthread_mutex_t m_zrtp_mutex;
	uint16_t m_zrtp_seq;
	uint32_t m_local_ssrc, m_peer_ssrc;
	struct sa m_raddr;
	struct udp_sock *m_rtpsock, *m_rtcpsock;
	struct udp_helper *m_uh_rtp;
	struct udp_helper *m_uh_rtcp;
	StreamMediaType m_media_type;
	Srtp *m_send_srtp, *m_recv_srtp;
	pthread_mutex_t m_send_mutex;
	SRTPStat m_srtp_stat, m_srtcp_stat;
	std::string m_sas, m_ciphers;

protected:
	virtual int32_t sendDataZRTP(const uint8_t* data, int32_t length);
	virtual int32_t activateTimer(int32_t time);
	virtual int32_t cancelTimer();
	virtual void sendInfo(GnuZrtpCodes::MessageSeverity severity,
	                      int32_t subCode);
	virtual bool srtpSecretsReady(SrtpSecret_t* secrets,
	                              EnableSecurity part);
	virtual void srtpSecretsOff(EnableSecurity part);
	virtual void srtpSecretsOn(std::string c, std::string s,
	                           bool verified);
	virtual void handleGoClear();
	virtual void zrtpNegotiationFailed(
	                        GnuZrtpCodes::MessageSeverity severity,
	                        int32_t subCode);
	virtual void zrtpNotSuppOther();
	virtual void synchEnter();
	virtual void synchLeave();
	virtual void zrtpAskEnrollment(GnuZrtpCodes::InfoEnrollment info);
	virtual void zrtpInformEnrollment(GnuZrtpCodes::InfoEnrollment info);
	virtual void signSAS(uint8_t* sasHash);
	virtual bool checkSASSignature(uint8_t* sasHash);
};


#endif // __STREAM_H

