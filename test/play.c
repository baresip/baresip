/**
 * @file test/play.c  Baresip selftest -- audio file player
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


#define NUM_SAMPLES 320  /* 8000 Hz, 1 channel, 40ms */


struct test {
	struct mbuf *mb_samp;
};


static struct mbuf *generate_tone(void)
{
	struct mbuf *mb;
	unsigned i;
	int err = 0;

	mb = mbuf_alloc(NUM_SAMPLES * 2);
	if (!mb)
		return NULL;

	for (i=0; i<NUM_SAMPLES; i++)
		err |= mbuf_write_u16(mb, i);

	mb->pos = 0;

	if (err)
		return mem_deref(mb);
	else
		return mb;
}


static void sample_handler(const void *sampv, size_t sampc, void *arg)
{
	struct test *test = arg;
	size_t bytec = sampc * 2;
	int err = 0;

	if (!test->mb_samp) {
		test->mb_samp = mbuf_alloc(bytec);
		ASSERT_TRUE(test->mb_samp != NULL);
	}

	/* save the samples that was played */
	err = mbuf_write_mem(test->mb_samp, (void *)sampv, bytec);

 out:
	/* stop the test? */
	if (err || test->mb_samp->end >= (NUM_SAMPLES*2))
		re_cancel();
}


int test_play(void)
{
	struct auplay *auplay = NULL;
	struct player *player = NULL;
	struct play *play = NULL;
	struct mbuf *mb_tone = NULL;
	char *play_mod = NULL;
	char *play_dev = NULL;
	struct test test = {0};
	int err;

	/* use a mock audio-driver to save the audio-samples */
	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   sample_handler, &test);
	ASSERT_EQ(0, err);

	err = play_init(&player);
	ASSERT_EQ(0, err);

	mb_tone = generate_tone();
	ASSERT_TRUE(mb_tone != NULL);

	err = play_tone(&play, player, mb_tone, 8000, 1, 0,
	                play_mod, play_dev);
	ASSERT_EQ(0, err);

	err = re_main_timeout(10000);
	ASSERT_EQ(0, err);

	/* verify the audio-samples that was played */
	TEST_MEMCMP(mb_tone->buf, NUM_SAMPLES*2,
		    test.mb_samp->buf, test.mb_samp->end);

 out:
	mem_deref(test.mb_samp);
	mem_deref(mb_tone);
	mem_deref(play);
	mem_deref(player);
	mem_deref(auplay);
	return err;
}
