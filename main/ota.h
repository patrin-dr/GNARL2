#ifndef _OTA_H
#define _OTA_H

#include <stdbool.h>

// OTA update mode: the device starts its own WiFi AP + HTTP server,
// firmware is uploaded via browser at http://192.168.4.1
// (or: curl -X POST --data-binary @image.bin http://192.168.4.1/update)
#define OTA_AP_SSID     "GNARL-OTA"
#define OTA_AP_PASSWORD "rileylink"   // WPA2 requires >= 8 chars
#define OTA_AP_IP       "192.168.4.1"

// Boot-time check with OLED prompt (~3 s window): returns true if the
// PRG button was held for >= 1 s within the window.
// Safe against the GPIO0 download-mode trap: the user presses PRG only
// AFTER releasing RST, when the prompt appears.
bool ota_entry_requested(void);

// Enter OTA mode: WiFi AP + HTTP update server + OLED progress.
// Never returns (device reboots after a successful upload).
void ota_start(void);

#endif // _OTA_H
