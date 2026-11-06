#ifndef VS_STATUS_CODEC_H
#define VS_STATUS_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "command.h"

/*
 * On-air byte layouts for Device Status (0x2A58) and Mode Selection (0x0001).
 *
 * Unresolved: the brief gives two incompatible Device Status layouts
 * ("1 B mode + 2 B battery + 1 B impedance" vs "bits 0-1 mode, bits 2-7 battery",
 * and 6 bits cannot hold 0-100), and describes a 1-byte mode write with a
 * 2-bit password while also requiring a 4-digit PIN. Until that is settled
 * these return STATUS_CODEC_UNRESOLVED and nothing else depends on a layout.
 */

typedef enum {
    STATUS_CODEC_OK = 0,
    STATUS_CODEC_UNRESOLVED = -1
} status_codec_result_t;

status_codec_result_t status_codec_encode_device_status(const vs_device_status_t *status,
                                                        uint8_t *out, size_t capacity, size_t *written);

status_codec_result_t status_codec_decode_mode_select(const uint8_t *in, size_t len,
                                                      vs_mode_t *mode, char pin[CMD_PIN_LENGTH]);

#endif
