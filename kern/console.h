/* See COPYRIGHT for copyright information. */

#ifndef _CONSOLE_H_
#define _CONSOLE_H_
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/types.h>

#define MONO_BASE	0x3B4
#define MONO_BUF	0xB0000
#define CGA_BASE	0x3D4
#define CGA_BUF		0xB8000

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

void cons_init(void);
void cons_putc(int c);
int cons_getc(void);

void kbd_intr(void); // irq 1
void serial_intr(void); // irq 4

#define	FORE_BLACK	30
#define	FORE_RED	31
#define	FORE_GREEN	32
#define	FORE_YELLOW	33
#define	FORE_BLUE	34
#define	FORE_MAGENTA	35
#define	FORE_CYAN	36
#define	FORE_WHITE	37

#define	BACK_BLACK	40
#define	BACK_RED	41
#define	BACK_GREEN	42
#define	BACK_YELLOW	43
#define	BACK_BLUE	44
#define	BACK_MAGENTA	45
#define	BACK_CYAN	46
#define	BACK_WHITE	47

#define ATTR_RESET	0

typedef enum pc_colors {
	pc_black	= 0,
	pc_blue		= 1,
	pc_green	= 2,
	pc_cyan		= 3,
	pc_red		= 4,
	pc_magenta	= 5,
	pc_brown	= 6,
	pc_white	= 7,
	pc_grey		= 8,
	pc_brt_blue	= 9,
	pc_brt_green	= 10,
	pc_brt_cyan	= 11,
	pc_brt_red	= 12,
	pc_brt_magenta	= 13,
	pc_yellow	= 14,
	pc_brt_white	= 15
} pc_colors_t;

#endif /* _CONSOLE_H_ */
