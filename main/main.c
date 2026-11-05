#include "gnarl.h"

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include "adc.h"
#include "led.h"
#include "oled.h"
#include "ota.h"
#include "rfm95.h"
#include "version.h"

#include <esp_ota_ops.h>

#include "soc/soc.h"
#ifndef CONFIG_IDF_TARGET_ESP32C6
#include "soc/rtc_cntl_reg.h"
#endif
#include <driver/gpio.h>
#include "pump_config.h"
// #define PUMP_FREQUENCY 868250000

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
// Anti-brick: a fresh OTA image must prove it is stable.
// If the device crashes or reboots within 60 s of boot, the bootloader
// automatically rolls back to the previous working image.
static void ota_confirm_task(void *arg) {
	vTaskDelay(pdMS_TO_TICKS(60000));
	esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
	ESP_LOGI(TAG, "OTA self-check passed: mark_app_valid -> %s", esp_err_to_name(err));
	vTaskDelete(NULL);
}
#endif

void app_main(void) {
#ifndef CONFIG_IDF_TARGET_ESP32C6
	// Brownout detector disabled to prevent infinite reboot loops when battery is low.
	// Instead, we use proactive battery monitoring in adc.c:
	// - Battery voltage checked every 60 seconds
	// - At critical level (3300 mV), device enters deep sleep
	// - This prevents battery damage from repeated brownout-triggered reboots
	WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif

	// Increase NimBLE verbosity to help diagnose disconnects
	esp_log_level_set("NimBLE", ESP_LOG_ERROR);
	ESP_LOGI(TAG, "%s (build %s)", SUBG_RFSPY_VERSION, FW_GIT_SHA);
	
	// Initialize NVS flash first (required for LED mode and BLE)
	// Recover from corrupted or full NVS (e.g. power loss during write):
	// erase and retry once instead of aborting on every boot.
	esp_err_t nvs_err = nvs_flash_init();
	if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_LOGW(TAG, "NVS corrupted (%s), erasing and reinitializing", esp_err_to_name(nvs_err));
		ESP_ERROR_CHECK(nvs_flash_erase());
		nvs_err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(nvs_err);

	// OTA entry: double-reset detector - press RST twice within ~4 s
	// to enter update mode (WiFi AP "GNARL-OTA", http://192.168.4.1).
	// Placed after NVS init: WiFi calibration data lives in NVS.
	// In OTA mode radio/BLE are NOT started (saves power, avoids conflicts).
	if (ota_entry_requested()) {
		ota_start();  // never returns
	}

	// Initialize GPIO ISR service (required for radio interrupt handling)
	// Must be called once before rfm95_init()
	ESP_ERROR_CHECK(gpio_install_isr_service(0));

	// Initialize LED GPIO and task
	led_init();
	// Load saved LED mode from NVS (or use default AUTO)
	led_load_mode();
	// Apply the loaded mode (no need to call led_set_mode - it would re-save)
	// current_led_mode already set by led_load_mode, just apply hardware state
	if (current_led_mode == LED_MODE_OFF) {
		led_off();
	} else if (current_led_mode == LED_MODE_ON) {
		led_on();
	} else {
		led_off(); // AUTO mode starts with LED off
	}
	
	rfm95_init();
	uint8_t v = read_version();
	ESP_LOGD(TAG, "radio version %d.%d", version_major(v), version_minor(v));
	
	// Load saved frequency from NVS (or use default PUMP_FREQUENCY)
	uint32_t startup_freq = gnarl_load_frequency();
	set_frequency(startup_freq);
	ESP_LOGD(TAG, "frequency set to %lu Hz", (unsigned long)read_frequency());
	
	adc_init();
	oled_init();
	gnarl_init();

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
	// Radio, OLED and BLE are up - start the 60 s self-validation timer.
	// If we crash or reboot before it fires, the bootloader rolls back
	// to the previous working image.
	xTaskCreate(ota_confirm_task, "ota_cf", 2048, NULL, 1, NULL);
#endif
}
