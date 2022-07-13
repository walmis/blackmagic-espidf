#include "esp_adc_cal.h"
#include "esp_log.h"
#include "driver/adc.h"

#define TAG "bmp-adc"

// ADC Calibration
#if CONFIG_IDF_TARGET_ESP32
#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_VREF
#elif CONFIG_IDF_TARGET_ESP32S2
#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32C3
#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32S3
#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP_FIT
#endif

#define ADC_CHANNEL ADC1_CHANNEL_7

static bool adc_calibration_init(esp_adc_cal_characteristics_t *adc_characteristics)
{
	esp_err_t ret;
	bool cali_enable = false;

	ret = esp_adc_cal_check_efuse(ADC_EXAMPLE_CALI_SCHEME);
	if (ret == ESP_ERR_NOT_SUPPORTED) {
		ESP_LOGW(TAG, "Calibration scheme not supported, skip software calibration");
	} else if (ret == ESP_ERR_INVALID_VERSION) {
		ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
	} else if (ret == ESP_OK) {
		cali_enable = true;
		esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_2_5, ADC_WIDTH_BIT_DEFAULT, 0, adc_characteristics);
	} else {
		ESP_LOGE(TAG, "Invalid arg");
	}

	return cali_enable;
}

static bool adc_init(esp_adc_cal_characteristics_t *adc_characteristics)
{
	bool ret = adc_calibration_init(adc_characteristics);
	ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
	ESP_ERROR_CHECK(adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_2_5));
	return ret;
}

int32_t adc_read_system_voltage(void)
{
	static bool calibrated = false;
	static esp_adc_cal_characteristics_t adc_characteristics;
	int adc_reading;

	if (!calibrated) {
		calibrated = adc_init(&adc_characteristics);
	}
	if (!calibrated) {
		return -1;
	}

	adc_reading = adc1_get_raw(ADC_CHANNEL);
	ESP_LOGI(TAG, "raw  data: %d", adc_reading);

	uint32_t voltage_reading = esp_adc_cal_raw_to_voltage(adc_reading, &adc_characteristics);

    // Farpatch has a divider that's 82k on top and 20k on the bottom.
    uint32_t adjusted_voltage = (voltage_reading * 51) / 10;

	ESP_LOGI(TAG, "cal data: %d mV, adjusted: %d mV", voltage_reading, adjusted_voltage);

    return adjusted_voltage;
}