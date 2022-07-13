#include <string.h>
#include <stdio.h>
// typedef uint8_t uint8;

#include <libesphttpd/httpd.h>
#include <libesphttpd/cgiwifi.h>
#include <libesphttpd/cgiflash.h>
#include <libesphttpd/auth.h>
#include <libesphttpd/captdns.h>
#include <libesphttpd/cgiwebsocket.h>
#include <libesphttpd/httpd-frogfs.h>
#include <libesphttpd/httpd-freertos.h>
#include <libesphttpd/cgiredirect.h>
#include "frogfs/frogfs.h"
#include "driver/uart.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "platform.h"
#include "hashmap.h"
#include "wifi.h"

#include "esp_attr.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "hal/interrupt_controller_hal.h"

// #ifdef ICACHE_FLASH_ATTR
// #undef ICACHE_FLASH_ATTR
// #endif
// #define ICACHE_FLASH_ATTR IRAM_ATTR

extern const uint8_t frogfs_bin[];
extern const size_t frogfs_bin_len;
extern void platform_set_baud(uint32_t);

static HttpdFreertosInstance instance;

void uart_write_all(const uint8_t *data, int len);
void rtt_append_data(const char *data, int len);

static void on_rtt_recv(Websock *ws, char *data, int len, int flags)
{
	rtt_append_data(data, len);
}

static void on_rtt_connect(Websock *ws)
{
	ws->recvCb = on_rtt_recv;
}

static void on_term_recv(Websock *ws, char *data, int len, int flags)
{
	uart_write_all((const uint8_t *)data, len);
}

static void on_term_connect(Websock *ws)
{
	ws->recvCb = on_term_recv;
}

static void on_debug_recv(Websock *ws, char *data, int len, int flags)
{
}

static void on_debug_connect(Websock *ws)
{
	ws->recvCb = on_debug_recv;
}

void uart_send_break();

CgiStatus cgi_uart_break(HttpdConnData *connData)
{
	if (connData->isConnectionClosed) {
		// Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

	uart_send_break();

	httpdStartResponse(connData, 200);
	httpdHeader(connData, "Content-Type", "text/json");
	httpdEndHeaders(connData);

	httpdSend(connData, 0, 0);

	return HTTPD_CGI_DONE;
}

CgiStatus cgi_baud(HttpdConnData *connData)
{
	int len;
	char buff[64];

	if (connData->isConnectionClosed) {
		// Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

	len = httpdFindArg(connData->getArgs, "set", buff, sizeof(buff));
	if (len > 0) {
		int baud = atoi(buff);
		// printf("baud %d\n", baud);
		if (baud) {
			platform_set_baud(baud);
		}
	}

	uint32_t baud = 0;
	uart_get_baudrate(0, &baud);

	len = snprintf(buff, sizeof(buff), "{\"baudrate\": %u }", baud);

	httpdStartResponse(connData, 200);
	httpdHeader(connData, "Content-Type", "text/json");
	httpdEndHeaders(connData);

	httpdSend(connData, buff, len);

	return HTTPD_CGI_DONE;
}

extern uint32_t uart_overrun_cnt;
extern uint32_t uart_frame_error_cnt;
extern uint32_t uart_queue_full_cnt;
extern uint32_t uart_rx_count;
extern uint32_t uart_tx_count;
extern uint32_t uart_irq_count;

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
static int task_status_cmp(const void *a, const void *b)
{
	TaskStatus_t *ta = (TaskStatus_t *)a;
	TaskStatus_t *tb = (TaskStatus_t *)b;
	return ta->xTaskNumber - tb->xTaskNumber;
}
#endif

static const char *const task_state_name[] = {
	"eRunning", /* A task is querying the state of itself, so must be running. */
	"eReady",   /* The task being queried is in a read or pending ready list. */
	"eBlocked", /* The task being queried is in the Blocked state. */
	"eSuspended", /* The task being queried is in the Suspended state, or is in the Blocked state with an infinite time out. */
	"eDeleted", /* The task being queried has been deleted, but its TCB has not yet been freed. */
	"eInvalid"  /* Used as an 'invalid state' value. */
};

#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
static const char *core_str(int core_id)
{
	switch (core_id) {
	case 0:
		return "0";
	case 1:
		return "1";
	case 2147483647:
		return "ANY";
	case -1:
		return "any";
	default:
		return "???";
	}
}
#endif

int32_t adc_read_system_voltage(void);

static void cgi_status_header(HttpdConnData *connData)
{
	int len;
	char buff[512];

	httpdStartResponse(connData, 200);
	httpdHeader(connData, "Cache-Control", "no-store, must-revalidate, no-cache, max-age=0");
	httpdHeader(connData, "Content-Type", "text/plain");
	httpdHeader(connData, "Refresh", "1");

	httpdEndHeaders(connData);

	uint32_t baud0 = 0;
	uint32_t baud1 = 0;
	uint32_t baud2 = 0;
	uart_get_baudrate(0, &baud0);
	uart_get_baudrate(1, &baud1);
	extern int swo_active;
	if (swo_active) {
		uart_get_baudrate(2, &baud2);
	}

	const esp_partition_t *current_partition = esp_ota_get_running_partition();
	const esp_partition_t *next_partition = esp_ota_get_next_update_partition(current_partition);
	esp_ota_img_states_t current_partition_state = ESP_OTA_IMG_UNDEFINED;
	esp_ota_img_states_t next_partition_state = ESP_OTA_IMG_UNDEFINED;
	extern uint32_t uart_rx_data_relay;
	esp_ota_get_state_partition(current_partition, &current_partition_state);
	esp_ota_get_state_partition(next_partition, &next_partition_state);
	const char *update_status = "";
	if (next_partition_state != ESP_OTA_IMG_VALID) {
		update_status = "UPDATE FAILED\n";
	}
	len = snprintf(buff, sizeof(buff) - 1,
		       "free_heap: %u,\n"
		       "uptime: %d ms\n"
		       "debug_baud_rate: %d\n"
		       "target_baud_rate: %d,\n"
		       "swo_baud_rate: %d,\n"
		       "src voltage: %d mV\n"
		       "uart_overruns: %d\n"
		       "uart_frame_errors: %d\n"
		       "uart_queue_full_cnt: %d\n"
		       "uart_rx_count: %d\n"
		       "uart_tx_count: %d\n"
		       "uart_irq_count: %d\n"
		       "uart_rx_data_relay: %d\n"
		       "current partition: 0x%08x %d\n"
		       "next partition: 0x%08x %d\n"
		       "%s"
		       "tasks:\n",
		       esp_get_free_heap_size(), xTaskGetTickCount() * portTICK_PERIOD_MS, baud0, baud1, baud2,
		       adc_read_system_voltage(), uart_overrun_cnt, uart_frame_error_cnt, uart_queue_full_cnt,
		       uart_rx_count, uart_tx_count, uart_irq_count, uart_rx_data_relay, current_partition->address,
		       current_partition_state, next_partition->address, next_partition_state, update_status);
	httpdSend(connData, buff, len);
}

struct cgi_status_state {
	TaskStatus_t *pxTaskStatusArray;
	int i;
	int uxArraySize;
	bool printInterrupts;
	uint32_t totalRuntime;
};

CgiStatus cgi_status(HttpdConnData *connData)
{
	static hashmap *task_times;
	if (!task_times) {
		task_times = hashmap_new();
	}

	if (connData->isConnectionClosed) {
		// Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

	struct cgi_status_state *state = (struct cgi_status_state *)connData->cgiData;
	if (state == NULL) {
		cgi_status_header(connData);
		struct cgi_status_state *state = malloc(sizeof(struct cgi_status_state));
		if (!state) {
			return HTTPD_CGI_DONE;
		}

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
		int uxArraySize = uxTaskGetNumberOfTasks();
		state->pxTaskStatusArray = malloc(uxArraySize * sizeof(TaskStatus_t));
		state->uxArraySize = uxTaskGetSystemState(state->pxTaskStatusArray, uxArraySize, &state->totalRuntime);
		qsort(state->pxTaskStatusArray, state->uxArraySize, sizeof(TaskStatus_t), task_status_cmp);
#else
		state->pxTaskStatusArray = NULL;
		state->uxArraySize = 0;
#endif
		state->i = 0;

		uint32_t tmp;
		static uint32_t lastTotalRuntime;
		/* Generate the (binary) data. */
		tmp = state->totalRuntime;
		state->totalRuntime = state->totalRuntime - lastTotalRuntime;
		lastTotalRuntime = tmp;
		state->totalRuntime /= 100;
		if (state->totalRuntime == 0)
			state->totalRuntime = 1;

		connData->cgiData = state;
		state->printInterrupts = true;
		return HTTPD_CGI_MORE;
	}

	if (state->printInterrupts) {
		char buff[512];
		int len = 0;

		int cpu = 0;
		int irq;
		for (cpu = 0; cpu < portNUM_PROCESSORS; cpu++) {
			len += snprintf(buff + len, sizeof(buff) - len, "CPU %d:", cpu);
			for (irq = 0; irq < 32; irq++) {
				len += snprintf(buff + len, sizeof(buff) - len, " %d",
						interrupt_controller_hal_has_handler(irq, cpu));
			}
			len += snprintf(buff + len, sizeof(buff) - len, "\n");
		}

		httpdSend(connData, buff, len);
		state->printInterrupts = false;
		return HTTPD_CGI_MORE;
	}

	if (state->pxTaskStatusArray != NULL && state->i < state->uxArraySize) {
		int len;
		char buff[256];
		TaskStatus_t *tsk = &state->pxTaskStatusArray[state->i];

		uint32_t last_task_time = tsk->ulRunTimeCounter;
		hashmap_get(task_times, tsk->xTaskNumber, &last_task_time);
		hashmap_set(task_times, tsk->xTaskNumber, tsk->ulRunTimeCounter);
		tsk->ulRunTimeCounter -= last_task_time;

		len = snprintf(buff, sizeof(buff),
			       "\tid: %3u, name: %16s, prio: %3u, state: %10s, stack_hwm: %5u, "
#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
			       "core: %3s, "
#endif
			       "cpu: %3d%%, pc: 0x%08x\n",
			       tsk->xTaskNumber, tsk->pcTaskName, tsk->uxCurrentPriority,
			       task_state_name[tsk->eCurrentState], tsk->usStackHighWaterMark,
#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
			       core_str((int)tsk->xCoreID),
#endif
			       tsk->ulRunTimeCounter / state->totalRuntime, (*((uint32_t **)tsk->xHandle))[1]);

		httpdSend(connData, buff, len);
		state->i++;
		return HTTPD_CGI_MORE;
	}

	if (state->pxTaskStatusArray != NULL) {
		free(state->pxTaskStatusArray);
	}
	free(state);
	connData->cgiData = NULL;

	httpdSend(connData, "\n", 1);

	return HTTPD_CGI_DONE;
}

#define FLASH_SIZE 2
#define LIBESPHTTPD_OTA_TAGNAME "blackmagic"

CgiUploadFlashDef uploadParams = { .type = CGIFLASH_TYPE_FW,
				   .fw1Pos = 0x2000,
				   .fw2Pos = ((FLASH_SIZE * 1024 * 1024)) + 0x2000,
				   .fwSize = ((FLASH_SIZE * 1024 * 1024)) - 0x2000,
				   .tagName = LIBESPHTTPD_OTA_TAGNAME };

HttpdBuiltInUrl builtInUrls[] = {
	//  {"*", cgiRedirectApClientToHostname, "esp8266.nonet"},
	{ "/", cgiRedirect, (const void *)"/index.html", 0 },
	//  {"/led.tpl", cgifrogfsTemplate, tplLed},
	//  {"/index.tpl", cgifrogfsTemplate, tplCounter},
	//  {"/led.cgi", cgiLed, NULL},
	{ "/flash/", cgiRedirect, (const void *)"/flash/index.html", 0 },
	{ "/flash", cgiRedirect, (const void *)"/flash/index.html", 0 },
	{ "/flash/next", cgiGetFirmwareNext, &uploadParams, 0 },
	{ "/flash/upload", cgiUploadFirmware, &uploadParams, 0 },
	{ "/flash/reboot", cgiRebootFirmware, NULL, 0 },

	//  //Routines to make the /wifi URL and everything beneath it work.
	//
	////Enable the line below to protect the WiFi configuration with an username/password combo.
	////  {"/wifi/*", authBasic, myPassFn},
	//
	//  {"/wifi", cgiRedirect, "/wifi/wifi.tpl"},
	//  {"/wifi/", cgiRedirect, "/wifi/wifi.tpl"},
	//  {"/wifi/wifiscan.cgi", cgiWiFiScan, NULL},
	//  {"/wifi/wifi.tpl", cgifrogfsTemplate, tplWlan},
	//  {"/wifi/connect.cgi", cgiWiFiConnect, NULL},
	//  {"/wifi/connstatus.cgi", cgiWiFiConnStatus, NULL},
	{ "/uart/baud", cgi_baud, NULL, 0 },
	{ "/uart/break", cgi_uart_break, NULL, 0 },
	{ "/status", cgi_status, NULL, 0 },
	//
	{ "/terminal", cgiWebsocket, (const void *)on_term_connect, 0 },
	{ "/debugws", cgiWebsocket, (const void *)on_debug_connect, 0 },
	{ "/rtt", cgiWebsocket, (const void *)on_rtt_connect, 0 },

	// Wifi Manager
	{ "/ap.json", cgiApJson, 0, 0 },
	{ "/connect.json", cgiConnectJson, 0, 0 },
	{ "/status.json", cgiStatusJson, 0, 0 },

	//  {"/websocket/echo.cgi", cgiWebsocket, myEchoWebsocketConnect},
	//
	//  {"/test", cgiRedirect, "/test/index.html"},
	//  {"/test/", cgiRedirect, "/test/index.html"},
	//  {"/test/test.cgi", cgiTestbed, NULL},

	{ "*", cgiFrogFsHook, NULL, 0 }, // Catch-all cgi function for the filesystem
	{ NULL, NULL, NULL, 0 }
};

void ICACHE_FLASH_ATTR http_term_broadcast_data(uint8_t *data, size_t len)
{
	cgiWebsockBroadcast(&instance.httpdInstance, "/terminal", (char *)data, len, WEBSOCK_FLAG_BIN);
}

void ICACHE_FLASH_ATTR http_term_broadcast_rtt(char *data, size_t len)
{
	cgiWebsockBroadcast(&instance.httpdInstance, "/rtt", data, len, WEBSOCK_FLAG_BIN);
}

void ICACHE_FLASH_ATTR http_debug_putc(char c, int flush)
{
	static uint8_t buf[256];
	static int bufsize = 0;

	buf[bufsize++] = c;
	if (flush || (bufsize == sizeof(buf))) {
		cgiWebsockBroadcast(&instance.httpdInstance, "/debugws", (char *)buf, bufsize, WEBSOCK_FLAG_BIN);

		bufsize = 0;
	}
}

#define maxConnections 12

void httpd_start()
{
	static frogfs_config_t frogfs_config = {
		.addr = frogfs_bin,
	};
	frogfs_fs_t *fs = frogfs_init(&frogfs_config);
	assert(fs != NULL);
	httpdRegisterFrogFs(fs);

	static char connectionMemory[sizeof(RtosConnType) * maxConnections];
	httpdFreertosInit(&instance, builtInUrls, 80, connectionMemory, maxConnections, HTTPD_FLAG_NONE);
	httpdFreertosStart(&instance);
	// httpdInit(builtInUrls, 80);
}
