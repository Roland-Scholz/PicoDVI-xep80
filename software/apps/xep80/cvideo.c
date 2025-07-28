//
// Title:	        Pico-mposite Video Output
// Description:		The composite video stuff
// Author:	        Dean Belfield
// Created:	        26/01/2021
// Last Updated:	27/09/2024
//
// Modinfo:
// 15/02/2021:      Border buffers now have horizontal sync pulse set correctly
//                  Decreased RAM usage by updating the image buffer scanline on the fly during the horizontal interrupt
//					Fixed logic error in cvideo_dma_handler; initial memcpy done twice
// 31/01/2022:      Refactored to use less memory
//					Split the video generation into two state machines; sync and data
// 01/02/2022:      Added a handful of graphics primitives
// 02/02/2022:      Split main loop out into main.c
// 04/02/2022:      Added set_border
// 05/02/2022:      Added support for colour, fixed bug in video generation
// 20/02/2022:      Bitmap is now dynamically allocated; added two higher resolution video modes
// 25/02/2022:      Lengthened HSYNC to 12us
// 27/09/2024:		PIO state machines now started simultaneously

#include <stdlib.h>
#include <stdio.h>

#include "memory.h"
#include "pico/stdlib.h"

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "cvideo.h"
#include "cvideo_sync.pio.h" // The assembled PIO code

#include "xep80.h"

// PIO pio_0;                      // The PIO that this uses
uint offset_0; // Program offsets

uint dma_channel_0; // DMA channel for transferring sync data to PIO
uint vline;         // Current PAL(ish) video line being processed
uint bline;         // Line in the bitmap to fetch

uint vblank_count; // Vblank counter
bool cursor_onoff = true;
int cursor_frame_cnt = 0;

extern int cursor_x;
extern int cursor_y;
extern int cursor_on;
extern int cursor_blink;
extern int graphics_mode;
extern int pal_mode;
extern char video_ram[];
extern int char_set;
extern char atari_font[];
extern char atari_font_int[];

char __attribute__((aligned(4))) hsync[232];
char __attribute__((aligned(4))) vsync[232];
char __attribute__((aligned(4))) line0[232];
char __attribute__((aligned(4))) line1[232];

char __attribute__((aligned(4))) mapping[] = {
    0b01010101,
    0b01010111,
    0b01011101,
    0b01011111,
    0b01110101,
    0b01110111,
    0b01111101,
    0b01111111,
    0b11010101,
    0b11010111,
    0b11011101,
    0b11011111,
    0b11110101,
    0b11110111,
    0b11111101,
    0b11111111};

short __attribute__((aligned(4))) bit_mapping[] = {
    0x5555,
    0x55F5,
    0x555F,
    0x55FF,
    0xF555,
    0xF5F5,
    0xF55F,
    0xF5FF,
    0x5F55,
    0x5FF5,
    0x5F5F,
    0x5FFF,
    0xFF55,
    0xFFF5,
    0xFF5F,
    0xFFFF,
};

char *pline0 = line0;
char *pline1 = line1;

extern char *line_pointers[XEP80_HEIGHT];

/*
void rearange_font(char *font, char* buffer) {

    int i;

    memcpy(buffer, &font[64 * 8], 32 * 8);
    memcpy(&buffer[32 * 8], font, 32 * 8);
    memcpy(&buffer[64 * 8], &font[32 * 8], 32 * 8);

    memcpy(font, video_ram, 96 * 8);
    memcpy(&font[128 * 8], font, 128 * 8);

    for (i = 128 * 8; i < 256 * 8; i++)
    {
        font[i] = ~font[i];
    }

    for (i = 0x9b * 8; i < 0x9b * 8 + 8; i++)
    {
        font[i] = 0;
    }

} */

void __not_in_flash("generate_synclines") generate_synclines()
{
    memset(hsync, BLACK, sizeof(hsync));
    memset(&hsync[HSYNCSTART], SYNC, 18);

    memset(&hsync[DATASTART - 17], GREY, 160 + 17 + 12);

    memset(vsync, SYNC, sizeof(vsync));

    memcpy(line0, hsync, sizeof(line0));
    memcpy(line1, hsync, sizeof(line0));

    /*
    rearange_font(atari_font, video_ram);
    rearange_font(atari_font_int, video_ram);

    printf("\n");
    printf("\n");
    for (int y = 0; y < 256; y++) {
       for (int x = 0; x < 8; x++) {
            printf("0x%02X, ", atari_font_int[(y<<3)+x]);
       }
       printf("\n");
    }
    */
}

void __not_in_flash("generate_line") generate_line(char *buffer)
{

    unsigned int screenline = bline >> 3;
    unsigned int charline = bline % 8;
    char *pscreen = line_pointers[screenline];
    char *charset;
    char c;
    int x;

    if (!graphics_mode || bline >= 200)
    {
        if (char_set == CHAR_SET_A)
        {
            charset = atari_font;
        }
        else
        {
            charset = atari_font_int;
        }

        for (x = 0; x < WIDTH; x++)
        {
            c = charset[(pscreen[x] << 3) + charline];
            //        c = pc_atari_font[(pscreen[x] << 3) + charline];
            //        c = font_8x8[(pscreen[x] << 3) + charline];

            if (cursor_on && !graphics_mode && cursor_x == x && cursor_y == screenline)
            {
                if (!cursor_blink || (cursor_blink && cursor_onoff))
                    c = ~c;
            }

            *buffer = mapping[c >> 4];
            buffer++;
            *buffer = mapping[c & 15];
            buffer++;
        }
    }
    else
    {
        pscreen = &video_ram[bline * 40];
        for (x = 0; x < 40; x++)
        {
            c = pscreen[x];

            *((short *)buffer) = bit_mapping[c & 15];
            buffer += 2;
            *((short *)buffer) = bit_mapping[c >> 4];
            buffer += 2;
        }
    }
}

/*
 * The main routine sets up the whole shebang
 */
void __not_in_flash("initialise_cvideo") initialise_cvideo(PIO pio)
{
    // pio_0 = pio0;	                    // Assign the PIO
    double divisor;

    generate_synclines();

    // Load up the PIO programs
    //
    offset_0 = pio_add_program(pio, &cvideo_sync_program);

    dma_channel_0 = dma_claim_unused_channel(true); // Claim a DMA channel for the sync

    vline = 1;        // Initialise the video scan line counter to 1
    bline = 0;        // And the index into the bitmap pixel buffer to 0
    vblank_count = 0; // And the vblank counter

    divisor = (clock_get_hz(clk_sys) / 14500000.0);
    // printf("SysClk: %lu\n", clock_get_hz(clk_sys));
    // printf("divisior: %f\n", divisor);

    // Initialise the first PIO (video sync)
    //
    pio_sm_set_enabled(pio, sm_sync, false); // Disable the PIO state machine
    pio_sm_clear_fifos(pio, sm_sync);        // Clear the PIO FIFO buffers
    cvideo_sync_initialise_pio(              // Initialise the PIO (function in cvideo.pio)
        pio,                                 // The PIO to attach this state machine to
        sm_sync,                             // The state machine number
        offset_0,                            // And offset
        gpio_base,                           // Start pin in the GPIO
        gpio_count,                          // Number of pins
        divisor                              // State machine clock frequency
    );

    cvideo_configure_pio_dma( // Configure the DMA
        pio,                  // The PIO to attach this DMA to
        sm_sync,              // The state machine number
        dma_channel_0,        // The DMA channel
        DMA_SIZE_8,           // Size of each transfer
        232,                  // Number of bytes to transfer
        cvideo_dma_handler    // The DMA handler
    );

    // Start the PIO state machines
    //
    pio_enable_sm_mask_in_sync(pio, (1u << sm_sync));

    printf("Composite-Video enabled \n");
}

// Wait for vblank
//
void __not_in_flash("wait_vblank") wait_vblank(void)
{
    uint c = vblank_count; // Get the current vblank count
    while (c == vblank_count)
    {                // Wait until it changes
        sleep_us(4); // Need to sleep for a minimum of 4us
    }
}

// The DMA interrupt handler
// This feeds the state machine cvideo_sync with data for the PAL(ish) video signal
//
void __not_in_flash("cvideo_dma_handler") cvideo_dma_handler(void)
{

    // Switch condition on the vertical scanline number (vline)
    // Each statement does a dma_channel_set_read_addr to point the PIO to the next data to output
    //

    // dma_channel_set_read_addr(dma_channel_0, line0, true);
    if (pal_mode)
    {
        switch (vline)
        {

        // First deal with the vertical sync scanlines
        // Also on scanline 3, preload the first pixel buffer scanline
        //
        case 1 ... 2:
            dma_channel_set_read_addr(dma_channel_0, vsync, true);
            break;
        case 3:
            dma_channel_set_read_addr(dma_channel_0, vsync, true);

            pline0 = line0;
            pline1 = line1;
            bline = 0;
            generate_line(pline1 + DATASTART);
            break;
        case 4 ... 5:
        case 310 ... 312:
            dma_channel_set_read_addr(dma_channel_0, vsync, true);
            break;

// Then the border scanlines
//
#ifdef STATUS_LINE_POSY
        case 6 ... 60:
#else
        case 6 ... 68:
#endif

        case 269 ... 309:
            dma_channel_set_read_addr(dma_channel_0, hsync, true);
            break;

        // Now point the dma at the first buffer for the pixel data,
        // and preload the data for the next scanline
        //
        default:
            if (pline0 == line0)
            {
                pline0 = line1;
                pline1 = line0;
            }
            else
            {
                pline0 = line0;
                pline1 = line1;
            }
            dma_channel_set_read_addr(dma_channel_0, pline0, true);
            bline++;
            generate_line(pline1 + DATASTART);
            break;
        }
    }
    else    //NTSC
    {
        switch (vline)
        {
        case 1 ... 2:
            dma_channel_set_read_addr(dma_channel_0, vsync, true);
            break;
        case 3:
            dma_channel_set_read_addr(dma_channel_0, vsync, true);

            pline0 = line0;
            pline1 = line1;
            bline = 0;
            generate_line(pline1 + DATASTART);
            break;

#ifdef STATUS_LINE_POSY
        case 40 ... 247:
#else
        case 40 ... 239:
#endif
            if (pline0 == line0)
            {
                pline0 = line1;
                pline1 = line0;
            }
            else
            {
                pline0 = line0;
                pline1 = line1;
            }

            dma_channel_set_read_addr(dma_channel_0, pline0, true);

            bline++;
            generate_line(pline1 + DATASTART);

            break;
        default:
            dma_channel_set_read_addr(dma_channel_0, hsync, true);
            break;
        }
    }

    //
    // Increment and wrap the counters
    //
    vline++;
    if ((pal_mode && vline > 312) || (!pal_mode && vline > 262))
    {              // If we've gone past the bottom scanline then
        vline = 1; // Reset the scanline counter
        vblank_count++;

        cursor_frame_cnt++;
        if (cursor_frame_cnt >= CURSOR_TIME)
        {
            cursor_frame_cnt = 0;
            cursor_onoff = !cursor_onoff;
        }
    }

    // Finally, clear the interrupt request ready for the next horizontal sync interrupt
    dma_hw->ints0 = 1u << dma_channel_0;
}

// Configure the PIO DMA
// Parameters:
// - pio: The PIO to attach this to
// - sm: The state machine number
// - dma_channel: The DMA channel
// - transfer_size: Size of each DMA bus transfer (DMA_SIZE_8, DMA_SIZE_16 or DMA_SIZE_32)
// - buffer_size_words: Number of bytes to transfer
// - handler: Address of the interrupt handler, or NULL for no interrupts
//
void __not_in_flash("cvideo_configure_pio_dma") cvideo_configure_pio_dma(PIO pio, uint sm, uint dma_channel, uint transfer_size, size_t buffer_size, irq_handler_t handler)
{
    dma_channel_config c = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&c, transfer_size);
    channel_config_set_read_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma_channel, &c,
                          &pio->txf[sm], // Destination pointer
                          NULL,          // Source pointer
                          buffer_size,   // Size of buffer
                          true           // Start flag (true = start immediately)
    );
    if (handler != NULL)
    {
        dma_channel_set_irq1_enabled(dma_channel, true);
        irq_set_exclusive_handler(DMA_IRQ_1, handler);
        irq_set_enabled(DMA_IRQ_1, true);
    }
}