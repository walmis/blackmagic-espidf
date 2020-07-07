# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/wpa_supplicant/include $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/wpa_supplicant/include/wps $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/wpa_supplicant/include/wpa2 $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/wpa_supplicant/port/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/wpa_supplicant -lwpa_supplicant
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += wpa_supplicant
COMPONENT_LDFRAGMENTS += 
component-wpa_supplicant-build: 
