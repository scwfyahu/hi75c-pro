#include "proto.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void nap(int ms) { if (ms > 0) Sleep((DWORD)ms); }
#else
#include <time.h>
static void nap(int ms) {
    struct timespec ts;
    if (ms <= 0) return;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

void p_frame(unsigned char *f, int cmd, int sub, const unsigned char *payload,
             int len, int index, int count) {
    int i, sum = 0;
    memset(f, 0, RPT_LEN);
    f[0] = RPT;
    f[1] = (unsigned char)cmd;
    f[2] = (unsigned char)count;
    f[3] = (unsigned char)index;
    f[4] = (unsigned char)(((sub & 0xf) << 4) | (len & 0xf));
    if (payload) for (i = 0; i < len && i < 14; i++) f[5 + i] = payload[i];
    for (i = 0; i < RPT_LEN - 1; i++) sum += f[i];
    f[RPT_LEN - 1] = (unsigned char)(sum & 0xff);
}

static int frame_ok(const unsigned char *buf) {
    int i, sum = 0;
    if (buf[0] != RPT) return 0;
    for (i = 0; i < RPT_LEN - 1; i++) sum += buf[i];
    return buf[RPT_LEN - 1] == (unsigned char)(sum & 0xff);
}

static int await_echo(Link *l, const unsigned char *sent, int timeout_ms) {
    unsigned char buf[64];
    int waited = 0;
    while (waited < timeout_ms) {
        int n = l->recv(l->ctx, buf, (int)sizeof(buf), 10);
        waited += 10;
        if (n < RPT_LEN || !frame_ok(buf)) continue;
        /* only the device echoing this exact packet counts as an acknowledgement:
           matching cmd+index alone lets a stale or foreign frame advance the
           transfer while the packet it supposedly confirms was never applied */
        if (memcmp(buf, sent, RPT_LEN) != 0) continue;
        return 1;
    }
    return 0;
}

int p_write(Link *l, int cmd, int sub, const unsigned char *data, int len) {
    unsigned char f[RPT_LEN];
    int off = 0, index = 0, count = (len + 13) / 14;
    if (count < 1) count = 1;
    while (index < count) {
        int chunk = len - off, attempt, acked = 0;
        if (chunk > 14) chunk = 14;
        if (chunk < 0) chunk = 0;
        p_frame(f, cmd, sub, data ? data + off : 0, chunk, index, count);
        for (attempt = 0; attempt < 3 && !acked; attempt++) {
            if (!l->send(l->ctx, f, RPT_LEN)) return 0;
            acked = await_echo(l, f, 350);
        }
        if (!acked) return 0;
        off += chunk;
        index++;
    }
    nap(120);
    return 1;
}

int p_read(Link *l, int cmd, int sub, unsigned char *out, int want) {
    unsigned char req[RPT_LEN], buf[64], seen[128];
    int count = -1, packets = 0, waited = 0;
    int i;

    memset(out, 0, (size_t)want);
    memset(seen, 0, sizeof(seen));
    p_frame(req, cmd, sub, 0, 0, 0, 1);
    if (!l->send(l->ctx, req, RPT_LEN)) return 0;

    while (waited < 2500) {
        int n = l->recv(l->ctx, buf, (int)sizeof(buf), 10);
        waited += 10;
        if (count > 0 && packets >= count) break;
        if (n < RPT_LEN || !frame_ok(buf)) continue;     /* checksum before trust */
        /* exact command and the layer that was asked for: masking the command
           let a write-ACK satisfy a read, and ignoring the layer let a base-layer
           reply be installed as the FN layer */
        if (buf[1] != (unsigned char)cmd) continue;
        if ((buf[4] >> 4) != (sub & 0x0f)) continue;
        {
            int len = buf[4] & 0x0f, idx = buf[3], at = idx * 14, c = buf[2];
            /* validate the frame before its count is allowed to size the transfer */
            if (c < 1 || c > 127) continue;
            if (len > 14 || idx >= 128 || at + len > want) continue;
            if (count < 0) {
                count = c;
                /* the device must say how many packets the whole blob needs; a
                   reply that cannot cover the request is a failed read */
                if (count * 14 < want) return 0;
            } else if (c != count) continue;
            if (!seen[idx]) {
                memcpy(out + at, buf + 5, (size_t)len);
                seen[idx] = 1;
                packets++;
            }
        }
    }
    if (count <= 0 || packets < count) return 0;
    for (i = 0; i < count; i++) if (!seen[i]) return 0;
    return 1;
}

int p_set_effect(unsigned char *cfg, int effect, int colour, int brightness, int speed) {
    int slot;
    if (cfg[126] != 0x5a || cfg[127] != 0xa5) return 0;
    if (effect < 1 || effect > 32) return 0;
    if (brightness < 0 || brightness > 9) brightness = 9;
    if (speed < 0 || speed > 3) speed = 1;
    if (colour < 0 || colour > 6) colour = 0;
    cfg[10] = (unsigned char)effect;
    slot = 58 + (effect - 1) * 2;
    if (slot + 1 < CFG_LEN) {
        cfg[slot] = (unsigned char)brightness;
        cfg[slot + 1] = (unsigned char)((speed << 4) | (colour & 7));
    }
    return 1;
}
