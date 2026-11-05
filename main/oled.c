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
#include <driver/gpio.h>
#include <nvs.h>

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

// Screensaver: display off after 5 minutes without BLE connection
#define OLED_SCREENSAVER_US  (5LL * 60 * 1000000)
// Long button press threshold (screen flip 180 degrees)
#define BTN_LONG_PRESS_US    (1500LL * 1000)
// Button debounce
#define BTN_DEBOUNCE_US      (50LL * 1000)

static uint8_t fb[OLED_WIDTH * OLED_PAGES]; // framebuffer: page-major

static bool display_on = true;
static bool screen_flipped = false;         // 180-degree rotation
static int64_t last_activity_us = 0;        // last BLE connection / button press

static void oled_apply_orientation(void);

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
	oled_apply_orientation();
}

//---------------------------------------------------------------------
// Power / orientation / NVS persistence
//---------------------------------------------------------------------

static void oled_set_power(bool on) {
	display_on = on;
	oled_cmd(on ? 0xAF : 0xAE);  // display ON / sleep
}

static void oled_apply_orientation(void) {
	oled_cmd(screen_flipped ? 0xA0 : 0xA1);  // segment remap
	oled_cmd(screen_flipped ? 0xC0 : 0xC8);  // COM scan direction
}

static void oled_load_flip(void) {
	nvs_handle_t h;
	if (nvs_open("pickle", NVS_READONLY, &h) == ESP_OK) {
		uint8_t v = 0;
		if (nvs_get_u8(h, "oled_flip", &v) == ESP_OK) {
			screen_flipped = (v != 0);
		}
		nvs_close(h);
	}
}

static void oled_save_flip(void) {
	nvs_handle_t h;
	if (nvs_open("pickle", NVS_READWRITE, &h) == ESP_OK) {
		nvs_set_u8(h, "oled_flip", screen_flipped ? 1 : 0);
		nvs_commit(h);
		nvs_close(h);
	}
}

//---------------------------------------------------------------------
// Drawing
//---------------------------------------------------------------------

static void oled_clear(void) {
	memset(fb, 0, sizeof(fb));
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

	// Line 0: name (left) + BLE state (flush right)
	snprintf(buf, sizeof(buf), "%s%*s", BLE_NAME,
		 OLED_LINE_CHARS - (int)strlen(BLE_NAME),
		 ble_is_connected() ? "BLE:CONNECTED" : "BLE:WAITING");
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

	// Line 4: pump link - averaged RSSI + seconds since last pump response
	if (last_pump_rx_us == 0) {
		snprintf(buf, sizeof(buf), "Pump --- no data");
	} else {
		uint32_t age_s = (uint32_t)((esp_timer_get_time() - last_pump_rx_us) / 1000000);
		if (age_s > 9999) {
			snprintf(buf, sizeof(buf), "Pump %ddBm  old", last_pump_rssi_avg);
		} else {
			snprintf(buf, sizeof(buf), "Pump %ddBm %lus", last_pump_rssi_avg, (unsigned long)age_s);
		}
	}
	oled_text(0, 4, buf);

	// Line 5: pump serial
	snprintf(buf, sizeof(buf), "Pump ID %s", PUMP_ID);
	oled_text(0, 5, buf);

	// Line 6: link quality - percent of successful RX vs timeouts
	{
		uint32_t rx_ok = radio_rx_count;
		uint32_t total = rx_ok + radio_rx_timeout_count;
		if (total == 0) {
			snprintf(buf, sizeof(buf), "Link ---");
		} else {
			snprintf(buf, sizeof(buf), "Link %lu%% ok", (unsigned long)(rx_ok * 100 / total));
		}
	}
	oled_text(0, 6, buf);

	// Line 7: uptime
	uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
	snprintf(buf, sizeof(buf), "Up %02lu:%02lu:%02lu",
		 (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60), (unsigned long)(up % 60));
	oled_text(0, 7, buf);

	oled_flush();
}

static void oled_task(void *arg) {
	// Wait a moment for power to stabilize after boot
	vTaskDelay(pdMS_TO_TICKS(500));
	oled_load_flip();
	oled_hw_init();
	ESP_LOGI("OLED", "SSD1306 initialized (%s)", BLE_NAME);

	// Button (BOOT button, active low)
	gpio_set_direction(BUTTON, GPIO_MODE_INPUT);
	gpio_set_pull_mode(BUTTON, GPIO_PULLUP_ONLY);

	last_activity_us = esp_timer_get_time();
	int64_t last_draw_us = 0;
	bool was_connected = ble_is_connected();
	bool btn_prev = false;
	int64_t btn_press_us = 0;

	while (1) {
		int64_t now = esp_timer_get_time();

		// --- Button: short press = display on/off, long press = flip 180 ---
		bool btn = (gpio_get_level(BUTTON) == 0);
		if (btn && !btn_prev) {
			btn_press_us = now;
		} else if (!btn && btn_prev) {
			int64_t held = now - btn_press_us;
			if (held >= BTN_LONG_PRESS_US) {
				screen_flipped = !screen_flipped;
				oled_save_flip();
				oled_apply_orientation();
				if (!display_on) oled_set_power(true);
				last_activity_us = now;
				last_draw_us = 0;  // force redraw
			} else if (held >= BTN_DEBOUNCE_US) {
				oled_set_power(!display_on);
				if (display_on) {
					last_activity_us = now;
					last_draw_us = 0;
				}
			}
		}
		btn_prev = btn;

		// --- Screensaver & wake-on-connect ---
		bool conn = ble_is_connected();
		if (conn) {
			last_activity_us = now;
			if (!was_connected && !display_on) {
				oled_set_power(true);
				last_draw_us = 0;
			}
		}
		was_connected = conn;

		if (display_on && (now - last_activity_us) > OLED_SCREENSAVER_US) {
			oled_set_power(false);
		}

		// --- Redraw: every 1s while connected (live RSSI/age), 5s when idle ---
		// Full flush is ~1 KB over I2C (~23 ms); idle data (battery) changes
		// only once a minute, so slow redraw saves bus activity and power.
		int64_t redraw_interval_us = conn ? 1000000 : 5000000;
		if (display_on && (now - last_draw_us) >= redraw_interval_us) {
			oled_draw_status();
			last_draw_us = now;
		}

		vTaskDelay(pdMS_TO_TICKS(50));
	}
}

void oled_init(void) {
	xTaskCreate(oled_task, "oled", 3072, NULL, 3, NULL);
}

//---------------------------------------------------------------------
// Standalone message screen (OTA mode, boot-time OTA prompt)
//---------------------------------------------------------------------

static bool ota_screen_inited = false;

void oled_ota_screen(const char *line1, const char *line2, int progress) {
	if (!ota_screen_inited) {
		// A second init later (normal boot path, oled_task -> oled_hw_init)
		// is harmless: i2c_driver_install just returns ESP_ERR_INVALID_STATE.
		oled_load_flip();
		oled_hw_init();
		ota_screen_inited = true;
		display_on = true;
		last_activity_us = esp_timer_get_time();
	}

	oled_clear();
	if (line1) {
		oled_text(0, 0, line1);
	}
	if (line2) {
		oled_text(0, 2, line2);
	}
	if (progress >= 0) {
		// "[=========     ]  63%" on line 5
		char buf[OLED_LINE_CHARS + 2];
		const int width = OLED_LINE_CHARS - 8;
		int filled = progress * width / 100;
		int pos = 0;
		buf[pos++] = '[';
		for (int i = 0; i < width; i++) {
			buf[pos++] = (i < filled) ? '=' : ' ';
		}
		snprintf(buf + pos, sizeof(buf) - pos, "] %3d%%", progress);
		oled_text(0, 5, buf);
	}
	oled_flush();
}

#else // !OLED_ENABLE

void oled_init(void) {
	// OLED disabled in module.h
}

void oled_ota_screen(const char *line1, const char *line2, int progress) {
	// OLED disabled in module.h
}

#endif // OLED_ENABLE
