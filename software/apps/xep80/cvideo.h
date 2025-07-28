//
// Title:	        Pico FBAS Video Output
//

#pragma once

#include "hardware/clocks.h"

// 14.5 Mhz pixelclock
// 928 pixels per line
// 2uS frontporch   28 pixels                7 bytes (14.5Mhz * 2uS)
// 5uS hsync        72 pixels               18 bytes
// 5uS backporch    72 pixels   162 pixels  18 bytes    
// pause            72 pixels   244         17 bytes
// start visible                           160 bytes
// end = 640+244 = 884                      12 bytes 
//                                         232 bytes

#define SYNC 0x00
#define BLACK 0x55
#define GREY 0xaa
#define WHITE 0xff
#define HSYNCSTART 7
#define DATASTART 60

#define gpio_base 0
#define gpio_count 2
#define sm_sync 3               // State machine number in the PIO for the sync data

#define WIDTH 80                //define 80 columns by 25 rows
#define HEIGHT 26

#define CURSOR_TIME 25

void initialise_cvideo(PIO);
void wait_vblank(void);
void printchar(char c);

void cvideo_configure_pio_dma(PIO pio, uint sm, uint dma_channel, uint transfer_size, size_t buffer_size,  irq_handler_t handler);
void cvideo_dma_handler(void);
void generate_line(char *buffer);

