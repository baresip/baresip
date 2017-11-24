/**
 * @file messages.cpp  GNU ZRTP: Engine messages
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
 */
#include <stdint.h>

#include <re.h>
#include <baresip.h>

#include <libzrtpcpp/ZRtp.h>

#include "stream.h"


using namespace GnuZrtpCodes;


#define NO_MESSAGE "NO MESSAGE DEFINED"


static const char *info_msg(int32_t subcode)
{
	const char *msg;

	switch (subcode) {
	case InfoHelloReceived:
		msg = "Hello received and prepared a Commit, "
		      "ready to get peer's hello hash";
		break;
	case InfoCommitDHGenerated:
		msg = "Commit: Generated a public DH key";
		break;
	case InfoRespCommitReceived:
		msg = "Responder: Commit received, preparing DHPart1";
		break;
	case InfoDH1DHGenerated:
		msg = "DH1Part: Generated a public DH key";
		break;
	case InfoInitDH1Received:
		msg = "Initiator: DHPart1 received, preparing DHPart2";
		break;
	case InfoRespDH2Received:
		msg = "Responder: DHPart2 received, preparing Confirm1";
		break;
	case InfoInitConf1Received:
		msg = "Initiator: Confirm1 received, preparing Confirm2";
		break;
	case InfoRespConf2Received:
		msg = "Responder: Confirm2 received, preparing Conf2Ack";
		break;
	case InfoRSMatchFound:
		msg = "At least one retained secret matches - security OK";
		break;
	case InfoSecureStateOn:
		msg = "Entered secure state";
		break;
	case InfoSecureStateOff:
		msg = "No more security for this session";
		break;
	default:
		msg = NO_MESSAGE;
		break;
	}

	return msg;
}


static const char *warning_msg(int32_t subcode)
{
	const char *msg;

	switch (subcode) {
	case WarningDHAESmismatch:
		msg = "Commit contains an AES256 cipher but does not offer a "
	              "Diffie-Helman 4096 - not used DH4096 was discarded";
		break;
	case WarningGoClearReceived:
		msg = "Received a GoClear message";
		break;
	case WarningDHShort:
		msg = "Hello offers an AES256 cipher but does not offer a "
		      "Diffie-Helman 4096- not used DH4096 was discarded";
		break;
	case WarningNoRSMatch:
		msg = "No retained shared secrets available - must verify SAS";
		break;
	case WarningCRCmismatch:
		msg = "Internal ZRTP packet checksum mismatch - "
		      "packet dropped";
		break;
	case WarningSRTPauthError:
		msg = "Dropping packet because SRTP authentication failed!";
		break;
	case WarningSRTPreplayError:
		msg = "Dropping packet because SRTP replay check failed!";
		break;
	case WarningNoExpectedRSMatch:
		msg = "Valid retained shared secrets availabe but no matches "
		      "found - must verify SAS";
		break;
	case WarningNoExpectedAuxMatch:
		msg = "Our AUX secret was set but the other peer's AUX secret "
		      "does not match ours";
		break;
	default:
		msg = NO_MESSAGE;
		break;
	}

	return msg;
}


static const char *severe_msg(int32_t subcode)
{
	const char *msg;

	switch (subcode) {
	case SevereHelloHMACFailed:
		msg = "Hash HMAC check of Hello failed!";
		break;
	case SevereCommitHMACFailed:
		msg = "Hash HMAC check of Commit failed!";
		break;
	case SevereDH1HMACFailed:
		msg = "Hash HMAC check of DHPart1 failed!";
		break;
	case SevereDH2HMACFailed:
		msg = "Hash HMAC check of DHPart2 failed!";
		break;
	case SevereCannotSend:
		msg = "Cannot send data - connection or peer down?";
		break;
	case SevereProtocolError:
		msg = "Internal protocol error occured!";
		break;
	case SevereNoTimer:
		msg = "Cannot start a timer - internal resources exhausted?";
		break;
	case SevereTooMuchRetries:
		msg = "Too much retries during ZRTP negotiation - connection "
		      "or peer down?";
		break;
	default:
		msg = NO_MESSAGE;
		break;
	}

	return msg;
}


static const char *zrtp_msg(int32_t subcode)
{
	const char *msg;

	switch (subcode) {
	case MalformedPacket:
		msg = "Malformed packet (CRC OK, but wrong structure)";
		break;
	case CriticalSWError:
		msg = "Critical software error";
		break;
	case UnsuppZRTPVersion:
		msg = "Unsupported ZRTP version";
		break;
	case HelloCompMismatch:
		msg = "Hello components mismatch";
		break;
	case UnsuppHashType:
		msg = "Hash type not supported";
		break;
	case UnsuppCiphertype:
		msg = "Cipher type not supported";
		break;
	case UnsuppPKExchange:
		msg = "Public key exchange not supported";
		break;
	case UnsuppSRTPAuthTag:
		msg = "SRTP auth. tag not supported";
		break;
	case UnsuppSASScheme:
		msg = "SAS scheme not supported";
		break;
	case NoSharedSecret:
		msg = "No shared secret available, DH mode required";
		break;
	case DHErrorWrongPV:
		msg = "DH Error: bad pvi or pvr ( == 1, 0, or p-1)";
		break;
	case DHErrorWrongHVI:
		msg = "DH Error: hvi != hashed data";
		break;
	case SASuntrustedMiTM:
		msg = "Received relayed SAS from untrusted MiTM";
		break;
	case ConfirmHMACWrong:
		msg = "Auth. Error: Bad Confirm pkt HMAC";
		break;
	case NonceReused:
		msg = "Nonce reuse";
		break;
	case EqualZIDHello:
		msg = "Equal ZIDs in Hello";
		break;
	case GoCleatNotAllowed:
		msg = "GoClear packet received, but not allowed";
		break;
	default:
		msg = NO_MESSAGE;
		break;
	}

	return msg;
}


void Stream::print_message(GnuZrtpCodes::MessageSeverity severity,
                           int32_t subcode)
{
	switch (severity) {
	case Info:
		debug("zrtp: INFO<%s>: %s\n",
		     media_name(), info_msg(subcode));
		break;
	case Warning:
		warning("zrtp: WARNING<%s>: %s\n",
		        media_name(), warning_msg(subcode));
		break;
	case Severe:
		warning("zrtp: SEVERE<%s>: %s\n",
		        media_name(), severe_msg(subcode));
		break;
	case ZrtpError:
		warning("zrtp: ZRTP_ERR<%s>: %s\n",
		        media_name(), zrtp_msg(subcode));
		break;
	default:
		return;
	}
}


const char *Stream::media_name() const
{
	switch (m_media_type) {
	case MT_AUDIO:       return "audio";
	case MT_VIDEO:       return "video";
	case MT_TEXT:        return "text";
	case MT_APPLICATION: return "application";
	case MT_MESSAGE:     return "message";
	default:             return "UNKNOWN";
	}
}
