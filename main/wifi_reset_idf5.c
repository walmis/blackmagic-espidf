#include "esp_idf_version.h"

#if ESP_IDF_VERSION_MAJOR == 5
#include <freertos/FreeRTOS.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "wifi_manager.h"

#define TAG "wifi-reset"
static gptimer_handle_t gptimer = NULL;

static bool IRAM_ATTR perform_wifi_reset(
	struct gptimer_t *timer, const gptimer_alarm_event_data_t *event, void *user_ctx)
{
	(void)user_ctx;
	BaseType_t high_task_awoken = pdFALSE;
	gptimer_stop(gptimer);

	if (gpio_get_level(GPIO_NUM_0) == 0) {
		wifi_manager_send_message_from_isr(WM_ORDER_FORGET_CONFIG, NULL, &high_task_awoken);
	}

	// return whether we need to yield at the end of ISR
	return high_task_awoken == pdTRUE;
}

void setup_wifi_reset(void)
{
	// ESP_LOGI(__func__, "handling button press");
	gptimer_config_t timer_config = {
		.clk_src = GPTIMER_CLK_SRC_DEFAULT,
		.direction = GPTIMER_COUNT_UP,
		.resolution_hz = 1000000, // 1MHz, 1 tick=1us
	};
	ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

	gptimer_event_callbacks_t cbs = {
		.on_alarm = perform_wifi_reset,
	};
	ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));

	ESP_LOGI(TAG, "Enable timer");
	ESP_ERROR_CHECK(gptimer_enable(gptimer));
}

void IRAM_ATTR handle_wifi_reset(void *parameter)
{
	(void)parameter;

	gptimer_alarm_config_t alarm_config = {
		.alarm_count = 3000000, // period = 1s, therefore 3 seconds
	};
	ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
	ESP_ERROR_CHECK(gptimer_start(gptimer));
}
#endif