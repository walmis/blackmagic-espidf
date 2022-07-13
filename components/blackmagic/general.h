#undef _GNU_SOURCE
#include_next <general.h>

#ifndef TEST
#define TEST
#include <string.h>
#undef DEBUG_WARN
#undef DEBUG_INFO
#undef DEBUG_GDB
#undef DEBUG_TARGET

#ifndef __FILENAME__
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#ifdef ENABLE_DEBUG
// Redefine a bunch of ESP debug macros here without the trailing linefeed
#define LOG_FORMAT_BMP(letter, format)             LOG_COLOR_##letter #letter " (%d) %s: " format LOG_RESET_COLOR
#define LOG_SYSTEM_TIME_FORMAT_BMP(letter, format) LOG_COLOR_##letter #letter " (%s) %s: " format LOG_RESET_COLOR

#define ESP_LOGE_BMP(tag, format, ...) ESP_LOG_LEVEL_LOCAL_BMP(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_LOGW_BMP(tag, format, ...) ESP_LOG_LEVEL_LOCAL_BMP(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define ESP_LOGI_BMP(tag, format, ...) ESP_LOG_LEVEL_LOCAL_BMP(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define ESP_LOGD_BMP(tag, format, ...) ESP_LOG_LEVEL_LOCAL_BMP(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define ESP_LOGV_BMP(tag, format, ...) ESP_LOG_LEVEL_LOCAL_BMP(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#define ESP_LOG_LEVEL_LOCAL_BMP(level, tag, format, ...)          \
	do {                                                          \
		if (LOG_LOCAL_LEVEL >= level)                             \
			ESP_LOG_LEVEL_BMP(level, tag, format, ##__VA_ARGS__); \
	} while (0)

#if CONFIG_LOG_TIMESTAMP_SOURCE_RTOS
#define ESP_LOG_LEVEL_BMP(level, tag, format, ...)                                                                   \
	do {                                                                                                             \
		if (level == ESP_LOG_ERROR) {                                                                                \
			esp_log_write(ESP_LOG_ERROR, tag, LOG_FORMAT_BMP(E, format), esp_log_timestamp(), tag, ##__VA_ARGS__);   \
		} else if (level == ESP_LOG_WARN) {                                                                          \
			esp_log_write(ESP_LOG_WARN, tag, LOG_FORMAT_BMP(W, format), esp_log_timestamp(), tag, ##__VA_ARGS__);    \
		} else if (level == ESP_LOG_DEBUG) {                                                                         \
			esp_log_write(ESP_LOG_DEBUG, tag, LOG_FORMAT_BMP(D, format), esp_log_timestamp(), tag, ##__VA_ARGS__);   \
		} else if (level == ESP_LOG_VERBOSE) {                                                                       \
			esp_log_write(ESP_LOG_VERBOSE, tag, LOG_FORMAT_BMP(V, format), esp_log_timestamp(), tag, ##__VA_ARGS__); \
		} else {                                                                                                     \
			esp_log_write(ESP_LOG_INFO, tag, LOG_FORMAT_BMP(I, format), esp_log_timestamp(), tag, ##__VA_ARGS__);    \
		}                                                                                                            \
	} while (0)
#elif CONFIG_LOG_TIMESTAMP_SOURCE_SYSTEM
#define ESP_LOG_LEVEL_BMP(level, tag, format, ...)                                                                    \
	do {                                                                                                              \
		if (level == ESP_LOG_ERROR) {                                                                                 \
			esp_log_write(ESP_LOG_ERROR, tag, LOG_SYSTEM_TIME_FORMAT_BMP(E, format), esp_log_system_timestamp(), tag, \
				##__VA_ARGS__);                                                                                       \
		} else if (level == ESP_LOG_WARN) {                                                                           \
			esp_log_write(ESP_LOG_WARN, tag, LOG_SYSTEM_TIME_FORMAT_BMP(W, format), esp_log_system_timestamp(), tag,  \
				##__VA_ARGS__);                                                                                       \
		} else if (level == ESP_LOG_DEBUG) {                                                                          \
			esp_log_write(ESP_LOG_DEBUG, tag, LOG_SYSTEM_TIME_FORMAT_BMP(D, format), esp_log_system_timestamp(), tag, \
				##__VA_ARGS__);                                                                                       \
		} else if (level == ESP_LOG_VERBOSE) {                                                                        \
			esp_log_write(ESP_LOG_VERBOSE, tag, LOG_SYSTEM_TIME_FORMAT_BMP(V, format), esp_log_system_timestamp(),    \
				tag, ##__VA_ARGS__);                                                                                  \
		} else {                                                                                                      \
			esp_log_write(ESP_LOG_INFO, tag, LOG_SYSTEM_TIME_FORMAT_BMP(I, format), esp_log_system_timestamp(), tag,  \
				##__VA_ARGS__);                                                                                       \
		}                                                                                                             \
	} while (0)
#endif //CONFIG_LOG_TIMESTAMP_SOURCE_xxx

#define DEBUG_WARN(x, ...)                       \
	do {                                         \
		ESP_LOGW_BMP("BMP:W", x, ##__VA_ARGS__); \
	} while (0)
#define DEBUG_INFO(x, ...)                       \
	do {                                         \
		ESP_LOGI_BMP("BMP:I", x, ##__VA_ARGS__); \
	} while (0)
#define DEBUG_GDB(x, ...)                      \
	do {                                       \
		ESP_LOGI_BMP("GDB", x, ##__VA_ARGS__); \
	} while (0)
#define DEBUG_TARGET(x, ...)                    \
	do {                                        \
		ESP_LOGI_BMP("TARG", x, ##__VA_ARGS__); \
	} while (0)

#else
#define DEBUG_WARN(x, ...)
#define DEBUG_INFO(x, ...)
#define DEBUG_GDB(x, ...)
#define DEBUG_TARGET(x, ...)
#endif
#endif