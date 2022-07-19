#include "esp_idf_version.h"

#if ESP_IDF_VERSION_MAJOR == 4
#include "driver/timer.h"
static bool timer_initialized = false;

static bool IRAM_ATTR perform_wifi_reset(void *user_ctx)
{
	(void)user_ctx;
	BaseType_t high_task_awoken = pdFALSE;

	if (gpio_get_level(GPIO_NUM_0) == 0) {
		wifi_manager_send_message_from_isr(WM_ORDER_FORGET_CONFIG, NULL, &high_task_awoken);
	}

	// return whether we need to yield at the end of ISR
	return high_task_awoken == pdTRUE;
}
#define TIMER_DIVIDER (16)                             //  Hardware timer clock divider
#define TIMER_SCALE   (TIMER_BASE_CLK / TIMER_DIVIDER) // convert counter value to seconds

void setup_wifi_reset(void)
{
}

void IRAM_ATTR handle_wifi_reset(void *parameter)
{
	(void)parameter;

	// ESP_LOGI(__func__, "handling button press");
	int group = TIMER_GROUP_0;
	int timer = TIMER_0;
	if (!timer_initialized) {
		bool auto_reload = false;
		int timer_interval_sec = 3;

		/* Select and initialize basic parameters of the timer */
		timer_config_t config = {
			.divider = TIMER_DIVIDER,
			.counter_dir = TIMER_COUNT_UP,
			.counter_en = TIMER_PAUSE,
			.alarm_en = TIMER_ALARM_EN,
			.auto_reload = auto_reload,
		}; // default clock source is APB
		timer_init(group, timer, &config);

		/* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
		timer_set_counter_value(group, timer, 0);

		/* Configure the alarm value and the interrupt on alarm. */
		timer_set_alarm_value(group, timer, timer_interval_sec * TIMER_SCALE);
		timer_enable_intr(group, timer);

		// example_timer_info_t *timer_info = calloc(1, sizeof(example_timer_info_t));
		// timer_info->timer_group = group;
		// timer_info->timer_idx = timer;
		// timer_info->auto_reload = auto_reload;
		// timer_info->alarm_interval = timer_interval_sec;
		timer_isr_callback_add(group, timer, perform_wifi_reset, NULL /*timer_info*/, 0);

		timer_initialized = true;
	}

	timer_start(group, timer);
}
#endif