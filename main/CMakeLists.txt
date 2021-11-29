set(srcs "gdb_if.cpp"
         "gdb_main.cpp"
         "hashmap.cpp"
         "http.c"
         "ota-tftp.c"
         "platform.c"
         "tinyprintf.c")
set(incs, "."
         "include")

idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "."
                                    "include")
