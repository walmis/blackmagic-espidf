#ifndef __BLACKMAGIC_WIFI_H__
#define __BLACKMAGIC_WIFI_H__

#include <esp_http_server.h>

void bm_update_wifi_ssid(void);
void bm_update_wifi_ps(void);

esp_err_t cgi_ap_json(httpd_req_t *req);
esp_err_t cgi_connect_json(httpd_req_t *req);
esp_err_t cgi_status_json(httpd_req_t *req);

#endif /* __BLACKMAGIC_WIFI_H__ */