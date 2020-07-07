# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/libsodium/libsodium/src/libsodium/include $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/libsodium/port_include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/libsodium -llibsodium
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += libsodium
COMPONENT_LDFRAGMENTS += 
component-libsodium-build: 
