cmake_minimum_required(VERSION 3.5)

idf_component_register(
    SRC_DIRS "blackmagic/src/target"
             "blackmagic/src"
             "blackmagic/src/platforms/common"
             "."
    EXCLUDE_SRCS "blackmagic/src/platforms/common/cdcacm.c"
                 "blackmagic/src/platforms/common/swdptap.c"
                 "blackmagic/src/target/jtagtap_generic.c"
                 "blackmagic/src/target/swdptap_generic.c"
                 "blackmagic/src/exception.c"
                 "blackmagic/src/gdb_main.c"
                 "blackmagic/src/gdb_packet.c"
                 "blackmagic/src/main.c"
    INCLUDE_DIRS "."
                 "blackmagic/src/target"
                 "blackmagic/src"
                 "blackmagic/src/include"
                 "blackmagic/src/platforms/common"
                 "../../main"
                 "../../main/include"
    PRIV_INCLUDE_DIRS "../../main"
                      "../../main/include"
)
add_definitions(-DPROBE_HOST=esp32 -DPC_HOSTED=0 -DNO_LIBOPENCM3=1)
component_compile_options(-Wno-error=char-subscripts -Wno-char-subscripts)