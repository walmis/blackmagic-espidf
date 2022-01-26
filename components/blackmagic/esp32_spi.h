#ifndef __ESP32_SPI_H__
#define __ESP32_SPI_H__

#include <stdint.h>
#include "driver/spi_master.h"

#define BMP_SPI_BUS_ID HSPI_HOST
extern spi_device_handle_t bmp_spi_handle;
extern spi_dev_t *bmp_spi_hw;

int esp32_spi_init(int swd);
void esp32_spi_mux_pin(int pin, int out_signal, int in_signal);
int esp32_spi_set_frequency(uint32_t frequency);
int esp32_spi_get_frequency(void);

#endif /* __ESP32_SPI_H__ */