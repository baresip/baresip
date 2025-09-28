#include "bs.h"

typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef unsigned long DWORD;

int ue(uint8_t *pBuff, int nLen, int *nStartBit)
{
    int nZeroNum = 0;
    while (*nStartBit < nLen * 8)
    {
        if (pBuff[*nStartBit / 8] & (0x80 >> (*nStartBit % 8)))
        {
            break;
        }
        nZeroNum++;
        (*nStartBit)++;
    }
    (*nStartBit)++;

    int dwRet = 0;
    for (int i = 0; i < nZeroNum; i++)
    {
        dwRet <<= 1;
        if (pBuff[*nStartBit / 8] & (0x80 >> (*nStartBit % 8)))
        {
            dwRet += 1;
        }
        (*nStartBit)++;
    }
    return (1 << nZeroNum) - 1 + dwRet;
}

int se(uint8_t *pBuff, int nLen, int *nStartBit)
{
    int UeVal = ue(pBuff, nLen, nStartBit);
    double k = UeVal;
    int nValue = (int)ceil(k / 2);
    if (UeVal % 2 == 0)
        nValue = -nValue;
    return nValue;
}

int u(int BitCount, uint8_t *buf, int *nStartBit)
{
    int dwRet = 0;
    for (int i = 0; i < BitCount; i++)
    {
        dwRet <<= 1;
        if (buf[*nStartBit / 8] & (0x80 >> (*nStartBit % 8)))
        {
            dwRet += 1;
        }
        (*nStartBit)++;
    }
    return dwRet;
}

void de_emulation_prevention(uint8_t *buf, unsigned int *buf_size)
{
    unsigned int i = 0, j = 0;
    uint8_t *tmp_ptr = NULL;
    unsigned int tmp_buf_size = 0;
    int val = 0;

    tmp_ptr = buf;
    tmp_buf_size = *buf_size;
    for (i = 0; i < (tmp_buf_size - 2); i++)
    {
        // check for 0x000003
        val = (tmp_ptr[i] ^ 0x00) + (tmp_ptr[i + 1] ^ 0x00) + (tmp_ptr[i + 2] ^ 0x03);
        if (val == 0)
        {
            // kick out 0x03
            for (j = i + 2; j < tmp_buf_size - 1; j++)
                tmp_ptr[j] = tmp_ptr[j + 1];

            // and so we should decrease bufsize
            (*buf_size)--;
        }
    }
}
