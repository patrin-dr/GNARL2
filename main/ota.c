#include "ota.h"

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include "module.h"
#include "oled.h"
#include "version.h"

#include <nvs.h>

#define TAG "ota"

// Double-reset detector: press RST twice within ~6 s to enter OTA mode.
// The counter lives in NVS flash: RTC slow memory does NOT survive
// EN-button resets on this board (RTC domain is power-cycled, visible
// as PHY recalibration on every boot), so RTC_DATA_ATTR is useless here.
#define DRD_WINDOW_MS 6000
#define DRD_NVS_NS    "pickle"
#define DRD_NVS_KEY   "drd"

// Upload state (single connection at a time; the AP accepts max 2 clients)
static volatile int s_progress = -1;   // -1 = idle, 0..99 = writing, 100 = done
static esp_ota_handle_t s_ota_handle;
static bool s_ota_active = false;

//---------------------------------------------------------------------
// Boot-time entry check (double-reset detector)
//---------------------------------------------------------------------

// Clear the DRD counter once the device has been up for DRD_WINDOW_MS -
// resets after that point count as a new "first press".
static void drd_clear_task(void *arg) {
	vTaskDelay(pdMS_TO_TICKS(DRD_WINDOW_MS));
	nvs_handle_t h;
	if (nvs_open(DRD_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
		nvs_erase_key(h, DRD_NVS_KEY);
		nvs_commit(h);
		nvs_close(h);
	}
	vTaskDelete(NULL);
}

bool ota_entry_requested(void) {
	// Count only RST-button / power-on resets. Crash resets (panic, WDT)
	// reset the counter, so a crash-looping image can never end up in
	// OTA mode by accident - the bootloader rollback handles that case.
	uint8_t count = 0;
	nvs_handle_t h;
	if (nvs_open(DRD_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
		nvs_get_u8(h, DRD_NVS_KEY, &count);  // stays 0 if key absent
		if (esp_reset_reason() == ESP_RST_POWERON) {
			count++;
		} else {
			count = 0;
		}
		ESP_LOGI(TAG, "reset count %u (reason %d)", count, esp_reset_reason());
		if (count >= 2) {
			nvs_erase_key(h, DRD_NVS_KEY);
			nvs_commit(h);
			nvs_close(h);
			ESP_LOGI(TAG, "double reset detected - entering OTA mode");
			return true;
		}
		nvs_set_u8(h, DRD_NVS_KEY, count);
		nvs_commit(h);
		nvs_close(h);
	} else {
		ESP_LOGW(TAG, "NVS open failed - double-reset detection disabled");
	}

	// Brief boot hint (the regular status screen redraws over it ~1.5 s later)
	oled_ota_screen("GNARL2 " FW_GIT_SHA, "2x RST = OTA mode", -1);

	xTaskCreate(drd_clear_task, "drd", 1536, NULL, 1, NULL);
	return false;
}

//---------------------------------------------------------------------
// HTTP server
//---------------------------------------------------------------------

// Upload page: raw binary POST via fetch() (no multipart parsing needed).
// The page head (with the current build SHA) is generated dynamically in
// index_get_handler; this static tail contains '%' in JS, so it must never
// be used as an snprintf format string.
static const char INDEX_BODY[] =
	"<p><input type=file id=f accept=\".bin\"></p>"
	"<p><button onclick=go()>Upload</button></p>"
	"<pre id=s>idle</pre>"
	"<script>"
	"function s(x){document.getElementById('s').textContent=x}"
	"async function go(){"
	"const f=document.getElementById('f').files[0];"
	"if(!f){s('pick a .bin first');return}"
	"s('uploading '+f.size+' bytes...');"
	"const t=setInterval(async()=>{try{const r=await fetch('/status');"
	"const j=await r.json();s('progress: '+j.progress+'%')}catch(e){}},500);"
	"try{"
	"const r=await fetch('/update',{method:'POST',"
	"headers:{'Content-Type':'application/octet-stream'},body:f});"
	"clearInterval(t);s(await r.text())"
	"}catch(e){clearInterval(t);s('error: '+e)}"
	"}"
	"</script>";

static esp_err_t index_get_handler(httpd_req_t *req) {
	httpd_resp_set_type(req, "text/html");
	// -Werror=format-truncation: GCC считает худший случай ~256 байт
	char head[320];
	snprintf(head, sizeof(head),
		 "<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\">"
		 "<title>GNARL2 OTA</title>"
		 "<style>body{font-family:sans-serif;max-width:30em;margin:2em auto;padding:0 1em}</style>"
		 "<h3>GNARL2 OTA update</h3>"
		 "<p>Current build: <b>%s</b></p>", FW_GIT_SHA);
	httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);
	httpd_resp_send_chunk(req, INDEX_BODY, sizeof(INDEX_BODY) - 1);
	httpd_resp_send_chunk(req, NULL, 0);  // end of response
	return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req) {
	char json[48];
	snprintf(json, sizeof(json), "{\"progress\":%d}", s_progress);
	httpd_resp_set_type(req, "application/json");
	return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static void restart_task(void *arg) {
	vTaskDelay(pdMS_TO_TICKS(2000));  // let the HTTP response reach the browser
	esp_restart();
}

static esp_err_t update_post_handler(httpd_req_t *req) {
	if (req->content_len <= 0) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
		return ESP_FAIL;
	}

	const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
	if (!part) {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "writing %d bytes to partition %s @0x%lx",
		 req->content_len, part->label, (unsigned long)part->address);

	esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin failed");
		return ESP_FAIL;
	}
	s_ota_active = true;
	s_progress = 0;
	oled_ota_screen("OTA update", "Writing...", 0);

	// Static buffer: keeps it off the httpd task stack
	static char buf[2048];
	int remaining = req->content_len;
	int received = 0;
	bool ok = true;

	while (remaining > 0) {
		int want = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
		int len = httpd_req_recv(req, buf, want);
		if (len <= 0) {
			ESP_LOGE(TAG, "upload interrupted at %d/%d bytes", received, req->content_len);
			ok = false;
			break;
		}
		err = esp_ota_write(s_ota_handle, buf, len);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
			ok = false;
			break;
		}
		received += len;
		remaining -= len;
		s_progress = received * 100 / req->content_len;
		if ((received % 16384) < len) {  // ~every 16 KB
			oled_ota_screen("OTA update", "Writing...", s_progress);
		}
	}

	if (!ok) {
		esp_ota_abort(s_ota_handle);
		s_ota_active = false;
		s_progress = -1;
		oled_ota_screen("OTA update", "FAILED - retry", -1);
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed - retry");
		return ESP_FAIL;
	}

	err = esp_ota_end(s_ota_handle);
	s_ota_active = false;
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
		s_progress = -1;
		oled_ota_screen("OTA update", "BAD IMAGE", -1);
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "image validation failed");
		return ESP_FAIL;
	}
	err = esp_ota_set_boot_partition(part);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
		s_progress = -1;
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot failed");
		return ESP_FAIL;
	}

	s_progress = 100;
	ESP_LOGI(TAG, "OTA done, rebooting");
	oled_ota_screen("OTA update", "Done! Rebooting", 100);
	httpd_resp_sendstr(req, "OK - device reboots in 2 s");
	xTaskCreate(restart_task, "ota_rst", 2048, NULL, 5, NULL);
	return ESP_OK;
}

//---------------------------------------------------------------------
// WiFi AP + server startup
//---------------------------------------------------------------------

static void wifi_ap_start(void) {
	// netif/event loop may already exist (guarded for reuse)
	esp_err_t err = esp_netif_init();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_ERROR_CHECK(err);
	}
	err = esp_event_loop_create_default();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_ERROR_CHECK(err);
	}
	esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	wifi_config_t ap = {
		.ap = {
			.channel = 1,
			.max_connection = 2,
			.authmode = WIFI_AUTH_WPA2_PSK,
			.pmf_cfg = { .required = false },
		},
	};
	strlcpy((char *)ap.ap.ssid, OTA_AP_SSID, sizeof(ap.ap.ssid));
	ap.ap.ssid_len = strlen(OTA_AP_SSID);
	strlcpy((char *)ap.ap.password, OTA_AP_PASSWORD, sizeof(ap.ap.password));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(TAG, "AP started: SSID=%s", OTA_AP_SSID);
}

void ota_start(void) {
	ESP_LOGI(TAG, "OTA mode: connect to %s, open http://%s", OTA_AP_SSID, OTA_AP_IP);
	oled_ota_screen("OTA mode", "WiFi: " OTA_AP_SSID, -1);

	wifi_ap_start();

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.stack_size = 8192;
	config.max_open_sockets = 3;
	config.lru_purge_enable = true;
	config.recv_wait_timeout = 30;
	config.send_wait_timeout = 30;

	httpd_handle_t server = NULL;
	ESP_ERROR_CHECK(httpd_start(&server, &config));

	const httpd_uri_t uri_index = {
		.uri = "/", .method = HTTP_GET, .handler = index_get_handler,
	};
	const httpd_uri_t uri_status = {
		.uri = "/status", .method = HTTP_GET, .handler = status_get_handler,
	};
	const httpd_uri_t uri_update = {
		.uri = "/update", .method = HTTP_POST, .handler = update_post_handler,
	};
	ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_index));
	ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_status));
	ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_update));

	ESP_LOGI(TAG, "HTTP server ready on http://%s", OTA_AP_IP);
	oled_ota_screen("OTA ready", OTA_AP_IP, -1);

	// OTA mode runs forever; the device reboots after a successful upload
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
