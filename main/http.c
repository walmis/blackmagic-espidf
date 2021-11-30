#include <string.h>
#include <stdio.h>
#define ICACHE_FLASH_ATTR
// typedef uint8_t uint8;

#include <libesphttpd/httpd.h>
#include <libesphttpd/cgiwifi.h>
#include <libesphttpd/cgiflash.h>
#include <libesphttpd/auth.h>
#include <libesphttpd/captdns.h>
#include <libesphttpd/cgiwebsocket.h>
#include <libesphttpd/httpd-espfs.h>
#include <libesphttpd/httpd-freertos.h>
#include <libesphttpd/cgiredirect.h>
#include "libespfs/espfs.h"
#include "driver/uart.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "platform.h"
#include "hashmap.h"

extern const uint8_t espfs_bin[];
extern const size_t espfs_bin_len;
extern void platform_set_baud(uint32_t);

static HttpdFreertosInstance instance;

static void on_term_recv(Websock *ws, char *data, int len, int flags)
{
    uart_write_bytes(TARGET_UART_IDX, data, len);
}

static void on_term_connect(Websock *ws)
{
    ws->recvCb = on_term_recv;

    // cgiWebsocketSend(ws, "Hi, Websocket!", 14, WEBSOCK_FLAG_NONE);
}
static void on_debug_recv(Websock *ws, char *data, int len, int flags)
{
}
static void on_debug_connect(Websock *ws)
{
    ws->recvCb = on_debug_recv;

    // cgiWebsocketSend(&instance.httpdInstance, ws, "Debug connected\r\n", 17, WEBSOCK_FLAG_NONE);
}

void uart_send_break();

CgiStatus cgi_uart_break(HttpdConnData *connData)
{
    // int len;
    // char buff[64];

    if (connData->isConnectionClosed)
    {
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

    if (connData->isConnectionClosed)
    {
        // Connection aborted. Clean up.
        return HTTPD_CGI_DONE;
    }

    len = httpdFindArg(connData->getArgs, "set", buff, sizeof(buff));
    if (len > 0)
    {
        int baud = atoi(buff);
        // printf("baud %d\n", baud);
        if (baud)
        {
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
extern uint32_t uart_errors;
extern uint32_t uart_queue_full_cnt;
extern uint32_t uart_rx_count;
extern uint32_t uart_tx_count;

static int task_status_cmp(const void *a, const void *b)
{
    TaskStatus_t *ta = (TaskStatus_t *)a;
    TaskStatus_t *tb = (TaskStatus_t *)b;
    return ta->xTaskNumber - tb->xTaskNumber;
}

static const char *const task_state_name[] = {
    "eRunning",   /* A task is querying the state of itself, so must be running. */
    "eReady",     /* The task being queried is in a read or pending ready list. */
    "eBlocked",   /* The task being queried is in the Blocked state. */
    "eSuspended", /* The task being queried is in the Suspended state, or is in the Blocked state with an infinite time out. */
    "eDeleted",   /* The task being queried has been deleted, but its TCB has not yet been freed. */
    "eInvalid"    /* Used as an 'invalid state' value. */
};

CgiStatus cgi_status(HttpdConnData *connData)
{
    int len;
    char buff[256];

    if (connData->isConnectionClosed)
    {
        // Connection aborted. Clean up.
        return HTTPD_CGI_DONE;
    }

    static hashmap *task_times;
    if (!task_times)
    {
        task_times = hashmap_new();
    }

    uint32_t baud = 0;
    uart_get_baudrate(0, &baud);

    len = snprintf(buff, sizeof(buff), "free_heap: %u,\n"
                                       "uptime: %d ms\n"
                                       "baud_rate: %d,\n"
                                       "uart_overruns: %d\n"
                                       "uart_errors: %d\n"
                                       "uart_rx_full_cnt: %d\n"
                                       "uart_rx_count: %d\n"
                                       "uart_tx_count: %d\n"
                                       "tasks:\n",
                   esp_get_free_heap_size(), xTaskGetTickCount() * portTICK_PERIOD_MS,
                   baud, uart_overrun_cnt, uart_errors,
                   uart_queue_full_cnt, uart_rx_count, uart_tx_count);

    httpdStartResponse(connData, 200);
    httpdHeader(connData, "Cache-Control", "no-store, must-revalidate, no-cache, max-age=0");
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdHeader(connData, "Refresh", "1");

    httpdEndHeaders(connData);

    httpdSend(connData, buff, len);

    int uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t *pxTaskStatusArray = malloc(uxArraySize * sizeof(TaskStatus_t));
    uint32_t total_runtime;
    uint32_t tmp;
    static uint32_t last_total_runtime;
    if (pxTaskStatusArray != NULL)
    {
        /* Generate the (binary) data. */
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &total_runtime);
        tmp = total_runtime;
        total_runtime = total_runtime - last_total_runtime;
        last_total_runtime = tmp;
        total_runtime /= 100;
        if (total_runtime == 0)
            total_runtime = 1;

        qsort(pxTaskStatusArray, uxArraySize, sizeof(TaskStatus_t), task_status_cmp);

        for (int i = 0; i < uxArraySize; i++)
        {
            TaskStatus_t *tsk = &pxTaskStatusArray[i];

            uint32_t last_task_time = tsk->ulRunTimeCounter;
            hashmap_get(task_times, tsk->xTaskNumber, &last_task_time);
            hashmap_set(task_times, tsk->xTaskNumber, tsk->ulRunTimeCounter);
            tsk->ulRunTimeCounter -= last_task_time;

            // len = snprintf(buff, sizeof(buff), "\tid: %4u, name: %16s, prio: %3u, state: %8s, stack_hwm: %4u, cpu: %d%%, pc: 0x%08x [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
            //     tsk->xTaskNumber, tsk->pcTaskName, tsk->uxCurrentPriority, task_state_name[tsk->eCurrentState], tsk->usStackHighWaterMark, tsk->ulRunTimeCounter / total_runtime,
            //     (*((uint32_t **)tsk->xHandle))[0],
            //     (*((uint32_t **)tsk->xHandle))[1],
            //     (*((uint32_t **)tsk->xHandle))[2],
            //     (*((uint32_t **)tsk->xHandle))[3],
            //     (*((uint32_t **)tsk->xHandle))[4],
            //     (*((uint32_t **)tsk->xHandle))[5],
            //     (*((uint32_t **)tsk->xHandle))[6],
            //     (*((uint32_t **)tsk->xHandle))[7]);
            len = snprintf(buff, sizeof(buff), "\tid: %4u, name: %16s, prio: %3u, state: %8s, stack_hwm: %4u, cpu: %d%%, pc: 0x%08x\n",
                           tsk->xTaskNumber, tsk->pcTaskName, tsk->uxCurrentPriority, task_state_name[tsk->eCurrentState], tsk->usStackHighWaterMark, tsk->ulRunTimeCounter / total_runtime,
                           (*((uint32_t **)tsk->xHandle))[1]);

            httpdSend(connData, buff, len);
        }
    }

    free(pxTaskStatusArray);

    len = snprintf(buff, sizeof(buff), "\n");
    httpdSend(connData, buff, len);

    return HTTPD_CGI_DONE;
}

#define FLASH_SIZE 2
#define LIBESPHTTPD_OTA_TAGNAME "blackmagic"

CgiUploadFlashDef uploadParams = {
    .type = CGIFLASH_TYPE_FW,
    .fw1Pos = 0x2000,
    .fw2Pos = ((FLASH_SIZE * 1024 * 1024)) + 0x2000,
    .fwSize = ((FLASH_SIZE * 1024 * 1024)) - 0x2000,
    .tagName = LIBESPHTTPD_OTA_TAGNAME};

HttpdBuiltInUrl builtInUrls[] = {
    //  {"*", cgiRedirectApClientToHostname, "esp8266.nonet"},
    {"/", cgiRedirect, (const void *)"/index.html", 0},
    //  {"/led.tpl", cgiEspFsTemplate, tplLed},
    //  {"/index.tpl", cgiEspFsTemplate, tplCounter},
    //  {"/led.cgi", cgiLed, NULL},
    //#ifndef ESP32
    {"/flash/", cgiRedirect, (const void *)"/flash/index.html", 0},
    //{"/flash/next", cgiGetFirmwareNext, &uploadParams, 0},
    {"/flash/upload", cgiUploadFirmware, &uploadParams, 0},
    {"/flash/reboot", cgiRebootFirmware, NULL, 0},
    //#endif
    //  //Routines to make the /wifi URL and everything beneath it work.
    //
    ////Enable the line below to protect the WiFi configuration with an username/password combo.
    ////  {"/wifi/*", authBasic, myPassFn},
    //
    //  {"/wifi", cgiRedirect, "/wifi/wifi.tpl"},
    //  {"/wifi/", cgiRedirect, "/wifi/wifi.tpl"},
    //  {"/wifi/wifiscan.cgi", cgiWiFiScan, NULL},
    //  {"/wifi/wifi.tpl", cgiEspFsTemplate, tplWlan},
    //  {"/wifi/connect.cgi", cgiWiFiConnect, NULL},
    //  {"/wifi/connstatus.cgi", cgiWiFiConnStatus, NULL},
    {"/uart/baud", cgi_baud, NULL, 0},
    {"/uart/break", cgi_uart_break, NULL, 0},
    {"/status", cgi_status, NULL, 0},
    //
    {"/terminal", cgiWebsocket, (const void *)on_term_connect, 0},
    {"/debugws", cgiWebsocket, (const void *)on_debug_connect, 0},
    //  {"/websocket/echo.cgi", cgiWebsocket, myEchoWebsocketConnect},
    //
    //  {"/test", cgiRedirect, "/test/index.html"},
    //  {"/test/", cgiRedirect, "/test/index.html"},
    //  {"/test/test.cgi", cgiTestbed, NULL},

    {"*", cgiEspFsHook, NULL, 0}, // Catch-all cgi function for the filesystem
    {NULL, NULL, NULL, 0}};

void http_term_broadcast_data(uint8_t *data, size_t len)
{
    cgiWebsockBroadcast(&instance.httpdInstance, "/terminal", (char *)data, len, WEBSOCK_FLAG_BIN);
}

void http_debug_putc(char c, int flush)
{
    static uint8_t buf[256];
    static int bufsize = 0;

    buf[bufsize++] = c;
    if (flush || (bufsize == sizeof(buf)))
    {
        cgiWebsockBroadcast(&instance.httpdInstance, "/debugws", (char *)buf, bufsize, WEBSOCK_FLAG_BIN);

        bufsize = 0;
    }
}

#define maxConnections 4

void httpd_start()
{
    static espfs_config_t espfs_config = {
        .addr = espfs_bin,
    };
    espfs_fs_t *fs = espfs_init(&espfs_config);
    assert(fs != NULL);
    httpdRegisterEspfs(fs);

    static char connectionMemory[sizeof(RtosConnType) * maxConnections];
    httpdFreertosInit(&instance,
                      builtInUrls,
                      80,
                      connectionMemory, maxConnections,
                      HTTPD_FLAG_NONE);
    httpdFreertosStart(&instance);
    // httpdInit(builtInUrls, 80);
}
