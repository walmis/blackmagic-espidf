#
# Component Makefile
#

COMPONENT_ADD_INCLUDEDIRS := . blackmagic/src/target blackmagic/src blackmagic/src/include blackmagic/src/platforms/common

COMPONENT_SRCDIRS := blackmagic/src/target blackmagic/src blackmagic/src/platforms/common .
CFLAGS += -Wno-error=char-subscripts -Wno-char-subscripts -DPROBE_HOST=esp8266

COMPONENT_OBJEXCLUDE := blackmagic/src/platforms/common/cdcacm.o \
						blackmagic/src/platforms/common/swdptap.o \
						blackmagic/src/target/jtagtap_generic.o \
						blackmagic/src/target/swdptap_generic.o \
						blackmagic/src/exception.o \
						blackmagic/src/gdb_main.o \
						blackmagic/src/gdb_packet.o \
						blackmagic/src/main.o \
						


$(COMPONENT_PATH)/blackmagic/src/include/version.h: 
	$(MAKE) -C $(COMPONENT_PATH)/blackmagic/src include/version.h
    
build: $(COMPONENT_PATH)/blackmagic/src/include/version.h
    

