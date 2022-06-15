#ifndef FARPATCH_UART_H__
#define FARPATCH_UART_H__

#include <stdarg.h>

void uart_dbg_install(void);
int vprintf_remote(const char *fmt, va_list va);
void uart_init(void);

#endif /* FARPATCH_UART_H__ */