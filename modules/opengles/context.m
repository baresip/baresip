/**
 * @file context.m OpenGLES Context for iOS
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <UIKit/UIKit.h>
#include <QuartzCore/CAEAGLLayer.h>
#include <OpenGLES/ES1/gl.h>
#include <OpenGLES/ES1/glext.h>
#include "opengles.h"


@interface GlView : UIView
{
	struct vidisp_st *st;
}
@end


static EAGLContext *ctx = NULL;


@implementation GlView


+ (Class)layerClass
{
	return [CAEAGLLayer class];
}


- (id)initWithFrame:(CGRect)frame vidisp:(struct vidisp_st *)vst
{
	self = [super initWithFrame:frame];
	if (!self)
		return nil;

	self.layer.opaque = YES;

	st = vst;

	return self;
}


- (void) render_sel:(id)unused
{
	(void)unused;

	if (!ctx) {
		UIWindow* window = [UIApplication sharedApplication].keyWindow;

		ctx = [EAGLContext alloc];
		[ctx initWithAPI:kEAGLRenderingAPIOpenGLES1];

		[EAGLContext setCurrentContext:ctx];

		[window addSubview:self];
		[window bringSubviewToFront:self];

		[window makeKeyAndVisible];
	}

	if (!st->framebuffer) {

		opengles_addbuffers(st);

		[ctx renderbufferStorage:GL_RENDERBUFFER_OES
		     fromDrawable:(CAEAGLLayer*)self.layer];
	}

	opengles_render(st);

	[ctx presentRenderbuffer:GL_RENDERBUFFER_OES];
}


- (void) dealloc
{
	[ctx release];

	[super dealloc];
}


@end


void context_destroy(struct vidisp_st *st)
{
	[(UIView *)st->view release];
}


int context_init(struct vidisp_st *st)
{
	UIWindow* window = [UIApplication sharedApplication].keyWindow;

	st->view = [[GlView new] initWithFrame:window.bounds vidisp:st];

	return 0;
}


void context_render(struct vidisp_st *st)
{
	UIView *view = st->view;

	[view performSelectorOnMainThread:@selector(render_sel:)
	  withObject:nil
	  waitUntilDone:YES];
}
