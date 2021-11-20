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
#define TRIM(out, in) int __len = strlen(in); \
                      char out[__len+1]; out[__len] = 0; \
                      memcpy(out, in, __len); \
                      if (__len && (out[__len-1] == '\n')) { out[__len-1] = ' '; }

#define DEBUG_WARN(x, ...) /*do { TRIM(tmp, x); ESP_LOGW("BMP:W", tmp, ##__VA_ARGS__); } while (0)*/
#define DEBUG_INFO(x, ...) /*do { TRIM(tmp, x);  ESP_LOGI("BMP:I", tmp, ##__VA_ARGS__); } while (0)*/
#define DEBUG_GDB(x, ...) /*do { TRIM(tmp, x);  ESP_LOGI("GDB", tmp, ##__VA_ARGS__); } while (0)*/
#define DEBUG_TARGET(x, ...) /*do { TRIM(tmp, x);  ESP_LOGI("TARG", tmp, ##__VA_ARGS__); } while (0)*/

#endif