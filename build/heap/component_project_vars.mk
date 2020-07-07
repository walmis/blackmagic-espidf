# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/heap/include $(PROJECT_PATH)/ESP8266_RTOS_SDK/components/heap/port/esp8266/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/heap -lheap
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += heap
COMPONENT_LDFRAGMENTS += 
component-heap-build: 
