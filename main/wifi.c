#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include <esp_netif.h>
#include "esp_system.h"
#include <esp_wifi.h>

#include "wifi_manager.h"
#include "wifi.h"

const static char http_location_hdr[] = "Location";
const static char http_content_length_hdr[] = "Content-Length";
const static char http_content_type_hdr[] = "Content-Type";
const static char http_content_type_html[] = "text/html";
const static char http_content_type_js[] = "text/javascript";
const static char http_content_type_css[] = "text/css";
const static char http_content_type_json[] = "application/json";
const static char http_cache_control_hdr[] = "Cache-Control";
const static char http_cache_control_no_cache[] = "no-store, no-cache, must-revalidate, max-age=0";
const static char http_cache_control_cache[] = "public, max-age=31536000";
const static char http_pragma_hdr[] = "Pragma";
const static char http_pragma_no_cache[] = "no-cache";

void bm_update_wifi_ssid(void)
{
    uint64_t chipid;
    esp_read_mac((uint8_t *)&chipid, ESP_MAC_WIFI_SOFTAP);
    snprintf((char *)wifi_settings.ap_ssid, sizeof(wifi_settings.ap_ssid) - 1, "Blackmagic_%X", (uint32_t)chipid);
}

void bm_update_wifi_ps(void)
{
    wifi_settings.sta_power_save = WIFI_PS_MAX_MODEM;
}

// const wifi_sta_config_t *bm_ap_config(void)
// {

//     static wifi_sta_config_t sta_config = {};
//     uint64_t chipid;
//     esp_read_mac((uint8_t *)&chipid, ESP_MAC_WIFI_SOFTAP);
//     snprintf((char *)sta_config.ssid, sizeof(sta_config.ssid) - 1, "Blackmagic_%X", (uint32_t)chipid);
//     sta_config.channel = 11;

//     return &sta_config;
// }

CgiStatus cgiApJson(HttpdConnData *connData)
{
    /* if we can get the mutex, write the last version of the AP list */
    if (wifi_manager_lock_json_buffer((TickType_t)10))
    {
        httpdStartResponse(connData, 200);
        httpdHeader(connData, http_content_type_hdr, http_content_type_json);
        httpdHeader(connData, http_cache_control_hdr, http_cache_control_no_cache);
        httpdHeader(connData, http_pragma_hdr, http_pragma_no_cache);
        char *ap_buf = wifi_manager_get_ap_list_json();

        size_t content_length = strlen(ap_buf);
        char content_length_str[16];
        snprintf(content_length_str, sizeof(content_length_str) - 1, "%d", content_length);
        httpdHeader(connData, http_content_length_hdr, content_length_str);

        httpdEndHeaders(connData);

        httpdSend(connData, ap_buf, content_length);
        wifi_manager_unlock_json_buffer();
    }
    else
    {
        httpdStartResponse(connData, 503);
        ESP_LOGE(__func__, "http_server_netconn_serve: GET /ap.json failed to obtain mutex");
    }

    /* request a wifi scan */
    wifi_manager_scan_async();
    return HTTPD_CGI_DONE;
}

static CgiStatus cgiConnectJsonAdd(HttpdConnData *connData)
{
    char ssid[33] = {};
    char password[65] = {};

    ESP_LOGI(__func__, "http_server_netconn_serve: POST /connect.json");
    if (!httpdGetHeader(connData, "X-Custom-ssid", ssid, sizeof(ssid)) ||
        !httpdGetHeader(connData, "X-Custom-pwd", password, sizeof(password)))
    {
        httpdStartResponse(connData, 400);
        httpdHeader(connData, http_content_type_hdr, http_content_type_json);
        httpdHeader(connData, http_cache_control_hdr, http_cache_control_no_cache);
        httpdHeader(connData, http_pragma_hdr, http_pragma_no_cache);
        const char *error = "{\"error\": \"missing ssid or password\"}";
        httpdSend(connData, error, strlen(error));
        ESP_LOGE(__func__, "http_server_netconn_serve: missing SSID or password");
        return HTTPD_CGI_DONE;
    }

    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);

    wifi_config_t *config = wifi_manager_get_wifi_sta_config();
    memset(config, 0x00, sizeof(wifi_config_t));
    memcpy(config->sta.ssid, ssid, ssid_len);
    memcpy(config->sta.password, password, password_len);
    ESP_LOGI(__func__, "ssid: %s, password: %s", ssid, password);
    ESP_LOGD(__func__, "http_server_post_handler: wifi_manager_connect_async() call");
    wifi_manager_connect_async();

    ESP_LOGD(__func__,
             "POST /connect.json: SSID:%s, PWD:%s - connect to WiFi",
             ssid,
             password);
    ESP_LOGI(__func__, "POST /connect.json: SSID:%s, PWD: ******** - connect to WiFi", ssid);

    httpdStartResponse(connData, 200);
    httpdHeader(connData, http_content_type_hdr, http_content_type_json);
    httpdHeader(connData, http_cache_control_hdr, http_cache_control_no_cache);
    httpdHeader(connData, http_pragma_hdr, http_pragma_no_cache);
    httpdSend(connData, "{}", 2);

    return HTTPD_CGI_DONE;
}

static CgiStatus cgiConnectJsonRemove(HttpdConnData *connData)
{
    wifi_manager_disconnect_async();

    httpdStartResponse(connData, 200);
    httpdHeader(connData, http_content_type_hdr, http_content_type_json);
    httpdHeader(connData, http_cache_control_hdr, http_cache_control_no_cache);
    httpdHeader(connData, http_pragma_hdr, http_pragma_no_cache);
    httpdSend(connData, "{}", 2);
    return HTTPD_CGI_DONE;
}

CgiStatus cgiConnectJson(HttpdConnData *connData)
{
    switch (connData->requestType)
    {
    case HTTPD_METHOD_POST:
        return cgiConnectJsonAdd(connData);
    case HTTPD_METHOD_DELETE:
        return cgiConnectJsonRemove(connData);
    default:
        return HTTPD_CGI_NOTFOUND;
    }
}

CgiStatus cgiStatusJson(HttpdConnData *connData)
{
    if (wifi_manager_lock_json_buffer((TickType_t)10))
    {
        char *buff = wifi_manager_get_ip_info_json();
        if (buff)
        {
            httpdStartResponse(connData, 200);
            httpdHeader(connData, http_content_type_hdr, http_content_type_json);
            httpdHeader(connData, http_cache_control_hdr, http_cache_control_no_cache);
            httpdHeader(connData, http_pragma_hdr, http_pragma_no_cache);

            size_t content_length = strlen(buff);
            char content_length_str[16];
            snprintf(content_length_str, sizeof(content_length_str) - 1, "%d", content_length);
            httpdHeader(connData, http_content_length_hdr, content_length_str);

            httpdEndHeaders(connData);

            httpdSend(connData, buff, content_length);
            wifi_manager_unlock_json_buffer();
        }
        else
        {
            httpdStartResponse(connData, 503);
        }
    }
    else
    {
        httpdStartResponse(connData, 503);
        ESP_LOGE(__func__, "http_server_netconn_serve: GET /status.json failed to obtain mutex");
    }
    return HTTPD_CGI_DONE;
}