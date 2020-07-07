# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/main/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/main -lmain -L $(IDF_PATH)/components/esp8266/lib -lcore -L $(PROJECT_PATH)/main -T esp8266.bootloader.ld -T $(IDF_PATH)/components/esp8266/ld/esp8266.rom.ld -T esp8266.bootloader.rom.ld
COMPONENT_LINKER_DEPS += $(PROJECT_PATH)/main/esp8266.bootloader.ld $(IDF_PATH)/components/esp8266/ld/esp8266.rom.ld $(PROJECT_PATH)/main/esp8266.bootloader.rom.ld
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += main
COMPONENT_LDFRAGMENTS += 
component-main-build: 
