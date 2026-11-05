#ifndef _OLED_H
#define _OLED_H

// Initialize SSD1306 OLED (128x64, I2C) and start the display task.
// No-op unless OLED_ENABLE is defined in module.h.
void oled_init(void);

// Draw a standalone two-line message with an optional progress bar
// (progress 0..100, or -1 for no bar). Self-contained: initializes the
// display hardware on first call. Used by OTA mode and the boot-time
// OTA prompt, where the regular status task is not running.
// No-op unless OLED_ENABLE is defined in module.h.
void oled_ota_screen(const char *line1, const char *line2, int progress);

#endif // _OLED_H
