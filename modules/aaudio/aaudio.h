/**
 * @file aaudio.h  Aaudio sound driver
 *
 */


#include <aaudio/AAudio.h>

extern AAudioStream *playerStream;
extern AAudioStream *recorderStream;

int aaudio_player_alloc(struct auplay_st **stp, const struct auplay *ap,
	struct auplay_prm *prm, const char *device,
	auplay_write_h *wh, void *arg);

int aaudio_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			  struct ausrc_prm *prm, const char *device,
			  ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
