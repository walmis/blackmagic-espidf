#include "exception.h"
#include <stdlib.h>
#include "general.h"
#include "FreeRTOS.h"
#include "task.h"
#include "esp_log.h"

struct exception** get_innermost_exception() {
    void** ptr = pvTaskGetThreadLocalStoragePointer(NULL, 0);
	//ESP_LOGI("EX", "exceptiongot ptr %p %p %p", ptr, ptr[0], ptr[1]);
    assert(ptr);
    return &ptr[1];
}

void raise_exception(uint32_t type, const char *msg)
{
	struct exception *e;
	ESP_LOGW("EX", "Exception: %s\n", msg);
	for (e = innermost_exception; e; e = e->outer) {
		if (e->mask & type) {
			e->type = type;
			e->msg = msg;
			innermost_exception = e->outer;
			longjmp(e->jmpbuf, type);
		}
	}
	abort();
}