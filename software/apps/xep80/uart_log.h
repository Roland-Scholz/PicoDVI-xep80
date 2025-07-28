#ifndef _UART_LOG_H_
#define _UART_LOG_H_

#include "pico/stdlib.h"
#include "pico/stdio_uart.h"

#define LOG_UART_ID uart0
#define LOG_BAUD_RATE 115200
#define LOG_UART_TX_PIN 16
#define LOG_UART_RX_PIN 17

void uart_log_init();
#endif