/**
 * @file netstring.h  Streaming API for netstrings.
 *
 * This file is public domain,
 * adapted from https://github.com/PeterScott/netstring-c/"
 */

#ifndef __NETSTRING_STREAM_H
#define __NETSTRING_STREAM_H

#include <string.h>

const char* netstring_error_str(int err);

int netstring_read(char *buffer, size_t buffer_length,
		   char **netstring_start, size_t *netstring_length);

size_t netstring_num_len(size_t num);
size_t netstring_buffer_size(size_t data_length);

size_t netstring_encode_new(char **netstring, char *data, size_t len);

#define NETSTRING_MAX_SIZE 999999999

/* Errors that can occur during netstring parsing */
typedef enum {
	NETSTRING_ERROR_TOO_LONG = -100,
	NETSTRING_ERROR_NO_COLON,
	NETSTRING_ERROR_TOO_SHORT,
	NETSTRING_ERROR_NO_COMMA,
	NETSTRING_ERROR_LEADING_ZERO,
	NETSTRING_ERROR_NO_LENGTH
} netstring_error;

#endif
