/**
 * @file png_vf.c  Write vidframe to a PNG-file
 *
 * Author: Doug Blewett
 * Review: Alfred E. Heggestad
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <string.h>
#include <png.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "png_vf.h"


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
	struct vidframe *f2 = NULL;
	int err = 0;

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
	fp = fopen(path, "wb");
	if (fp == NULL) {
		err = errno;
		goto out;
	}

	png_init_io(png_ptr, fp);
	png_set_rows(png_ptr, info_ptr, png_row_pointers);
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	info("png: wrote %s\n", path);

	module_event("snapshot", "wrote", NULL, NULL, path);

 out:
	/* Finish writing. */
	mem_deref(f2);
	png_save_free(png_ptr, png_row_pointers, height);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	if (fp)
		fclose(fp);

	return err;
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


