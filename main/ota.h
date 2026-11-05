#ifndef _OTA_H
#define _OTA_H

#include <stdbool.h>

// OTA update mode: the device starts its own WiFi AP + HTTP server,
// firmware is uploaded via browser at http://192.168.4.1
// (or: curl -X POST --data-binary @image.bin http://192.168.4.1/update)
#define OTA_AP_SSID     "GNARL-OTA"
#define OTA_AP_PASSWORD "rileylink"   // WPA2 requires >= 8 chars
#define OTA_AP_IP       "192.168.4.1"

// Boot-time check (double-reset detector): returns true when the device
// was reset twice in quick succession via the RST button.
// This board revision has no PRG/IO0 button, so double-RST is the entry.
// The counter lives in NVS flash: RTC slow memory does NOT survive
// EN-button resets on this board (RTC domain is power-cycled).
// Only POWERON resets (RST button / power cycle) are counted - a crash
// loop can never trigger OTA mode (the bootloader rollback handles that).
bool ota_entry_requested(void);

// Enter OTA mode: WiFi AP + HTTP update server + OLED progress.
// Never returns (device reboots after a successful upload).
void ota_start(void);

#endif // _OTA_H
