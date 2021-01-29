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
	struct tmr tmr_alert;         /**< Incoming call alert timer      */
	struct tmr tmr_stat;          /**< Call status timer              */
	struct play *play;            /**< Current audio player state     */
	struct mbuf *dialbuf;         /**< Buffer for dialled number      */
	struct le *le_cur;            /**< Current User-Agent (struct ua) */
	struct ua *ua_cur;
	bool bell;                    /**< ANSI Bell alert enabled        */
	bool ringback_disabled;	      /**< no ringback on sip 180 respons */
	struct tmr tmr_redial;        /**< Timer for auto-reconnect       */
	uint32_t redial_delay;        /**< Redial delay in [seconds]      */
	uint32_t redial_attempts;     /**< Number of re-dial attempts     */
	uint32_t current_attempts;    /**< Current number of re-dials     */
	uint64_t start_ticks;         /**< Ticks when app started         */
	enum statmode statmode;       /**< Status mode                    */
	bool clean_number;            /**< Remove -/() from diald numbers */
	char redial_aor[128];
	int32_t adelay;               /**< Outgoing auto answer delay     */
};

/*Get menu object*/
struct menu *menu_get(void);
void menu_uacur_set(struct ua *ua);
struct ua *menu_uacur(void);


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
