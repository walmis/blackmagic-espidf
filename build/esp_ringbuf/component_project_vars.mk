# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/esp_ringbuf/include $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/esp_ringbuf/include/freertos
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/esp_ringbuf -lesp_ringbuf
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += esp_ringbuf
COMPONENT_LDFRAGMENTS += 
component-esp_ringbuf-build: 
