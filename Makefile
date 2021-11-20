#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

IDF_PATH=$(PWD)/esp-idf

PROJECT_NAME := blackmagic

include $(IDF_PATH)/make/project.mk
CFLAGS += -DPC_HOSTED=0 -DNO_LIBOPENCM3=1

tftpflash: build/$(PROJECT_NAME).bin
	tftp -v -m octet 10.0.237.67 -c put build/$(PROJECT_NAME).bin firmware.bin
