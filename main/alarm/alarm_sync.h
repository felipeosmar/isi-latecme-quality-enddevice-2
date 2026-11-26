/**
 * alarm_sync - pure codec for platform-synced alarm limits and events.
 *
 * Downlink fPort 11 (18 bytes, big-endian):
 *   0x01 | version u16 | 3 x [flags u8 | low i16 | high i16]
 *   channel order: temperature, humidity, thermocouple. Values in tenths.
 *   flags: bit0 = enabled, bit1 = keep (channel is left untouched).
 * Uplink fPort 3 event (10 bytes, big-endian):
 *   0x01 | version u16 (bit15 = last change was local) | states u8
 *   | button u8 | ipv4[4] | error u8
 *   states: bits1-0 ch0, bits3-2 ch1, bits5-4 ch2.
 * URL fragment (limites.html), fixed key order v, c0, c1, c2:
 *   [#]v=<version>&c0=<flags>,<low>,<high>&c1=...&c2=...
 *
 * No platform dependencies: only stdint/stdbool/string.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_SYNC_FPORT_LIMITS 11
#define ALARM_SYNC_FPORT_EVENT  3
#define ALARM_SYNC_LIMITS_LEN   18
#define ALARM_SYNC_EVENT_LEN    10

typedef enum {
    ALARM_SYNC_OK         = 0,
    ALARM_SYNC_ERR_FORMAT = 1,
    ALARM_SYNC_ERR_ORDER  = 2,
    ALARM_SYNC_ERR_RANGE  = 3,
    ALARM_SYNC_ERR_SAVE   = 4,
} alarm_sync_err_t;

/** Values in tenths (0.1 degC or 0.1 %RH). */
typedef struct {
    bool keep;
    bool enabled;
    int16_t low_d;
    int16_t high_d;
} alarm_sync_channel_t;

typedef struct {
    uint16_t version;
    alarm_sync_channel_t ch[3];
} alarm_sync_limits_t;

typedef struct {
    uint16_t version;
    bool origin_local;
    uint8_t state[3];
    uint8_t button;
    uint8_t ip[4];
    uint8_t error;
} alarm_sync_event_t;

/** Parse an fPort 11 payload. *out is written only on success. */
alarm_sync_err_t alarm_sync_parse_limits(const uint8_t *buf, size_t len, alarm_sync_limits_t *out);

/** Order (low < high) then physical range, only for channels enabled && !keep. */
alarm_sync_err_t alarm_sync_validate(const alarm_sync_limits_t *l);

/** Encode an fPort 3 event; returns ALARM_SYNC_EVENT_LEN. */
size_t alarm_sync_build_event(const alarm_sync_event_t *ev, uint8_t out[ALARM_SYNC_EVENT_LEN]);

/** Parse the limites.html URL fragment. *out is written only on success. */
alarm_sync_err_t alarm_sync_parse_fragment(const char *frag, alarm_sync_limits_t *out);

#ifdef __cplusplus
}
#endif
