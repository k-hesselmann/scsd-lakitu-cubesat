#include "fdir/crc16.h"

uint16_t CRC16_Ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;

    for (size_t i = 0U; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            else
                crc <<= 1;
        }
    }

    return crc;
}
