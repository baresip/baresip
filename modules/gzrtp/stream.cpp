/**
 * @file stream.cpp  GNU ZRTP: Stream class implementation
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
 */
#include <stdint.h>
#include <pthread.h>

#include <re.h>
#include <baresip.h>

#include <libzrtpcpp/ZRtp.h>
#include <libzrtpcpp/ZrtpStateClass.h>

#include "session.h"
#include "stream.h"
#include "srtp.h"


// A burst of SRTP/SRTCP errors enough to display a warning
// Set to 1 to display all warnings
#define SRTP_ERR_BURST_THRESHOLD  20


enum {
	PRESZ = 36  /* Preamble size for TURN/STUN header */
};


enum pkt_type {
	PKT_TYPE_UNKNOWN = 0,
	PKT_TYPE_RTP = 1,
	PKT_TYPE_RTCP = 2,
	PKT_TYPE_ZRTP = 4
};


static enum pkt_type get_packet_type(const struct mbuf *mb)
{
	uint8_t b, pt;
	uint32_t magic;

	if (mbuf_get_left(mb) < 8)
		return PKT_TYPE_UNKNOWN;

	b = mbuf_buf(mb)[0];

	if (127 < b && b < 192) {
		pt = mbuf_buf(mb)[1] & 0x7f;
		if (72 <= pt && pt <= 76)
			return PKT_TYPE_RTCP;
		else
			return PKT_TYPE_RTP;
	}
	else {
		memcpy(&magic, &mbuf_buf(mb)[4], 4);
		magic = ntohl(magic);
		if (magic == ZRTP_MAGIC)
			return PKT_TYPE_ZRTP;
	}

	return PKT_TYPE_UNKNOWN;
}


ZRTPConfig::ZRTPConfig(const struct conf *conf, const char *conf_dir)
{
#ifdef GZRTP_USE_RE_SRTP
	// Standard ciphers only
	zrtp.clear();

	zrtp.addAlgo(HashAlgorithm, zrtpHashes.getByName(s256));

	zrtp.addAlgo(CipherAlgorithm, zrtpSymCiphers.getByName(aes3));
	zrtp.addAlgo(CipherAlgorithm, zrtpSymCiphers.getByName(aes1));

	zrtp.addAlgo(PubKeyAlgorithm, zrtpPubKeys.getByName(ec25));
	zrtp.addAlgo(PubKeyAlgorithm, zrtpPubKeys.getByName(dh3k));
	zrtp.addAlgo(PubKeyAlgorithm, zrtpPubKeys.getByName(ec38));
	zrtp.addAlgo(PubKeyAlgorithm, zrtpPubKeys.getByName(dh2k));
	zrtp.addAlgo(PubKeyAlgorithm, zrtpPubKeys.getByName(mult));

	zrtp.addAlgo(SasType, zrtpSasTypes.getByName(b32));

	zrtp.addAlgo(AuthLength, zrtpAuthLengths.getByName(hs32));
	zrtp.addAlgo(AuthLength, zrtpAuthLengths.getByName(hs80));
#else
	zrtp.setStandardConfig();
#endif

	str_ncpy(client_id, "baresip/gzrtp", sizeof(client_id));

	re_snprintf(zid_filename, sizeof(zid_filename),
	            "%s/gzrtp.zid", conf_dir);

	start_parallel = true;
	(void)conf_get_bool(conf, "zrtp_parallel", &start_parallel);
}

SRTPStat::SRTPStat(const Stream *st, bool srtcp, uint64_t threshold)
	: m_stream(st)
	, m_control(srtcp)
	, m_threshold(threshold)
{
	reset();
}


void SRTPStat::update(int ret_code, bool quiet)
{
	const char *err_msg;
	uint64_t *burst;

	// Srtp::unprotect/unprotect_ctrl return codes
	switch (ret_code) {
	case 0:
		++m_ok;
		m_decode_burst = 0;
		m_auth_burst = 0;
		m_replay_burst = 0;
		return;
	case EBADMSG:
		++m_decode;
		burst = &m_decode_burst;
		err_msg = "packet decode error";
		break;
	case EAUTH:
		++m_auth;
		burst = &m_auth_burst;
		err_msg = "authentication failed";
		break;
	case EALREADY:
		++m_replay;
		burst = &m_replay_burst;
		err_msg = "replay check failed";
		break;
	default:
		warning("zrtp: %s unprotect failed: %m\n",
		        (m_control)? "SRTCP" : "SRTP", ret_code);
		return;
	}

	++(*burst);
	if (*burst == m_threshold) {
		*burst = 0;

		if (!quiet)
			warning("zrtp: Stream <%s>: %s %s, %d packets\n",
				m_stream->media_name(),
				(m_control)? "SRTCP" : "SRTP",
				err_msg,
				m_threshold);
	}
}


void SRTPStat::reset()
{
	m_ok = 0;
	m_decode = 0; m_auth = 0; m_replay = 0;
	m_decode_burst = 0; m_auth_burst = 0; m_replay_burst = 0;
}


Stream::Stream(int& err, const ZRTPConfig& config, Session *session,
               udp_sock *rtpsock, udp_sock *rtcpsock,
               uint32_t local_ssrc, StreamMediaType media_type)
	: m_session(session)
	, m_zrtp(NULL)
	, m_started(false)
	, m_local_ssrc(local_ssrc)
	, m_peer_ssrc(0)
	, m_rtpsock(NULL)
	, m_rtcpsock(NULL)
	, m_uh_rtp(NULL)
	, m_uh_rtcp(NULL)
	, m_media_type(media_type)
	, m_send_srtp(NULL)
	, m_recv_srtp(NULL)
	, m_srtp_stat(this, false, SRTP_ERR_BURST_THRESHOLD)
	, m_srtcp_stat(this, true, SRTP_ERR_BURST_THRESHOLD)
{
	err = 0;

	m_zrtp_seq = rand_u16() & 0x7fff;
	sa_init(&m_raddr, AF_INET);
	tmr_init(&m_zrtp_timer);

	pthread_mutexattr_t attr;
	err  = pthread_mutexattr_init(&attr);
	err |= pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
	err |= pthread_mutex_init(&m_zrtp_mutex, &attr);
	err |= pthread_mutex_init(&m_send_mutex, &attr);
	if (err)
		return;

	int layer = 10; // above zero
	if (rtpsock) {
		m_rtpsock = (struct udp_sock *)mem_ref(rtpsock);
		err |= udp_register_helper(&m_uh_rtp, rtpsock, layer,
		                           Stream::udp_helper_send_cb,
		                           Stream::udp_helper_recv_cb,
		                           this);
	}
	if (rtcpsock && (rtcpsock != rtpsock)) {
		m_rtcpsock = (struct udp_sock *)mem_ref(rtcpsock);
		err |= udp_register_helper(&m_uh_rtcp, rtcpsock, layer,
		                           Stream::udp_helper_send_cb,
		                           Stream::udp_helper_recv_cb,
		                           this);
	}
	if (err)
		return;

	ZIDCache* zf = getZidCacheInstance();
	if (!zf->isOpen()) {
		if (zf->open((char *)config.zid_filename) == -1) {
			warning("zrtp: Couldn't open/create ZID file %s\n",
			        config.zid_filename);
			err = ENOENT;
			return;
		}
	}

	m_zrtp = new ZRtp((uint8_t *)zf->getZid(), this, config.client_id,
	                  (ZrtpConfigure *)&config.zrtp, false, false);
	if (!m_zrtp) {
		err = ENOMEM;
		return;
	}

	return;
}


Stream::~Stream()
{
	stop();

	delete m_zrtp;

	mem_deref(m_uh_rtp);
	mem_deref(m_uh_rtcp);
	mem_deref(m_rtpsock);
	mem_deref(m_rtcpsock);

	pthread_mutex_destroy(&m_zrtp_mutex);
	pthread_mutex_destroy(&m_send_mutex);

	tmr_cancel(&m_zrtp_timer);
}


int Stream::start(Stream *master)
{
	if (started())
		return EPERM;

	if (master) {
		ZRtp *zrtp_master;

		std::string params =
		        master->m_zrtp->getMultiStrParams(&zrtp_master);
		if (params.empty())
			return EPROTO;

		m_zrtp->setMultiStrParams(params, zrtp_master);
	}

	debug("zrtp: Starting <%s> stream%s\n", media_name(),
	      (m_zrtp->isMultiStream())? " (multistream)" : "");

	m_srtp_stat.reset();
	m_srtcp_stat.reset();
	m_sas.clear();
	m_ciphers.clear();

	m_started = true;
	m_zrtp->startZrtpEngine();

	return 0;
}


void Stream::stop()
{
	if (!started())
		return;

	m_started = false;

	// If we got only a small amount of valid SRTP packets after ZRTP
	// negotiation then assume that our peer couldn't store the RS data,
	// thus make sure we have a second retained shared secret available.
	// Refer to RFC 6189bis, chapter 4.6.1 50 packets are about 1 second
	// of audio data
	if (!m_zrtp->isMultiStream() && m_recv_srtp && m_srtp_stat.ok() < 20) {

		debug("zrtp: Stream <%s>: received too few valid SRTP "
		      "packets (%u), storing RS2\n",
		       media_name(), m_srtp_stat.ok());

		m_zrtp->setRs2Valid();
	}

	debug("zrtp: Stopping <%s> stream\n", media_name());

	m_zrtp->stopZrtp();

	pthread_mutex_lock(&m_send_mutex);
	delete m_send_srtp;
	m_send_srtp = NULL;
	pthread_mutex_unlock(&m_send_mutex);

	delete m_recv_srtp;
	m_recv_srtp = NULL;

	debug("zrtp: Stream <%s> stopped\n", media_name());
}


int Stream::sdp_encode(struct sdp_media *sdpm)
{
	// NOTE: signaling hash
	return 0;
}


int Stream::sdp_decode(const struct sdp_media *sdpm)
{
	if (sa_isset(sdp_media_raddr(sdpm), SA_ALL)) {
		m_raddr = *sdp_media_raddr(sdpm);
	}
	// NOTE: signaling hash

	return 0;
}


bool Stream::udp_helper_send_cb(int *err, struct sa *src, struct mbuf *mb,
                                void *arg)
{
	Stream *st = (Stream *)arg;

	if (st)
		return st->udp_helper_send(err, src, mb);

	return false;
}


bool Stream::udp_helper_send(int *err, struct sa *src, struct mbuf *mb)
{
	bool ret = false;
	enum pkt_type ptype = get_packet_type(mb);
	size_t len = mbuf_get_left(mb);
	int rerr = 0;

	pthread_mutex_lock(&m_send_mutex);

	if (ptype == PKT_TYPE_RTCP && m_send_srtp && len > 8) {

		rerr = m_send_srtp->protect_ctrl(mb);
	}
	else if (ptype == PKT_TYPE_RTP && m_send_srtp &&
	         len > RTP_HEADER_SIZE) {

		rerr = m_send_srtp->protect(mb);
	}
	else
		goto out;

	if (rerr) {
		warning("zrtp: protect/protect_ctrl failed (len=%u): %m\n",
		        len, rerr);

		if (rerr == ENOMEM)
			*err = rerr;
		// drop
		ret = true;
	}

 out:
	pthread_mutex_unlock(&m_send_mutex);

	return ret;
}


bool Stream::udp_helper_recv_cb(struct sa *src, struct mbuf *mb, void *arg)
{
	Stream *st = (Stream *)arg;

	if (st)
		return st->udp_helper_recv(src, mb);

	return false;
}


bool Stream::udp_helper_recv(struct sa *src, struct mbuf *mb)
{
	if (!started())
		return false;

	enum pkt_type ptype = get_packet_type(mb);
	int err = 0;

	if (ptype == PKT_TYPE_RTCP && m_recv_srtp) {

		err = m_recv_srtp->unprotect_ctrl(mb);

		m_srtcp_stat.update(err);
	}
	else if (ptype == PKT_TYPE_RTP && m_recv_srtp) {

		err = m_recv_srtp->unprotect(mb);

		m_srtp_stat.update(err);

		if (!err) {
			// Got a good SRTP, check state and if in WaitConfAck
			// (an Initiator state) then simulate a conf2Ack,
			// refer to RFC 6189, chapter 4.6, last paragraph
			if (m_zrtp->inState(WaitConfAck))
				m_zrtp->conf2AckSecure();
		}
	}
	else if (ptype == PKT_TYPE_ZRTP) {
		return recv_zrtp(mb);
	}
	else
		return false;

	if (err)
		// drop
		return true;

	return false;
}


// <RTP> + <ext. header> + <ZRTP message type> + CRC32
#define ZRTP_MIN_PACKET_LENGTH  (RTP_HEADER_SIZE + 4 + 8 + 4)

bool Stream::recv_zrtp(struct mbuf *mb)
{
	uint32_t crc32;
	uint8_t *buf = mbuf_buf(mb);
	size_t size = mbuf_get_left(mb);

	if (size < ZRTP_MIN_PACKET_LENGTH) {
		warning("zrtp: incoming packet size (%d) is too small\n",
		        size);
		return false;
	}

	// check CRC
	memcpy(&crc32, buf + size - 4, 4);
	crc32 = ntohl(crc32);
	if (!zrtpCheckCksum(buf, size - 4, crc32)) {
		sendInfo(GnuZrtpCodes::Warning,
		         GnuZrtpCodes::WarningCRCmismatch);
		return false;
	}

	// store peer's SSRC for creating the CryptoContext
	memcpy(&m_peer_ssrc, buf + 8, 4);
	m_peer_ssrc = ntohl(m_peer_ssrc);

	m_zrtp->processZrtpMessage(buf + RTP_HEADER_SIZE, m_peer_ssrc, size);

	return true;
}


void Stream::verify_sas(bool verify)
{
	if (verify)
		m_zrtp->SASVerified();
	else
		m_zrtp->resetSASVerified();
}


bool Stream::sas_verified()
{
	return m_zrtp->isSASVerified();
}


//
// callbacks
//


int32_t Stream::sendDataZRTP(const uint8_t* data, int32_t length)
{
	struct mbuf *mb;
	uint8_t *crc_buf;
	uint32_t crc32;
	size_t start_pos = PRESZ;
	int err = 0;

	if (!sa_isset(&m_raddr, SA_ALL))
		return 0;

	mb = mbuf_alloc(start_pos + RTP_HEADER_SIZE + length);
	if (!mb)
		return 0;

	mbuf_set_end(mb, start_pos);
	mbuf_set_pos(mb, start_pos);
	crc_buf = mbuf_buf(mb);

	// write RTP header
	err  = mbuf_write_u8(mb, 0x10);
	err |= mbuf_write_u8(mb, 0x00);
	err |= mbuf_write_u16(mb, htons(m_zrtp_seq++));
	err |= mbuf_write_u32(mb, htonl(ZRTP_MAGIC));
	err |= mbuf_write_u32(mb, htonl(m_local_ssrc));

	// copy ZRTP message data
	err |= mbuf_write_mem(mb, data, length - 4);

	// compute CRC
	crc32 = zrtpGenerateCksum(crc_buf, RTP_HEADER_SIZE + length - 4);
	crc32 = zrtpEndCksum(crc32);

	// store CRC
	err |= mbuf_write_u32(mb, htonl(crc32));
	if (err)
		goto out;

	// send ZRTP packet using RTP socket
	mbuf_set_pos(mb, start_pos);
	err = udp_send_helper(m_rtpsock, &m_raddr, mb, m_uh_rtp);
	if (err)
		warning("zrtp: udp_send_helper: %m\n", err);

 out:
	mem_deref(mb);

	return (err == 0);
}


void Stream::zrtp_timer_cb(void *arg)
{
	Stream *s = (Stream *)arg;

	s->m_zrtp->processTimeout();
}


int32_t Stream::activateTimer(int32_t time)
{
	tmr_start(&m_zrtp_timer, time, &Stream::zrtp_timer_cb, this);
	return 1;
}


int32_t Stream::cancelTimer()
{
	tmr_cancel(&m_zrtp_timer);
	return 1;
}


void Stream::sendInfo(GnuZrtpCodes::MessageSeverity severity, int32_t subCode)
{
	print_message(severity, subCode);

	if (severity == GnuZrtpCodes::Info) {
		if (subCode == GnuZrtpCodes::InfoSecureStateOn) {
			m_session->on_secure(this);
		}
		else if (subCode == GnuZrtpCodes::InfoHelloReceived &&
		         !m_zrtp->isMultiStream()) {

			m_session->request_master(this);
		}
	}
}


bool Stream::srtpSecretsReady(SrtpSecret_t* secrets, EnableSecurity part)
{
	Srtp *s;
	int err = 0;

	debug("zrtp: Stream <%s>: secrets are ready for %s\n",
	      media_name(),
	      (part == ForSender)? "sender" : "receiver");

	s = new Srtp(err, secrets, part);
	if (!s || err) {
		warning("zrtp: Stream <%s>: Srtp creation failed: %m\n",
		        media_name(), err);
		delete s;
		return false;
	}

	if (part == ForSender) {
		pthread_mutex_lock(&m_send_mutex);
		m_send_srtp = s;
		pthread_mutex_unlock(&m_send_mutex);
	}
	else if (part == ForReceiver)
		m_recv_srtp = s;
	else
		return false;

	return true;
}


void Stream::srtpSecretsOff(EnableSecurity part)
{
	debug("zrtp: Stream <%s>: secrets are off for %s\n",
	      media_name(),
	      (part == ForSender)? "sender" : "receiver");

	if (part == ForSender) {
		pthread_mutex_lock(&m_send_mutex);
		delete m_send_srtp;
		m_send_srtp = NULL;
		pthread_mutex_unlock(&m_send_mutex);
	}

	if (part == ForReceiver) {
		delete m_recv_srtp;
		m_recv_srtp = NULL;
	}
}


void Stream::srtpSecretsOn(std::string c, std::string s, bool verified)
{
	m_sas = s;
	m_ciphers = c;

	if (s.empty()) {
		info("zrtp: Stream <%s> is encrypted (%s)\n",
		     media_name(), c.c_str());
	}
	else {
		info("zrtp: Stream <%s> is encrypted (%s), "
		     "SAS is [%s] (%s)\n",
		     media_name(), c.c_str(), s.c_str(),
		     (verified)? "verified" : "NOT VERIFIED");
		if (!verified)
			warning("zrtp: SAS is not verified, type "
			        "'/zrtp_verify %d' to verify\n",
			        m_session->id());
	}
}


void Stream::handleGoClear()
{
}


void Stream::zrtpNegotiationFailed(GnuZrtpCodes::MessageSeverity severity,
                                   int32_t subCode)
{
}


void Stream::zrtpNotSuppOther()
{
}


void Stream::synchEnter()
{
	pthread_mutex_lock(&m_zrtp_mutex);
}


void Stream::synchLeave()
{
	pthread_mutex_unlock(&m_zrtp_mutex);
}


void Stream::zrtpAskEnrollment(GnuZrtpCodes::InfoEnrollment info)
{
}


void Stream::zrtpInformEnrollment(GnuZrtpCodes::InfoEnrollment info)
{
}


void Stream::signSAS(uint8_t* sasHash)
{
}


bool Stream::checkSASSignature(uint8_t* sasHash)
{
	return true;
}

