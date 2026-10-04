/*
 * settings_store.h: settings and the bar's own state in NVS (namespace "tinybar"), with debounced writes so a
 * stream of changes costs one flash write. Owner: lead developer.
 *
 * Keys: "settings" (TB_SETTINGS_VERSION + tb_settings_t), "state" (own status, last status, message, today's
 * tomatoes and focused time with their date). A blob with an unknown version is ignored (defaults are used).
 */
#pragma once

#include "esp_err.h"
#include "tb_app.h"

/* Open NVS (erasing the partition if it's from an incompatible IDF), and the nvs_sec partition. */
esp_err_t settings_store_init(void);
/* Load settings, or defaults for this device id. */
void settings_store_load(tb_settings_t *out, const char *device_id);
/* Restore the own status and today's tallies into a freshly initialized model. */
void settings_store_restore(tb_app_t *a);
/* Note a change (from TB_FX_SAVE_SETTINGS / TB_FX_SAVE_STATE); written 2 s after the last change. */
void settings_store_mark_settings(void);
void settings_store_mark_state(void);
/* Called every app loop: writes what's due. */
void settings_store_poll(const tb_app_t *a, tb_ms_t now);
/* Write the settings and the state now, pending or not (before power off and restart; focused time included). */
void settings_store_flush(const tb_app_t *a);
