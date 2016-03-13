/**
 * @file opengles.h Internal API to OpenGLES module
 *
 * Copyright (C) 2010 Creytiv.com
 */


struct vidisp_st {
	const struct vidisp *vd;  /* pointer to base-class (inheritance) */
	struct vidframe *vf;

	/* GLES: */
	GLuint framebuffer;
	GLuint renderbuffer;
	GLuint texture_id;
	GLfloat vertices[4 * 3];

	void *view;
};


void opengles_addbuffers(struct vidisp_st *st);
void opengles_render(struct vidisp_st *st);


int  context_init(struct vidisp_st *st);
void context_destroy(struct vidisp_st *st);
void context_render(struct vidisp_st *st);
