# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/log/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/log -llog
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += log
COMPONENT_LDFRAGMENTS += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/log/linker.lf
component-log-build: 
