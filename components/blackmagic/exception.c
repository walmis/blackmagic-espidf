#include "exception.h"
#include <stdlib.h>
#include "general.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"

void raise_exception(uint32_t type, const char *msg)
{
	struct exception *e;
	ESP_LOGW("EX", "Exception: %s\n", msg);
	for (e = innermost_exception; e; e = e->outer) {

		if (e->mask & type) {
			e->type = type;
			e->msg = msg;
			innermost_exception = e->outer;
#if 0
			for(int i = 0; i < sizeof(e->jmpbuf)/4; i++) {
				ESP_LOGI("JBUF", "%d:%08X", i, ((uint32_t*)e->jmpbuf)[i]);
			}
#endif
			longjmp(e->jmpbuf, type);
		}
	}
	ESP_LOGW("EX", "Unhandled exception %s", msg);

	abort();
}