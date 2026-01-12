/**
 * @file call.h  Call tests -- internal API
 *
 * Copyright (C) 2026 Alfred E. Heggestad, Christian Spielberger
 */


#define MAGIC 0x7004ca11

enum {
	IP_127_0_0_1 = 0x7f000001,
};

enum behaviour {
	BEHAVIOUR_ANSWER = 0,
	BEHAVIOUR_NOTHING,
	BEHAVIOUR_REJECT,
	BEHAVIOUR_REJECTF,
	BEHAVIOUR_GET_HDRS,
	BEHAVIOUR_PROGRESS,
};

enum action {
	ACTION_RECANCEL = 0,
	ACTION_HANGUP_A,
	ACTION_HANGUP_B,
	ACTION_NOTHING,
	ACTION_TRANSFER,
	ACTION_ATT_TRANSFER,
};


struct cancel_rule {
	struct le le;

	enum bevent_ev ev;
	const char *prm;
	struct ua *ua;
	bool checkack;

	unsigned n_incoming;
	unsigned n_progress;
	unsigned n_established;
	unsigned n_audio_estab;
	unsigned n_video_estab;
	unsigned n_offer_cnt;
	unsigned n_answer_cnt;
	unsigned n_vidframe;
	unsigned n_auframe;
	unsigned n_audebug;
	unsigned n_rtcp;
	unsigned n_closed;
	double aulvl;

	struct cancel_rule *cr_and;
	bool met;
};

struct agent {
	struct fixture *fix;    /* pointer to parent */
	struct agent *peer;
	struct ua *ua;
	uint16_t close_scode;
	char *close_prm;
	bool failed;

	unsigned n_incoming;
	unsigned n_progress;
	unsigned n_established;
	unsigned n_closed;
	unsigned n_transfer_fail;
	unsigned n_dtmf_recv;
	unsigned n_transfer;
	unsigned n_mediaenc;
	unsigned n_rtpestab;
	unsigned n_rtcp;
	unsigned n_audio_estab;
	unsigned n_video_estab;
	unsigned n_offer_cnt;
	unsigned n_answer_cnt;
	unsigned n_hold_cnt;
	unsigned n_resume_cnt;
	unsigned n_vidframe;
	unsigned n_auframe;
	unsigned n_audebug;
	double aulvl;

	struct tmr tmr_ack;
	bool gotack;

	struct tmr tmr;
};


struct fixture {
	uint32_t magic;
	struct agent a, b, c;
	struct sa dst;
	struct sa laddr_udp;
	struct sa laddr_tcp;
	struct sa laddr_tls;
	enum behaviour behaviour;
	enum action estab_action;
	char buri[256];
	char buri_tcp[256];
	int err;
	struct call *xfer;
	unsigned exp_estab;
	unsigned exp_closed;
	bool fail_transfer;
	const struct list *hdrs;
	const char *dtmf_digits;
	struct list rules;
	char *command;
};


int fixture_init_priv(struct fixture *f, const char *prm);
void fixture_close(struct fixture *f);
void fixture_abort(struct fixture *f, int err);


#define fixture_init_prm(f, prm) \
	err = fixture_init_priv((f), (prm)); \
	TEST_ERR(err);

#define fixture_init(f) \
	err = fixture_init_priv((f), ("")); \
	TEST_ERR(err);

void process_rules(struct agent *ag, enum bevent_ev ev, const char *prm);


struct cancel_rule *fixture_add_cancel_rule(struct fixture *f,
					    enum bevent_ev ev,
					    struct ua *ua,
					    unsigned n_incoming,
					    unsigned n_progress,
					    unsigned n_established);
struct cancel_rule *cancel_rule_and_alloc(struct cancel_rule *cr,
					  enum bevent_ev ev,
					  struct ua *ua,
					  unsigned n_incoming,
					  unsigned n_progress,
					  unsigned n_established);
void failure_debug(struct fixture *f, bool c);
void check_ack(void *arg);
int agent_wait_for_ack(struct agent *ag, unsigned n_incoming,
		       unsigned n_progress, unsigned n_established);

#define cancel_rule_new(ev, ua, n_incoming, n_progress, n_established)    \
	cr = fixture_add_cancel_rule(f, ev, ua, n_incoming, n_progress,   \
				     n_established);			  \
	if (!cr) {							  \
		err = ENOMEM;						  \
		goto out;						  \
	}


#define cancel_rule_and(ev, ua, n_incoming, n_progress, n_established)	  \
	cr = cancel_rule_and_alloc(cr, ev, ua, n_incoming, n_progress,	  \
				   n_established);			  \
	if (!cr) {							  \
		err = ENOMEM;						  \
		goto out;						  \
	}


#define cancel_rule_pop()						  \
	mem_deref(list_tail(&f->rules)->data);

void fixture_delayed_command(struct fixture *f,
			     uint32_t delay_ms, const char *cmd);
