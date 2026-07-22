#include "ttc/ttc_auth.h"

#define ROTL64(value, bits) \
    (((value) << (bits)) | ((value) >> (64U - (bits))))

static void TTC_SipRound(uint64_t *v0, uint64_t *v1,
                         uint64_t *v2, uint64_t *v3)
{
    *v0 += *v1;
    *v1 = ROTL64(*v1, 13U);
    *v1 ^= *v0;
    *v0 = ROTL64(*v0, 32U);
    *v2 += *v3;
    *v3 = ROTL64(*v3, 16U);
    *v3 ^= *v2;
    *v0 += *v3;
    *v3 = ROTL64(*v3, 21U);
    *v3 ^= *v0;
    *v2 += *v1;
    *v1 = ROTL64(*v1, 17U);
    *v1 ^= *v2;
    *v2 = ROTL64(*v2, 32U);
}

static uint64_t TTC_LoadLe64(const uint8_t *data)
{
    uint64_t value = 0U;
    uint8_t i;
    for (i = 0U; i < 8U; i++)
        value |= ((uint64_t)data[i]) << (8U * i);
    return value;
}

uint64_t TTC_AuthTag(const uint8_t *data, uint8_t length)
{
    const uint64_t k0 = (uint64_t)TTC_AUTH_KEY_0;
    const uint64_t k1 = (uint64_t)TTC_AUTH_KEY_1;
    uint64_t v0 = UINT64_C(0x736f6d6570736575) ^ k0;
    uint64_t v1 = UINT64_C(0x646f72616e646f6d) ^ k1;
    uint64_t v2 = UINT64_C(0x6c7967656e657261) ^ k0;
    uint64_t v3 = UINT64_C(0x7465646279746573) ^ k1;
    uint64_t final_block = ((uint64_t)length) << 56U;
    uint64_t message;
    uint8_t offset = 0U;
    uint8_t i;

    while ((uint8_t)(length - offset) >= 8U)
    {
        message = TTC_LoadLe64(&data[offset]);
        v3 ^= message;
        TTC_SipRound(&v0, &v1, &v2, &v3);
        TTC_SipRound(&v0, &v1, &v2, &v3);
        v0 ^= message;
        offset = (uint8_t)(offset + 8U);
    }

    for (i = 0U; i < (uint8_t)(length - offset); i++)
        final_block |= ((uint64_t)data[offset + i]) << (8U * i);

    v3 ^= final_block;
    TTC_SipRound(&v0, &v1, &v2, &v3);
    TTC_SipRound(&v0, &v1, &v2, &v3);
    v0 ^= final_block;
    v2 ^= UINT64_C(0xff);
    for (i = 0U; i < 4U; i++)
        TTC_SipRound(&v0, &v1, &v2, &v3);
    return v0 ^ v1 ^ v2 ^ v3;
}

static uint8_t TTC_HexNibble(uint8_t value, uint8_t *nibble)
{
    if (value >= (uint8_t)'0' && value <= (uint8_t)'9')
        *nibble = (uint8_t)(value - (uint8_t)'0');
    else if (value >= (uint8_t)'a' && value <= (uint8_t)'f')
        *nibble = (uint8_t)(value - (uint8_t)'a' + 10U);
    else if (value >= (uint8_t)'A' && value <= (uint8_t)'F')
        *nibble = (uint8_t)(value - (uint8_t)'A' + 10U);
    else
        return 0U;
    return 1U;
}

uint8_t TTC_AuthVerifyHex(const uint8_t *data, uint8_t length,
                          const uint8_t *tag_hex)
{
    uint64_t received = 0U;
    uint64_t difference;
    uint8_t nibble;
    uint8_t i;

    for (i = 0U; i < TTC_AUTH_TAG_HEX_LENGTH; i++)
    {
        if (!TTC_HexNibble(tag_hex[i], &nibble))
            return 0U;
        received = (received << 4U) | nibble;
    }

    difference = received ^ TTC_AuthTag(data, length);
    difference |= (uint64_t)(0U - difference);
    return (uint8_t)((difference >> 63U) ^ 1U);
}
