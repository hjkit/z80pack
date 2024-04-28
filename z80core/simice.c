/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 1987-2022 by Udo Munk
 * Copyright (C) 2024 by Thomas Eberhardt
 */

/*
 *	This module is an ICE type user interface to debug Z80/8080 programs
 *	on a host system.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/time.h>
#include "sim.h"
#include "simglb.h"
#include "memory.h"

extern void run_cpu(void), step_cpu(void);
extern void disass(WORD *);
extern int exatoi(char *);
extern int getkey(void);
extern void int_on(void), int_off(void);
extern int load_file(char *, WORD, int);
extern void report_cpu_error(void);

static void do_step(void);
static void do_trace(char *);
static void do_go(char *);
static int handle_break(void);
static void do_dump(char *);
static void do_list(char *);
static void do_modify(char *);
static void do_fill(char *);
static void do_move(char *);
static void do_port(char *);
static void do_reg(char *);
static void print_head(void);
static void print_reg(void);
static void do_break(char *);
static void do_hist(char *);
static void do_count(char *);
static void do_clock(void);
static void timeout(int);
static void do_show(void);
static void do_load(char *);
static void do_unix(char *);
static void do_help(void);

static WORD wrk_addr;

void (*ice_before_go)(void);
void (*ice_after_go)(void);
void (*ice_cust_cmd)(char *, WORD *);
void (*ice_cust_help)(void);

/*
 *	The function "ice_cmd_loop()" is the dialog user interface, called
 *	from the simulation when desired with go_flag set to 0, or, when
 *	no other user interface is needed, directly after any necessary
 *	initialization work at program start with go_flag set to 1.
 *
 *	There are also two function pointers "ice_before_go" and
 *	"ice_after_go" which can be set to a void function(void) to be
 *	called before and after the go command. For example, mosteksim
 *	uses "set_unix_terminal" and "reset_unix_terminal".
 *
 *	Additionally you can add custom commands by setting "ice_cust_cmd"
 *	to a void function(char *, WORD *) which gets called with the
 *	command line and a pointer to the current working address as
 *	parameters. "ice_cust_help" can be set to a void function(void)
 *	to display help for custom commands.
 */
void ice_cmd_loop(int go_flag)
{
	register int eoj = 1;
	WORD a;
	static char cmd[LENCMD];

	if (!go_flag) {
		report_cpu_error();
		print_head();
		print_reg();
		a = PC;
		disass(&a);
	}
	wrk_addr = PC;

	while (eoj) {
		if (go_flag) {
			strcpy(cmd, "g");
			go_flag = 0;
		} else {
			printf(">>> ");
			fflush(stdout);
			if (fgets(cmd, LENCMD, stdin) == NULL) {
				putchar('\n');
				if (isatty(fileno(stdin)))
					clearerr(stdin);
				else
					eoj = 0;
				continue;
			}
		}
		switch (tolower((unsigned char) *cmd)) {
		case '\n':
			do_step();
			break;
		case 't':
			do_trace(cmd + 1);
			break;
		case 'g':
			do_go(cmd + 1);
			break;
		case 'd':
			do_dump(cmd + 1);
			break;
		case 'l':
			do_list(cmd + 1);
			break;
		case 'm':
			do_modify(cmd + 1);
			break;
		case 'f':
			do_fill(cmd + 1);
			break;
		case 'v':
			do_move(cmd + 1);
			break;
		case 'x':
			do_reg(cmd + 1);
			break;
		case 'p':
			do_port(cmd + 1);
			break;
		case 'b':
			do_break(cmd + 1);
			break;
		case 'h':
			do_hist(cmd + 1);
			break;
		case 'z':
			do_count(cmd + 1);
			break;
		case 'c':
			do_clock();
			break;
		case 's':
			do_show();
			break;
		case '?':
			do_help();
			break;
		case 'r':
			do_load(cmd + 1);
			break;
		case '!':
			do_unix(cmd + 1);
			break;
		case 'q':
			eoj = 0;
			break;
		default:
			if (ice_cust_cmd)
				(*ice_cust_cmd)(cmd, &wrk_addr);
			else
				puts("what??");
			break;
		}
	}
}

/*
 *	Execute a single step
 */
static void do_step(void)
{
	WORD a;

	step_cpu();
	if (cpu_error == OPHALT)
		handle_break();
	report_cpu_error();
	print_head();
	print_reg();
	a = PC;
	disass(&a);
	wrk_addr = PC;
}

/*
 *	Execute several steps with trace output
 */
static void do_trace(char *s)
{
	register int count, i;

	while (isspace((unsigned char) *s))
		s++;
	if (*s == '\0')
		count = 20;
	else
		count = atoi(s);
	print_head();
	print_reg();
	for (i = 0; i < count; i++) {
		step_cpu();
		print_reg();
		if (cpu_error) {
			if (cpu_error == OPHALT) {
				if (!handle_break()) {
					break;
				}
			} else
				break;
		}
	}
	report_cpu_error();
	wrk_addr = PC;
}

/*
 *	Run the CPU emulation endless
 */
static void do_go(char *s)
{
	while (isspace((unsigned char) *s))
		s++;
	if (isxdigit((unsigned char) *s))
		PC = exatoi(s);
	if (ice_before_go)
		(*ice_before_go)();
	for (;;) {
		run_cpu();
		if (cpu_error) {
			if (cpu_error == OPHALT) {
				if (!handle_break()) {
					break;
				}
			} else
				break;
		}
	}
	if (ice_after_go)
		(*ice_after_go)();
	report_cpu_error();
	print_head();
	print_reg();
	wrk_addr = PC;
}

/*
 *	Handling of software breakpoints (HALT opcode):
 *
 *	Output:	0 breakpoint or other HALT opcode reached (stop)
 *		1 breakpoint reached, pass counter not reached (continue)
 */
static int handle_break(void)
{
#ifdef SBSIZE
	register int i;
	int break_address;

	for (i = 0; i < SBSIZE; i++)	/* search for breakpoint */
		if (soft[i].sb_addr == PC - 1)
			break;
	if (i == SBSIZE)		/* no breakpoint found */
		return (0);
#ifdef HISIZE
	h_next--;			/* correct history */
	if (h_next < 0)
		h_next = 0;
#endif
	break_address = PC - 1;		/* store addr of breakpoint */
	PC--;				/* substitute HALT opcode by */
	putmem(PC, soft[i].sb_oldopc);	/* original opcode */
	step_cpu();			/* and execute it */
	putmem(soft[i].sb_addr, 0x76);	/* restore HALT opcode again */
	soft[i].sb_passcount++;		/* increment pass counter */
	if (soft[i].sb_passcount != soft[i].sb_pass)
		return (1);		/* pass not reached, continue */
	printf("Software breakpoint %d reached at %04x\n", i, break_address);
	soft[i].sb_passcount = 0;	/* reset pass counter */
	return (0);			/* pass reached, stop */
#else
	return (0);
#endif
}

/*
 *	Memory dump
 */
static void do_dump(char *s)
{
	register int i, j;
	BYTE c;

	while (isspace((unsigned char) *s))
		s++;
	if (isxdigit((unsigned char) *s))
		wrk_addr = exatoi(s) - exatoi(s) % 16;
	printf("Addr   ");
	for (i = 0; i < 16; i++)
		printf("%02x ", i);
	puts(" ASCII");
	for (i = 0; i < 16; i++) {
		printf("%04x - ", (unsigned int) wrk_addr);
		for (j = 0; j < 16; j++)
			printf("%02x ", getmem(wrk_addr++));
		putchar('\t');
		for (j = -16; j < 0; j++) {
			c = getmem(wrk_addr + j);
			printf("%c", isprint((unsigned char) c) ? c : '.');
		}
		putchar('\n');
	}
}

/*
 *	Disassemble
 */
static void do_list(char *s)
{
	register int i;

	while (isspace((unsigned char) *s))
		s++;
	if (isxdigit((unsigned char) *s))
		wrk_addr = exatoi(s);
	for (i = 0; i < 10; i++) {
		printf("%04x - ", (unsigned int) wrk_addr);
		disass(&wrk_addr);
	}
}

/*
 *	Memory modify
 */
static void do_modify(char *s)
{
	static char nv[LENCMD];

	while (isspace((unsigned char) *s))
		s++;
	if (isxdigit((unsigned char) *s))
		wrk_addr = exatoi(s);
	for (;;) {
		printf("%04x = %02x : ", (unsigned int) wrk_addr,
		       getmem(wrk_addr));
		if (fgets(nv, sizeof(nv), stdin) == NULL) {
			putchar('\n');
			if (isatty(fileno(stdin)))
				clearerr(stdin);
			break;
		}
		if (nv[0] == '\n') {
			wrk_addr++;
			continue;
		}
		if (!isxdigit((unsigned char) nv[0]))
			break;
		putmem(wrk_addr++, exatoi(nv));
	}
}

/*
 *	Memory fill
 */
static void do_fill(char *s)
{
	register WORD a;
	register int i;
	register BYTE val;

	while (isspace((unsigned char) *s))
		s++;
	a = exatoi(s);
	while (*s != ',' && *s != '\0')
		s++;
	if (*s) {
		i = exatoi(++s);
	} else {
		puts("count missing");
		return;
	}
	while (*s != ',' && *s != '\0')
		s++;
	if (*s) {
		val = exatoi(++s);
	} else {
		puts("value missing");
		return;
	}
	while (i--)
		putmem(a++, val);
}

/*
 *	Memory move
 */
static void do_move(char *s)
{
	register WORD a1, a2;
	register int count;

	while (isspace((unsigned char) *s))
		s++;
	a1 = exatoi(s);
	while (*s != ',' && *s != '\0')
		s++;
	if (*s) {
		a2 = exatoi(++s);
	} else {
		puts("to missing");
		return;
	}
	while (*s != ',' && *s != '\0')
		s++;
	if (*s) {
		count = exatoi(++s);
	} else {
		puts("count missing");
		return;
	}
	while (count--)
		putmem(a2++, getmem(a1++));
}

/*
 *	Port modify
 */
static void do_port(char *s)
{
	extern BYTE io_in(BYTE, BYTE);
	extern void io_out(BYTE, BYTE, BYTE);
	register BYTE port;
	static char nv[LENCMD];

	while (isspace((unsigned char) *s))
		s++;
	port = exatoi(s);
	printf("%02x = %02x : ", port, io_in(port, 0));
	if (fgets(nv, sizeof(nv), stdin) == NULL) {
		putchar('\n');
		if (isatty(fileno(stdin)))
			clearerr(stdin);
	} else if (isxdigit((unsigned char) *nv))
		io_out(port, 0, (BYTE) exatoi(nv));
}

/*
 *	definition of register types
 */
#define R_8	1		/* 8-bit register */
#define R_88	2		/* 8-bit register pair */
#define R_16	3		/* 16-bit register */
#define R_F	4		/* F or F' register */
#define R_M	5		/* status register flag mask */

/*
 *	register definitions table (must be sorted by name length)
 */
static const struct reg_def {
	const char *name;	/* register name */
	char len;		/* register name length */
	const char *prt;	/* printable register name */
	char z80;		/* Z80 only flag */
	char type;		/* register type */
	union {
		BYTE *r8;	/* 8-bit register pointer */
		struct {	/* 8-bit register pair pointers */
			BYTE *r8h;
			BYTE *r8l;
		};
		WORD *r16;	/* 16-bit register pointer */
		int *rf;	/* F or F' register pointer */
		BYTE rm;	/* status register flag mask */
	};
} regs[] = {
#ifndef EXCLUDE_Z80
	{ "bc'", 3, "BC'", 1, R_88, .r8h = &B_, .r8l = &C_ },
	{ "de'", 3, "DE'", 1, R_88, .r8h = &D_, .r8l = &E_ },
	{ "hl'", 3, "HL'", 1, R_88, .r8h = &H_, .r8l = &L_ },
#endif
	{ "pc",  2, "PC",  0, R_16, .r16 = &PC },
	{ "bc",  2, "BC",  0, R_88, .r8h = &B, .r8l = &C },
	{ "de",  2, "DE",  0, R_88, .r8h = &D, .r8l = &E },
	{ "hl",  2, "HL",  0, R_88, .r8h = &H, .r8l = &L },
#ifndef EXCLUDE_Z80
	{ "ix",  2, "IX",  1, R_16, .r16 = &IX },
	{ "iy",  2, "IY",  1, R_16, .r16 = &IY },
#endif
	{ "sp",  2, "SP",  0, R_16, .r16 = &SP },
	{ "fs",  2, "S",   0, R_M,  .rm = S_FLAG },
	{ "fz",  2, "Z",   0, R_M,  .rm = Z_FLAG },
	{ "fh",  2, "H",   0, R_M,  .rm = H_FLAG },
	{ "fp",  2, "P",   0, R_M,  .rm = P_FLAG },
#ifndef EXCLUDE_Z80
	{ "fn",  2, "N",   1, R_M,  .rm = N_FLAG },
#endif
	{ "fc",  2, "C",   0, R_M,  .rm = C_FLAG },
#ifndef EXCLUDE_Z80
	{ "a'",  2, "A'",  1, R_8,  .r8 = &A_ },
	{ "f'",  2, "F'",  1, R_F,  .rf = &F_ },
	{ "b'",  2, "B'",  1, R_8,  .r8 = &B_ },
	{ "c'",  2, "C'",  1, R_8,  .r8 = &C_ },
	{ "d'",  2, "D'",  1, R_8,  .r8 = &D_ },
	{ "e'",  2, "E'",  1, R_8,  .r8 = &E_ },
	{ "h'",  2, "H'",  1, R_8,  .r8 = &H_ },
	{ "l'",  2, "L'",  1, R_8,  .r8 = &L_ },
	{ "i",   1, "I",   1, R_8,  .r8 = &I },
#endif
	{ "a",   1, "A",   0, R_8,  .r8 = &A },
	{ "f",   1, "F",   0, R_F,  .rf = &F },
	{ "b",   1, "B",   0, R_8,  .r8 = &B },
	{ "c",   1, "C",   0, R_8,  .r8 = &C },
	{ "d",   1, "D",   0, R_8,  .r8 = &D },
	{ "e",   1, "E",   0, R_8,  .r8 = &E },
	{ "h",   1, "H",   0, R_8,  .r8 = &H },
	{ "l",   1, "L",   0, R_8,  .r8 = &L }
};
static int nregs = sizeof(regs) / sizeof(struct reg_def);

/*
 *	Register modify
 */
static void do_reg(char *s)
{
	register int i;
	register const struct reg_def *p;
	WORD w;
	static char nv[LENCMD];

	while (isspace((unsigned char) *s))
		s++;
	if (*s) {
		for (i = 0, p = regs; i < nregs; i++, p++) {
#ifndef EXCLUDE_Z80
			if (p->z80 && cpu != Z80)
				continue;
#endif
			if (strncmp(s, p->name, p->len) == 0)
				break;
		}
		if (i < nregs) {
			switch (p->type) {
			case R_8:
				printf("%s = %02x : ", p->prt, *(p->r8));
				break;
			case R_88:
				printf("%s = %04x : ", p->prt,
				       (*(p->r8h) << 8) + *(p->r8l));
				break;
			case R_16:
				printf("%s = %04x : ", p->prt, *(p->r16));
				break;
			case R_F:
				printf("%s = %02x : ", p->prt, *(p->rf));
				break;
			case R_M:
				printf("%s-FLAG = %c : ", p->prt,
				       (F & p->rm) ? '1' : '0');
				break;
			default:
				break;
			}
			if (fgets(nv, sizeof(nv), stdin) == NULL) {
				putchar('\n');
				if (isatty(fileno(stdin)))
					clearerr(stdin);
			} else if (nv[0] != '\n') {
				w = exatoi(nv);
				switch (p->type) {
				case R_8:
					*(p->r8) = w & 0xff;
					break;
				case R_88:
					*(p->r8h) = (w >> 8) & 0xff;
					*(p->r8l) = w & 0xff;
					break;
				case R_16:
					*(p->r16) = w;
					break;
				case R_F:
					*(p->rf) = w & 0xff;
					break;
				case R_M:
					F = w ? (F | p->rm) : (F & ~p->rm);
					break;
				default:
					break;
				}
			}
		} else
			printf("unknown register %s", s);
	}
	print_head();
	print_reg();
}

/*
 *	Output header for the CPU registers
 */
static void print_head(void)
{
	switch (cpu) {
#ifndef EXCLUDE_Z80
	case Z80:
		printf("\nPC   A  SZHPNC I  IFF BC   DE   HL   "
		       "A'F' B'C' D'E' H'L' IX   IY   SP\n");
		break;
#endif
#ifndef EXCLUDE_I8080
	case I8080:
		printf("\nPC   A  SZHPC BC   DE   HL   SP\n");
		break;
#endif
	default:
		break;
	}
}

/*
 *	Output all CPU registers
 */
static void print_reg(void)
{
	printf("%04x %02x ", PC, A);
	printf("%c", F & S_FLAG ? '1' : '0');
	printf("%c", F & Z_FLAG ? '1' : '0');
	printf("%c", F & H_FLAG ? '1' : '0');
	printf("%c", F & P_FLAG ? '1' : '0');
	switch (cpu) {
#ifndef EXCLUDE_Z80
	case Z80:
		printf("%c", F & N_FLAG ? '1' : '0');
		printf("%c", F & C_FLAG ? '1' : '0');
		printf(" %02x ", I);
		printf("%c", IFF & 1 ? '1' : '0');
		printf("%c", IFF & 2 ? '1' : '0');
		printf("  %02x%02x %02x%02x %02x%02x %02x%02x "
		       "%02x%02x %02x%02x %02x%02x %04x %04x %04x\n",
		       B, C, D, E, H, L, A_, F_,
		       B_, C_, D_, E_, H_, L_, IX, IY, SP);
		break;
#endif
#ifndef EXCLUDE_I8080
	case I8080:
		printf("%c", F & C_FLAG ? '1' : '0');
		printf(" %02x%02x %02x%02x %02x%02x %04x\n",
		       B, C, D, E, H, L, SP);
		break;
#endif
	default:
		break;
	}
}

/*
 *	Software breakpoints
 */
static void do_break(char *s)
{
#ifndef SBSIZE
	UNUSED(s);

	puts("Sorry, no breakpoints available");
	puts("Please recompile with SBSIZE defined in sim.h");
#else
	register int i;

	if (*s == '\n') {
		puts("No Addr Pass  Counter");
		for (i = 0; i < SBSIZE; i++)
			if (soft[i].sb_pass)
				printf("%02d %04x %05d %05d\n", i,
				       soft[i].sb_addr, soft[i].sb_pass,
				       soft[i].sb_passcount);
		return;
	}
	if (isxdigit((unsigned char) *s)) {
		i = atoi(s++);
		if (i >= SBSIZE) {
			printf("breakpoint %d not available\n", i);
			return;
		}
	} else {
		i = sb_next++;
		if (sb_next == SBSIZE)
			sb_next = 0;
	}
	while (isspace((unsigned char) *s))
		s++;
	if (*s == 'c') {
		putmem(soft[i].sb_addr, soft[i].sb_oldopc);
		memset((char *) &soft[i], 0, sizeof(struct softbreak));
		return;
	}
	if (soft[i].sb_pass)
		putmem(soft[i].sb_addr, soft[i].sb_oldopc);
	soft[i].sb_addr = exatoi(s);
	soft[i].sb_oldopc = getmem(soft[i].sb_addr);
	putmem(soft[i].sb_addr, 0x76);
	while (!iscntrl((unsigned char) *s) && !ispunct((unsigned char) *s))
		s++;
	if (*s != ',')
		soft[i].sb_pass = 1;
	else
		soft[i].sb_pass = exatoi(++s);
	soft[i].sb_passcount = 0;
#endif
}

/*
 *	History
 */
static void do_hist(char *s)
{
#ifndef HISIZE
	UNUSED(s);

	puts("Sorry, no history available");
	puts("Please recompile with HISIZE defined in sim.h");
#else
	int i, l, b, e, c, sa;

	while (isspace((unsigned char) *s))
		s++;
	switch (*s) {
	case 'c':
		memset((char *) his, 0, sizeof(struct history) * HISIZE);
		h_next = 0;
		h_flag = 0;
		break;
	default:
		if ((h_next == 0) && (h_flag == 0)) {
			puts("History memory is empty");
			break;
		}
		e = h_next;
		b = (h_flag) ? h_next + 1 : 0;
		l = 0;
		while (isspace((unsigned char) *s))
			s++;
		if (*s)
			sa = exatoi(s);
		else
			sa = -1;
		for (i = b; i != e; i++) {
			if (i == HISIZE)
				i = 0;
			if (sa != -1) {
				if (his[i].h_addr < sa)
					continue;
				else
					sa = -1;
			}
			switch (cpu) {
#ifndef EXCLUDE_Z80
			case Z80:
				printf("%04x AF=%04x BC=%04x DE=%04x HL=%04x "
				       "IX=%04x IY=%04x SP=%04x\n",
				       his[i].h_addr, his[i].h_af, his[i].h_bc,
				       his[i].h_de, his[i].h_hl, his[i].h_ix,
				       his[i].h_iy, his[i].h_sp);
				break;
#endif
#ifndef EXCLUDE_I8080
			case I8080:
				printf("%04x AF=%04x BC=%04x DE=%04x HL=%04x "
				       "SP=%04x\n",
				       his[i].h_addr, his[i].h_af, his[i].h_bc,
				       his[i].h_de, his[i].h_hl, his[i].h_sp);
				break;
#endif
			default:
				break;
			}
			l++;
			if (l == 20) {
				l = 0;
				printf("q = quit, else continue: ");
				c = getkey();
				putchar('\n');
				if (toupper((unsigned char) c) == 'Q')
					break;
			}
		}
		break;
	}
#endif
}

/*
 *	Runtime measurement by counting the executed T states
 */
static void do_count(char *s)
{
#ifndef WANT_TIM
	UNUSED(s);

	puts("Sorry, no t-state count available");
	puts("Please recompile with WANT_TIM defined in sim.h");
#else
	while (isspace((unsigned char) *s))
		s++;
	if (*s == '\0') {
		puts("start  stop  status  T-states");
		printf("%04x   %04x    %s   %lu\n",
		       t_start, t_end,
		       t_flag ? "on " : "off", t_states);
	} else {
		t_start = exatoi(s);
		while (*s != ',' && *s != '\0')
			s++;
		if (*s)
			t_end = exatoi(++s);
		t_states = 0L;
		t_flag = 0;
	}
#endif
}

/*
 *	Calculate the clock frequency of the emulated CPU:
 *	into memory locations 0000H to 0002H the following
 *	code will be stored:
 *		LOOP: JP LOOP
 *	It uses 10 T states for each execution. A 3 second
 *	timer is started and then the CPU. For every opcode
 *	fetch the R register is incremented by one and after
 *	the timer is down and stops the emulation, the clock
 *	speed of the CPU is calculated with:
 *		f = R / 300000
 */
static void do_clock(void)
{
	BYTE save[3];
	static struct sigaction newact;
	static struct itimerval tim;

	save[0] = getmem(0x0000);	/* save memory locations */
	save[1] = getmem(0x0001);	/* 0000H - 0002H */
	save[2] = getmem(0x0002);
	putmem(0x0000, 0xc3);		/* store opcode JP 0000H at address */
	putmem(0x0001, 0x00);		/* 0000H */
	putmem(0x0002, 0x00);
	PC = 0;				/* set PC to this code */
	R = 0L;				/* clear refresh register */
	newact.sa_handler = timeout;	/* set timer interrupt handler */
	sigemptyset(&newact.sa_mask);
	newact.sa_flags = 0;
	sigaction(SIGALRM, &newact, NULL);
	tim.it_value.tv_sec = 3;	/* start 3 second timer */
	tim.it_value.tv_usec = 0;
	tim.it_interval.tv_sec = 0;
	tim.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &tim, NULL);
	run_cpu();			/* start CPU */
	newact.sa_handler = SIG_DFL;	/* reset timer interrupt handler */
	sigaction(SIGALRM, &newact, NULL);
	putmem(0x0000, save[0]);	/* restore memory locations */
	putmem(0x0001, save[1]);	/* 0000H - 0002H */
	putmem(0x0002, save[2]);
	if (cpu_error == NONE) {
		printf("CPU executed %ld %s instructions in 3 seconds\n",
		       R, cpu == Z80 ? "JP" : "JMP");
		printf("clock frequency = %5.2f Mhz\n",
		       ((float) R) / 300000.0);
	} else
		puts("Interrupted by user");
}

/*
 *	This function is the signal handler for the timer interrupt.
 *	The CPU emulation is stopped here.
 */
static void timeout(int sig)
{
	UNUSED(sig);

	cpu_state = STOPPED;
}

/*
 *	Output information about compiling options
 */
static void do_show(void)
{
	register int i;

	printf("Release: %s\n", RELEASE);
#ifdef HISIZE
	i = HISIZE;
#else
	i = 0;
#endif
	printf("No. of entries in history memory: %d\n", i);
#ifdef SBSIZE
	i = SBSIZE;
#else
	i = 0;
#endif
	printf("No. of software breakpoints: %d\n", i);
#ifdef UNDOC_INST
	i = u_flag;
#else
	i = 1;
#endif
	printf("Undocumented op-codes %sexecuted\n", i ? "not " : "");
#ifdef WANT_TIM
	i = 1;
#else
	i = 0;
#endif
	printf("T-State counting %spossible\n", i ? "" : "im");
}

/*
 *	Load a file into the memory of the emulated CPU
 */
static void do_load(char *s)
{
	static char fn[MAX_LFN];
	char *pfn = fn;

	while (isspace((unsigned char) *s))
		s++;
	while (*s != ',' && *s != '\n' && *s != '\0')
		*pfn++ = *s++;
	*pfn = '\0';
	if (*s == ',')
		load_file(fn, exatoi(++s), -1);
	else
		load_file(fn, 0, 0);
	wrk_addr = PC;
}

/*
 *	Call system function from simulator
 */
static void do_unix(char *s)
{
	int_off();
	if (system(s) == -1)
		perror("external command");
	int_on();
}

/*
 *	Output help text
 */
static void do_help(void)
{
	puts("r filename[,address]      read object into memory");
	puts("d [address]               dump memory");
	puts("l [address]               list memory");
	puts("m [address]               modify memory");
	puts("f address,count,value     fill memory");
	puts("v from,to,count           move memory");
	puts("p address                 show/modify port");
	puts("g [address]               run program");
	puts("t [count]                 trace program");
	puts("return                    single step program");
	puts("x [register]              show/modify register");
	puts("x f<flag>                 modify flag");
	puts("b[no] address[,pass]      set soft breakpoint");
	puts("b                         show soft breakpoints");
	puts("b[no] c                   clear soft breakpoint");
	puts("h [address]               show history");
	puts("h c                       clear history");
	puts("z start,stop              set trigger addr for t-state count");
	puts("z                         show t-state count");
	puts("c                         measure clock frequency");
	puts("s                         show settings");
	puts("! command                 execute external command");
	if (ice_cust_help)
		(*ice_cust_help)();
	puts("q                         quit");
}
