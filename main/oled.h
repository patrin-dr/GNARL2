#ifndef _OLED_H
#define _OLED_H

// Initialize SSD1306 OLED (128x64, I2C) and start the display task.
// No-op unless OLED_ENABLE is defined in module.h.
void oled_init(void);

#endif // _OLED_H
