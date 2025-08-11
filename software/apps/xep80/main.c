// #include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/stdio_uart.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/dma.h"
#include "pico/sem.h"
#include <stdio.h>
#include <string.h>

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode_font_2bpp.h"
#include "hardware/pio.h"
#include "uart_9n1_rx.pio.h"
#include "uart_9n1_tx.pio.h"
#include "uart_log.h"
#include "cvideo.h"

#include "gfx.h"
#include "xep80.h"
#include "atari_8x8.h"
#include "atari_int_8x8.h"
#include "tmds_encode.h"

struct dvi_inst dvi0;

PIO pio = pio1;

#define XEP80_UART_SM 0
#define XEP80_BAUD_RATE 15700
#define XEP80_RX_PIN 3
#define XEP80_TX_PIN 4

#define XEP80_DATA_BITS 8
#define XEP80_STOP_BITS 1
#define XEP80_PARITY UART_PARITY_NONE

uint frame_cnt = 0;
char buf[30];

// DVI fonts only 1k, inverse is handeled elsewhere
__attribute__((aligned(4))) char font_int_8x8[1024];
__attribute__((aligned(4))) char font_8x8[1024];

extern int char_set;
extern char video_ram[];
extern int graphics_mode;

static __attribute__((aligned(4))) char map[] = {
	0x00,
	0xc0,
	0x30,
	0xf0,

	0x0c,
	0xcc,
	0x3c,
	0xfc,

	0x03,
	0xc3,
	0x33,
	0xf3,

	0x0f,
	0xcf,
	0x3f,
	0xff,
};

void __not_in_flash("process_loop") process_loop()
{
	printf("\e[0m");
	printf("starting loop\r\n");

	sleep_ms(1000);

	ColdStart();
	printf("started\r\n");

	printf("\e[1;31mdata sent to Atari\r\n");
	printf("\e[1;32mdata received by XEP80\r\n");

	while (true)
	{
		if (uart_9n1_rx_program_avail(pio, XEP80_UART_SM))
		{
			uint16_t c = uart_9n1_rx_program_getc(pio, XEP80_UART_SM);
			ReceiveWord(c);
		}
#ifdef STATUS_LINE_POSY
		sprintf(buf, "frame: %09d", frame_cnt);
		x_print_at(64, STATUS_LINE_POSY, buf);
#endif
	}
	__builtin_unreachable();
}

void core1_main()
{
	char *charset;
	__attribute__((aligned(4))) char line[80], c;

	int i, j;

	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	dvi_start(&dvi0);
	while (true)
	{
		if (char_set == CHAR_SET_A)
		{
			charset = font_8x8;
		}
		else
		{
			charset = font_int_8x8;
		}

		for (uint y = 0; y < FRAME_HEIGHT; ++y)
		{
			uint32_t *tmdsbuf;
			queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);

			if (!graphics_mode || (y >= 400))
			{
				for (int plane = 0; plane < 3; ++plane)
				{
					tmds_encode_font_2bpp(
						(const uint8_t *)&charbuf[(y / CHAR_VERT_FACTOR) / FONT_CHAR_HEIGHT * CHAR_COLS],
						&colourbuf[(y / CHAR_VERT_FACTOR) / FONT_CHAR_HEIGHT * (COLOUR_PLANE_SIZE_WORDS / CHAR_ROWS) + plane * COLOUR_PLANE_SIZE_WORDS],
						tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
						FRAME_WIDTH,
						(const uint8_t *)&charset[(y / CHAR_VERT_FACTOR) % FONT_CHAR_HEIGHT * FONT_N_CHARS] - FONT_FIRST_ASCII);
				}
			}
			else
			{
				for (i = j = 0; i < 40; i++)
				{
					c = video_ram[(y >> 1) * 40 + i];
					line[j++] = map[c & 0x0f];
					line[j++] = map[c >> 4];
				}

				for (int plane = 0; plane < 3; ++plane)
				{
					tmds_encode_1bpp((uint32_t *)line, tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD), FRAME_WIDTH);
				}
			}
			queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
		}
		frame_cnt++;
		if (frame_cnt % 32 == 0)
		{
			HandleBlink();
		}
	}
	__builtin_unreachable();
}

void __not_in_flash("compute_dvi_font") compute_dvi_font(char *atari_font, char *dvi_font)
{
	int i, x, y, z;
	char c, mask;

	z = 0;
	for (y = 0; y < 8; y++)
	{
		for (x = 0; x < 128; x++)
		{
			c = atari_font[(x << 3) + y];
			dvi_font[z] = 0;
			mask = 128;
			for (i = 0; i < 8; i++)
			{
				if (c & (1 << i))
					dvi_font[z] |= mask;
				mask = mask >> 1;
			}
			z++;
		}
	}
}

int __not_in_flash("main") main()
{
	int x, y;

	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);

	// Run system at TMDS bit clock
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

	uart_log_init();
	printf("\e[0m\e[2J\e[HPiXEP-80 Started\n");

	// compute fonts for DVI output from ATARI fonts (normal / international charset)
	compute_dvi_font(atari_font, font_8x8);
	compute_dvi_font(atari_font_int, font_int_8x8);

	initialise_cvideo(pio);

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

	printf("DVI enabled \n");

	for (y = 0; y < CHAR_ROWS; ++y)
	{
		for (x = 0; x < CHAR_COLS; ++x)
		{
			x_set_char(x, y, ' ');
			x_set_colour_at(x, y, 255, 0);
		}
	}

	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	multicore_launch_core1(core1_main);

	uint offset = pio_add_program(pio, &uart_9n1_rx_program);
	uart_9n1_rx_program_init(pio, 0, offset, XEP80_RX_PIN, XEP80_BAUD_RATE);

	uint offset_tx = pio_add_program(pio, &uart_9n1_tx_program);
	uart_9n1_tx_program_init(pio, 1, offset_tx, XEP80_TX_PIN, XEP80_BAUD_RATE);

	process_loop();
	__builtin_unreachable();
}
