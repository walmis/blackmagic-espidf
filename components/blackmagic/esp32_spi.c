#include "esp32_spi.h"
#include "sdkconfig.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "hal/gpio_hal.h"
#include "hal/spi_ll.h"
#include "hal/spi_hal.h"

#define INITIAL_FREQUENCY (20 * 1000 * 1000)
spi_device_handle_t bmp_spi_handle;
spi_dev_t *bmp_spi_hw = SPI_LL_GET_HW(BMP_SPI_BUS_ID);

#define TAG "esp32-spi"

static bool is_swd = 0;
static int actual_freq = INITIAL_FREQUENCY;
int esp32_spi_set_frequency(uint32_t frequency)
{
    esp_err_t ret;
    spi_hal_timing_conf_t temp_timing_conf;
    spi_hal_timing_param_t timing_param = {
        // SWD reuses the same pin for both input and output, JTAG does not
        .half_duplex = is_swd,

        // We need to add dummy cycles to compensate for timing
        .no_compensate = 0,

        // 5 MHz
        .clock_speed_hz = frequency,

        // 50% duty cycle
        .duty_cycle = 128,

        // No input delay
        .input_delay_ns = 0,

        // Use GPIO matrix for more flexible pin assignment at the
        // cost of slightly worse performance.
        .use_gpio = 1,
    };

    ret = spi_hal_cal_clock_conf(&timing_param, &actual_freq, &temp_timing_conf);
    ESP_ERROR_CHECK(ret);
    spi_ll_master_set_clock_by_reg(bmp_spi_hw, &temp_timing_conf.clock_reg);
    spi_ll_set_dummy(bmp_spi_hw, temp_timing_conf.timing_dummy);

    uint32_t miso_delay_num = 0;
    uint32_t miso_delay_mode = 0;
    if (temp_timing_conf.timing_miso_delay < 0)
    {
        miso_delay_mode = 2;
        miso_delay_num = 0;
    }
    else
    {
        // if the data is so fast that dummy_bit is used, delay some apb clocks to meet the timing
        miso_delay_mode = 0;
        miso_delay_num = temp_timing_conf.timing_miso_delay;
    }
    spi_ll_set_miso_delay(bmp_spi_hw, miso_delay_mode, miso_delay_num);

    return actual_freq;
}

int esp32_spi_get_frequency(void)
{
    return actual_freq;
}

void esp32_spi_mux_pin(int pin, int out_signal, int in_signal)
{
    if (pin < 0)
    {
        return;
    }

    gpio_dev_t *hw = GPIO_HAL_GET_HW(GPIO_PORT_0);
    // gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
    hw->func_in_sel_cfg[pin].val = in_signal;
    hw->func_out_sel_cfg[pin].val = out_signal;
    // esp_rom_gpio_connect_out_signal(pin, out_signal, false, false);
    // esp_rom_gpio_connect_in_signal(pin, in_signal, false);
#if CONFIG_IDF_TARGET_ESP32S2
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[pin]);
#endif
    // gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
}

int esp32_spi_init(int swd)
{
    static bool initialized = false;
    is_swd = swd;

    esp_err_t ret;

    if (initialized)
    {
        // spi_device_release_bus(bmp_spi_handle);

        ret = spi_bus_remove_device(bmp_spi_handle);
        ESP_ERROR_CHECK(ret);

        ret = spi_bus_free(BMP_SPI_BUS_ID);
        ESP_ERROR_CHECK(ret);

        initialized = false;
    }

    // gpio_reset_pin(CONFIG_TDI_GPIO);
    // gpio_reset_pin(CONFIG_TDO_GPIO);
    // gpio_reset_pin(CONFIG_TCK_SWCLK_GPIO);
    // gpio_reset_pin(CONFIG_TMS_SWDIO_GPIO);

    if (!initialized)
    {
        ESP_LOGI(TAG, "Initializing bus SPI%d...", BMP_SPI_BUS_ID + 1);
        const spi_bus_config_t buscfg = {
            .miso_io_num = swd ? -1 : CONFIG_TDO_GPIO,
            .mosi_io_num = CONFIG_TMS_SWDIO_GPIO,
            .sclk_io_num = CONFIG_TCK_SWCLK_GPIO,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            // Maximum transfer size is in bytes. We define 5 bytes here because
            // the system will provide us at most one uint32_t of data, and we
            // sometimes need to add a parity bit.
            .max_transfer_sz = 5,
        };
        // Initialize the SPI bus without DMA. Our packets all fit into one or two 32-bit registers, so we
        // don't need DMA.
        ret = spi_bus_initialize(BMP_SPI_BUS_ID, &buscfg, SPI_DMA_DISABLED);
        ESP_ERROR_CHECK(ret);

        const spi_device_interface_config_t devcfg = {
            .clock_speed_hz = actual_freq,
            .mode = 3, // SPI mode 3
            .spics_io_num = -1,
            .queue_size = 1,
            .flags = (swd ? (SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE) : 0) | SPI_DEVICE_BIT_LSBFIRST,
            .input_delay_ns = 0,
        };

        // Attach the SWD device to the SPI bus
        ESP_LOGI(TAG, "Adding interface to SPI bus...");
        ret = spi_bus_add_device(BMP_SPI_BUS_ID, &devcfg, &bmp_spi_handle);
        if (ret != ESP_OK)
        {
            ESP_LOGI(TAG, "Unable to add interface: %d\n", ret);
            goto cleanup;
        }

        // Acquire the bus, and don't release it. This configures much of the hardware.
        // ret = spi_device_acquire_bus(bmp_spi_handle, portMAX_DELAY);
        // ESP_ERROR_CHECK(ret);

        initialized = 1;
    }

    ESP_LOGI(TAG, "Setting SPI frequency to %d Hz...", actual_freq);
    esp32_spi_set_frequency(actual_freq);

    if (swd)
    {
        spi_ll_enable_miso(bmp_spi_hw, 1);
        spi_ll_enable_mosi(bmp_spi_hw, 0);
    }
    else
    {
        spi_ll_enable_miso(bmp_spi_hw, 1);
        spi_ll_enable_mosi(bmp_spi_hw, 1);
    }

    // #if SOC_SPI_SUPPORT_AS_CS
    //     spi_ll_master_set_cksel(bmp_spi_hw, dev->cs_pin_id, dev->as_cs);
    // #endif
    //     spi_ll_master_set_pos_cs(bmp_spi_hw, dev->cs_pin_id, dev->positive_cs);

    // SWD and JTAG are LSB protocols
    spi_ll_set_rx_lsbfirst(bmp_spi_hw, 1);
    spi_ll_set_tx_lsbfirst(bmp_spi_hw, 1);

    spi_ll_master_set_mode(bmp_spi_hw, 3);

    if (swd)
    {
        spi_ll_set_half_duplex(bmp_spi_hw, 1);
        spi_ll_set_sio_mode(bmp_spi_hw, 1);
    }
    else
    {
        spi_ll_set_half_duplex(bmp_spi_hw, 0);
        spi_ll_set_sio_mode(bmp_spi_hw, 0);
    }

    // Configure CS pin and timing
    spi_ll_master_set_cs_setup(bmp_spi_hw, 0);
    spi_ll_master_set_cs_hold(bmp_spi_hw, 0);
    spi_ll_master_select_cs(bmp_spi_hw, -1);

    // ESP_LOGI(TAG, "Unmuxing various pins...");
    // // Default these to ordinary GPIOs -- they will be reinitialized by their
    // // corresponding functions.
    gpio_reset_pin(CONFIG_TDI_GPIO);
    // gpio_reset_pin(CONFIG_TDO_GPIO);
    gpio_reset_pin(CONFIG_TMS_SWDIO_GPIO);
    gpio_reset_pin(CONFIG_TCK_SWCLK_GPIO);

    // Mux TDO as MISO
    if (!swd)
    {
        esp32_spi_mux_pin(CONFIG_TDO_GPIO,
                          spi_periph_signal[BMP_SPI_BUS_ID].spiq_out,
                          spi_periph_signal[BMP_SPI_BUS_ID].spiq_in);
    }

    // Mux TDI as a GPIO for now -- it will be remuxed as necessary
    if (swd)
    {
        esp32_spi_mux_pin(CONFIG_TDI_GPIO, SIG_GPIO_OUT_IDX | (1 << 10), 128);
    }

    // Mux TMS as either MISO (for SWD) or as a GPIO (for JTAG)
    if (swd)
    {
        gpio_set_direction(CONFIG_TMS_SWDIO_GPIO, GPIO_MODE_INPUT_OUTPUT);
        // Set the OEN bit to be controlled by us, rather than by the peripheral. This bit will
        // be altered when we do the turnaround above.
        GPIO_HAL_GET_HW(GPIO_PORT_0)->func_out_sel_cfg[CONFIG_TMS_SWDIO_GPIO].oen_sel = 1;
        GPIO_HAL_GET_HW(GPIO_PORT_0)->enable_w1tc = (1 << CONFIG_TMS_SWDIO_GPIO);

        esp32_spi_mux_pin(CONFIG_TMS_SWDIO_GPIO,
                          spi_periph_signal[BMP_SPI_BUS_ID].spid_out,
                          spi_periph_signal[BMP_SPI_BUS_ID].spid_in);
    }
    else
    {
        esp32_spi_mux_pin(CONFIG_TMS_SWDIO_GPIO, SIG_GPIO_OUT_IDX | (1 << 10), 128);
    }

    // The clock pin is shared among SWD and JTAG, so define it here
    ESP_LOGI(TAG, "Muxing clock pins...");
    esp32_spi_mux_pin(CONFIG_TCK_SWCLK_GPIO,
                      spi_periph_signal[BMP_SPI_BUS_ID].spiclk_out,
                      spi_periph_signal[BMP_SPI_BUS_ID].spiclk_in);

    // Normal IO mode, not DIO or QIO
    spi_ll_master_set_io_mode(bmp_spi_hw, SPI_LL_IO_MODE_NORMAL);

    // We use neither the `addr` nor the `command` features
    spi_ll_set_addr_bitlen(bmp_spi_hw, 0);
    spi_ll_set_command_bitlen(bmp_spi_hw, 0);

    return 0;

cleanup:
    spi_bus_remove_device(bmp_spi_handle);
    return -1;
}