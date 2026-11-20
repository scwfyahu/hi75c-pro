/* interface between the core (app.c) and the native UI (ui_native.cpp) */
#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int connected, effect, colour, brightness, speed, r, g, b;
    char note[160];
} AppState;

void      app_init(void);
void      app_shutdown(void);
int       app_connected(void);
AppState *app_state(void);

int         app_effect_count(void);
const char *app_effect_name(int i);
int         app_effect_id(int i);

void app_set_effect(int hw_id);
void app_set_colour(int colour);
void app_set_brightness(int level);
void app_set_speed(int level);
void app_restore_keymap(void);
void app_rescan(void);

int app_self_test(void);

#ifdef __cplusplus
}
#endif

#endif
