#include <string.h>
#include <stdio.h>
#define  ICACHE_FLASH_ATTR
//typedef uint8_t uint8;

#include <libesphttpd/httpd.h>
#include <libesphttpd/cgiwifi.h>
#include <libesphttpd/cgiflash.h>
#include <libesphttpd/auth.h>
#include <libesphttpd/captdns.h>
#include <libesphttpd/cgiwebsocket.h>
#include <libesphttpd/httpd-espfs.h>
#include <libesphttpd/httpd-freertos.h>
#include <libesphttpd/cgiredirect.h>
#include "espfs.h"
#include "driver/uart.h"
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#include "platform.h"

extern char _binary_image_espfs_start[] ;
extern void platform_set_baud(uint32_t);

static HttpdFreertosInstance instance;

static void on_term_recv(Websock *ws, char *data, int len, int flags) {
#ifdef USE_GPIO2_UART
	  uart_write_bytes(1, data, len);
#else
	  uart_write_bytes(0, data, len);
#endif

}

static void on_term_connect(Websock *ws) {
  ws->recvCb = on_term_recv;

  //cgiWebsocketSend(ws, "Hi, Websocket!", 14, WEBSOCK_FLAG_NONE);
}

CgiStatus cgi_baud(HttpdConnData *connData) {
  int len;
  char buff[64];

  if (connData->isConnectionClosed) {
    //Connection aborted. Clean up.
    return HTTPD_CGI_DONE;
  }

  len=httpdFindArg(connData->getArgs, "set", buff, sizeof(buff));
  if (len>0) {
    int baud = atoi(buff);
    //printf("baud %d\n", baud);
    if(baud) {
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

#define FLASH_SIZE 2
#define LIBESPHTTPD_OTA_TAGNAME "blackmagic"

CgiUploadFlashDef uploadParams={
  .type=CGIFLASH_TYPE_FW,
  .fw1Pos=0x2000,
  .fw2Pos=((FLASH_SIZE*1024*1024))+0x2000,
  .fwSize=((FLASH_SIZE*1024*1024))-0x2000,
  .tagName=LIBESPHTTPD_OTA_TAGNAME
};

HttpdBuiltInUrl builtInUrls[]={
//  {"*", cgiRedirectApClientToHostname, "esp8266.nonet"},
  {"/", cgiRedirect, (const void*)"/index.html", 0},
//  {"/led.tpl", cgiEspFsTemplate, tplLed},
//  {"/index.tpl", cgiEspFsTemplate, tplCounter},
//  {"/led.cgi", cgiLed, NULL},
//#ifndef ESP32
  {"/flash/", cgiRedirect, (const void*)"/flash/index.html", 0},
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
//
  {"/terminal", cgiWebsocket, (const void*)on_term_connect, 0},
//  {"/websocket/echo.cgi", cgiWebsocket, myEchoWebsocketConnect},
//
//  {"/test", cgiRedirect, "/test/index.html"},
//  {"/test/", cgiRedirect, "/test/index.html"},
//  {"/test/test.cgi", cgiTestbed, NULL},

  {"*", cgiEspFsHook, NULL, 0}, //Catch-all cgi function for the filesystem
  {NULL, NULL, NULL, 0}
};

void http_term_broadcast_data(uint8_t* data, size_t len) {
  cgiWebsockBroadcast(&instance.httpdInstance, "/terminal", (char*)data, len, WEBSOCK_FLAG_NONE);
}


#define maxConnections 4

void httpd_start() {
  espFsInit((void*)(_binary_image_espfs_start));

  static char connectionMemory[sizeof(RtosConnType) * maxConnections];
  httpdFreertosInit(&instance,
                      builtInUrls,
                      80,
                      connectionMemory, maxConnections,
                      HTTPD_FLAG_NONE);
  httpdFreertosStart(&instance);
  //httpdInit(builtInUrls, 80);
}
