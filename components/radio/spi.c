#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "module.h"
#include "spi.h"

#define MILLISECONDS	1000

// SPI host selection:
// - ESP32: SPI2_HOST (HSPI) and SPI3_HOST (VSPI) available
// - ESP32-C6: Only SPI2_HOST available (no SPI3)
#ifdef CONFIG_IDF_TARGET_ESP32C6
    #define RFM95_SPI_HOST  SPI2_HOST
#else
    #define RFM95_SPI_HOST  SPI3_HOST
#endif

static spi_device_handle_t spi_dev;

// spi_device_transmit() is NOT thread-safe per Espressif docs when several
// tasks share one SPI device (get_trans_result may return another task's
// transaction -> assert "ret_trans == trans_desc"). Serialize all accesses.
static SemaphoreHandle_t spi_mutex;

static inline void spi_lock(void) {
	xSemaphoreTake(spi_mutex, portMAX_DELAY);
}

static inline void spi_unlock(void) {
	xSemaphoreGive(spi_mutex);
}

void spi_init(void) {
	spi_mutex = xSemaphoreCreateMutex();
	// Initialize the SPI bus.
	spi_bus_config_t buscfg = {
		.mosi_io_num   = LORA_MOSI,
		.miso_io_num   = LORA_MISO,
		.sclk_io_num   = LORA_SCK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
	};
	ESP_ERROR_CHECK(spi_bus_initialize(RFM95_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

	gpio_set_direction(LORA_CS, GPIO_MODE_OUTPUT);
	gpio_set_level(LORA_CS, 1);

	// Attach the radio to the SPI bus.
	spi_device_interface_config_t devcfg = {
		.command_bits	= 8,
		.address_bits	= 0,
		.mode		= 0,
#ifdef CONFIG_IDF_TARGET_ESP32C6
		// Lower SPI clock for debugging on ESP32-C6 boards (some boards have slow SPI lines or shared flash pins)
		.clock_speed_hz = 1*MEGAHERTZ,
#else
		.clock_speed_hz = 5*MEGAHERTZ,
#endif
		.spics_io_num	= LORA_CS,
		.queue_size	= 1,
	};
	ESP_ERROR_CHECK(spi_bus_add_device(RFM95_SPI_HOST, &devcfg, &spi_dev));
}

// Read register by sending the address followed by a dummy byte.
// Register value is returned while the dummy byte is being written.
uint8_t read_register(uint8_t addr) {
	spi_transaction_t t = {
		.flags	= SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
		.cmd	= addr,
		.length = 8,  // bits
	};
	spi_lock();
	esp_err_t err = spi_device_transmit(spi_dev, &t);
	spi_unlock();
	ESP_ERROR_CHECK(err);
	return t.rx_data[0];
}

void read_burst(uint8_t addr, uint8_t *buf, int count) {
	spi_transaction_t t = {
		.cmd	   = addr,
		.length	   = 8 * count,  // bits
		.tx_buffer = buf,
		.rx_buffer = buf,
	};
	spi_lock();
	esp_err_t err = spi_device_transmit(spi_dev, &t);
	spi_unlock();
	ESP_ERROR_CHECK(err);
}

// Write register by sending the address with the write bit set,
// followed by the value.
void write_register(uint8_t addr, uint8_t value) {
	spi_transaction_t t = {
		.flags	= SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
		.cmd	= addr | SPI_WRITE,
		.length = 8,  // bits
	};
	t.tx_data[0] = value;
	spi_lock();
	esp_err_t err = spi_device_transmit(spi_dev, &t);
	spi_unlock();
	ESP_ERROR_CHECK(err);
}

void write_burst(uint8_t addr, uint8_t *buf, int count) {
	spi_transaction_t t = {
		.flags	   = SPI_TRANS_USE_RXDATA,
		.cmd	   = addr | SPI_WRITE,
		.length	   = 8 * count,  // bits
		.rxlength  = 8,  // bits
		.tx_buffer = buf,
	};
	spi_lock();
	esp_err_t err = spi_device_transmit(spi_dev, &t);
	spi_unlock();
	ESP_ERROR_CHECK(err);
}
