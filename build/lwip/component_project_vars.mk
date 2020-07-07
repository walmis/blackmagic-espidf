# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/lwip/include $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/lwip/include/lwip/apps $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/lwip/include/lwip $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/lwip/lwip/src/include $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/lwip/lwip/src/include/posix $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/lwip/port/esp8266/include $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/lwip/port/esp8266/include/port
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/lwip -llwip
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += lwip
COMPONENT_LDFRAGMENTS += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/lwip/linker.lf
component-lwip-build: 
