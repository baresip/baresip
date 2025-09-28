#ifndef BS_H
#define BS_H
#include <stdint.h>
int u(int length, uint8_t *nalData, int *nalBitIndex);
int ue(uint8_t *nalData, int len, int *nalBitIndex);
int se(uint8_t *nalData, int len, int *nalBitIndex);
void de_emulation_prevention(uint8_t *buf, int *buf_size);
#endif
