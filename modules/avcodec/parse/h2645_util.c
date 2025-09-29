
#include "h2645_util.h"
#include <stdio.h>
#include <string.h>

uint32_t remove_emulation_bytes(uint8_t* to, uint32_t toMaxSize,
                                    const uint8_t* from, uint32_t fromSize)
{
	uint32_t toSize = 0;
	uint32_t i = 0;
	while (i < fromSize && toSize < toMaxSize)
	{
	    if (i+2 < fromSize
			&& from[i] == 0
			&& from[i+1] == 0
			&& from[i+2] == 3)
		{
			to[toSize] = to[toSize+1] = 0;
			toSize += 2;
			i += 3;
		}
		else
		{
			to[toSize] = from[i];
			toSize += 1;
			i += 1;
		}
	}
	return toSize;
}
