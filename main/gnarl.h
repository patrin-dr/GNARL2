#include <stdint.h>
#include <stdbool.h>

#include "version.h"

#define TAG		"PICKLE_MY"
#define LOG_LOCAL_LEVEL	ESP_LOG_DEBUG
#include <esp_log.h>

#define MILLISECONDS	1000
#define SECONDS		1000000

#define MHz		1000000

#define STATE_OK "OK"

// Diagnostic build: hex dumps of BLE commands and radio packets
// (activates print_bytes in ble.c). Remove for production builds.
#define BLE_DEBUG

void gnarl_init(void);
void start_gnarl_task(void);
uint32_t gnarl_load_frequency(void);
void rfspy_command(const uint8_t *buf, int count, int rssi);
void gnarl_queue_clear(void);
void send_code(const uint8_t code);
void send_bytes(const uint8_t *buf, int count);
void print_bytes(const char* msg, const uint8_t *buf, int count);
bool ble_is_connected(void);
bool ble_is_ready(void);  // Returns true after NimBLE fully initialized
int8_t ble_get_rssi(void);  // 127 when not connected

// Radio packet counters (incremented in led_indicate_radio_tx/rx)
extern volatile uint32_t radio_tx_count;
extern volatile uint32_t radio_rx_count;
extern volatile uint32_t radio_rx_timeout_count;
extern volatile int last_pump_rssi;        // dBm of last received pump packet
extern volatile int last_pump_rssi_avg;    // exponential moving average of pump RSSI
extern volatile int64_t last_pump_rx_us;   // time of last pump RX (0 = never)

// Percent of successful RX over a sliding window of the last ~100 outcomes;
// -1 when no packets received yet
int radio_link_quality(void);
