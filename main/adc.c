#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#define TAG "bmp-adc"

#define ADC_CHANNEL ADC_CHANNEL_8
#define ADC_UNIT    ADC_UNIT_1

static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
	adc_cali_handle_t handle = NULL;
	esp_err_t ret = ESP_FAIL;
	bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
	if (!calibrated) {
		ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
		adc_cali_curve_fitting_config_t cali_config = {
			.unit_id = unit,
			.atten = atten,
			.bitwidth = ADC_BITWIDTH_DEFAULT,
		};
		ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
		if (ret == ESP_OK) {
			calibrated = true;
		}
	}
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
	if (!calibrated) {
		ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
		adc_cali_line_fitting_config_t cali_config = {
			.unit_id = unit,
			.atten = atten,
			.bitwidth = ADC_BITWIDTH_DEFAULT,
		};
		ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
		if (ret == ESP_OK) {
			calibrated = true;
		}
	}
#endif

	*out_handle = handle;
	if (ret == ESP_OK) {
		ESP_LOGI(TAG, "Calibration Success");
	} else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
		ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
	} else {
		ESP_LOGE(TAG, "Invalid arg or no memory");
	}

	return calibrated;
}

static bool adc_init(adc_cali_handle_t *adc_cali_handle, adc_oneshot_unit_handle_t *adc_handle)
{
	const adc_oneshot_unit_init_cfg_t init_config = {
		.unit_id = ADC_UNIT,
	};
	const adc_oneshot_chan_cfg_t config = {
		.bitwidth = ADC_BITWIDTH_DEFAULT,
		.atten = ADC_ATTEN_DB_2_5,
	};
	if (!adc_calibration_init(ADC_UNIT, ADC_ATTEN_DB_2_5, adc_cali_handle)) {
		return false;
	}
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, adc_handle));
	ESP_ERROR_CHECK(adc_oneshot_config_channel(*adc_handle, ADC_CHANNEL, &config));
	return true;
}

int32_t adc_read_system_voltage(void)
{
	static adc_cali_handle_t adc_cali_handle;
	static adc_oneshot_unit_handle_t adc_handle;
	int adc_reading;
	int voltage_reading;

	if (adc_handle == NULL && !adc_init(&adc_cali_handle, &adc_handle)) {
		return -1;
	}

	ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_reading));
	ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT + 1, ADC_CHANNEL, adc_reading);
	// ESP_LOGD(TAG, "raw  data: %d", adc_reading);

	ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_reading, &voltage_reading));
	ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT + 1, ADC_CHANNEL, voltage_reading);

	// Farpatch has a divider that's 82k on top and 20k on the bottom.
	int adjusted_voltage = (voltage_reading * 51) / 10;
	ESP_LOGD(TAG, "cal data: %d mV, adjusted: %d mV", voltage_reading, adjusted_voltage);

	return adjusted_voltage;
}