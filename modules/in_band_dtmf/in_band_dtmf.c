/**
 * @file in_band_dtmf.c  DTMF decoder
 */
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup in_band_dtmf in_band_dtmf
 *
 * Audio filter that de- and encodes DTMF tones.
 *
 * The tone length of an encoded tone is 0.1s.
 * New tones can be added while encoding is active.
 */


struct in_band_dtmf_filt_dec {
	struct aufilt_enc_st af;  /* inheritance */
	struct dtmf_dec *dec;
	struct ua *ua;
	struct call *call;
	struct le le_priv;
};


struct in_band_dtmf_filt_enc {
	struct aufilt_enc_st af;  /* inheritance */
	struct mbuf *mb;
	unsigned srate;
	struct le le_priv;
};


static struct list encs;
static struct list decs;


static void in_band_dtmf_dec_handler(char digit, void *arg)
{
	(void)arg;
	char key_str[2];
	struct in_band_dtmf_filt_dec *st;

	key_str[0] = digit;
	key_str[1] = '\0';

	if (!list_count(&decs)) {
		warning("in_band_dtmf: no active call\n");
		return;
	}

	st = decs.head->data;

	ua_event(st->ua, UA_EVENT_CALL_DTMF_START, st->call, "%s", key_str);
	ua_event(st->ua, UA_EVENT_CALL_DTMF_END, st->call, NULL);
}


static void enc_destructor(void *arg)
{
	struct in_band_dtmf_filt_enc *st =
			(struct in_band_dtmf_filt_enc *) arg;

	list_unlink(&st->af.le);
	list_unlink(&st->le_priv);
	mem_deref(st->mb);
}


static void dec_destructor(void *arg)
{
	struct in_band_dtmf_filt_dec *st =
			(struct in_band_dtmf_filt_dec *) arg;

	list_unlink(&st->af.le);
	list_unlink(&st->le_priv);
	mem_deref(st->dec);
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct in_band_dtmf_filt_enc *st;
	int err = 0;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	st->mb = mbuf_alloc(0);
	if (!st->mb) {
		mem_deref(st);
		err = ENOMEM;
	}
	else {
		st->srate = prm->srate;
		list_append(&encs, &st->le_priv, st);
		*stp = (struct aufilt_enc_st *)st;
	}

	return err;
}


static int encode(struct aufilt_enc_st *aufilt_enc_st, struct auframe *af)
{
	struct in_band_dtmf_filt_enc *st = (void *)aufilt_enc_st;
	uint16_t* data = af->sampv;
	uint16_t i;

	if (mbuf_get_left(st->mb)) {
		af->fmt = AUFMT_S16LE; /* TODO: Take care about format? */
		for (i = 0; (i < af->sampc) && (mbuf_get_left(st->mb)); ++i)
			data[i] = mbuf_read_u16(st->mb);
		if (!mbuf_get_left(st->mb))
			mbuf_reset(st->mb);
	}

	return 0;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct in_band_dtmf_filt_dec *st;
	int err = 0;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	list_append(&decs, &st->le_priv, st);
	st->ua = NULL;
	st->call = NULL;

	err = dtmf_dec_alloc(&st->dec, prm->srate, prm->ch,
			in_band_dtmf_dec_handler, NULL);

	if (err)
		mem_deref(st);
	else
		*stp = (struct aufilt_dec_st *)st;

	return err;
}


static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct in_band_dtmf_filt_dec *sf = (struct in_band_dtmf_filt_dec *)st;

	if (!st || !af)
		return EINVAL;

	/* TODO: Take care of float format? */
	dtmf_dec_probe(sf->dec, af->sampv, af->sampc);

	return 0;
}


static void print_usage(void)
{
	info("in_band_dtmf: Missing parameter. Usage:\n"
			"in_band_dtmf_send <sequence>\n"
			"sequence Sequence of DTMF tones to encode.\n");
}


/**
 * Add new DTMF tones
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument. See \ref cmdv !
 *
 * @return	0 if success, otherwise error code.
 */
static int in_band_dtmf_send(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *digits = carg->prm;
	struct in_band_dtmf_filt_enc *st;
	int err = 0;
	size_t i;
	char digit;
	size_t old_pos;
	(void)pf;

	if (!list_count(&encs)) {
		warning("in_band_dtmf: no active call\n");
		return EINVAL;
	}

	if (!digits) {
		print_usage();
		return EINVAL;
	}

	st = encs.head->data;
	old_pos = st->mb->pos;
	mbuf_skip_to_end(st->mb);

	for (i = 0; i < strlen(digits); ++i) {
		digit = toupper(digits[i]);
		switch (digit) {

		case '1': case '2': case '3': case 'A':
		case '4': case '5': case '6': case 'B':
		case '7': case '8': case '9': case 'C':
		case '*': case '0': case '#': case 'D':
			err |= autone_dtmf(st->mb, st->srate, digit);
			/* Reduce tone length to 0.1s */
			mbuf_set_end(st->mb,
				st->mb->end - 2 * 0.9f * st->srate);
			break;

		default: warning("in_band_dtmf: skip unsupported DTMF "
				"character: %c\n", digit);
			break;
		}
	}

	mbuf_set_pos(st->mb, old_pos);

	return err;
}


static struct aufilt in_band_dtmf = {
	.name    = "in_band_dtmf",
	.encupdh = encode_update,
	.ench    = encode,
	.decupdh = decode_update,
	.dech    = decode
};


/**
 * \struct cmdv
 * The commands for this module.
 * in_band_dtmf_send expects a single parameter.
 *	- A string that will be splitted into single characters.
 *	  Each valid DTMF character will be send as in-band DTMF tone.
 *
 *	E.g. "1234"
 */
static const struct cmd cmdv[] = {

{"in_band_dtmf_send", 0, CMD_PRM, "Send digit(s) as in-band DTMF tone",
	in_band_dtmf_send},

};

static void ua_event_handler(struct ua *ua, enum ua_event ev,
		struct call *call, const char *prm, void *arg)
{
	(void)prm;
	(void)arg;
	struct in_band_dtmf_filt_dec *st;

	switch (ev) {

	case UA_EVENT_CALL_ESTABLISHED:
		if (!list_count(&decs)) {
			warning("in_band_dtmf: no active call\n");
			return;
		}
		st = decs.head->data;
		st->ua = ua;
		st->call = call;
		break;

	default:
		break;
	}
}

static int module_init(void)
{
	int err;
	aufilt_register(baresip_aufiltl(), &in_band_dtmf);

	err = cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
	err |= uag_event_register(ua_event_handler, 0);

	return err;
}


static int module_close(void)
{
	cmd_unregister(baresip_commands(), cmdv);
	aufilt_unregister(&in_band_dtmf);
	uag_event_unregister(ua_event_handler);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(in_band_dtmf) = {
	"in_band_dtmf",
	"filter",
	module_init,
	module_close
};

