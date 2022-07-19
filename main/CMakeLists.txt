set(srcs "gdb_if.cpp"
    "gdb_main.cpp"
    "hashmap.cpp"
    "http.c"
    "ota-tftp.c"
    "platform.c"
    "rtt.c"
    "tinyprintf.c"
    "websocket.c"
    )
set(incs, "."
    "include")

idf_component_register(SRC_DIRS "."
    INCLUDE_DIRS "."
    "include")
