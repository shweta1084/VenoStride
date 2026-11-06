#include "status_codec.h"

status_codec_result_t status_codec_encode_device_status(const vs_device_status_t *status,
                                                        uint8_t *out, size_t capacity, size_t *written)
{
    (void)status;
    (void)out;
    (void)capacity;
    if (written != NULL) {
        *written = 0;
    }
    return STATUS_CODEC_UNRESOLVED;
}

status_codec_result_t status_codec_decode_mode_select(const uint8_t *in, size_t len,
                                                      vs_mode_t *mode, char pin[CMD_PIN_LENGTH])
{
    (void)in;
    (void)len;
    (void)mode;
    (void)pin;
    return STATUS_CODEC_UNRESOLVED;
}
