INVITE sip:192.168.1.3 SIP/2.0
Via: SIP/2.0/TCP 192.168.1.1:43124;branch=z9hG4bK72482fad4a89eac4;rport
Contact: <sip:A-0x55b6f08e7370@192.168.1.1:43124;transport=tcp>
Max-Forwards: 70
To: <sip:192.168.1.3>
From: "Name A" <sip:A@192.168.1.1>;tag=a9c3bcd28561b732
Call-ID: b1c2225803af75f5
CSeq: 25003 INVITE
User-Agent: baresip v3.24.0 (x86_64/Linux)
Allow: INVITE,ACK,BYE,CANCEL,OPTIONS,NOTIFY,INFO,MESSAGE,UPDATE,PRACK,REFER
Supported: gruu,replaces,norefersub,100rel
Content-Type: application/sdp
Content-Length: 766

v=0
o=- 3903548297 1853860041 IN IP4 192.168.1.1
s=-
c=IN IP4 192.168.1.1
t=0 0
m=audio 7574 RTP/AVP 9 0 8 101
a=rtpmap:9 G722/8000
a=rtpmap:0 PCMU/8000
a=rtpmap:8 PCMA/8000
a=rtpmap:101 telephone-event/8000
a=fmtp:101 0-15
a=sendrecv
a=rtcp-rsize
a=ssrc:3669794641 cname:sip:A@192.168.1.1
a=mid:0
a=minptime:20
a=ptime:20
a=label:1
m=video 37772 RTP/AVP 96 97 98
a=rtpmap:96 H264/90000
a=fmtp:96 packetization-mode=0;profile-level-id=42e01f
a=rtpmap:97 H264/90000
a=fmtp:97 packetization-mode=1;profile-level-id=42e01f
a=rtpmap:98 H265/90000
a=fmtp:98 profile-id=1
a=sendrecv
a=rtcp-rsize
a=ssrc:444802481 cname:sip:A@192.168.1.1
a=mid:1
a=framerate:25.00
a=rtcp-fb:* nack
a=rtcp-fb:* nack pli
a=content:main
a=label:2

