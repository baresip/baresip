/**
 * @file opengl.m Video driver for OpenGL on MacOSX
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <Cocoa/Cocoa.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup opengl opengl
 *
 * Video display module for OpenGL on MacOSX
 */


#if (MAC_OS_X_VERSION_MAX_ALLOWED >= 101200)
#define NSTitledWindowMask         NSWindowStyleMaskTitled
#define NSClosableWindowMask       NSWindowStyleMaskClosable
#define NSMiniaturizableWindowMask NSWindowStyleMaskMiniaturizable
#endif


struct vidisp_st {
	const struct vidisp *vd;        /**< Inheritance (1st)     */
	struct vidsz size;              /**< Current size          */
	NSOpenGLContext *ctx;
	NSWindow *win;
	GLhandleARB PHandle;
	char *prog;
};


static struct vidisp *vid;       /**< OPENGL Video-display      */


static const char *FProgram=
  "uniform sampler2DRect Ytex;\n"
  "uniform sampler2DRect Utex,Vtex;\n"
  "void main(void) {\n"
  "  float nx,ny,r,g,b,y,u,v;\n"
  "  vec4 txl,ux,vx;"
  "  nx=gl_TexCoord[0].x;\n"
  "  ny=%d.0-gl_TexCoord[0].y;\n"
  "  y=texture2DRect(Ytex,vec2(nx,ny)).r;\n"
  "  u=texture2DRect(Utex,vec2(nx/2.0,ny/2.0)).r;\n"
  "  v=texture2DRect(Vtex,vec2(nx/2.0,ny/2.0)).r;\n"

  "  y=1.1643*(y-0.0625);\n"
  "  u=u-0.5;\n"
  "  v=v-0.5;\n"

  "  r=y+1.5958*v;\n"
  "  g=y-0.39173*u-0.81290*v;\n"
  "  b=y+2.017*u;\n"

  "  gl_FragColor=vec4(r,g,b,1.0);\n"
  "}\n";


static void destructor(void *arg)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	struct vidisp_st *st = arg;

	if (st->ctx) {
		[st->ctx clearDrawable];
		[st->ctx release];
	}

	[st->win close];

	if (st->PHandle) {
		glUseProgramObjectARB(0);
		glDeleteObjectARB(st->PHandle);
	}

	mem_deref(st->prog);

	[pool release];
}


static int create_window(struct vidisp_st *st)
{
	NSRect rect = NSMakeRect(0, 0, 100, 100);
	NSUInteger style;

	if (st->win)
		return 0;

	style = NSTitledWindowMask |
		NSClosableWindowMask |
		NSMiniaturizableWindowMask;

	st->win = [[NSWindow alloc] initWithContentRect:rect
				    styleMask:style
				    backing:NSBackingStoreBuffered
				    defer:FALSE];
	if (!st->win) {
		warning("opengl: could not create NSWindow\n");
		return ENOMEM;
	}

	[st->win setLevel:NSFloatingWindowLevel];

	return 0;
}


static void opengl_reset(struct vidisp_st *st, const struct vidsz *sz)
{
	if (st->PHandle) {
		glUseProgramObjectARB(0);
		glDeleteObjectARB(st->PHandle);
		st->PHandle = 0;
		st->prog = mem_deref(st->prog);
	}

	st->size = *sz;
}


static int setup_shader(struct vidisp_st *st, int width, int height)
{
	GLhandleARB FSHandle, PHandle;
	const char *progv[1];
	char buf[1024];
	int err, i;

	if (st->PHandle)
		return 0;

	err = re_sdprintf(&st->prog, FProgram, height);
	if (err)
		return err;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, 0, height, -1, 1);
	glViewport(0, 0, width, height);
	glClearColor(0, 0, 0, 0);
	glColor3f(1.0f, 0.84f, 0.0f);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);

	/* Set up program objects. */
	PHandle = glCreateProgramObjectARB();
	FSHandle = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);

	/* Compile the shader. */
	progv[0] = st->prog;
	glShaderSourceARB(FSHandle, 1, progv, NULL);
	glCompileShaderARB(FSHandle);

	/* Print the compilation log. */
	glGetObjectParameterivARB(FSHandle, GL_OBJECT_COMPILE_STATUS_ARB, &i);
	if (i != 1) {
		warning("opengl: shader compile failed\n");
		return ENOSYS;
	}

	glGetInfoLogARB(FSHandle, sizeof(buf), NULL, buf);

	/* Create a complete program object. */
	glAttachObjectARB(PHandle, FSHandle);
	glLinkProgramARB(PHandle);

	/* And print the link log. */
	glGetInfoLogARB(PHandle, sizeof(buf), NULL, buf);

	/* Finally, use the program. */
	glUseProgramObjectARB(PHandle);

	st->PHandle = PHandle;

	return 0;
}


static int alloc(struct vidisp_st **stp, const struct vidisp *vd,
		 struct vidisp_prm *prm, const char *dev,
		 vidisp_resize_h *resizeh, void *arg)
{
	NSOpenGLPixelFormatAttribute attr[] = {
		NSOpenGLPFAColorSize, 32,
		NSOpenGLPFADepthSize, 16,
		NSOpenGLPFADoubleBuffer,
		0
	};
	NSOpenGLPixelFormat *fmt;
	NSAutoreleasePool *pool;
	struct vidisp_st *st;
	GLint vsync = 1;
	int err = 0;

	(void)dev;
	(void)resizeh;
	(void)arg;

	pool = [[NSAutoreleasePool alloc] init];
	if (!pool)
		return ENOMEM;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vd = vd;

	fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attr];
	if (!fmt) {
		err = ENOMEM;
		warning("opengl: Failed creating OpenGL format\n");
		goto out;
	}

	st->ctx = [[NSOpenGLContext alloc] initWithFormat:fmt
					   shareContext:nil];

	[fmt release];

	if (!st->ctx) {
		err = ENOMEM;
		warning("opengl: Failed creating OpenGL context\n");
		goto out;
	}

	/* Use provided view, or create our own */
	if (prm && prm->view) {
		[st->ctx setView:prm->view];
	}
	else {
		err = create_window(st);
		if (err)
			goto out;

		if (prm)
			prm->view = [st->win contentView];
	}

	/* Enable vertical sync */
	[st->ctx setValues:&vsync forParameter:NSOpenGLCPSwapInterval];

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	[pool release];

	return err;
}


static inline void draw_yuv(GLhandleARB PHandle, int width, int height,
			    const uint8_t *Ytex, int linesizeY,
			    const uint8_t *Utex, int linesizeU,
			    const uint8_t *Vtex, int linesizeV)
{
	int i;

	/* This might not be required, but should not hurt. */
	glEnable(GL_TEXTURE_2D);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	/* Select texture unit 1 as the active unit and bind the U texture. */
	glActiveTexture(GL_TEXTURE1);
	i = glGetUniformLocationARB(PHandle, "Utex");
	glUniform1iARB(i,1);  /* Bind Utex to texture unit 1 */
	glBindTexture(GL_TEXTURE_RECTANGLE_EXT,1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, linesizeU);

	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
			GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
			GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_DECAL);
	glTexImage2D(GL_TEXTURE_RECTANGLE_EXT,0,GL_LUMINANCE,
		     width/2, height/2, 0,
		     GL_LUMINANCE,GL_UNSIGNED_BYTE,Utex);

	/* Select texture unit 2 as the active unit and bind the V texture. */
	glActiveTexture(GL_TEXTURE2);
	i = glGetUniformLocationARB(PHandle, "Vtex");
	glBindTexture(GL_TEXTURE_RECTANGLE_EXT,2);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, linesizeV);
	glUniform1iARB(i,2);  /* Bind Vtext to texture unit 2 */

	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
			GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
			GL_TEXTURE_MIN_FILTER,GL_LINEAR);

	glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_DECAL);
	glTexImage2D(GL_TEXTURE_RECTANGLE_EXT,0,GL_LUMINANCE,
		     width/2, height/2, 0,
		     GL_LUMINANCE,GL_UNSIGNED_BYTE,Vtex);

	/* Select texture unit 0 as the active unit and bind the Y texture. */
	glActiveTexture(GL_TEXTURE0);
	i = glGetUniformLocationARB(PHandle,"Ytex");
	glUniform1iARB(i,0);  /* Bind Ytex to texture unit 0 */
	glBindTexture(GL_TEXTURE_RECTANGLE_EXT,3);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, linesizeY);

	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
			GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
			GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_DECAL);

	glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_LUMINANCE,
		     width, height, 0,
		     GL_LUMINANCE, GL_UNSIGNED_BYTE, Ytex);
}


static inline void draw_blit(int width, int height)
{
	glClear(GL_COLOR_BUFFER_BIT);

	/* Draw image */

	glBegin(GL_QUADS);
	{
		glTexCoord2i(0, 0);
		glVertex2i(0, 0);
		glTexCoord2i(width, 0);
		glVertex2i(width, 0);
		glTexCoord2i(width, height);
		glVertex2i(width, height);
		glTexCoord2i(0, height);
		glVertex2i(0, height);
	}
	glEnd();
}


static inline void draw_rgb(const uint8_t *pic, int w, int h)
{
	glEnable(GL_TEXTURE_RECTANGLE_EXT);
	glBindTexture(GL_TEXTURE_RECTANGLE_EXT, 1);

	glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_EXT, w * h * 2, pic);

	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
			GL_TEXTURE_STORAGE_HINT_APPLE,
			GL_STORAGE_SHARED_APPLE);
	glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);

	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER,
			GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER,
			GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_S,
			GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_T,
			GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA, w, h, 0,
		     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pic);

	/* draw */
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_TEXTURE_2D);

	glViewport(0, 0, w, h);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	glOrtho( (GLfloat)0, (GLfloat)w, (GLfloat)0, (GLfloat)h, -1.0, 1.0);

	glBindTexture(GL_TEXTURE_RECTANGLE_EXT, 1);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();

	glBegin(GL_QUADS);
	{
		glTexCoord2f(0.0f, 0.0f);
		glVertex2f(0.0f, h);
		glTexCoord2f(0.0f, h);
		glVertex2f(0.0f, 0.0f);
		glTexCoord2f(w, h);
		glVertex2f(w, 0.0f);
		glTexCoord2f(w, 0.0f);
		glVertex2f(w, h);
	}
	glEnd();
}


static int display(struct vidisp_st *st, const char *title,
		   const struct vidframe *frame, uint64_t timestamp)
{
	NSAutoreleasePool *pool;
	bool upd = false;
	int err = 0;
	(void)timestamp;

	pool = [[NSAutoreleasePool alloc] init];
	if (!pool)
		return ENOMEM;

	if (!vidsz_cmp(&st->size, &frame->size)) {
		if (st->size.w && st->size.h) {
			info("opengl: reset: %u x %u  --->  %u x %u\n",
			     st->size.w, st->size.h,
			     frame->size.w, frame->size.h);
		}

		opengl_reset(st, &frame->size);

		upd = true;
	}

	if (upd && st->win) {

		const NSSize size = {frame->size.w, frame->size.h};
		char capt[256];

		[st->win setContentSize:size];

		if (title) {
			re_snprintf(capt, sizeof(capt), "%s - %u x %u",
				    title, frame->size.w, frame->size.h);
		}
		else {
			re_snprintf(capt, sizeof(capt), "%u x %u",
				    frame->size.w, frame->size.h);
		}

		[st->win setTitle:[NSString stringWithUTF8String:capt]];

		[st->win makeKeyAndOrderFront:nil];
		[st->win display];
		[st->win center];

		[st->ctx clearDrawable];
		[st->ctx setView:[st->win contentView]];
	}

	[st->ctx makeCurrentContext];

	if (frame->fmt == VID_FMT_YUV420P) {

		if (!st->PHandle) {

			debug("opengl: using Vertex shader with YUV420P\n");

			err = setup_shader(st, frame->size.w, frame->size.h);
			if (err)
				goto out;
		}

		draw_yuv(st->PHandle, frame->size.w, frame->size.h,
			 frame->data[0], frame->linesize[0],
			 frame->data[1], frame->linesize[1],
			 frame->data[2], frame->linesize[2]);
		draw_blit(frame->size.w, frame->size.h);
	}
	else if (frame->fmt == VID_FMT_RGB32) {

		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glViewport(0, 0, frame->size.w, frame->size.h);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		draw_rgb(frame->data[0], frame->size.w, frame->size.h);
	}
	else {
		warning("opengl: unknown pixel format %s\n",
			vidfmt_name(frame->fmt));
		err = EINVAL;
	}

	[st->ctx flushBuffer];

 out:
	[pool release];

	return err;
}


static void hide(struct vidisp_st *st)
{
	if (!st)
		return;

	[st->win orderOut:nil];
}


static int module_init(void)
{
	NSApplication *app;
	int err;

	app = [NSApplication sharedApplication];
	if (!app)
		return ENOSYS;

	err = vidisp_register(&vid, baresip_vidispl(),
			      "opengl", alloc, NULL, display, hide);
	if (err)
		return err;

	return 0;
}


static int module_close(void)
{
	vid = mem_deref(vid);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(opengl) = {
	"opengl",
	"vidisp",
	module_init,
	module_close,
};
