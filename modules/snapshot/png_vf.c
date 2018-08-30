/**
 * @file png_vf.c  Write vidframe to a PNG-file
 *
 * Author: Doug Blewett
 * Review: Alfred E. Heggestad
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <string.h>
#include <time.h>
#include <png.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "png_vf.h"


static char *png_filename(const struct tm *tmx, const char *name,
			  char *buf, unsigned int length);
static void png_save_free(png_structp png_ptr, png_byte **png_row_pointers,
			  int png_height);


int png_save_vidframe(const struct vidframe *vf, const char *path)
{
	png_byte **png_row_pointers = NULL;
	png_byte *row;
	const png_byte *p;
	png_byte red, green, blue;
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;
	FILE *fp = NULL;
	size_t x, y;
	unsigned int width = vf->size.w & ~1;
	unsigned int height = vf->size.h & ~1;
	unsigned int bytes_per_pixel = 3; /* RGB format */
	time_t tnow;
	struct tm *tmx;
	char filename_buf[64];
	struct vidframe *f2 = NULL;
	int err = 0;

	tnow = time(NULL);
	tmx = localtime(&tnow);

	if (vf->fmt != VID_FMT_RGB32) {

		err = vidframe_alloc(&f2, VID_FMT_RGB32, &vf->size);
		if (err)
			goto out;

		vidconv(f2, vf, NULL);
		vf = f2;
	}

	/* Initialize the write struct. */
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
					  NULL, NULL, NULL);
	if (png_ptr == NULL) {
		err = ENOMEM;
		goto out;
	}

	/* Initialize the info struct. */
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		err = ENOMEM;
		goto out;
	}

	/* Set up error handling. */
	if (setjmp(png_jmpbuf(png_ptr))) {
		err = ENOMEM;
		goto out;
	}

	/* Set image attributes. */
	png_set_IHDR(png_ptr,
		     info_ptr,
		     width,
		     height,
		     8,
		     PNG_COLOR_TYPE_RGB,
		     PNG_INTERLACE_NONE,
		     PNG_COMPRESSION_TYPE_DEFAULT,
		     PNG_FILTER_TYPE_DEFAULT);

	/* Initialize rows of PNG
	 *    bytes_per_row = width * bytes_per_pixel;
	 */
	png_row_pointers = png_malloc(png_ptr,
				      height * sizeof(png_byte *));

	for (y = 0; y < height; ++y) {
		png_row_pointers[y] =
			(png_byte *) png_malloc(png_ptr,
						width * sizeof(uint8_t) *
						bytes_per_pixel);
	}

	p = vf->data[0];
	for (y = 0; y < height; ++y) {

		row = png_row_pointers[y];

		for (x = 0; x < width; ++x) {

			red   = *p++;
			green = *p++;
			blue  = *p++;

			*row++ = blue;
			*row++ = green;
			*row++ = red;

			++p;		/* skip alpha */
		}
	}

	/* Write the image data. */
	fp = fopen(png_filename(tmx, path,
				filename_buf, sizeof(filename_buf)), "wb");
	if (fp == NULL) {
		err = errno;
		goto out;
	}

	png_init_io(png_ptr, fp);
	png_set_rows(png_ptr, info_ptr, png_row_pointers);
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	info("png: wrote %s\n", filename_buf);

 out:
	/* Finish writing. */
	mem_deref(f2);
	png_save_free(png_ptr, png_row_pointers, height);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	if (fp)
		fclose(fp);

	return 0;
}


static void png_save_free(png_structp png_ptr, png_byte **png_row_pointers,
			  int png_height)
{
	int y;

	/* Cleanup. */
	if (png_height == 0 || png_row_pointers == NULL)
		return;

	for (y = 0; y < png_height; y++) {
		png_free(png_ptr, png_row_pointers[y]);
	}
	png_free(png_ptr, png_row_pointers);
}


static char *png_filename(const struct tm *tmx, const char *name,
			  char *buf, unsigned int length)
{
	/*
	 * -2013-03-03-15-22-56.png - 24 chars
	 */
	if (strlen(name) + 24 >= length) {
		buf[0] = '\0';
		return buf;
	}

	sprintf(buf, (tmx->tm_mon < 9 ? "%s-%d-0%d" : "%s-%d-%d"), name,
		1900 + tmx->tm_year, tmx->tm_mon + 1);

	sprintf(buf + strlen(buf), (tmx->tm_mday < 10 ? "-0%d" : "-%d"),
		tmx->tm_mday);

	sprintf(buf + strlen(buf), (tmx->tm_hour < 10 ? "-0%d" : "-%d"),
		tmx->tm_hour);

	sprintf(buf + strlen(buf), (tmx->tm_min < 10 ? "-0%d" : "-%d"),
		tmx->tm_min);

	sprintf(buf + strlen(buf), (tmx->tm_sec < 10 ? "-0%d.png" : "-%d.png"),
		tmx->tm_sec);

	return buf;
}
