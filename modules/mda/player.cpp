/**
 * @file player.cpp  Symbian MDA audio driver -- player
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <e32def.h>
#include <e32std.h>
#include <mdaaudiooutputstream.h>
#include <mda/common/audio.h>

extern "C" {
#include <re.h>
#include <baresip.h>
#include "mda.h"

#define DEBUG_MODULE "player"
#define DEBUG_LEVEL 5
#include <re_dbg.h>
}


enum {VOLUME = 100};

class mda_player;
struct auplay_st {
	struct auplay *ap;      /* inheritance */
	mda_player *mda;
	auplay_write_h *wh;
	void *arg;
};


class mda_player : public MMdaAudioOutputStreamCallback, public CBase
{
public:
	mda_player(struct auplay_st *st, struct auplay_prm *prm);
	~mda_player();
	void play();

	/* from MMdaAudioOutputStreamCallback */
	virtual void MaoscOpenComplete(TInt aError);
	virtual void MaoscBufferCopied(TInt aError, const TDesC8& aBuffer);
	virtual void MaoscPlayComplete(TInt aError);

private:
	CMdaAudioOutputStream *iOutput;
	TMdaAudioDataSettings iSettings;
	TBool iIsReady;
	TBuf8<320> iBuf;
	struct auplay_st *state;
};


mda_player::mda_player(struct auplay_st *st, struct auplay_prm *prm)
	:iIsReady(EFalse)
{
	state = st;

	iBuf.FillZ(320);

	iSettings.iSampleRate = convert_srate(prm->srate);
	iSettings.iChannels = convert_channels(prm->ch);
	iSettings.iVolume = VOLUME;

	iOutput = CMdaAudioOutputStream::NewL(*this);
	iOutput->Open(&iSettings);
}


mda_player::~mda_player()
{
	if (iOutput) {
		iOutput->Stop();
		delete iOutput;
	}
}


void mda_player::play()
{
	/* call write handler here */
	state->wh((uint8_t *)&iBuf[0], iBuf.Length(), state->arg);

	TRAPD(ret, iOutput->WriteL(iBuf));
	if (KErrNone != ret) {
		DEBUG_WARNING("WriteL left with %d\n", ret);
	}
}


void mda_player::MaoscOpenComplete(TInt aError)
{
	if (KErrNone != aError) {
		iIsReady = EFalse;
		DEBUG_WARNING("mda player error: %d\n", aError);
		return;
	}

	iOutput->SetAudioPropertiesL(iSettings.iSampleRate,
				     iSettings.iChannels);
	iOutput->SetPriority(EMdaPriorityNormal,
			     EMdaPriorityPreferenceTime);

	iIsReady = ETrue;

	play();
}


/*
 * Note: In reality, this function is called approx. 1 millisecond after the
 * last block was played, hence we have to generate buffer N+1 while buffer N
 * is playing.
 */
void mda_player::MaoscBufferCopied(TInt aError, const TDesC8& aBuffer)
{
	(void)aBuffer;

	if (KErrNone != aError && KErrCancel != aError) {
		DEBUG_WARNING("MaoscBufferCopied [aError=%d]\n", aError);
	}
	if (aError == KErrAbort) {
		DEBUG_NOTICE("player aborted\n");
		return;
	}

	play();
}


void mda_player::MaoscPlayComplete(TInt aError)
{
	if (KErrNone != aError) {
		DEBUG_WARNING("MaoscPlayComplete [aError=%d]\n", aError);
	}
}


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = (struct auplay_st *)arg;

	delete st->mda;

	mem_deref(st->ap);
}


int mda_player_alloc(struct auplay_st **stp, struct auplay *ap,
		     struct auplay_prm *prm, const char *device,
		     auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err = 0;

	(void)device;

	st = (struct auplay_st *)mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = (struct auplay *)mem_ref(ap);
	st->wh  = wh;
	st->arg = arg;

	st->mda = new mda_player(st, prm);
	if (!st->mda)
		err = ENOMEM;

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
