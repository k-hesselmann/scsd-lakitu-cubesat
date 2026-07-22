#ifndef TTC_AUTH_H
#define TTC_AUTH_H

#include <stdint.h>

#ifndef TTC_AUTH_KEY_0
#error "Define TTC_AUTH_KEY_0 as the low 64 bits of the provisioned uplink key"
#endif

#ifndef TTC_AUTH_KEY_1
#error "Define TTC_AUTH_KEY_1 as the high 64 bits of the provisioned uplink key"
#endif

#define TTC_AUTH_TAG_HEX_LENGTH 16U

uint64_t TTC_AuthTag(const uint8_t *data, uint8_t length);
uint8_t TTC_AuthVerifyHex(const uint8_t *data, uint8_t length,
                          const uint8_t *tag_hex);

#endif
