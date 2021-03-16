#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

IDF_PATH=$(PWD)/ESP8266_RTOS_SDK

PROJECT_NAME := blackmagic

include $(IDF_PATH)/make/project.mk
CFLAGS += -DPC_HOSTED=0
tftpflash: build/$(PROJECT_NAME).bin
	tftp -v -m octet 192.168.4.1 -c put build/$(PROJECT_NAME).bin firmware.bin
