# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/components/esp32-espfs/include $(PROJECT_PATH)/components/esp32-espfs/lib/heatshrink
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/esp32-espfs -lesp32-espfs -limage-espfs
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += esp32-espfs
COMPONENT_LDFRAGMENTS += 
component-esp32-espfs-build: 
