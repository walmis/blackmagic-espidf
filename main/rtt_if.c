#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <esp_log.h>
#include <esp_http_server.h>

#include "CBUF.h"
#include "http.h"
#include "rtt.h"
#include "rtt_if.h"

static bool rtt_initialized;

#define TAG "rtt"

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
	rtt_initialized = true;
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

esp_err_t cgi_rtt_status(httpd_req_t *req)
{
	char buff[256];
	char value_string[64];

	// `enable` may be 0 or nonzero
	httpd_req_get_url_query_str(req, buff, sizeof(buff));
	ESP_LOGI("rtt", "query string: %s", buff);
	if (ESP_OK == httpd_query_key_value(buff, "enable", value_string, sizeof(value_string))) {
		int enable = atoi(value_string);
		ESP_LOGI("rtt", "enable value: %d (%s)", enable, value_string);
		rtt_enabled = !!enable;
	}

	// `channel` must be either "auto" or a series of digits
	if (ESP_OK == httpd_query_key_value(buff, "channel", value_string, sizeof(value_string))) {
		// Disable all channels.
		for (uint32_t i = 0; i < MAX_RTT_CHAN; i++)
			rtt_channel[i].is_enabled = false;
		if (!strcasecmp(value_string, "auto")) {
			rtt_auto_channel = true;
		} else {
			char *chan_ptr = value_string;
			rtt_auto_channel = false;
			int i = 2;
			while (*chan_ptr) {
				int chan = strtoul(chan_ptr, &chan_ptr, 0);
				if ((chan >= 0) && (chan < MAX_RTT_CHAN))
					rtt_channel[chan].is_enabled = true;

				// Advance to the next digit in the string, e.g.
				// channel=1,2,3,4
				char c = *chan_ptr;
				while ((*chan_ptr) && !isdigit(c)) {
					chan_ptr++;
					c = *chan_ptr;
				}
				i += 1;
			}
		}
	}

	const char *format = "{"
						 "\"ident\":\"%s\","
						 "\"enabled\":%s,"
						 "\"found\":%s,"
						 "\"cbaddr\":%d,"
						 "\"min_poll_ms\":%d,"
						 "\"max_poll_ms\":%d,"
						 "\"max_poll_errs\":%d,"
						 "\"max_auto_channel\":%s,"
						 "\"max_flag_skip\":%s,"
						 "\"max_flag_block\":%s"
						 "}";
	int len = snprintf(buff, sizeof(buff), format, rtt_ident, rtt_enabled ? "true" : "false",
		rtt_found ? "true" : "false", rtt_cbaddr, rtt_min_poll_ms, rtt_max_poll_ms, rtt_max_poll_errs,
		rtt_auto_channel ? "true" : "false", rtt_flag_skip ? "true" : "false", rtt_flag_block ? "true" : "false");
	httpd_resp_set_type(req, "text/json");
	httpd_resp_send(req, buff, len);

	return ESP_OK;
}