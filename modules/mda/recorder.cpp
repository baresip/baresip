/**
 * @file recorder.cpp  Symbian MDA audio driver -- recorder
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <e32def.h>
#include <e32std.h>
#include <mdaaudioinputstream.h>
#include <mda/common/audio.h>

extern "C" {
#include <re.h>
#include <baresip.h>
#include "mda.h"

#define DEBUG_MODULE "recorder"
#define DEBUG_LEVEL 5
#include <re_dbg.h>
}


enum {VOLUME = 100};

class mda_recorder;
struct ausrc_st {
	struct ausrc *as;      /* inheritance */
	mda_recorder *mda;
	ausrc_read_h *rh;
	void *arg;
};


class mda_recorder : public MMdaAudioInputStreamCallback, public CBase
{
public:
	mda_recorder(struct ausrc_st *st, struct ausrc_prm *prm);
	~mda_recorder();

	/* from MMdaAudioInputStreamCallback */
	virtual void MaiscOpenComplete(TInt aError);
	virtual void MaiscBufferCopied(TInt aError, const TDesC8& aBuffer);
	virtual void MaiscRecordComplete(TInt aError);

private:
	CMdaAudioInputStream *iInput;
	TMdaAudioDataSettings iSettings;
	TBool iIsReady;
	TBuf8<320> iBuf;
	struct ausrc_st *state;
};


mda_recorder::mda_recorder(struct ausrc_st *st, struct ausrc_prm *prm)
	:iIsReady(EFalse)
{
	state = st;

	iInput = CMdaAudioInputStream::NewL(*this);

	iSettings.iSampleRate = convert_srate(prm->srate);
	iSettings.iChannels = convert_channels(prm->ch);
	iSettings.iVolume = VOLUME;

	iInput->Open(&iSettings);
}


mda_recorder::~mda_recorder()
{
	if (iInput) {
		iInput->Stop();
		delete iInput;
	}
}


void mda_recorder::MaiscOpenComplete(TInt aError)
{
	if (KErrNone != aError) {
		DEBUG_WARNING("MaiscOpenComplete %d\n", aError);
		return;
	}

	iInput->SetGain(iInput->MaxGain());
	iInput->SetAudioPropertiesL(iSettings.iSampleRate,
				    iSettings.iChannels);
	iInput->SetPriority(EMdaPriorityNormal,
			    EMdaPriorityPreferenceTime);

	TRAPD(ret, iInput->ReadL(iBuf));
	if (KErrNone != ret) {
		DEBUG_WARNING("ReadL left with %d\n", ret);
	}
}


void mda_recorder::MaiscBufferCopied(TInt aError, const TDesC8& aBuffer)
{
	if (KErrNone != aError) {
		DEBUG_WARNING("MaiscBufferCopied: error=%d %d bytes\n",
			      aError, aBuffer.Length());
		return;
	}

	state->rh(aBuffer.Ptr(), aBuffer.Length(), state->arg);

	iBuf.Zero();
	TRAPD(ret, iInput->ReadL(iBuf));
	if (KErrNone != ret) {
		DEBUG_WARNING("ReadL left with %d\n", ret);
	}
}


void mda_recorder::MaiscRecordComplete(TInt aError)
{
	DEBUG_NOTICE("MaiscRecordComplete: error=%d\n", aError);

#if 0
	if (KErrOverflow == aError) {

		/* re-open input stream */
		iInput->Stop();
		iInput->Open(&iSettings);
	}
#endif
}


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = (struct ausrc_st *)arg;

	delete st->mda;

	mem_deref(st->as);
}


int mda_recorder_alloc(struct ausrc_st **stp, struct ausrc *as,
		       struct media_ctx **ctx,
		       struct ausrc_prm *prm, const char *device,
		       ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err = 0;

	(void)ctx;
	(void)device;
	(void)errh;

	st = (struct ausrc_st *)mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = (struct ausrc *)mem_ref(as);
	st->rh  = rh;
	st->arg = arg;

	st->mda = new mda_recorder(st, prm);
	if (!st->mda)
		err = ENOMEM;

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
