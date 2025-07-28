#include "uart_log.h"

void __not_in_flash("uart_log_init") uart_log_init()
{
    // init
    stdio_uart_init();

    // UART initialisieren
    uart_init(LOG_UART_ID, LOG_BAUD_RATE);

    // GPIOs für UART konfigurieren
    gpio_set_function(LOG_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(LOG_UART_RX_PIN, GPIO_FUNC_UART);
}