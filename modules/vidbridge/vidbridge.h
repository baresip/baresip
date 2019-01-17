/**
 * @file vidbridge.h Video bridge -- internal interface
 *
 * Copyright (C) 2010 Creytiv.com
 */


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance (1st) */

	struct le le;
	struct vidisp_st *vidisp;
	double fps;
	char *device;
	vidsrc_frame_h *frameh;
	void *arg;
};


struct vidisp_st {
	const struct vidisp *vd;  /* inheritance (1st) */

	struct le le;
	struct vidsrc_st *vidsrc;
	char *device;
};


extern struct hash *ht_src;
extern struct hash *ht_disp;


int vidbridge_disp_alloc(struct vidisp_st **stp, const struct vidisp *vd,
			 struct vidisp_prm *prm, const char *dev,
			 vidisp_resize_h *resizeh, void *arg);
int vidbridge_disp_display(struct vidisp_st *st, const char *title,
			   const struct vidframe *frame, uint64_t timestamp);
struct vidisp_st *vidbridge_disp_find(const char *device);


int vidbridge_src_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
			struct media_ctx **ctx, struct vidsrc_prm *prm,
			const struct vidsz *size, const char *fmt,
			const char *dev, vidsrc_frame_h *frameh,
			vidsrc_error_h *errorh, void *arg);
struct vidsrc_st *vidbridge_src_find(const char *device);
void vidbridge_src_input(struct vidsrc_st *st,
			 const struct vidframe *frame, uint64_t timestamp);
