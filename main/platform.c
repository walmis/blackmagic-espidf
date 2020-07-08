menu "Blackmagic Configuration"

choice ESP_WIFI_MODE
    prompt "AP or STA"
    default ESP_WIFI_IS_SOFTAP
    help
        Whether the esp32 is softAP or station.

config ESP_WIFI_IS_SOFTAP
    bool "SoftAP"
config ESP_WIFI_IS_STATION
    bool "Station"
endchoice

config ESP_WIFI_MODE_AP
    bool
    default y if ESP_WIFI_IS_SOFTAP
    default n if ESP_WIFI_IS_STATION

config ESP_WIFI_SSID
    string "WiFi SSID"
    default "auto"
    help
	   SSID (network name) for ap and sta modes.
	   auto - generates name automatically when in ap mode using mac address for ex. blackmagic_27FCF5E

config ESP_WIFI_PASSWORD
    string "WiFi Password"
    default "helloworld"
    help
	   WiFi password (WPA or WPA2) for the example to use.
		
config MAX_STA_CONN
    int "Max STA conn"
    default 4
    help
	Max number of the STA connects to AP.

config TDI_GPIO
    int "TDI GPIO"
    default 13
    help
	TDI GPIO number
	
config TDO_GPIO
    int "TDO GPIO"
    default 14
    help
	TDO GPIO number
	
config TMS_SWDIO_GPIO
    int "SWDIO/TMS GPIO"
    default 0
    help
	TMS/SWDIO GPIO number
	
config TCK_SWCLK_GPIO
    int "SWCLK/TCK GPIO"
    default 2
    help
	TCK/SWDIO GPIO number		
		
config SRST_GPIO
    int "SRST GPIO"
    default 12
    help
	Reset GPIO Number

config TARGET_UART
    bool "Monitor target UART"
    default y
    help
        Uses the ESP8266 UART to pass through the target UART pins.

        Disable to debug blackmagic-espidf.

config BLACKMAGIC_HOSTNAME
    string "Hostname"
    default "blackmagic"
    help
        Hostname for the blackmagic probe.
		
endmenu
