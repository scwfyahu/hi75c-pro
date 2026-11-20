/* device protocol core, OS-independent so it can be tested off-Windows */
#ifndef PROTO_H
#define PROTO_H

#define RPT 0x13
#define RPT_LEN 20
#define CFG_LEN 128
#define KEYMAP_LEN 504
#define PROFILE_LEN 378

typedef struct {
    void *ctx;
    int (*send)(void *ctx, const unsigned char *frame, int len);
    int (*recv)(void *ctx, unsigned char *buf, int len, int timeout_ms);
} Link;

void p_frame(unsigned char *f, int cmd, int sub, const unsigned char *payload,
             int len, int index, int count);
int p_write(Link *l, int cmd, int sub, const unsigned char *data, int len);
int p_read(Link *l, int cmd, int sub, unsigned char *out, int want);

/* apply an effect into a config blob; returns 0 if the blob is not valid */
int p_set_effect(unsigned char *cfg, int effect, int colour, int brightness, int speed);

#ifdef __cplusplus
}
#endif
#endif
