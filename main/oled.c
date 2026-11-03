#include "oled.h"

#include "module.h"

#ifdef OLED_ENABLE

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <driver/i2c.h>

#include "gnarl.h"
#include "adc.h"
#include "rfm95.h"
#include "pump_config.h"
#include "oled_font.h"

#ifndef OLED_ADDR
#define OLED_ADDR 0x3C
#endif

#define OLED_I2C_PORT   I2C_NUM_0
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_PAGES      (OLED_HEIGHT / 8)
#define OLED_LINE_CHARS (OLED_WIDTH / 6)

static uint8_t fb[OLED_WIDTH * OLED_PAGES]; // framebuffer: page-major

//---------------------------------------------------------------------
// Low-level SSD1306 I2C
//---------------------------------------------------------------------

static esp_err_t oled_write(const uint8_t *data, size_t len, uint8_t control) {
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
	i2c_master_write_byte(cmd, control, true);
	i2c_master_write(cmd, (uint8_t *)data, len, true);
	i2c_master_stop(cmd);
	esp_err_t err = i2c_master_cmd_begin(OLED_I2C_PORT, cmd, pdMS_TO_TICKS(100));
	i2c_cmd_link_delete(cmd);
	return err;
}

static esp_err_t oled_cmd(uint8_t c) {
	return oled_write(&c, 1, 0x00);
}

static void oled_hw_init(void) {
	i2c_config_t cfg = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = OLED_SDA,
		.scl_io_num = OLED_SCL,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 400000,
	};
	i2c_param_config(OLED_I2C_PORT, &cfg);
	i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

	static const uint8_t init_seq[] = {
		0xAE,       // display off
		0xD5, 0x80, // clock divide
		0xA8, 0x3F, // multiplex 64
		0xD3, 0x00, // display offset 0
		0x40,       // start line 0
		0x8D, 0x14, // charge pump on
		0x20, 0x00, // horizontal addressing
		0xA1,       // segment remap (flip horizontally)
		0xC8,       // COM scan descending (flip vertically)
		0xDA, 0x12, // COM pins config
		0x81, 0xCF, // contrast
		0xD9, 0xF1, // pre-charge
		0xDB, 0x40, // VCOMH level
		0xA4,       // display from RAM
		0xA6,       // normal (not inverted)
		0xAF,       // display on
	};
	for (size_t i = 0; i < sizeof(init_seq); i++) {
		oled_cmd(init_seq[i]);
	}
}

static void oled_flush(void) {
	// Horizontal addressing: full window
	oled_cmd(0x21); oled_cmd(0); oled_cmd(OLED_WIDTH - 1);   // columns
	oled_cmd(0x22); oled_cmd(0); oled_cmd(OLED_PAGES - 1);   // pages

	// Send framebuffer in chunks (I2C buffer limits)
	size_t sent = 0;
	while (sent < sizeof(fb)) {
		size_t chunk = sizeof(fb) - sent;
		if (chunk > 128) chunk = 128;
		oled_write(fb + sent, chunk, 0x40);
		sent += chunk;
	}
}

//---------------------------------------------------------------------
// Drawing
//---------------------------------------------------------------------

static void oled_clear(void) {
	memset(fb, 0, sizeof(fb));
}

static void oled_char(int x, int page, char c) {
	if (c < 32 || c > 126) c = '?';
	const unsigned char *g = font6x8[(unsigned char)c - 32];
	for (int i = 0; i < 6; i++) {
		int col = x + i;
		if (col >= 0 && col < OLED_WIDTH) {
			fb[page * OLED_WIDTH + col] |= g[i];
		}
	}
}

static void oled_text(int x, int line, const char *s) {
	while (*s && x < OLED_WIDTH - 5) {
		oled_char(x, line, *s++);
		x += 6;
	}
}

static void oled_hline(int page) {
	memset(fb + page * OLED_WIDTH, 0x01, OLED_WIDTH); // dotted line at top of page
}

//---------------------------------------------------------------------
// Status screen
//---------------------------------------------------------------------

static void oled_draw_status(void) {
	char buf[OLED_LINE_CHARS + 2];

	oled_clear();

	// Line 0: name + BLE state
	snprintf(buf, sizeof(buf), "%-8s BLE:%s", BLE_NAME,
		 ble_is_connected() ? "CONN" : "WAIT");
	oled_text(0, 0, buf);
	oled_hline(1);

	// Line 2: battery
	uint16_t mv = get_battery_voltage();
	snprintf(buf, sizeof(buf), "Batt %d.%02dV    %3d%%",
		 mv / 1000, (mv % 1000) / 10, battery_percent(mv));
	oled_text(0, 2, buf);

	// Line 3: radio frequency
	uint32_t f = read_frequency();
	snprintf(buf, sizeof(buf), "Freq %lu.%03lu MHz",
		 (unsigned long)(f / 1000000), (unsigned long)((f % 1000000) / 1000));
	oled_text(0, 3, buf);

	// Line 4: packet counters
	snprintf(buf, sizeof(buf), "TX %-6lu RX %-6lu",
		 (unsigned long)radio_tx_count, (unsigned long)radio_rx_count);
	oled_text(0, 4, buf);

	// Line 5: pump serial
	snprintf(buf, sizeof(buf), "Pump %s", PUMP_ID);
	oled_text(0, 5, buf);

	// Line 6: BLE RSSI
	int8_t rssi = ble_get_rssi();
	if (rssi == 127) {
		snprintf(buf, sizeof(buf), "RSSI --- dBm");
	} else {
		snprintf(buf, sizeof(buf), "RSSI %d dBm", rssi);
	}
	oled_text(0, 6, buf);

	// Line 7: uptime
	uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
	snprintf(buf, sizeof(buf), "Up %02d:%02d:%02d", up / 3600, (up / 60) % 60, up % 60);
	oled_text(0, 7, buf);

	oled_flush();
}

static void oled_task(void *arg) {
	// Wait a moment for power to stabilize after boot
	vTaskDelay(pdMS_TO_TICKS(500));
	oled_hw_init();
	ESP_LOGI("OLED", "SSD1306 initialized (%s)", BLE_NAME);

	while (1) {
		oled_draw_status();
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

void oled_init(void) {
	xTaskCreate(oled_task, "oled", 3072, NULL, 3, NULL);
}

#else // !OLED_ENABLE

void oled_init(void) {
	// OLED disabled in module.h
}

#endif // OLED_ENABLE
