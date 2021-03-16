#pragma once
#include <stdarg.h>
#define BUF_SIZE	1024
#include "FreeRTOS.h"
#include "semphr.h"
extern "C" {
#include "gdb_packet.h"
}

#undef DEBUG_GDB
# define DEBUG_GDB(fmt, ...) ESP_LOGI("GDB" , fmt, ##__VA_ARGS__)

class Lock {
public:
    Lock(xSemaphoreHandle sem) : sem(sem){
        xSemaphoreTakeRecursive(sem, -1);
    }
    ~Lock() {
        xSemaphoreGiveRecursive(sem);
    }
    xSemaphoreHandle sem;
};

extern xSemaphoreHandle gdb_mutex;

class GDB {
public:
    void gdb_main(void);
    virtual ~GDB() {};

    virtual unsigned char gdb_if_getchar(void) = 0;
    virtual void gdb_if_putchar(unsigned char c, int flush) = 0;
    virtual unsigned char gdb_if_getchar_to(int timeout) = 0;

    int gdb_getpacket(char *packet, int size);
    void gdb_putpacket(const char *packet, int size);
    void gdb_putpacket_f(const char *fmt, ...);
    void gdb_out(const char *buf);
    void gdb_voutf(const char *fmt, va_list ap);
    void gdb_outf(const char *fmt, ...);

    virtual int fileno() = 0;

protected:
    void handle_q_packet(char *packet, int len);
    void handle_v_packet(char *packet, int plen);
    void handle_z_packet(char *packet, int plen);
    int gdb_main_loop(struct target_controller *tc, bool in_syscall);
    void handle_q_string_reply(const char *str, const char *param);

    char pbuf[BUF_SIZE+1];
    bool non_stop;

    inline static  int num_clients;
};

#define GDB_LOCK() Lock l(gdb_mutex)
