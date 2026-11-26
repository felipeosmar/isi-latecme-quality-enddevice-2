#include "alarm_sync.h"

#define VERSION_MAX 32767

static const int16_t RANGE_MIN[3] = {-400, 0, 0};
static const int16_t RANGE_MAX[3] = {1250, 1000, 10240};

alarm_sync_err_t alarm_sync_parse_limits(const uint8_t *buf, size_t len, alarm_sync_limits_t *out)
{
    if (!buf || !out || len != ALARM_SYNC_LIMITS_LEN || buf[0] != 0x01) {
        return ALARM_SYNC_ERR_FORMAT;
    }
    uint16_t version = (uint16_t)((buf[1] << 8) | buf[2]);
    if (version < 1 || version > VERSION_MAX) {
        return ALARM_SYNC_ERR_FORMAT;
    }
    alarm_sync_limits_t t;
    t.version = version;
    for (int i = 0; i < 3; i++) {
        const uint8_t *p = buf + 3 + 5 * i;
        t.ch[i].enabled = (p[0] & 0x01) != 0;
        t.ch[i].keep = (p[0] & 0x02) != 0;
        t.ch[i].low_d = (int16_t)(uint16_t)((p[1] << 8) | p[2]);
        t.ch[i].high_d = (int16_t)(uint16_t)((p[3] << 8) | p[4]);
    }
    *out = t;
    return ALARM_SYNC_OK;
}

alarm_sync_err_t alarm_sync_validate(const alarm_sync_limits_t *l)
{
    if (!l) {
        return ALARM_SYNC_ERR_FORMAT;
    }
    for (int i = 0; i < 3; i++) {
        const alarm_sync_channel_t *c = &l->ch[i];
        if (!c->enabled || c->keep) {
            continue;
        }
        if (c->low_d >= c->high_d) {
            return ALARM_SYNC_ERR_ORDER;
        }
        if (c->low_d < RANGE_MIN[i] || c->high_d > RANGE_MAX[i]) {
            return ALARM_SYNC_ERR_RANGE;
        }
    }
    return ALARM_SYNC_OK;
}

size_t alarm_sync_build_event(const alarm_sync_event_t *ev, uint8_t out[ALARM_SYNC_EVENT_LEN])
{
    uint16_t v = (uint16_t)(ev->version & 0x7FFF);
    if (ev->origin_local) {
        v |= 0x8000;
    }
    out[0] = 0x01;
    out[1] = (uint8_t)(v >> 8);
    out[2] = (uint8_t)(v & 0xFF);
    out[3] = (uint8_t)((ev->state[0] & 3) | ((ev->state[1] & 3) << 2) | ((ev->state[2] & 3) << 4));
    out[4] = ev->button;
    memcpy(&out[5], ev->ip, 4);
    out[9] = ev->error;
    return ALARM_SYNC_EVENT_LEN;
}

/* Parse optional '-' + digits (at least one) into [lo, hi]; advances *s. */
static bool parse_int(const char **s, long lo, long hi, long *val)
{
    const char *p = *s;
    bool neg = false;
    if (*p == '-') {
        neg = true;
        p++;
    }
    if (*p < '0' || *p > '9') {
        return false;
    }
    long v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 100000) { /* far beyond any valid range; avoids overflow */
            return false;
        }
        p++;
    }
    if (neg) {
        v = -v;
    }
    if (v < lo || v > hi) {
        return false;
    }
    *s = p;
    *val = v;
    return true;
}

static bool expect(const char **s, const char *lit)
{
    size_t n = strlen(lit);
    if (strncmp(*s, lit, n) != 0) {
        return false;
    }
    *s += n;
    return true;
}

alarm_sync_err_t alarm_sync_parse_fragment(const char *frag, alarm_sync_limits_t *out)
{
    if (!frag || !out) {
        return ALARM_SYNC_ERR_FORMAT;
    }
    const char *p = frag;
    if (*p == '#') {
        p++;
    }
    alarm_sync_limits_t t;
    long v;
    if (!expect(&p, "v=") || !parse_int(&p, 1, VERSION_MAX, &v)) {
        return ALARM_SYNC_ERR_FORMAT;
    }
    t.version = (uint16_t)v;
    static const char *const keys[3] = {"&c0=", "&c1=", "&c2="};
    for (int i = 0; i < 3; i++) {
        long flags, low, high;
        if (!expect(&p, keys[i]) ||
            !parse_int(&p, 0, 3, &flags) || !expect(&p, ",") ||
            !parse_int(&p, -32768, 32767, &low) || !expect(&p, ",") ||
            !parse_int(&p, -32768, 32767, &high)) {
            return ALARM_SYNC_ERR_FORMAT;
        }
        t.ch[i].enabled = (flags & 1) != 0;
        t.ch[i].keep = (flags & 2) != 0;
        t.ch[i].low_d = (int16_t)low;
        t.ch[i].high_d = (int16_t)high;
    }
    if (*p != '\0') {
        return ALARM_SYNC_ERR_FORMAT;
    }
    *out = t;
    return ALARM_SYNC_OK;
}
