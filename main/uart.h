/*
 * uart.hpp
 *
 *  Created on: Oct 25, 2016
 *      Author: walmis
 */

#ifndef UART_HPP_
#define UART_HPP_

#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>

#define QUEUE_SIZE (2048)

void uart0_ovr_reset(void);
void uart0_rx_init(void);
int uart0_getchar();
uint32_t uart0_rxcount();
uint32_t uart0_overruns();
uint32_t uart0_rxavail();

void uart0_rxqueue_reset();

int uart_add_rx_poll(SemaphoreHandle_t sem);
int remove_rx_poll();
#endif /* UART_HPP_ */
