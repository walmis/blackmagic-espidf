cmake_minimum_required(VERSION 3.5)

idf_component_register(
    REQUIRES driver esp_wifi
    SRC_DIRS "blackmagic/src/target"
             "blackmagic/src"
             "blackmagic/src/platforms/common"
             "."
    LDFRAGMENTS "blackmagic.ld"
    EXCLUDE_SRCS "blackmagic/src/platforms/common/swdptap.c"
                 "blackmagic/src/platforms/common/cdcacm.c"
                 "blackmagic/src/target/jtagtap_generic.c"
                 "blackmagic/src/target/swdptap_generic.c"
                 "blackmagic/src/exception.c"
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
add_definitions(-DPROBE_HOST=esp32 -DPC_HOSTED=0 -DNO_LIBOPENCM3=1 -DENABLE_RTT)
component_compile_options(-Wno-error=char-subscripts -Wno-char-subscripts -Wno-error=format= -Wno-format)

#set(ulp_app_name ulp_bmp)
#set(ulp_riscv_sources "ulp/main.c")
#set(ulp_exp_dep_srcs "swdptap-ulp.c")
#ulp_embed_binary(${ulp_app_name} "${ulp_riscv_sources}" "${ulp_exp_dep_srcs}")

#set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -pg -g -ggdb3")
#set(CMAKE_C_FLAGS_RELEASE  "${CMAKE_C_FLAGS_RELEASE} -pg -g -ggdb3")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg -g -ggdb3")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg -g -ggdb3")
