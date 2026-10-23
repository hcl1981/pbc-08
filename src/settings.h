/* settings.h -- /pico8.cfg, shared by the cart menu and the VM host.
 * Plain "key = value" lines; unknown keys are kept out of the way. */
#ifndef PBC_SETTINGS_H
#define PBC_SETTINGS_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int scale;      /* VIDEO_1TO1 / VIDEO_240 / VIDEO_256CROP */
    int volume;     /* 0..8, 4 = unity */
    int menustyle;  /* fake-08 settings, kept for its settings cart */
    int bgcolor;
} pbc_settings_t;

#define SETTINGS_VOLUME_MAX 8

void settings_load(pbc_settings_t *s);
/* writes only when the file would change (every write erases flash) */
void settings_save(const pbc_settings_t *s);

#ifdef __cplusplus
}
#endif
#endif
