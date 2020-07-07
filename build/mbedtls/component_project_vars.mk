# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/mbedtls/port/include $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/mbedtls/mbedtls/include $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/mbedtls/port/esp8266/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/mbedtls -lmbedtls
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/mbedtls/mbedtls
COMPONENT_LIBRARIES += mbedtls
COMPONENT_LDFRAGMENTS += 
component-mbedtls-build: 
