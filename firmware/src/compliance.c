#include "compliance.h"

#include <string.h>

#include "vs_types.h"

#define SEQ_ERASED 0xFFFFFFFFu

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void encode(const compliance_record_t *r, uint8_t out[COMPLIANCE_RECORD_SIZE])
{
    put_u32(&out[0], r->seq);
    put_u32(&out[4], r->timestamp);
    out[8] = r->type;
    out[9] = r->arg;
    put_u16(&out[10], r->v16);
    put_u32(&out[12], r->v32);
}

static void decode(const uint8_t in[COMPLIANCE_RECORD_SIZE], compliance_record_t *r)
{
    r->seq = get_u32(&in[0]);
    r->timestamp = get_u32(&in[4]);
    r->type = in[8];
    r->arg = in[9];
    r->v16 = get_u16(&in[10]);
    r->v32 = get_u32(&in[12]);
}

static uint32_t slot_seq(uint32_t slot)
{
    uint8_t raw[4];
    hal_flash_read(slot * COMPLIANCE_RECORD_SIZE, raw, sizeof(raw));
    return get_u32(raw);
}

void compliance_init(compliance_t *c)
{
    uint32_t max_seq = 0;
    uint32_t max_slot = 0;
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < COMPLIANCE_SLOTS; slot++) {
        const uint32_t seq = slot_seq(slot);
        if (seq == SEQ_ERASED) {
            continue;
        }
        count++;
        if (seq >= max_seq) {
            max_seq = seq;
            max_slot = slot;
        }
    }
    c->count = count;
    c->timestamp = VS_TIMESTAMP_UNKNOWN;
    if (count == 0) {
        c->next_seq = 1;
        c->write_slot = 0;
    } else {
        c->next_seq = max_seq + 1u;
        c->write_slot = (max_slot + 1u) % COMPLIANCE_SLOTS;
    }
}

void compliance_format(compliance_t *c)
{
    for (uint32_t page = 0; page < VS_FLASH_LOG_PAGES; page++) {
        hal_flash_erase_page(page);
    }
    compliance_init(c);
}

void compliance_set_time(compliance_t *c, uint32_t utc_s)
{
    c->timestamp = utc_s;
}

static void reclaim_page(compliance_t *c, uint32_t page)
{
    const uint32_t first = page * COMPLIANCE_SLOTS_PER_PAGE;
    uint32_t dropped = 0;
    for (uint32_t i = 0; i < COMPLIANCE_SLOTS_PER_PAGE; i++) {
        if (slot_seq(first + i) != SEQ_ERASED) {
            dropped++;
        }
    }
    if (dropped > 0) {
        hal_flash_erase_page(page);
        c->count -= dropped;
    }
}

uint32_t compliance_append(compliance_t *c, compliance_type_t type, uint8_t arg, uint16_t v16, uint32_t v32)
{
    if (c->write_slot % COMPLIANCE_SLOTS_PER_PAGE == 0u) {
        reclaim_page(c, c->write_slot / COMPLIANCE_SLOTS_PER_PAGE);
    }

    compliance_record_t r = {
        .seq = c->next_seq,
        .timestamp = c->timestamp,
        .type = (uint8_t)type,
        .arg = arg,
        .v16 = v16,
        .v32 = v32,
    };
    uint8_t raw[COMPLIANCE_RECORD_SIZE];
    encode(&r, raw);
    hal_flash_write(c->write_slot * COMPLIANCE_RECORD_SIZE, raw, sizeof(raw));

    c->write_slot = (c->write_slot + 1u) % COMPLIANCE_SLOTS;
    c->next_seq++;
    c->count++;
    return r.seq;
}

uint32_t compliance_count(const compliance_t *c)
{
    return c->count;
}

uint32_t compliance_oldest_seq(const compliance_t *c)
{
    return c->next_seq - c->count;
}

uint32_t compliance_newest_seq(const compliance_t *c)
{
    return c->count == 0 ? 0 : c->next_seq - 1u;
}

size_t compliance_read(const compliance_t *c, uint32_t from_seq, compliance_record_t *out, size_t max)
{
    const uint32_t oldest = compliance_oldest_seq(c);
    if (from_seq < oldest) {
        from_seq = oldest;
    }
    size_t n = 0;
    for (uint32_t seq = from_seq; seq < c->next_seq && n < max; seq++) {
        const uint32_t back = c->next_seq - seq;
        const uint32_t slot = (c->write_slot + COMPLIANCE_SLOTS - back) % COMPLIANCE_SLOTS;
        uint8_t raw[COMPLIANCE_RECORD_SIZE];
        hal_flash_read(slot * COMPLIANCE_RECORD_SIZE, raw, sizeof(raw));
        decode(raw, &out[n]);
        if (out[n].seq == seq) {
            n++;
        }
    }
    return n;
}
