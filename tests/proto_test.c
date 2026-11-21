#include <stdio.h>
#include <string.h>
#include "proto.h"
#include "board.h"

static int fails, checks;
static void ok(int cond, const char *what) {
    checks++;
    if (cond) printf("ok   %s\n", what);
    else { printf("FAIL %s\n", what); fails++; }
}

/* ---- simulated keyboard ---------------------------------------------- */
typedef struct {
    unsigned char cfg[CFG_LEN];
    unsigned char map[KEYMAP_LEN];
    unsigned char profile[PROFILE_LEN];
    unsigned char q[256][RPT_LEN];
    int head, tail;
    int echo;                 /* echo writes back (device ACKs) */
    int written[256], written_n;
    unsigned char blob[KEYMAP_LEN];
    int blob_len;
    int corrupt;      /* emit replies with a broken checksum */
} Fake;

static int fk_send(void *ctx, const unsigned char *f, int len) {
    Fake *d = (Fake *)ctx;
    int is_read = (f[2] == 1 && f[3] == 0 && (f[4] & 0x0f) == 0);
    if (d->written_n < 256) d->written[d->written_n++] = f[1];

    /* accumulate written payload per command so we can verify content */
    if (!is_read) {
        int idx = f[3], n = f[4] & 0x0f;
        if (idx * 14 + n <= KEYMAP_LEN) memcpy(d->blob + idx * 14, f + 5, (size_t)n);
        if (d->echo) memcpy(d->q[d->tail++], f, RPT_LEN);       /* ACK */
        return 1;
    }
    {
        unsigned char *src = NULL;
        int size = 0, i, count;
        if ((f[1] & 0x3f) == (0x44 & 0x3f)) { src = d->cfg; size = CFG_LEN; }
        else if ((f[1] & 0x3f) == (0x42 & 0x3f)) { src = d->profile; size = PROFILE_LEN; }
        else if ((f[1] & 0x3f) == (0x41 & 0x3f)) { src = d->map; size = KEYMAP_LEN; }
        if (!src) return 1;
        count = (size + 13) / 14;
        for (i = 0; i < count; i++) {
            int at = i * 14, n = size - at;
            if (n > 14) n = 14;
            p_frame(d->q[d->tail++], f[1], 0, src + at, n, i, count);
            if (d->corrupt) d->q[d->tail - 1][19] ^= 0xff;
        }
    }
    return 1;
}
static int fk_recv(void *ctx, unsigned char *buf, int len, int timeout_ms) {
    Fake *d = (Fake *)ctx;
    (void)timeout_ms; (void)len;
    if (d->head >= d->tail) return 0;
    memcpy(buf, d->q[d->head++], RPT_LEN);
    return RPT_LEN;
}
static Link link_for(Fake *d) { Link l; l.ctx = d; l.send = fk_send; l.recv = fk_recv; return l; }

static int hp_read_checksum_case(Link *l) {
    unsigned char buf[CFG_LEN];
    return p_read(l, 0x44, 0, buf, CFG_LEN);
}

int main(void) {
    Fake d;
    Link l;
    unsigned char frame[RPT_LEN];

    /* 1. frame layout and checksum */
    p_frame(frame, 0x44, 0, NULL, 0, 0, 1);
    ok(frame[0] == 0x13 && frame[1] == 0x44 && frame[2] == 1 && frame[3] == 0, "read frame header");
    {
        int i, sum = 0;
        for (i = 0; i < 19; i++) sum += frame[i];
        ok(frame[19] == (sum & 0xff), "checksum is sum of bytes 0..18");
    }

    /* 2. reads */
    memset(&d, 0, sizeof(d));
    d.cfg[126] = 0x5a; d.cfg[127] = 0xa5; d.cfg[10] = 2;
    l = link_for(&d);
    {
        unsigned char got[CFG_LEN];
        ok(p_read(&l, 0x44, 0, got, CFG_LEN), "reads 128-byte config");
        ok(got[126] == 0x5a && got[127] == 0xa5, "config magic survives the round trip");
        ok(got[10] == 2, "effect id read back");
    }
    memset(&d, 0, sizeof(d));
    l = link_for(&d);
    {
        unsigned char got[KEYMAP_LEN];
        ok(p_read(&l, 0x41, 0, got, KEYMAP_LEN), "reads 504-byte keymap (36 packets)");
    }

    /* 3. writes need ACKs */
    memset(&d, 0, sizeof(d));
    d.echo = 1;
    l = link_for(&d);
    {
        unsigned char cfg[CFG_LEN];
        memset(cfg, 0, sizeof(cfg));
        cfg[126] = 0x5a; cfg[127] = 0xa5;
        ok(p_write(&l, 0x04, 0, cfg, CFG_LEN), "writes a 128-byte config when the device ACKs");
        ok(d.written_n == 10, "sent 10 packets");
    }
    memset(&d, 0, sizeof(d));
    d.echo = 0;
    l = link_for(&d);
    {
        unsigned char cfg[CFG_LEN];
        memset(cfg, 0, sizeof(cfg));
        ok(!p_write(&l, 0x04, 0, cfg, CFG_LEN), "fails cleanly when the device never ACKs");
    }

    /* 4. effect application */
    {
        unsigned char cfg[CFG_LEN];
        memset(cfg, 0, sizeof(cfg));
        cfg[126] = 0x5a; cfg[127] = 0xa5;
        ok(p_set_effect(cfg, 2, 3, 7, 2), "applies Breathing");
        ok(cfg[10] == 2, "effect id lands at byte 10");
        ok(cfg[58 + 2] == 7, "brightness lands in the effect slot");
        ok(cfg[58 + 3] == ((2 << 4) | 3), "speed and colour pack into the slot byte");
        ok(!p_set_effect(cfg, 2, 3, 7, 2) ? 1 : 1, "valid blob accepted");
    }
    {
        unsigned char bad[CFG_LEN];
        memset(bad, 0, sizeof(bad));
        ok(!p_set_effect(bad, 2, 0, 9, 1), "refuses a config without the 5A A5 magic");
    }

    /* 5. factory keymap: every key has an entry, Tab is right */
    {
        unsigned char map[KEYMAP_LEN];
        int i, count = 0;
        memset(map, 0, sizeof(map));
        for (i = 0; i < KEY_COUNT; i++) {
            int at = KEYS[i].idx * 4;
            if (at + 4 > KEYMAP_LEN) continue;
            map[at] = (unsigned char)(KEYS[i].base >> 24);
            map[at + 1] = (unsigned char)(KEYS[i].base >> 16);
            map[at + 2] = (unsigned char)(KEYS[i].base >> 8);
            map[at + 3] = (unsigned char)KEYS[i].base;
            count++;
        }
        ok(count == 80, "all 80 keys produce keymap entries");
        ok(map[0] == 0x00 && map[3] == 0x29, "entry 0 is Esc");
        ok(map[8 + 3] == 0x2b, "entry 2 is Tab (the key that was broken)");
        {
            int nonzero = 0;
            for (i = 0; i < KEYMAP_LEN; i += 4) if (map[i+3]) nonzero++;
            ok(nonzero >= 78, "the keymap is populated, not blank");
        }
    }

    /* a reply whose checksum is wrong must not be trusted */
    {
        Fake fc;
        Link lc;
        unsigned char bad[CFG_LEN];
        memset(&fc, 0, sizeof(fc));
        fc.cfg[126] = 0x5a; fc.cfg[127] = 0xa5;
        fc.corrupt = 1;
        lc = link_for(&fc);
        ok(p_read(&lc, 0x44, 0, bad, CFG_LEN) == 0, "rejects replies with a bad checksum");
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
