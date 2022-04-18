#ifndef __BLACKMAGIC_WIFI_H__
#define __BLACKMAGIC_WIFI_H__

#include <libesphttpd/httpd.h>

void bm_update_wifi_ssid(void);
void bm_update_wifi_ps(void);

CgiStatus cgiApJson(HttpdConnData *connData);
CgiStatus cgiConnectJson(HttpdConnData *connData);
CgiStatus cgiStatusJson(HttpdConnData *connData);

#endif /* __BLACKMAGIC_WIFI_H__ */