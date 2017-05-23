/**
 * @file qtcapture.m Video source using QTKit QTCapture
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <QTKit/QTKit.h>


static void frame_handler(struct vidsrc_st *st,
			  const CVImageBufferRef videoFrame);
static struct vidsrc *vidsrc;


@interface qtcap : NSObject
{
	QTCaptureSession                 *sess;
	QTCaptureDeviceInput             *input;
	QTCaptureDecompressedVideoOutput *output;
	struct vidsrc_st *vsrc;
}
@end


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */

	qtcap *cap;
	struct lock *lock;
	struct vidsz app_sz;
	struct vidsz sz;
	struct mbuf *buf;
	vidsrc_frame_h *frameh;
	void *arg;
	bool started;
#ifdef QTCAPTURE_RUNLOOP
	struct tmr tmr;
#endif
};


@implementation qtcap


- (id)init:(struct vidsrc_st *)st
       dev:(const char *)name
{
	NSAutoreleasePool *pool;
	QTCaptureDevice *dev;
	BOOL success = NO;
	NSError *err;

	pool = [[NSAutoreleasePool alloc] init];
	if (!pool)
		return nil;

	self = [super init];
	if (!self)
		goto out;

	vsrc = st;
	sess = [[QTCaptureSession alloc] init];
	if (!sess)
		goto out;

	if (str_isset(name)) {
		NSString *s = [NSString stringWithUTF8String:name];
		dev = [QTCaptureDevice deviceWithUniqueID:s];
		info("qtcapture: using device: %s\n", name);
	}
	else {
		dev = [QTCaptureDevice
		         defaultInputDeviceWithMediaType:QTMediaTypeVideo];
	}

	success = [dev open:&err];
	if (!success)
		goto out;

	input = [[QTCaptureDeviceInput alloc] initWithDevice:dev];
	success = [sess addInput:input error:&err];
	if (!success)
		goto out;

	output = [[QTCaptureDecompressedVideoOutput alloc] init];
	[output setDelegate:self];
	[output setPixelBufferAttributes:
	 [NSDictionary dictionaryWithObjectsAndKeys:
          [NSNumber numberWithInt:st->app_sz.h], kCVPixelBufferHeightKey,
          [NSNumber numberWithInt:st->app_sz.w], kCVPixelBufferWidthKey,
#if 0
	/* This does not work reliably */
	  [NSNumber numberWithInt:kCVPixelFormatType_420YpCbCr8Planar],
	    (id)kCVPixelBufferPixelFormatTypeKey,
#endif
		       nil]];

	success = [sess addOutput:output error:&err];
	if (!success)
		goto out;

	/* Start */
	[sess startRunning];

 out:
	if (!success && self) {
		[self dealloc];
		self = nil;
	}

	[pool release];

	return self;
}


- (void)stop:(id)unused
{
	(void)unused;

	[sess stopRunning];

	if ([[input device] isOpen]) {
		[[input device] close];
		[sess removeInput:input];
		[input release];
	}

	if (output) {
		[output setDelegate:nil];
		[sess removeOutput:output];
		[output release];
	}
}


- (void)dealloc
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

	[self performSelectorOnMainThread:@selector(stop:)
	      withObject:nil
	      waitUntilDone:YES];

	[sess release];

	[super dealloc];

	[pool release];
}


- (void)captureOutput:(QTCaptureOutput *)captureOutput
  didOutputVideoFrame:(CVImageBufferRef)videoFrame
     withSampleBuffer:(QTSampleBuffer *)sampleBuffer
       fromConnection:(QTCaptureConnection *)connection
{
	(void)captureOutput;
	(void)sampleBuffer;
	(void)connection;

#if 0
	printf("got frame: %zu x %zu - fmt=0x%08x\n",
	       CVPixelBufferGetWidth(videoFrame),
	       CVPixelBufferGetHeight(videoFrame),
	       CVPixelBufferGetPixelFormatType(videoFrame));
#endif

	frame_handler(vsrc, videoFrame);
}


@end


static enum vidfmt get_pixfmt(OSType type)
{
	switch (type) {

	case kCVPixelFormatType_420YpCbCr8Planar: return VID_FMT_YUV420P;
	case kCVPixelFormatType_422YpCbCr8:       return VID_FMT_UYVY422;
	case 0x79757673: /* yuvs */               return VID_FMT_YUYV422;
	case kCVPixelFormatType_32ARGB:           return VID_FMT_ARGB;
	default:                                  return -1;
	}
}


static inline void avpict_init_planar(struct vidframe *p,
				      const CVImageBufferRef f)
{
	int i;

	if (!p)
		return;

	for (i=0; i<3; i++) {
		p->data[i]     =      CVPixelBufferGetBaseAddressOfPlane(f, i);
		p->linesize[i] = (int)CVPixelBufferGetBytesPerRowOfPlane(f, i);
	}

	p->data[3]     = NULL;
	p->linesize[3] = 0;
}


static inline void avpict_init_chunky(struct vidframe *p,
				      const CVImageBufferRef f)
{
	p->data[0]     =      CVPixelBufferGetBaseAddress(f);
	p->linesize[0] = (int)CVPixelBufferGetBytesPerRow(f);

	p->data[1]     = p->data[2]     = p->data[3]     = NULL;
	p->linesize[1] = p->linesize[2] = p->linesize[3] = 0;
}


static void frame_handler(struct vidsrc_st *st,
			  const CVImageBufferRef videoFrame)
{
	struct vidframe src;
	vidsrc_frame_h *frameh;
	void *arg;
	enum vidfmt vidfmt;

	lock_write_get(st->lock);
	frameh = st->frameh;
	arg    = st->arg;
	lock_rel(st->lock);

	if (!frameh)
		return;

	vidfmt = get_pixfmt(CVPixelBufferGetPixelFormatType(videoFrame));
	if (vidfmt == (enum vidfmt)-1) {
		warning("qtcapture: unknown pixel format: 0x%08x\n",
			  CVPixelBufferGetPixelFormatType(videoFrame));
		return;
	}

	st->started = true;

	st->sz.w = (int)CVPixelBufferGetWidth(videoFrame);
	st->sz.h = (int)CVPixelBufferGetHeight(videoFrame);

	CVPixelBufferLockBaseAddress(videoFrame, 0);

	if (CVPixelBufferIsPlanar(videoFrame))
		avpict_init_planar(&src, videoFrame);
	else
		avpict_init_chunky(&src, videoFrame);

	src.fmt = vidfmt;
	src.size = st->sz;

	CVPixelBufferUnlockBaseAddress(videoFrame, 0);

	frameh(&src, arg);
}


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

#ifdef QTCAPTURE_RUNLOOP
	tmr_cancel(&st->tmr);
#endif

	lock_write_get(st->lock);
	st->frameh = NULL;
	lock_rel(st->lock);

	[st->cap dealloc];

	mem_deref(st->buf);
	mem_deref(st->lock);
}


#ifdef QTCAPTURE_RUNLOOP
static void tmr_handler(void *arg)
{
	struct vidsrc_st *st = arg;

	/* Check if frame_handler was called */
	if (st->started)
		return;

	tmr_start(&st->tmr, 100, tmr_handler, st);

	/* Simulate the Run-Loop */
	(void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, YES);
}
#endif


static int alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		 struct media_ctx **ctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt,
		 const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err;

	(void)ctx;
	(void)prm;
	(void)fmt;
	(void)errorh;

	if (!stp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs     = vs;
	st->frameh = frameh;
	st->arg    = arg;

	if (size)
		st->app_sz = *size;

	err = lock_alloc(&st->lock);
	if (err)
		goto out;

	st->cap = [[qtcap alloc] init:st dev:dev];
	if (!st->cap) {
		err = ENODEV;
		goto out;
	}

#ifdef QTCAPTURE_RUNLOOP
	tmr_start(&st->tmr, 10, tmr_handler, st);
#endif

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static void device_info(void)
{
	NSAutoreleasePool *pool;
	NSArray *devs;

	pool = [[NSAutoreleasePool alloc] init];
	if (!pool)
		return;

	devs = [QTCaptureDevice inputDevicesWithMediaType:QTMediaTypeVideo];

	if (devs && [devs count] > 1) {
		QTCaptureDevice *d;

		debug("qtcapture: devices:\n");

		for (d in devs) {
			NSString *name = [d localizedDisplayName];

			debug("    %s: %s\n",
			      [[d uniqueID] UTF8String],
			      [name UTF8String]);
		}
	}

	[pool release];
}


static int module_init(void)
{
	device_info();
	return vidsrc_register(&vidsrc, baresip_vidsrcl(),
			       "qtcapture", alloc, NULL);
}


static int module_close(void)
{
	vidsrc = mem_deref(vidsrc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(qtcapture) = {
	"qtcapture",
	"vidsrc",
	module_init,
	module_close
};
