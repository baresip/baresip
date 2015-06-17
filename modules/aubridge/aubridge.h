/**
 * @file aubridge.h Audio bridge -- internal interface
 *
 * Copyright (C) 2010 Creytiv.com
 */


struct device;

struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	struct device *dev;
	struct ausrc_prm prm;
	ausrc_read_h *rh;
	void *arg;
};

struct auplay_st {
	const struct auplay *ap;      /* inheritance */
	struct device *dev;
	struct auplay_prm prm;
	auplay_write_h *wh;
	void *arg;
};


extern struct hash *ht_device;


int play_alloc(struct auplay_st **stp, const struct auplay *ap,
	       struct auplay_prm *prm, const char *device,
	       auplay_write_h *wh, void *arg);
int src_alloc(struct ausrc_st **stp, const struct ausrc *as,
	      struct media_ctx **ctx,
	      struct ausrc_prm *prm, const char *device,
	      ausrc_read_h *rh, ausrc_error_h *errh, void *arg);


int  device_connect(struct device **devp, const char *device,
		    struct auplay_st *auplay, struct ausrc_st *ausrc);
void device_stop(struct device *dev);
