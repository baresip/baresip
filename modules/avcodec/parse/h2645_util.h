#ifndef H2645_UTIL_H
#define H2645_UTIL_H
#include <stdint.h>

uint32_t remove_emulation_bytes(uint8_t *to, uint32_t toMaxSize, const uint8_t *from, uint32_t fromSize);
uint8_t *avc_find_startcode(uint8_t *p, uint8_t *end);

#endif
