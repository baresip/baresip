/**
 * @file menu.h
 *
 * Copyright (c) 2020 Commend.com - c.huber@commend.com
 */


/** Defines the status modes */
enum statmode {
	STATMODE_CALL = 0,
	STATMODE_OFF,
};


struct menu{
	struct tmr tmr_stat;          /**< Call status timer              */
	struct play *play;            /**< Current audio player state     */
	struct mbuf *dialbuf;         /**< Buffer for dialled number      */
	struct call *xfer_call;       /**< Attended transfer call         */
	struct call *xfer_targ;       /**< Transfer target call           */
	struct call *curcall;         /**< Call-id of current call        */
	bool ringback_disabled;       /**< no ringback on sip 180 response*/
	bool ringback;                /**< Ringback played currently      */
	struct tmr tmr_redial;        /**< Timer for auto-reconnect       */
	struct tmr tmr_invite;        /**< Timer for follow up invite     */
	uint32_t redial_delay;        /**< Redial delay in [seconds]      */
	uint32_t redial_attempts;     /**< Number of re-dial attempts     */
	uint32_t current_attempts;    /**< Current number of re-dials     */
	uint64_t start_ticks;         /**< Ticks when app started         */
	enum statmode statmode;       /**< Status mode                    */
	bool clean_number;            /**< Remove -/() from diald numbers */
	char *invite_uri;             /**< Follow up invite URI           */
	char redial_aor[128];
	int32_t adelay;               /**< Outgoing auto answer delay     */
	char *ansval;                 /**< Call-Info/Alert-Info value     */
	struct odict *ovaufile;       /**< Override aufile dictionary     */
	struct tmr tmr_play;          /**< Tones play timer               */
	size_t outcnt;                /**< Outgoing call counter          */
	bool dnd;                     /**< Do not disturb flag            */
	bool message_tone;            /**< Play tone for SIP MESSAGE      */
};

struct call_params {
	struct ua *ua;	              /**< User-Agent                     */
	struct pl callid;             /**< Call-ID                        */
	struct pl dname;              /**< Display name                   */
	struct pl uri;	              /**< SIP URI                        */
	enum sdp_dir adir;	      /**< Audio media direction          */
	enum sdp_dir vdir;	      /**< Video media direction          */
	struct pl from;		      /**< From URI                       */
	struct pl userdata;	      /**< User data                      */

	char *req_uri;	              /**< Request URI                    */
};

/*Get menu object*/
struct menu *menu_get(void);

/* Active call and UA */
void menu_selcall(struct call *call);
struct call *menu_callcur(void);
struct ua   *menu_uacur(void);
struct ua   *menu_ua_carg(struct re_printf *pf, const struct cmd_arg *carg,
		struct pl *word1, struct pl *word2);


/*Dynamic menu related functions*/
int dynamic_menu_register(void);
void dynamic_menu_unregister(void);


/*Static menu related functions*/
int static_menu_register(void);
void static_menu_unregister(void);

int dial_menu_register(void);
void dial_menu_unregister(void);


/* Generic menu functions */
void menu_update_callstatus(bool incall);
int  menu_param_decode(const char *prm, const char *name, struct pl *val);
int menu_get_call_ua(struct re_printf *pf, const struct cmd_arg *carg,
		     struct ua **uap, struct call **callp);
struct call *menu_find_call(call_match_h *matchh, const struct call *exclude);
struct call *menu_find_call_state(enum call_state st);

int menu_call_params_alloc(struct call_params **cparp);
int menu_call_params_decode(struct call_params *cp, const char *prm,
			    struct re_printf *pf);
int menu_decode_dial_params(struct call_params **cparp,
			    const struct cmd_arg *carg, struct re_printf *pf);
