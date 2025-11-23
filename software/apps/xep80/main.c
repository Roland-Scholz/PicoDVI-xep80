// #include <stdlib.h>
#include "pico/stdlib.h"
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
#include "cvideo.h"

#include "gfx.h"
#include "xep80.h"
#include "atari_8x8.h"
#include "atari_int_8x8.h"
#include "tmds_encode.h"

#include "vt100_font_8x16.h"
#include "libtsm.h"

#define XEP80_UART_SM 0
#define XEP80_BAUD_RATE 15700
#define XEP80_RX_PIN 2
#define XEP80_JOYEN 6
#define XEP80_RX_LED 18
#define XEP80_TX_LED 19
#define XEP80_LED_FRAMES 5

#define XEP80_DATA_BITS 8
#define XEP80_STOP_BITS 1
#define XEP80_PARITY UART_PARITY_NONE

#define CURSOR_COLOR 0x0c
#define BCKG_COLOR 0x01
#define FORE_COLOR 0x3f

struct tsm_screen *con;
struct tsm_vte *out;
tsm_age_t act_age = 0;

struct dvi_inst dvi0;

PIO pio = pio1;
uint frame_cnt = 0;
uint term_type = 0;
uint xep_uart_pin = XEP80_RX_PIN;
uint16_t inbuf[1024];
volatile uint instart = 0;
volatile uint inend = 0;
int rx_led_cnt = 0;
int tx_led_cnt = 0;

// DVI fonts only 1k, inverse is handeled elsewhere
__attribute__((aligned(4))) char font_int_8x8[1024];
__attribute__((aligned(4))) char font_8x8[1024];
__attribute__((aligned(4))) char vt100_font_8x16_dvi[16 * 256];

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

void color(int i)
{
	printf("\e[%dm", i);
}

void red()
{
	color(30);
}
void yellow()
{
	color(33);
}
void green()
{
	color(32);
}
void white()
{
	color(0);
}
void grey()
{
	color(37);
}

void my_logger(void *data,
			   const char *file,
			   int line,
			   const char *func,
			   const char *subs,
			   unsigned int sev,
			   const char *format,
			   va_list args)
{
	switch (sev)
	{
	case 0 ... 3:
		red();
		break;
	case 4 ... 5:
		yellow();
		break;
	case 6:
		green();
		break;
	default:
		grey();
	}
	printf("func:%s line:%d sav:%d ", func, line, sev);
	vprintf(format, args);
	printf("\n");

	white();
}

void write_cb(struct tsm_vte *vte,
			  const char *u8,
			  size_t len,
			  void *data)
{
	// printf("%s", u8);
}

static inline void set_char(uint x, uint y, char c)
{
	if (x >= CHAR_COLS || y >= CHAR_ROWS)
		return;
	charbuf[x + y * CHAR_COLS] = c;
}

// Pixel format RGB222
static inline void set_colour(uint x, uint y, uint8_t fg, uint8_t bg)
{
	if (x >= CHAR_COLS || y >= CHAR_ROWS)
		return;
	uint char_index = x + y * CHAR_COLS;
	uint bit_index = char_index % 8 * 4;
	uint word_index = char_index / 8;
	for (int plane = 0; plane < 3; ++plane)
	{
		uint32_t fg_bg_combined = (fg & 0x3) | (bg << 2 & 0xc);
		colourbuf[word_index] = (colourbuf[word_index] & ~(0xfu << bit_index)) | (fg_bg_combined << bit_index);
		fg >>= 2;
		bg >>= 2;
		word_index += COLOUR_PLANE_SIZE_WORDS;
	}
}

uint8_t toRGB222(uint8_t red, uint8_t green, uint8_t blue)
{
	uint8_t col;
	uint8_t mask = 0xc0;

	col = (red & mask) >> 2;
	col |= (green & mask) >> 4;
	col |= (blue & mask) >> 6;

	return (col);
}

int draw_cb(struct tsm_screen *con,
			uint32_t id,
			const uint32_t *ch,
			size_t len,
			unsigned int width,
			unsigned int posx,
			unsigned int posy,
			const struct tsm_screen_attr *attr,
			tsm_age_t age,
			void *data)
{
	uint8_t fg, bg;

	if (act_age < age /* && len >= 1 */)
	{
		set_char(posx, posy + 2, (char)(id));

		fg = toRGB222(attr->fr, attr->fg, attr->fb);
		if (attr->blink)
		{
			fg = toRGB222(255, 0, 0); // red
		}
		else if (attr->bold)
		{
			fg = toRGB222(255, 255, 0); // yellow
		}

		bg = toRGB222(attr->br, attr->bg, attr->bb);

		if (attr->inverse)
		{
			set_colour(posx, posy + 2, bg, fg);
		}
		else
		{
			set_colour(posx, posy + 2, fg, bg);
		}

		// printf("%u %s len:%d width:%d x:%d y:%d age: %d inv: %d\n", (unsigned int)id, (char *)ch, len, width, posx, posy, age, attr->inverse);
		//  printf("fccode:%d bccode:%d fg:%d bg:%d\n", attr->fccode, attr->bccode, attr->fg, attr->bg);
	}
	return 0;
}

void __not_in_flash("process_loop") process_loop()
{
	uint16_t c;
	char c8;

	printf("\e[0m");
	printf("starting loop\r\n");

	sleep_ms(1000);

	ColdStart();
	printf("started\r\n");

	/*
		printf("\e[1;31mdata sent to Atari\r\n");
		printf("\e[1;32mdata received by XEP80\r\n");
	*/

	/*
	term_type = 1;
	while (true) {
		// Prüfe ob Daten verfügbar sind
		if (uart_is_readable(uart0)) {
			c8 = uart_getc(uart0) & 0x7f;
			//printf("%02X ", c8);
			tsm_vte_input(out, (const char *)&c8, 1);
	   }
		act_age = tsm_screen_draw(con, &draw_cb, NULL);
	}
	*/

	while (true)
	{
		if (instart != inend)
		{
			instart++;
			instart &= 0x3ff;

			// pio_sm_is_rx_fifo_empty(pio, XEP80_UART_SM);

			c = inbuf[instart];
			c8 = ((char)c) & 0x7f;
			// printf("%04X instart:%d inend:%d\n", inbuf[instart], instart, inend);
			rx_led_cnt = XEP80_LED_FRAMES;

			if (term_type == 0 || c >= 256)
			{
				ReceiveWord(c);
			}
			else
			{
				tsm_vte_input(out, (const char *)&c8, 1);
			}
		}
		else
		{
			if (term_type == 1)
			{
				act_age = tsm_screen_draw(con, &draw_cb, NULL);
			}
			// printf("instart:%d inend:%d\n", instart, inend);
			// sleep_ms(500);
		}

#ifdef STATUS_LINE_POSY
		sprintf(buf, "frame: %09d", frame_cnt);
		x_print_at(64, STATUS_LINE_POSY, buf);
#endif
	}
	__builtin_unreachable();
}

void __not_in_flash("handle_LEDs") handle_LEDs() {
			if (rx_led_cnt > 0)
			{
				rx_led_cnt--;
				gpio_put(XEP80_RX_LED, 0);
			}
			else
			{
				gpio_put(XEP80_RX_LED, 1);
			}
			if (tx_led_cnt > 0)
			{
				tx_led_cnt--;
				gpio_put(XEP80_TX_LED, 0);
			}
			else
			{
				gpio_put(XEP80_TX_LED, 1);
			}
}

void __not_in_flash("core1_main") core1_main()
{
	char *charset;
	char *cbuf;
	char blanks[80];
	int i, j, cheight, cnum, vfactor;
	uint32_t *tmdsbuf;

	__attribute__((aligned(4))) char line[80], c;

	memset(blanks, 32, sizeof(blanks));

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

		cheight = 8;
		vfactor = 2;
		cnum = 128;
		if (term_type == 1)
		{
			cheight = 16;
			vfactor = 1;
			cnum = 256;
			charset = vt100_font_8x16_dvi;
		}

		for (uint y = 0; y < FRAME_HEIGHT; ++y)
		{
			cbuf = &charbuf[y / (cheight * vfactor) * CHAR_COLS];
			queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);

			if (!graphics_mode || (y >= 400))
			{
				for (int plane = 0; plane < 3; ++plane)
				{
					tmds_encode_font_2bpp(
						(const unsigned char *)cbuf,
						&colourbuf[(y / (cheight * vfactor)) * (COLOUR_PLANE_SIZE_WORDS / CHAR_ROWS) + plane * COLOUR_PLANE_SIZE_WORDS],
						tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
						FRAME_WIDTH,
						(const uint8_t *)&charset[((y / vfactor) % cheight) * cnum]);
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
		handle_LEDs();
		if (frame_cnt % 32 == 0)
		{
			//handle_LEDs();
			//HandleBlink();
		}
	}
	__builtin_unreachable();
}

void compute_dvi_font(const char *font, char *dvi_font, int cnum, int cheight)
{
	int i, x, y, z;
	char c, inmask, outmask;

	for (y = z = 0; y < cheight; y++)
	{
		for (x = 0; x < cnum; x++)
		{
			c = font[(x * cheight) + y];
			// reverse bits
			dvi_font[z] = 0;
			inmask = 1;
			outmask = 128;
			for (i = 0; i < 8; i++)
			{
				if (c & inmask)
					dvi_font[z] |= outmask;
				inmask <<= 1;
				outmask >>= 1;
			}
			z++;
		}
	}
}

void __not_in_flash("pio_irq0_handler") pio_irq0_handler()
{
	uint i;
	// Interrupt-Handler
	// if (pio_interrupt_get(pio, 0))
	//{
	// IRQ 0 wurde ausgelöst
	i = (inend + 1) & 0x3ff;
	inbuf[i] = pio_sm_get(pio, XEP80_UART_SM);
	// pio_interrupt_clear(pio, 0);
	inend = i;
	// printf("%04X %d %d\n", inbuf[i], instart, inend);
	// }
}

int __not_in_flash("main") main()
{
	int x, y, rc;

	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);

	// Run system at TMDS bit clock
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

	uart_init(uart0, 115200);
	gpio_set_function(16, GPIO_FUNC_UART);
	gpio_set_function(17, GPIO_FUNC_UART);
	stdio_uart_init_full(uart0, 115200, 16, 17);
	uart_set_translate_crlf(uart0, false);

	gpio_init(XEP80_JOYEN);
	gpio_set_dir(XEP80_JOYEN, GPIO_IN);
	gpio_pull_down(XEP80_JOYEN); // interner Pulldown

	printf("\e[0m\e[2J\e[HPiXEP-80 Started\n");

	// compute fonts for DVI output from ATARI fonts (normal / international charset)
	compute_dvi_font(atari_font, font_8x8, 128, 8);
	compute_dvi_font(atari_font_int, font_int_8x8, 128, 8);
	compute_dvi_font(vt100_font_8x16, vt100_font_8x16_dvi, 256, 16);

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
	hw_set_bits(&bus_ctrl_hw->priority,
				BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
					BUSCTRL_BUS_PRIORITY_DMA_R_BITS);

	multicore_launch_core1(core1_main);

	/*
		set GPIO out for RX/TX LEDs
	*/
	gpio_init(XEP80_RX_LED);
	gpio_set_dir(XEP80_RX_LED, GPIO_OUT);
	gpio_init(XEP80_TX_LED);
	gpio_set_dir(XEP80_TX_LED, GPIO_OUT);

	/*
		set 4 pins from XEP80_RX_PIN on to high-impedance
		check if powerd via JoyPort or SIO and init RX/TX accordingly
		2/3 = SIO RX / TX
		4/5 = Joy RX / TX
	*/
	for (int i = 0; i < xep_uart_pin + 4; i++)
	{
		gpio_init(i);
		gpio_set_dir(i, GPIO_IN); // Eingang
		gpio_disable_pulls(i);	  // weder Pull-up noch Pull-down
	}

	if (gpio_get(XEP80_JOYEN))
	{
		xep_uart_pin += 2;
	}

	uint offset_rx = pio_add_program(pio, &uart_9n1_rx_program);
	uart_9n1_rx_program_init(pio, 0, offset_rx, xep_uart_pin, XEP80_BAUD_RATE);
	uint offset_tx = pio_add_program(pio, &uart_9n1_tx_program);
	uart_9n1_tx_program_init(pio, 1, offset_tx, xep_uart_pin + 1, XEP80_BAUD_RATE);

	// activate IRQ0 on XEP80 UART RX
	// pis_sm0_rx_fifo_not_empty -> CPU-IRQ0
	pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty, true);
	// configure handler
	irq_set_exclusive_handler(PIO1_IRQ_0, pio_irq0_handler);
	irq_set_enabled(PIO1_IRQ_0, true);

	rc = tsm_screen_new(&con, my_logger, NULL);
	printf("tsm_screen_new rc: %d\n", rc);
	rc = tsm_vte_new(&out, con,
					 &write_cb, NULL,
					 my_logger, NULL);
	printf("tsm_vte_new rc: %d\n", rc);

	if (rc == 0)
		initialise_cvideo(pio, con);

	process_loop();
	__builtin_unreachable();
}
