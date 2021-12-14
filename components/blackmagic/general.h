#undef _GNU_SOURCE
#include_next <general.h>

#ifndef TEST
#define TEST
#include <string.h>
#undef DEBUG_WARN
#undef DEBUG_INFO
#undef DEBUG_GDB
#undef DEBUG_TARGET

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define TRIM(out, in)                      \
    int __len = strlen(in);                \
    char tmp______[__len + 1];                   \
    out[__len] = 0;                        \
    memcpy(out, in, __len);                \
    if (__len && (out[__len - 1] == '\n')) \
    {                                      \
        out[__len - 1] = ' ';              \
    }

#if 1
#define DEBUG_WARN(x, ...)                     \
    do                                         \
    {                                          \
        ESP_LOGW("BMP:W", x, ##__VA_ARGS__); \
    } while (0)
#define DEBUG_INFO(x, ...)                     \
    do                                         \
    {                                          \
        ESP_LOGI("BMP:I", x, ##__VA_ARGS__); \
    } while (0)
#define DEBUG_GDB(x, ...)                    \
    do                                       \
    {                                        \
        ESP_LOGI("GDB", x, ##__VA_ARGS__); \
    } while (0)
#define DEBUG_TARGET(x, ...)                  \
    do                                        \
    {                                         \
        ESP_LOGI("TARG", x, ##__VA_ARGS__); \
    } while (0)
#else
#define DEBUG_WARN(x, ...)
#define DEBUG_INFO(x, ...)
#define DEBUG_GDB(x, ...)
#define DEBUG_TARGET(x, ...)
#endif
#endif