#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include <esp_netif.h>
#include "esp_system.h"
#include <esp_wifi.h>

#include "wifi_manager.h"
#include "wifi.h"

const static char http_content_type_json[] = "application/json";
const static char http_cache_control_hdr[] = "Cache-Control";
const static char http_cache_control_no_cache[] = "no-store, no-cache, must-revalidate, max-age=0";
// const static char http_cache_control_cache[] = "public, max-age=31536000";
const static char http_pragma_hdr[] = "Pragma";
const static char http_pragma_no_cache[] = "no-cache";

void bm_update_wifi_ssid(void)
{
	uint64_t chipid;
	esp_read_mac((uint8_t *)&chipid, ESP_MAC_WIFI_SOFTAP);
	snprintf((char *)wifi_settings.ap_ssid, sizeof(wifi_settings.ap_ssid) - 1, "Farpatch_%" PRIX32, (uint32_t)chipid);
}

void bm_update_wifi_ps(void)
{
	// wifi_settings.sta_power_save = WIFI_PS_MAX_MODEM;
}

esp_err_t cgi_ap_json(httpd_req_t *req)
{
	/* if we can get the mutex, write the last version of the AP list */
	if (!wifi_manager_lock_json_buffer((TickType_t)10)) {
		ESP_LOGE(__func__, "http_server_netconn_serve: GET /ap.json failed to obtain mutex");
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to obtain mutex");
	}

	httpd_resp_set_type(req, http_content_type_json);
	httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_cache);
	httpd_resp_set_hdr(req, http_pragma_hdr, http_pragma_no_cache);

	char *ap_buf = wifi_manager_get_ap_list_json();

	httpd_resp_send(req, ap_buf, strlen(ap_buf));

	wifi_manager_unlock_json_buffer();

	/* request a wifi scan */
	wifi_manager_scan_async();
	return ESP_OK;
}

static esp_err_t cgi_connect_json_add(httpd_req_t *req)
{
	char ssid[33] = {};
	char password[65] = {};

	ESP_LOGI(__func__, "http_server_netconn_serve: POST /connect.json");
	if ((ESP_OK != httpd_req_get_hdr_value_str(req, "X-Custom-ssid", ssid, sizeof(ssid))) ||
		(ESP_OK != httpd_req_get_hdr_value_str(req, "X-Custom-pwd", password, sizeof(password)))) {
		const char *error = "{\"error\": \"missing ssid or password\"}";
		ESP_LOGE(__func__, "http_server_netconn_serve: missing SSID or password");

		httpd_resp_set_type(req, http_content_type_json);
		httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_cache);
		httpd_resp_set_hdr(req, http_pragma_hdr, http_pragma_no_cache);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
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

	ESP_LOGD(__func__, "POST /connect.json: SSID:%s, PWD:%s - connect to WiFi", ssid, password);
	ESP_LOGI(__func__, "POST /connect.json: SSID:%s, PWD: ******** - connect to WiFi", ssid);

	httpd_resp_set_type(req, http_content_type_json);
	httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_cache);
	httpd_resp_set_hdr(req, http_pragma_hdr, http_pragma_no_cache);
	httpd_resp_send(req, "{}", 2);

	return ESP_OK;
}

static esp_err_t cgi_connect_json_remove(httpd_req_t *req)
{
	wifi_manager_disconnect_async();
	httpd_resp_set_type(req, http_content_type_json);
	httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_cache);
	httpd_resp_set_hdr(req, http_pragma_hdr, http_pragma_no_cache);
	httpd_resp_send(req, "{}", 2);

	return ESP_OK;
}

esp_err_t cgi_connect_json(httpd_req_t *req)
{
	switch (req->method) {
	case HTTP_POST:
		return cgi_connect_json_add(req);
	case HTTP_DELETE:
		return cgi_connect_json_remove(req);
	default:
		return httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "request type not allowed");
	}
}

esp_err_t cgi_status_json(httpd_req_t *req)
{
	if (!wifi_manager_lock_json_buffer((TickType_t)10)) {
		ESP_LOGE(__func__, "http_server_netconn_serve: GET /status.json failed to obtain mutex");
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to obtain mutex");
	}
	char *buff = wifi_manager_get_ip_info_json();
	if (!buff) {
		ESP_LOGE(__func__, "http_server_netconn_serve: GET /status.json failed to obtain mutex");
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to obtain mutex");
	}
	httpd_resp_set_type(req, http_content_type_json);
	httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_cache);
	httpd_resp_set_hdr(req, http_pragma_hdr, http_pragma_no_cache);
	httpd_resp_send(req, buff, strlen(buff));
	wifi_manager_unlock_json_buffer();
	return ESP_OK;
}