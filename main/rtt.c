#include <stdbool.h>
#include <stdint.h>

#include "CBUF.h"
#include "http.h"
#include "rtt_if.h"

static bool rtt_initialized;

static struct {
	volatile uint8_t m_get_idx;
	volatile uint8_t m_put_idx;
	uint8_t m_entry[256];
} rtt_msg_queue;

/* host: initialisation */
int rtt_if_init(void)
{
	if (rtt_initialized) {
		return 0;
	}
	CBUF_Init(rtt_msg_queue);
	return 0;
}

/* host: teardown */
int rtt_if_exit(void)
{
	if (!rtt_initialized) {
		return 0;
	}
	rtt_initialized = false;
	return 0;
}

/* target to host: write len bytes from the buffer starting at buf. return number bytes written */
uint32_t rtt_write(const char *buf, uint32_t len)
{
	http_term_broadcast_rtt((uint8_t *)buf, len);
	return len;
}

/* host to target: read one character, non-blocking. return character, -1 if no character */
int32_t rtt_getchar(void)
{
	if (CBUF_IsEmpty(rtt_msg_queue)) {
		return -1;
	}
	return CBUF_Pop(rtt_msg_queue);
}

/* host to target: true if no characters available for reading */
bool rtt_nodata(void)
{
	return CBUF_IsEmpty(rtt_msg_queue);
}

void IRAM_ATTR rtt_append_data(const uint8_t *data, int len)
{
	while ((len-- > 0) && !CBUF_IsFull(rtt_msg_queue)) {
		CBUF_Push(rtt_msg_queue, *data++);
	}
}
