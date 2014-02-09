/**
 * @file sess.c  AudioUnit sound driver - session
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <re.h>
#include <baresip.h>
#include "audiounit.h"


struct audiosess {
	struct list sessl;
};


struct audiosess_st {
	struct audiosess *as;
	struct le le;
	audiosess_int_h *inth;
	void *arg;
};


static struct audiosess *gas;


#if TARGET_OS_IPHONE
static void propListener(void *inClientData, AudioSessionPropertyID inID,
			 UInt32 inDataSize, const void *inData)
{
	struct audiosess *sess = inClientData;
	CFDictionaryRef dref = inData;
	CFNumberRef nref;
	SInt32 reason = 0;

	(void)inDataSize;
	(void)sess;

	if (kAudioSessionProperty_AudioRouteChange != inID)
		return;

	nref = CFDictionaryGetValue(
			dref,
			CFSTR(kAudioSession_AudioRouteChangeKey_Reason)
			);

	CFNumberGetValue(nref, kCFNumberSInt32Type, &reason);

	info("audiounit: AudioRouteChange - reason %d\n", reason);
}
#endif


static void sess_destructor(void *arg)
{
	struct audiosess_st *st = arg;

	list_unlink(&st->le);
	mem_deref(st->as);
}


static void destructor(void *arg)
{
	struct audiosess *as = arg;
#if TARGET_OS_IPHONE
	AudioSessionPropertyID id = kAudioSessionProperty_AudioRouteChange;

	AudioSessionRemovePropertyListenerWithUserData(id, propListener, as);
	AudioSessionSetActive(false);
#endif

	list_flush(&as->sessl);

	gas = NULL;
}


int audiosess_alloc(struct audiosess_st **stp,
		    audiosess_int_h *inth, void *arg)
{
	struct audiosess_st *st = NULL;
	struct audiosess *as = NULL;
	int err = 0;
	bool created = false;
#if TARGET_OS_IPHONE
	AudioSessionPropertyID id = kAudioSessionProperty_AudioRouteChange;
	UInt32 category;
	OSStatus ret;
#endif

	if (!stp)
		return EINVAL;

#if TARGET_OS_IPHONE
	/* Must be done for all modules */
	category = kAudioSessionCategory_PlayAndRecord;
	ret = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory,
				      sizeof(category), &category);
	if (ret) {
		warning("audiounit: Audio Category: %d\n", ret);
		return EINVAL;
	}
#endif

	if (gas)
		goto makesess;

	as = mem_zalloc(sizeof(*as), destructor);
	if (!as)
		return ENOMEM;

#if TARGET_OS_IPHONE
	ret = AudioSessionSetActive(true);
	if (ret) {
		warning("audiounit: AudioSessionSetActive: %d\n", ret);
		err = ENOSYS;
		goto out;
	}

	ret = AudioSessionAddPropertyListener(id, propListener, as);
	if (ret) {
		warning("audiounit: AudioSessionAddPropertyListener: %d\n",
			ret);
		err = EINVAL;
		goto out;
	}
#endif

	gas = as;
	created = true;

 makesess:
	st = mem_zalloc(sizeof(*st), sess_destructor);
	if (!st) {
		err = ENOMEM;
		goto out;
	}
	st->inth = inth;
	st->arg = arg;
	st->as = created ? gas : mem_ref(gas);

	list_append(&gas->sessl, &st->le, st);

 out:
	if (err) {
		mem_deref(as);
		mem_deref(st);
	}
	else {
		*stp = st;
	}

	return err;
}


void audiosess_interrupt(bool start)
{
	struct le *le;

	if (!gas)
		return;

	for (le = gas->sessl.head; le; le = le->next) {

		struct audiosess_st *st = le->data;

		if (st->inth)
			st->inth(start, st->arg);
	}
}
