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
void      app_pump(void *hwnd);
void      app_shutdown(void);
int       app_connected(void);
const char *app_device_path(void);  /* hidraw path of the open device, "" when none */
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
void app_import_effect(void *owner);   /* opens a file dialog and runs the effect */
void app_stop_effect(void);
void app_set_layer(int layer);
int  app_layer(void);
int  app_keymap_set(int key_index, int type, int usage);   /* 1 on success */
int  app_keymap_get(int key_index);                        /* current usage, -1 on failure */
int  app_keymap_cached(int key_index);                     /* cache only, never touches the device */
void app_keymap_request(void);                             /* queue a refresh from the device */
void app_keymap_apply(int key_index, int usage);            /* queue a remap */
void app_clear_note(void);
void app_save_state(int wx, int wy, int ww, int wh);
void app_load_state(int *wx, int *wy, int *ww, int *wh);
const char *app_import_name(void);

int app_self_test(void);
int app_effect_test(const char *path);
int  app_update_check(void);        /* 1 if a newer build exists */
int  app_update_apply(void);        /* download + swap; 1 on success */
void app_update_install(void);      /* queue the update, never blocks the UI */
void app_stop_background_driver(void);  /* ask a running daemon to exit before we write */
const char *app_version(void);
const char *app_latest_tag(void);
int app_daemon(const char *path);

#ifdef __cplusplus
}
#endif

#endif
