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
	char *callid;                 /**< Call-id of active call         */
	bool ringback_disabled;       /**< no ringback on sip 180 respons */
	bool ringback;                /**< Ringback played currently      */
	struct tmr tmr_redial;        /**< Timer for auto-reconnect       */
	uint32_t redial_delay;        /**< Redial delay in [seconds]      */
	uint32_t redial_attempts;     /**< Number of re-dial attempts     */
	uint32_t current_attempts;    /**< Current number of re-dials     */
	uint64_t start_ticks;         /**< Ticks when app started         */
	enum statmode statmode;       /**< Status mode                    */
	bool clean_number;            /**< Remove -/() from diald numbers */
	char redial_aor[128];
	int32_t adelay;               /**< Outgoing auto answer delay     */
	char *ansval;                 /**< Call-Info/Alert-Info value     */
	struct odict *ovaufile;       /**< Override aufile dictionary     */
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


/*Generic menu funtions*/
void menu_update_callstatus(bool incall);
int  menu_param_decode(const char *prm, const char *name, struct pl *val);
struct call *menu_find_call(call_match_h *matchh);
struct call *menu_find_call_state(enum call_state st);
enum sdp_dir decode_sdp_enum(const struct pl *pl);
