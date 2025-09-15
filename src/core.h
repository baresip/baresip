/**
 * @file core.h  Internal API
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */


#include <limits.h>


/* max bytes in pathname */
#if defined (PATH_MAX)
#define FS_PATH_MAX PATH_MAX
#elif defined (_POSIX_PATH_MAX)
#define FS_PATH_MAX _POSIX_PATH_MAX
#else
#define FS_PATH_MAX 512
#endif


/** Media constants */
enum {
	AUDIO_BANDWIDTH = 128000,  /**< Bandwidth for audio in bits/s      */
	VIDEO_SRATE     =  90000,  /**< Sampling rate for video            */
};


/* forward declarations */
struct stream_param;


/*
 * Account
 */

struct uasauth {
	struct le he;

	char *met;
	bool deny;
};


struct account {
	char *buf;                   /**< Buffer for the SIP address         */
	struct sip_addr laddr;       /**< Decoded SIP address                */
	struct uri luri;             /**< Decoded AOR uri                    */
	char *dispname;              /**< Display name                       */
	char *aor;                   /**< Local SIP uri                      */

	/* parameters: */
	bool sipans;                 /**< Allow SIP header auto answer mode  */
	enum sipansbeep sipansbeep;  /**< Beep mode for SIP auto answer      */
	enum rel100_mode rel100_mode;  /**< 100rel mode for incoming calls   */
	enum answermode answermode;  /**< Answermode for incoming calls      */
	bool autoredirect;           /**< Autoredirect on 3xx reply on/off   */
	int32_t adelay;              /**< Delay for delayed auto answer [ms] */
	enum dtmfmode dtmfmode;      /**< Send type for DTMF tones           */
	enum inreq_mode inreq_mode;  /**< Incoming request mode              */
	struct le acv[16];           /**< List elements for aucodecl         */
	struct list aucodecl;        /**< List of preferred audio-codecs     */
	char *auth_user;             /**< Authentication username            */
	char *auth_pass;             /**< Authentication password            */
	char *mnatid;                /**< Media NAT handling                 */
	char *mencid;                /**< Media encryption type              */
	const struct mnat *mnat;     /**< MNAT module                        */
	const struct menc *menc;     /**< MENC module                        */
	char *outboundv[2];          /**< Optional SIP outbound proxies      */
	uint32_t ptime;              /**< Configured packet time in [ms]     */
	uint32_t regint;             /**< Registration interval in [seconds] */
	uint32_t fbregint;           /**< Fallback R. interval in [seconds]  */
	uint32_t rwait;              /**< R. Int. in [%] from proxy expiry   */
	uint32_t pubint;             /**< Publication interval in [seconds]  */
	uint32_t prio;               /**< Prio for serial registration       */
	uint16_t tcpsrcport;         /**< TCP source port for SIP            */
	char *regq;                  /**< Registration Q-value               */
	char *sipnat;                /**< SIP Nat mechanism                  */
	char *stun_user;             /**< STUN Username                      */
	char *stun_pass;             /**< STUN Password                      */
	struct stun_uri *stun_host;  /**< STUN Server                        */
	struct le vcv[8];            /**< List elements for vidcodecl        */
	struct list vidcodecl;       /**< List of preferred video-codecs     */
	bool videoen;                /**< Video enabled flag                 */
	bool mwi;                    /**< MWI on/off                         */
	bool refer;                  /**< REFER method on/off                */
	char *cert;                  /**< SIP TLS client certificate+keyfile */
	char *ausrc_mod;
	char *ausrc_dev;
	char *auplay_mod;
	char *auplay_dev;
	char *vidsrc_mod;
	char *vidsrc_dev;
	char *viddisp_mod;
	char *viddisp_dev;
	uint32_t autelev_pt;         /**< Payload type for telephone-events  */
	char *extra;                 /**< Extra parameters                   */
	char *uas_user;              /**< UAS authentication username        */
	char *uas_pass;              /**< UAS authentication password        */
	bool rtcp_mux;               /**< RTCP multiplexing                  */
	bool pinhole;                /**< NAT pinhole flag                   */
	bool catchall;               /**< Catch all inbound requests         */
};


/*
 * Audio Stream
 */

struct audio;

int  audio_send_digit(struct audio *a, char key);
void audio_sdp_attr_decode(struct audio *a);
int  audio_enable_level(struct audio *au);


/*
 * Audio Codec
 */
int aucodec_print(struct re_printf *pf, const struct aucodec *ac);


/*
 * Audio Receiver Pipeline
 */

struct audio_recv;

int  aurecv_alloc(struct audio_recv **aupp, const struct config_audio *cfg,
		  size_t sampc, uint32_t ptime);
int  aurecv_decoder_set(struct audio_recv *ar,
			const struct aucodec *ac, int pt, const char *params);
int  aurecv_payload_type(const struct audio_recv *ar);
int  aurecv_filt_append(struct audio_recv *ar, struct aufilt_dec_st *decst);
void aurecv_flush(struct audio_recv *ar);
void aurecv_set_extmap(struct audio_recv *ar, uint8_t aulevel);
int  aurecv_set_module(struct audio_recv *ar, const char *module);
int  aurecv_set_device(struct audio_recv *ar, const char *device);
void aurecv_receive(struct audio_recv *ar, const struct rtp_header *hdr,
		    struct rtpext *extv, size_t extc,
		    struct mbuf *mb, unsigned lostc, bool *ignore);
int  aurecv_start_player(struct audio_recv *ar, struct list *auplayl);
bool aurecv_player_started(const struct audio_recv *ar);
void aurecv_stop(struct audio_recv *ar);
void aurecv_stop_auplay(struct audio_recv *ar);

const struct aucodec *aurecv_codec(const struct audio_recv *ar);
uint64_t aurecv_latency(const struct audio_recv *ar);
bool aurecv_started(const struct audio_recv *ar);
bool aurecv_filt_empty(const struct audio_recv *ar);
bool aurecv_level_set(const struct audio_recv *ar);
double aurecv_level(const struct audio_recv *ar);
int aurecv_debug(struct re_printf *pf, const struct audio_recv *ar);
int aurecv_print_pipeline(struct re_printf *pf, const struct audio_recv *ar);


/*
 * Call Control
 */

enum {
	CALL_LINENUM_MIN  =   1,
	CALL_LINENUM_MAX  = 256
};

struct call;

/** Call parameters */
struct call_prm {
	struct sa laddr;
	enum vidmode vidmode;
	int af;
	bool use_rtp;
};

int  call_alloc(struct call **callp, const struct config *cfg,
		struct list *lst,
		const char *local_name, const char *local_uri,
		struct account *acc, struct ua *ua, const struct call_prm *prm,
		const struct sip_msg *msg, struct call *xcall,
		struct dnsc *dnsc,
		call_event_h *eh, void *arg);
int  call_accept(struct call *call, struct sipsess_sock *sess_sock,
		 const struct sip_msg *msg);
int  call_sdp_get(const struct call *call, struct mbuf **descp, bool offer);
int  call_info(struct re_printf *pf, const struct call *call);
int  call_reset_transp(struct call *call, const struct sa *laddr);
int  call_af(const struct call *call);
void call_set_xrtpstat(struct call *call);
void call_set_custom_hdrs(struct call *call, const struct list *hdrs);
const struct sa *call_laddr(const struct call *call);
int call_streams_alloc(struct call *call);

/*
* Custom headers
*/
int custom_hdrs_print(struct re_printf *pf,
		       const struct list *custom_hdrs);

/*
 * Conf
 */

int conf_get_csv(const struct conf *conf, const char *name,
		 char *str1, size_t sz1, char *str2, size_t sz2);


struct metric;

int      metric_init(struct metric *metric);
void     metric_reset(struct metric *metric);
void     metric_add_packet(struct metric *metric, size_t packetsize);
double   metric_avg_bitrate(const struct metric *metric);
uint32_t metric_n_packets(struct metric *metric);
uint32_t metric_n_bytes(struct metric *metric);
uint32_t metric_n_err(struct metric *metric);
uint32_t metric_bitrate(struct metric *metric);
void     metric_inc_err(struct metric *metric);

struct metric *metric_alloc(void);

/*
 * Module
 */

int module_init(const struct conf *conf);


/*
 * Register client
 */

struct reg;

int  reg_add(struct list *lst, struct ua *ua, int regid);
int  reg_register(struct reg *reg, const char *reg_uri,
		    const char *params, uint32_t regint, const char *outbound);
void reg_unregister(struct reg *reg);
void reg_stop(struct reg *reg);
bool reg_isok(const struct reg *reg);
bool reg_failed(const struct reg *reg);
int  reg_debug(struct re_printf *pf, const struct reg *reg);
int  reg_json_api(struct odict *od, const struct reg *reg);
int  reg_status(struct re_printf *pf, const struct reg *reg);
int  reg_af(const struct reg *reg);
const struct sa *reg_laddr(const struct reg *reg);
const struct sa *reg_paddr(const struct reg *reg);
void reg_set_custom_hdrs(struct reg *reg, const struct list *hdrs);

/*
 * RTP Stats
 */

int rtpstat_print(struct re_printf *pf, const struct call *call);

/*
 * STUN URI
 */

int stunuri_decode_uri(struct stun_uri **sup, const struct uri *uri);


/*
 * SDP
 */

int sdp_decode_multipart(const struct pl *ctype_prm, struct mbuf *mb);


/* bundle (per media stream) */


enum bundle_state {
	BUNDLE_NONE = 0,
	BUNDLE_BASE,
	BUNDLE_MUX
};

struct bundle;

int  bundle_alloc(struct bundle **bunp);
void bundle_handle_extmap(struct bundle *bun, struct sdp_media *sdp);
int  bundle_start_socket(struct bundle *bun, struct udp_sock *us,
			 struct list *streaml);
enum bundle_state bundle_state(const struct bundle *bun);
uint8_t bundle_extmap_mid(const struct bundle *bun);
int bundle_set_extmap(struct bundle *bun, struct sdp_media *sdp,
		      uint8_t extmap_mid);
void bundle_set_state(struct bundle *bun, enum bundle_state st);
int  bundle_debug(struct re_printf *pf, const struct bundle *bun);


const char *bundle_state_name(enum bundle_state st);


/*
 * Stream
 */

enum media_type {
	MEDIA_AUDIO = 0,
	MEDIA_VIDEO,
};

struct sender;
struct rtp_receiver;
struct stream;
struct rtp_header;

enum {STREAM_PRESZ = 4+12}; /* same as RTP_HEADER_SIZE */

typedef void (stream_rtp_h)(const struct rtp_header *hdr,
			    struct rtpext *extv, size_t extc,
			    struct mbuf *mb, unsigned lostc, bool *ignore,
			    void *arg);
typedef int (stream_pt_h)(uint8_t pt, struct mbuf *mb, void *arg);


int  stream_alloc(struct stream **sp, struct list *streaml,
		  const struct stream_param *prm,
		  const struct config_avt *cfg,
		  struct sdp_session *sdp_sess,
		  enum media_type type,
		  const struct mnat *mnat, struct mnat_sess *mnat_sess,
		  const struct menc *menc, struct menc_sess *menc_sess,
		  bool offerer,
		  stream_rtp_h *rtph, stream_rtcp_h *rtcph, stream_pt_h *pth,
		  void *arg);
void stream_hold(struct stream *s, bool hold);
void stream_set_ldir(struct stream *s, enum sdp_dir dir);
void stream_set_rtcp_interval(struct stream *s, uint32_t n);
void stream_set_srate(struct stream *s, uint32_t srate_tx, uint32_t srate_rx);
bool stream_is_ready(const struct stream *strm);
int  stream_print(struct re_printf *pf, const struct stream *s);
void stream_remove_menc_media_state(struct stream *strm);
enum media_type stream_type(const struct stream *strm);
enum sdp_dir stream_ldir(const struct stream *s);
struct rtp_sock *stream_rtp_sock(const struct stream *strm);
const struct sa *stream_raddr(const struct stream *strm);
const char *stream_mid(const struct stream *strm);
uint8_t stream_generate_extmap_id(struct stream *strm);

/* Send */
void stream_update_encoder(struct stream *s, int pt_enc);
int  stream_pt_enc(const struct stream *strm);
int  stream_send(struct stream *s, bool ext, bool marker, int pt, uint32_t ts,
		 struct mbuf *mb);
int  stream_resend(struct stream *s, uint16_t seq, bool ext, bool marker,
		  int pt, uint32_t ts, struct mbuf *mb);

/* Receive */
void stream_flush(struct stream *s);
int  stream_ssrc_rx(const struct stream *strm, uint32_t *ssrc);


struct bundle *stream_bundle(const struct stream *strm);
void stream_parse_mid(struct stream *strm);
void stream_enable_bundle(struct stream *strm, enum bundle_state st);
void stream_enable_natpinhole(struct stream *strm, bool enable);
void stream_open_natpinhole(struct stream *strm);
void stream_stop_natpinhole(struct stream *strm);
void stream_process_rtcp(struct stream *strm, struct rtcp_msg *msg);
void stream_mnat_connected(struct stream *strm, const struct sa *raddr1,
			   const struct sa *raddr2);


/*
 * User-Agent
 */

struct ua;

void         ua_printf(const struct ua *ua, const char *fmt, ...);

int ua_print_allowed(struct re_printf *pf, const struct ua *ua);
int ua_print_require(struct re_printf *pf, const struct ua *ua);
struct call *ua_find_call_onhold(const struct ua *ua);
struct call *ua_find_active_call(struct ua *ua);
void ua_handle_options(struct ua *ua, const struct sip_msg *msg);
bool ua_handle_refer(struct ua *ua, const struct sip_msg *msg);
void sipsess_conn_handler(const struct sip_msg *msg, void *arg);
bool ua_catchall(struct ua *ua);
bool ua_reghasladdr(const struct ua *ua, const struct sa *laddr);
int uas_req_auth(struct ua *ua, const struct sip_msg *msg);

/*
 * User-Agent Group
 */

struct uag {
	struct config_sip *cfg;        /**< SIP configuration               */
	struct list ual;               /**< List of User-Agents (struct ua) */
	struct sip *sip;               /**< SIP Stack                       */
	struct sip_lsnr *lsnr;         /**< SIP Listener                    */
	struct sipsess_sock *sock;     /**< SIP Session socket              */
	struct sipevent_sock *evsock;  /**< SIP Event socket                */
	uint32_t transports;           /**< Supported transports mask       */
	bool delayed_close;            /**< Module will close SIP stack     */
	sip_msg_h *subh;               /**< Subscribe handler               */
	ua_exit_h *exith;              /**< UA Exit handler                 */
	bool nodial;                   /**< Prevent outgoing calls          */
	void *arg;                     /**< UA Exit handler argument        */
	char *eprm;                    /**< Extra UA parameters             */
#ifdef USE_TLS
	struct tls *tls;               /**< TLS Context                     */
	struct tls *wss_tls;           /**< Secure websocket TLS Context    */
#endif
};

struct config_sip *uag_cfg(void);
const char *uag_eprm(void);
bool uag_delayed_close(void);
sip_msg_h *uag_subh(void);
int uag_raise(struct ua *ua, struct le *le);

void u32mask_enable(uint32_t *mask, uint8_t bit, bool enable);
bool u32mask_enabled(uint32_t mask, uint8_t bit);


/*
 * Video Stream
 */

struct video;

int  video_decoder_set(struct video *v, struct vidcodec *vc, int pt_rx,
		       const char *fmtp);
int  video_print(struct re_printf *pf, const struct video *v);


/*
 * Timestamp helpers
 */


/**
 * This struct is used to keep track of timestamps for
 * incoming RTP packets.
 */
struct timestamp_recv {
	uint32_t first;
	uint32_t last;
	bool is_set;
	unsigned num_wraps;
};


int      timestamp_wrap(uint32_t ts_new, uint32_t ts_old);
void     timestamp_set(struct timestamp_recv *ts, uint32_t rtp_ts);
uint64_t timestamp_duration(const struct timestamp_recv *ts);
uint64_t timestamp_calc_extended(uint32_t num_wraps, uint32_t ts);
double   timestamp_calc_seconds(uint64_t ts, uint32_t clock_rate);


/*
 * WebRTC Media Track
 */


typedef void (mediatrack_close_h)(int err, void *arg);

/*
 * https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack
 *
 * The MediaStreamTrack interface represents a single media track within
 * a stream; typically, these are audio or video tracks, but other
 * track types may exist as well.
 *
 * NOTE: one-to-one mapping with 'struct stream'
 */
struct media_track {
	struct le le;
	enum media_kind kind;
	union {
		struct audio *au;
		struct video *vid;
		void *p;
	} u;

	bool ice_conn;
	bool dtls_ok;
	bool rtp;
	bool rtcp;

	mediatrack_close_h *closeh;
	void *arg;
};


struct media_track *media_track_add(struct list *lst,
				    enum media_kind kind,
				    mediatrack_close_h *closeh, void *arg);
void mediatrack_stop(struct media_track *media);
void mediatrack_set_handlers(struct media_track *media);
void mediatrack_summary(const struct media_track *media);
int  mediatrack_debug(struct re_printf *pf, const struct media_track *media);
struct media_track *mediatrack_lookup_media(const struct list *medial,
					    struct stream *strm);
void mediatrack_close(struct media_track *media, int err);
void mediatrack_sdp_attr_decode(struct media_track *media);

/*
 * Stream RTP receiver
 */
int  rtprecv_alloc(struct rtp_receiver **rxp,
		   struct stream *strm,
		   const char *name,
		   const struct config_avt *cfg,
		   stream_rtp_h *rtph,
		   stream_pt_h *pth, void *arg);
void rtprecv_set_handlers(struct rtp_receiver *rx,
			  stream_rtpestab_h *rtpestabh, void *arg);
struct metric *rtprecv_metric(struct rtp_receiver *rx);
struct jbuf *rtprecv_jbuf(struct rtp_receiver *rx);
void rtprecv_decode(const struct sa *src, const struct rtp_header *hdr,
		    struct mbuf *mb, void *arg);
void rtprecv_handle_rtcp(const struct sa *src, struct rtcp_msg *msg,
			 void *arg);
void rtprecv_set_socket(struct rtp_receiver *rx, struct rtp_sock *rtp);
void rtprecv_set_ssrc(struct rtp_receiver *rx, uint32_t ssrc);
uint64_t rtprecv_ts_last(struct rtp_receiver *rx);
void rtprecv_set_ts_last(struct rtp_receiver *rx, uint64_t ts_last);
void rtprecv_flush(struct rtp_receiver *rx);
void rtprecv_enable(struct rtp_receiver *rx, bool enable);
int  rtprecv_get_ssrc(struct rtp_receiver *rx, uint32_t *ssrc);
void rtprecv_enable_mux(struct rtp_receiver *rx, bool enable);
int  rtprecv_debug(struct re_printf *pf, const struct rtp_receiver *rx);
int  rtprecv_start_thread(struct rtp_receiver *rx);
void rtprecv_mnat_connected_handler(const struct sa *raddr1,
				    const struct sa *raddr2, void *arg);
int  rtprecv_start_rtcp(struct rtp_receiver *rx, const char *cname,
			const struct sa *peer, bool pinhole);
bool rtprecv_running(const struct rtp_receiver *rx);
void rtprecv_set_srate(struct rtp_receiver *rx, uint32_t srate);
