/**
 * @file jpg_vf.c  Write vidframe to a JPG-file
 *
 * Author: Doug Blewett
 * Review: Alfred E. Heggestad
 */
#define _BSD_SOURCE 1
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "jpeglib.h"
#include "jpg_vf.h"

int jpg_save_vidframe(const struct vidframe *vf, const char *path)
{

	struct		jpeg_compress_struct cinfo;
	struct		jpeg_error_mgr jerr;
  	JSAMPROW	row_pointer[1];
	
	unsigned char *imgdata,*src,*dst;
	int row_stride,pixs;
	
	FILE * fp;

	struct vidframe *f2 = NULL;
	int err = 0;

	unsigned int width = vf->size.w & ~1;
	unsigned int height = vf->size.h & ~1;
	
	if (vf->fmt != VID_FMT_RGB32) 
	{
		err |= vidframe_alloc(&f2, VID_FMT_RGB32, &vf->size);
		if (err) goto out;
		vidconv(f2, vf, NULL);
	}
	else
		f2 = vf;

	fp = fopen(path, "wb");
	if (fp == NULL) 
	{
		err = errno;
		goto out;
	}

	// RGB32  -> 24 RGB
	imgdata = f2->data[0];
	pixs = width*height;
	src = imgdata; 
	dst = imgdata; 
	while (pixs--)
	{
		*dst++ = src[2];
		*dst++ = src[1];
		*dst++ = *src;
		src+=4;
	}

	// create jpg structures
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, fp);
	
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3; // 24 bpp
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 85 , TRUE); // quality 85%

	// compress
	jpeg_start_compress(&cinfo, TRUE);

	row_stride = width * cinfo.input_components;		

	while (cinfo.next_scanline < cinfo.image_height) 
	{
		row_pointer[0] = & imgdata[cinfo.next_scanline * row_stride];
		(void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);

	
	/* Finish writing. */
out:
	jpeg_destroy_compress(&cinfo);
	if (f2 != vf)	mem_deref(f2);
	if (fp) fclose(fp);
	return 0;
}


