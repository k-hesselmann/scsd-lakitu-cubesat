#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Shared by the telemetry
 * packet builder (fsm.c) and the SCV flash persistence layer (scv.c). */
uint16_t CRC16_Ccitt(const uint8_t *data, size_t length);

#endif /* CRC16_H */
