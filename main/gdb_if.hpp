#pragma once
#include <stdarg.h>
#define BUF_SIZE 1024
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
extern "C"
{
#include "gdb_packet.h"
    int gdb_main_loop(struct target_controller *tc, bool in_syscall);
}

void gdb_lock();
void gdb_unlock();
int gdb_breaklock();
void gdb_restorelock(int state);

struct GDBLock
{
    GDBLock()
    {
        gdb_lock();
    }
    ~GDBLock()
    {
        gdb_unlock();
    }
};
struct GDBBreakLock
{
    GDBBreakLock()
    {
        state = gdb_breaklock();
    }
    ~GDBBreakLock()
    {
        gdb_restorelock(state);
    }
    int state;
};

class GDB
{
public:
    void gdb_main(void);
    virtual ~GDB(){};

    virtual unsigned char gdb_if_getchar(void) = 0;
    virtual void gdb_if_putchar(unsigned char c, int flush) = 0;
    virtual unsigned char gdb_if_getchar_to(int timeout) = 0;

    int gdb_getpacket(char *packet, int size);
    void gdb_putpacket(const char *packet, int size, char pktstart = '$');
    void gdb_putpacket_f(const char *fmt, ...);
    void gdb_putnotifpacket_f(const char *fmt, ...);

    void gdb_out(const char *buf);
    void gdb_voutf(const char *fmt, va_list ap);
    void gdb_outf(const char *fmt, ...);

    virtual int fileno() = 0;

protected:
    friend int ::gdb_main_loop(struct target_controller *tc, bool in_syscall);
    void handle_q_packet(char *packet, int len);
    void handle_v_packet(char *packet, int plen);
    void handle_z_packet(char *packet, int plen);
    int gdb_main_loop(struct target_controller *tc, bool in_syscall);
    void handle_q_string_reply(const char *str, const char *param);

    char pbuf[BUF_SIZE + 1];
    bool non_stop = 0;
    bool no_ack_mode = 0;
};

#define GDB_LOCK() GDBLock gdb_lock

// The index in the pthread local storage buffer for the `this` object
#define GDB_TLS_INDEX 1