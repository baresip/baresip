/**
 * @file dcmap.c RFC 8864 data-channel SDP attributes
 *
 * Copyright (C) 2026 The baresip project
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


static bool utf8_valid(const uint8_t *buf, size_t len)
{
	size_t i = 0;

	while (i < len) {
		uint8_t byte = buf[i++];
		uint32_t code;
		size_t continuation;

		if (!byte)
			return false;
		if (byte < 0x80)
			continue;
		if (byte >= 0xc2 && byte <= 0xdf) {
			code = byte & 0x1f;
			continuation = 1;
		}
		else if (byte >= 0xe0 && byte <= 0xef) {
			code = byte & 0x0f;
			continuation = 2;
		}
		else if (byte >= 0xf0 && byte <= 0xf4) {
			code = byte & 0x07;
			continuation = 3;
		}
		else {
			return false;
		}

		if (continuation > len - i)
			return false;
		for (size_t n = 0; n < continuation; ++n) {
			uint8_t next = buf[i++];

			if ((next & 0xc0) != 0x80)
				return false;
			code = (code << 6) | (next & 0x3f);
		}
		if ((continuation == 2 && code < 0x800) ||
		    (continuation == 3 && code < 0x10000) ||
		    code > 0x10ffff ||
		    (code >= 0xd800 && code <= 0xdfff))
			return false;
	}

	return true;
}


static int hex_value(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	return -1;
}


static bool quoted_char(uint8_t value)
{
	return value == ' ' || value == '!' ||
		(value >= '#' && value <= '$') ||
		(value >= '&' && value <= '~');
}


static int quoted_decode(char **strp, const char *value, size_t len)
{
	char *decoded;
	size_t end = 0;

	if (!strp || !value || len < 2 || value[0] != '"' ||
	    value[len - 1] != '"')
		return EPROTO;

	decoded = mem_alloc(len, NULL);
	if (!decoded)
		return ENOMEM;

	for (size_t i = 1; i + 1 < len; ++i) {
		uint8_t byte = (uint8_t)value[i];

		if (byte == '%') {
			int high;
			int low;

			if (i + 3 >= len) {
				mem_deref(decoded);
				return EPROTO;
			}
			high = hex_value(value[++i]);
			low = hex_value(value[++i]);
			if (high < 0 || low < 0) {
				mem_deref(decoded);
				return EPROTO;
			}
			byte = (uint8_t)((high << 4) | low);
		}
		else if (!quoted_char(byte)) {
			mem_deref(decoded);
			return EPROTO;
		}
		decoded[end++] = (char)byte;
	}
	decoded[end] = '\0';
	if (!utf8_valid((const uint8_t *)decoded, end)) {
		mem_deref(decoded);
		return EPROTO;
	}

	*strp = decoded;
	return 0;
}


static int decimal(const char *value, size_t len, uint32_t limit,
		   uint32_t *result)
{
	uint64_t parsed = 0;

	if (!value || !len || !result)
		return EPROTO;
	for (size_t i = 0; i < len; ++i) {
		if (!isdigit((unsigned char)value[i]))
			return EPROTO;
		parsed = parsed * 10 + (unsigned)(value[i] - '0');
		if (parsed > limit)
			return EPROTO;
	}
	*result = (uint32_t)parsed;
	return 0;
}


void dcmap_reset(struct dcmap *map)
{
	if (!map)
		return;

	map->label = mem_deref(map->label);
	map->protocol = mem_deref(map->protocol);
	memset(map, 0, sizeof(*map));
}


int dcmap_decode(struct dcmap *map, const char *value)
{
	enum {
		SEEN_ORDERED = 1U << 0,
		SEEN_PROTOCOL = 1U << 1,
		SEEN_LABEL = 1U << 2,
		SEEN_RETRANSMITS = 1U << 3,
		SEEN_LIFETIME = 1U << 4,
		SEEN_PRIORITY = 1U << 5,
	};
	const char *cursor;
	const char *space;
	uint32_t seen = 0;
	uint32_t parsed;
	int err;

	if (!map || !value)
		return EINVAL;
	memset(map, 0, sizeof(*map));
	map->ordered = true;
	map->max_retransmits = -1;
	map->max_packet_lifetime = -1;
	map->priority = 256;

	space = strchr(value, ' ');
	err = decimal(value, space ? (size_t)(space - value) : strlen(value),
		      UINT16_MAX, &parsed);
	if (err)
		goto out;
	map->id = (uint16_t)parsed;
	cursor = space ? space + 1 : value + strlen(value);
	if (space && !*cursor) {
		err = EPROTO;
		goto out;
	}

	while (*cursor) {
		const char *end = strchr(cursor, ';');
		const char *equal;
		const char *name_end;
		size_t option_len = end ? (size_t)(end - cursor)
					     : strlen(cursor);
		size_t value_len;
		uint32_t flag;

		if (!option_len || memchr(cursor, ' ', option_len)) {
			err = EPROTO;
			goto out;
		}
		equal = memchr(cursor, '=', option_len);
		if (!equal) {
			err = EPROTO;
			goto out;
		}
		name_end = equal;
		value_len = option_len - (size_t)(equal + 1 - cursor);

			if ((size_t)(name_end - cursor) == 7 &&
			    !memcmp(cursor, "ordered", 7)) {
				flag = SEEN_ORDERED;
				if (value_len == 5 && !memcmp(equal + 1, "false", 5))
					map->ordered = false;
				else if (value_len == 4 &&
					 !memcmp(equal + 1, "true", 4))
					map->ordered = true;
				else {
					err = EPROTO;
					goto out;
				}
			}
		else if ((size_t)(name_end - cursor) == 11 &&
			 !memcmp(cursor, "subprotocol", 11)) {
			flag = SEEN_PROTOCOL;
			map->protocol = mem_deref(map->protocol);
			err = quoted_decode(&map->protocol, equal + 1,
					    value_len);
			if (err)
				goto out;
		}
		else if ((size_t)(name_end - cursor) == 5 &&
			 !memcmp(cursor, "label", 5)) {
			flag = SEEN_LABEL;
			map->label = mem_deref(map->label);
			err = quoted_decode(&map->label, equal + 1, value_len);
			if (err)
				goto out;
		}
		else if ((size_t)(name_end - cursor) == 8 &&
			 !memcmp(cursor, "max-retr", 8)) {
			flag = SEEN_RETRANSMITS;
			err = decimal(equal + 1, value_len, INT32_MAX,
				      &parsed);
			if (err)
				goto out;
			map->max_retransmits = (int32_t)parsed;
		}
		else if ((size_t)(name_end - cursor) == 8 &&
			 !memcmp(cursor, "max-time", 8)) {
			flag = SEEN_LIFETIME;
			err = decimal(equal + 1, value_len, INT32_MAX,
				      &parsed);
			if (err)
				goto out;
			map->max_packet_lifetime = (int32_t)parsed;
		}
		else if ((size_t)(name_end - cursor) == 8 &&
			 !memcmp(cursor, "priority", 8)) {
			flag = SEEN_PRIORITY;
			err = decimal(equal + 1, value_len, UINT16_MAX,
				      &parsed);
			if (err)
				goto out;
			map->priority = (uint16_t)parsed;
		}
		else {
			err = EPROTO;
			goto out;
		}

		if (seen & flag) {
			err = EPROTO;
			goto out;
		}
		seen |= flag;
		cursor = end ? end + 1 : cursor + option_len;
		if (end && !*cursor) {
			err = EPROTO;
			goto out;
		}
	}

	if ((seen & SEEN_RETRANSMITS) && (seen & SEEN_LIFETIME)) {
		err = EPROTO;
		goto out;
	}
	if (!map->label && str_dup(&map->label, ""))
		err = ENOMEM;
	if (!err && !map->protocol && str_dup(&map->protocol, ""))
		err = ENOMEM;

out:
	if (err)
		dcmap_reset(map);
	return err;
}


static int quoted_print(struct re_printf *pf, const char *value)
{
	int err = 0;

	for (const uint8_t *byte = (const uint8_t *)value; *byte; ++byte) {
		if (quoted_char(*byte))
			err |= re_hprintf(pf, "%c", *byte);
		else
			err |= re_hprintf(pf, "%%%02X", *byte);
	}
	return err;
}


int dcmap_print(struct re_printf *pf, void *arg)
{
	const struct dcmap *map = arg;
	bool option = false;
	int err;

	if (!pf || !map || !map->label || !map->protocol)
		return EINVAL;

	err = re_hprintf(pf, "%u", map->id);
#define PRINT_OPTION(format, ...) \
	do { \
		err |= re_hprintf(pf, option ? ";" format : " " format, \
				  __VA_ARGS__); \
		option = true; \
	} while (0)
	if (*map->protocol)
		PRINT_OPTION("subprotocol=\"%H\"", quoted_print,
			     map->protocol);
	if (*map->label)
		PRINT_OPTION("label=\"%H\"", quoted_print, map->label);
	if (!map->ordered)
		PRINT_OPTION("ordered=%s", "false");
	if (map->max_retransmits >= 0)
		PRINT_OPTION("max-retr=%d", map->max_retransmits);
	if (map->max_packet_lifetime >= 0)
		PRINT_OPTION("max-time=%d", map->max_packet_lifetime);
	if (map->priority != 256)
		PRINT_OPTION("priority=%u", map->priority);
#undef PRINT_OPTION
	return err;
}


int dcsa_decode(uint16_t *id, struct pl *attribute, const char *value)
{
	const char *space;
	const char *colon;
	uint32_t parsed;
	int err;

	if (!id || !attribute || !value)
		return EINVAL;
	space = strchr(value, ' ');
	if (!space || !space[1])
		return EPROTO;
	err = decimal(value, (size_t)(space - value), UINT16_MAX, &parsed);
	if (err)
		return err;

	attribute->p = space + 1;
	attribute->l = strlen(space + 1);
	if (memchr(attribute->p, '\r', attribute->l) ||
	    memchr(attribute->p, '\n', attribute->l))
		return EPROTO;

	colon = memchr(attribute->p, ':', attribute->l);
	size_t name_len = colon ? (size_t)(colon - attribute->p)
				: attribute->l;
	if (!name_len)
		return EPROTO;
	for (size_t i = 0; i < name_len; ++i) {
		unsigned char ch = (unsigned char)attribute->p[i];

		if (!isalnum(ch) && ch != '-' && ch != '_')
			return EPROTO;
	}

	*id = (uint16_t)parsed;
	return 0;
}
