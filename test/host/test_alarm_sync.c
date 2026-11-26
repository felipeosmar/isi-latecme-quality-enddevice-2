#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "alarm_sync.h"

static const uint8_t VEC[18] = {0x01, 0x00, 0x07,
                                0x01, 0x00, 0x96, 0x00, 0xfa,
                                0x02, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00};

static void check_vec(const alarm_sync_limits_t *l)
{
    assert(l->version == 7);
    assert(l->ch[0].enabled && !l->ch[0].keep);
    assert(l->ch[0].low_d == 150 && l->ch[0].high_d == 250);
    assert(l->ch[1].keep);
    assert(!l->ch[2].enabled && !l->ch[2].keep);
}

static void test_parse(void)
{
    alarm_sync_limits_t l;
    memset(&l, 0, sizeof l);
    assert(alarm_sync_parse_limits(VEC, 18, &l) == ALARM_SYNC_OK);
    check_vec(&l);

    assert(alarm_sync_parse_limits(VEC, 17, &l) == ALARM_SYNC_ERR_FORMAT);
    assert(alarm_sync_parse_limits(VEC, 19, &l) == ALARM_SYNC_ERR_FORMAT);

    uint8_t b[18];
    memcpy(b, VEC, 18);
    b[0] = 0x02;
    assert(alarm_sync_parse_limits(b, 18, &l) == ALARM_SYNC_ERR_FORMAT);
    memcpy(b, VEC, 18);
    b[1] = 0; b[2] = 0;
    assert(alarm_sync_parse_limits(b, 18, &l) == ALARM_SYNC_ERR_FORMAT);
    b[1] = 0x80; b[2] = 0;
    assert(alarm_sync_parse_limits(b, 18, &l) == ALARM_SYNC_ERR_FORMAT);
    b[1] = 0x7f; b[2] = 0xff;
    assert(alarm_sync_parse_limits(b, 18, &l) == ALARM_SYNC_OK);
}

static void test_validate(void)
{
    alarm_sync_limits_t l;
    memset(&l, 0, sizeof l);
    l.version = 1;
    l.ch[0] = (alarm_sync_channel_t){false, true, 250, 150};
    assert(alarm_sync_validate(&l) == ALARM_SYNC_ERR_ORDER);
    l.ch[0] = (alarm_sync_channel_t){false, true, 100, 1300};
    assert(alarm_sync_validate(&l) == ALARM_SYNC_ERR_RANGE);
    l.ch[0] = (alarm_sync_channel_t){false, false, 30000, -30000};
    l.ch[1] = (alarm_sync_channel_t){true, true, 30000, -30000};
    l.ch[2] = (alarm_sync_channel_t){false, false, 5, 5};
    assert(alarm_sync_validate(&l) == ALARM_SYNC_OK);
    l.ch[0] = (alarm_sync_channel_t){false, true, -105, 250}; /* ff97 */
    assert(alarm_sync_validate(&l) == ALARM_SYNC_OK);
    l.ch[0] = (alarm_sync_channel_t){false, true, -400, 1250};
    assert(alarm_sync_validate(&l) == ALARM_SYNC_OK);
    l.ch[0] = (alarm_sync_channel_t){false, true, -401, 0};
    assert(alarm_sync_validate(&l) == ALARM_SYNC_ERR_RANGE);
    l.ch[0] = (alarm_sync_channel_t){false, false, 0, 0};
    l.ch[1] = (alarm_sync_channel_t){false, true, 0, 1001};
    assert(alarm_sync_validate(&l) == ALARM_SYNC_ERR_RANGE);
    l.ch[1] = (alarm_sync_channel_t){false, true, -1, 500};
    assert(alarm_sync_validate(&l) == ALARM_SYNC_ERR_RANGE);
    l.ch[1] = (alarm_sync_channel_t){false, false, 0, 0};
    l.ch[2] = (alarm_sync_channel_t){false, true, 0, 10240};
    assert(alarm_sync_validate(&l) == ALARM_SYNC_OK);
    l.ch[2] = (alarm_sync_channel_t){false, true, 0, 10241};
    assert(alarm_sync_validate(&l) == ALARM_SYNC_ERR_RANGE);

    /* parse of ff97 keeps sign */
    uint8_t b[18];
    memcpy(b, VEC, 18);
    b[4] = 0xff; b[5] = 0x97;
    assert(alarm_sync_parse_limits(b, 18, &l) == ALARM_SYNC_OK);
    assert(l.ch[0].low_d == -105);
    assert(alarm_sync_validate(&l) == ALARM_SYNC_OK);
}

static void test_build(void)
{
    alarm_sync_event_t ev = {.version = 5, .origin_local = true,
                             .state = {1, 0, 2}, .button = 3,
                             .ip = {192, 168, 10, 23}, .error = 0};
    uint8_t out[ALARM_SYNC_EVENT_LEN];
    const uint8_t exp[10] = {0x01, 0x80, 0x05, 0x21, 0x03, 0xc0, 0xa8, 0x0a, 0x17, 0x00};
    assert(alarm_sync_build_event(&ev, out) == 10);
    assert(memcmp(out, exp, 10) == 0);
    ev.origin_local = false;
    ev.error = 3;
    assert(alarm_sync_build_event(&ev, out) == 10);
    assert(out[1] == 0x00 && out[2] == 0x05 && out[9] == 3);
}

static void test_fragment(void)
{
    alarm_sync_limits_t l;
    memset(&l, 0, sizeof l);
    assert(alarm_sync_parse_fragment("v=7&c0=1,150,250&c1=2,0,0&c2=0,0,0", &l) == ALARM_SYNC_OK);
    check_vec(&l);
    assert(alarm_sync_parse_fragment("#v=7&c0=1,150,250&c1=2,0,0&c2=0,0,0", &l) == ALARM_SYNC_OK);
    check_vec(&l);
    assert(alarm_sync_parse_fragment("v=3&c0=1,-105,250&c1=0,0,0&c2=0,0,0", &l) == ALARM_SYNC_OK);
    assert(l.version == 3 && l.ch[0].low_d == -105);

    const char *bad[] = {
        "", "#", "v=7", "v=0&c0=1,1,2&c1=0,0,0&c2=0,0,0",
        "v=32768&c0=1,1,2&c1=0,0,0&c2=0,0,0",
        "v=7&c0=4,1,2&c1=0,0,0&c2=0,0,0",
        "v=7&c0=1,1,2&c1=0,0,0",
        "v=7&c0=1,1&c1=0,0,0&c2=0,0,0",
        "v=7&c0=1,1,2&c1=0,0,0&c2=0,0,0x",
        "v=7&c0=1,1,2&c1=0,0,0&c2=0,0,0&",
        "v=7&c0=1,40000,2&c1=0,0,0&c2=0,0,0",
        "v=7&c0=1,-40000,2&c1=0,0,0&c2=0,0,0",
        "v=&c0=1,1,2&c1=0,0,0&c2=0,0,0",
        "v=7&c0=1,,2&c1=0,0,0&c2=0,0,0",
        "v=7&c0=1,-,2&c1=0,0,0&c2=0,0,0",
        "v=7&c1=1,1,2&c0=0,0,0&c2=0,0,0",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        alarm_sync_err_t e = alarm_sync_parse_fragment(bad[i], &l);
        if (e != ALARM_SYNC_ERR_FORMAT) {
            printf("bad[%zu] '%s' -> %d\n", i, bad[i], (int)e);
        }
        assert(e == ALARM_SYNC_ERR_FORMAT);
    }
    assert(alarm_sync_parse_fragment("v=7&c0=1,-32768,32767&c1=0,0,0&c2=0,0,0", &l) == ALARM_SYNC_OK);
}

int main(void)
{
    test_parse();
    test_validate();
    test_build();
    test_fragment();
    puts("OK");
    return 0;
}
