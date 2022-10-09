/*
 *	Z80 - Assembler
 *	Copyright (C) 1987-2022 by Udo Munk
 *	Copyright (C) 2022 by Thomas Eberhardt
 *
 *	History:
 *	17-SEP-1987 Development under Digital Research CP/M 2.2
 *	28-JUN-1988 Switched to Unix System V.3
 *	22-OCT-2006 changed to ANSI C for modern POSIX OS's
 *	03-FEB-2007 more ANSI C conformance and reduced compiler warnings
 *	18-MAR-2007 use default output file extension dependent on format
 *	04-OCT-2008 fixed comment bug, ';' string argument now working
 *	22-FEB-2014 fixed is...() compiler warnings
 *	13-JAN-2016 fixed buffer overflow, new expression parser from Didier
 *	02-OCT-2017 bug fixes in expression parser from Didier
 *	28-OCT-2017 added variable symbol length and other improvements
 *	15-MAY-2018 mark unreferenced symbols in listing
 *	30-JUL-2021 fix verbose option
 *	28-JAN-2022 added syntax check for OUT (n),A
 *	24-SEP-2022 added undocumented Z80 instructions and 8080 mode (TE)
 *	04-OCT-2022 new expression parser (TE)
 */

/*
 *	processing of all PSEUDO ops
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "z80a.h"
#include "z80aglb.h"

/* z80amain.c */
extern void fatal(int, char *);
extern void p1_file(char *);
extern void p2_file(char *);

/* z80anum.c */
extern int eval(char *);
extern int chk_byte(int);

/* z80aout.c */
extern void asmerr(int);
extern void lst_header(void);
extern void lst_attl(void);
extern void lst_line(int, int);
extern void obj_org(int);
extern void obj_fill(int);
extern void obj_fill_value(int, int);

/* z80atab.c */
extern struct sym *get_sym(char *);
extern int put_sym(char *, int);

/*
 *	.8080 and .Z80
 */
int op_opset(int op_code, int dummy)
{
	UNUSED(dummy);

	ad_mode = AD_NONE;
	switch (op_code) {
	case 1:				/* .8080 */
		opset = OPSET_8080;
		break;
	case 2:				/* .Z80 */
		opset = OPSET_Z80;
		break;
	default:
		fatal(F_INTERN, "invalid opcode for function op_opset");
	}
	return(0);
}

/*
 *	ORG
 */
int op_org(int dummy1, int dummy2)
{
	register int i;

	UNUSED(dummy1);
	UNUSED(dummy2);

	if (phs_flag) {
		asmerr(E_ORGPHS);
		return(0);
	}
	i = eval(operand);
	if (pass == 1) {		/* PASS 1 */
		if (!load_flag) {
			load_addr = i;
			load_flag = 1;
		}
	} else {			/* PASS 2 */
		obj_org(i);
		ad_mode = AD_NONE;
	}
	rpc = pc = i;
	return(0);
}

/*
 *	.PHASE
 */
int op_phase(int dummy1, int dummy2)
{
	UNUSED(dummy1);
	UNUSED(dummy2);

	if (phs_flag)
		asmerr(E_PHSNEST);
	else {
		phs_flag = 1;
		pc = eval(operand);
		ad_mode = AD_NONE;
	}
	return(0);
}

/*
 *	.DEPHASE
 */
int op_dephase(int dummy1, int dummy2)
{
	UNUSED(dummy1);
	UNUSED(dummy2);

	if (!phs_flag)
		asmerr(E_MISPHS);
	else {
		phs_flag = 0;
		pc = rpc;
		ad_mode = AD_NONE;
	}
	return(0);
}

/*
 *	.RADIX
 */
int op_radix(int dummy1, int dummy2)
{
	int i;

	UNUSED(dummy1);
	UNUSED(dummy2);

	i = eval(operand);
	if (i < 2 || i > 16)
		asmerr(E_VALOUT);
	else
		radix = i;
	ad_mode = AD_NONE;
	return(0);
}

/*
 *	EQU
 */
int op_equ(int dummy1, int dummy2)
{
	UNUSED(dummy1);
	UNUSED(dummy2);

	if (pass == 1) {		/* PASS 1 */
		if (get_sym(label) == NULL) {
			ad_addr = eval(operand);
			if (put_sym(label, ad_addr))
				fatal(F_OUTMEM, "symbols");
		} else
			asmerr(E_MULSYM);
	} else {			/* PASS 2 */
		ad_mode = AD_ADDR;
		ad_addr = eval(operand);
	}
	return(0);
}

/*
 *	DEFL and ASET and for 8080 SET
 */
int op_dl(int dummy1, int dummy2)
{
	UNUSED(dummy1);
	UNUSED(dummy2);

	ad_mode = AD_ADDR;
	ad_addr = eval(operand);
	if (put_sym(label, ad_addr))
		fatal(F_OUTMEM, "symbols");
	return(0);
}

/*
 *	DEFS and DS
 */
int op_ds(int dummy1, int dummy2)
{
	register char *p, *p1, *p2;
	register int count, value;

	UNUSED(dummy1);
	UNUSED(dummy2);

	p = operand;
	if (!*p)
		asmerr(E_MISOPE);
	else {
		ad_addr = pc;
		ad_mode = AD_ADDR;
		if ((p1 = strchr(operand, ','))) {
			p2 = tmp;
			while (*p != ',')
				*p2++ = *p++;
			*p2 = '\0';
			count = eval(tmp);
			if (pass == 2) {
				value = eval(p1 + 1);
				obj_fill_value(count, value);
			}
		} else {
			count = eval(operand);
			if (pass == 2)
				obj_fill(count);
		}
		pc += count;
		rpc += count;
	}
	return(0);
}

/*
 *	DEFB, DB, DEFM, DEFC, DC, DEFZ
 */
int op_db(int op_code, int dummy)
{
	register int i, j;
	register char *p;
	register char *s;

	UNUSED(dummy);

	i = 0;
	p = operand;
	while (*p) {
		j = 0;
		if (*p == STRDEL || *p == STRDEL2) {
			s = p + 1;
			while (1) {
				/* check for double delim */
				if ((*s == *p) && (*++s != *p))
					break;
				if (*s == '\n' || *s == '\0') {
					asmerr(E_MISDEL);
					goto delim_error;
				}
				ops[i++] = *s++;
				if (i >= OPCARRAY)
				    fatal(F_INTERN, "op-code buffer overflow");
				j++;
			}
			/* reset ops and evaluate if not followed by a comma */
			if ((*s != ',') && (*s != '\0')) {
				i -= j;
				j = 0;
			}
			else {
				p = s;
				j = 1;
			}
		}
		if (j == 0) {
			s = tmp;
			while (*p != ',' && *p != '\0')
				*s++ = *p++;
			*s = '\0';
			if (s != tmp) {
				if (pass == 2)
					ops[i] = chk_byte(eval(tmp));
				if (++i >= OPCARRAY)
				    fatal(F_INTERN, "op-code buffer overflow");
			}
		}
		if (*p == ',')
			p++;
	}
	switch (op_code) {
	case 1:				/* DEFB, DB, DEFM */
		break;
	case 2:				/* DEFC, DC */
		if (i)
			ops[i - 1] |= 0x80;
		break;
	case 3:				/* DEFZ */
		ops[i++] = '\0';
		if (i >= OPCARRAY)
			fatal(F_INTERN, "op-code buffer overflow");
		break;
	default:
		fatal(F_INTERN, "invalid opcode for function op_db");
	}
delim_error:
	return(i);
}

/*
 *	DEFW and DW
 */
int op_dw(int dummy1, int dummy2)
{
	register int i, temp;
	register char *p;
	register char *s;

	UNUSED(dummy1);
	UNUSED(dummy2);

	p = operand;
	i = 0;
	while (*p) {
		s = tmp;
		while (*p != ',' && *p != '\0')
			*s++ = *p++;
		*s = '\0';
		if (s != tmp) {
			if (pass == 2) {
				temp = eval(tmp);
				ops[i] = temp & 0xff;
				ops[i + 1] = temp >> 8;
			}
			i += 2;
			if (i >= OPCARRAY)
				fatal(F_INTERN, "op-code buffer overflow");
		}
		if (*p == ',')
			p++;
	}
	return(i);
}

/*
 *	EJECT, LIST, NOLIST, PAGE, PRINT, TITLE, INCLUDE
 */
int op_misc(int op_code, int dummy)
{
	register char *p, *d, *s;
	static char fn[LENFN];
	static int incnest;
	static struct inc incl[INCNEST];

	UNUSED(dummy);

	ad_mode = AD_NONE;
	switch(op_code) {
	case 1:				/* EJECT */
		if (pass == 2)
			p_line = ppl;
		break;
	case 2:				/* LIST */
		if (pass == 2)
			list_flag = 1;
		break;
	case 3:				/* NOLIST */
		if (pass == 2)
			list_flag = 0;
		break;
	case 4:				/* PAGE */
		if (pass == 2)
			ppl = eval(operand);
		break;
	case 5:				/* PRINT */
		if (pass == 1) {
			p = operand;
			if (*p == STRDEL || *p == STRDEL2) {
				p++;
				while (1) {
					/* check for double delim */
					if ((*p == *operand)
					    && (*++p != *operand))
						break;
					if (*p == '\0') {
						putchar('\n');
						asmerr(E_MISDEL);
						return(0);
					}
					putchar(*p++);
				}
			}
			else
				fputs(operand, stdout);
			putchar('\n');
		}
		break;
	case 6:				/* INCLUDE */
		if (incnest >= INCNEST) {
			asmerr(E_INCNEST);
			break;
		}
		incl[incnest].inc_line = c_line;
		incl[incnest].inc_fn = srcfn;
		incl[incnest].inc_fp = srcfp;
		incnest++;
		p = line;
		d = fn;
		while(isspace((unsigned char) *p))
			p++;	/* ignore white space until INCLUDE */
		while(!isspace((unsigned char) *p))
			p++;	/* ignore INCLUDE */
		while(isspace((unsigned char) *p))
			p++;	/* ignore white space until filename */
		while(!isspace((unsigned char) *p) && *p != COMMENT)
				/* get filename */
			*d++ = *p++;
		*d = '\0';
		if (pass == 1) {	/* PASS 1 */
			if (ver_flag)
				printf("   Include %s\n", fn);
			p1_file(fn);
		} else {		/* PASS 2 */
			ad_mode = AD_NONE;
			lst_line(0, 0);
			if (ver_flag)
				printf("   Include %s\n", fn);
			p2_file(fn);
		}
		incnest--;
		c_line = incl[incnest].inc_line;
		srcfn = incl[incnest].inc_fn;
		srcfp = incl[incnest].inc_fp;
		if (ver_flag)
			printf("   Resume  %s\n", srcfn);
		if (list_flag && (pass == 2)) {
			lst_header();
			lst_attl();
		}
		ad_mode = AD_SUPPR;
		break;
	case 7:				/* TITLE */
		if (pass == 2) {
			p = line;
			d = title;
			while (isspace((unsigned char) *p))
				p++;	/* ignore white space until TITLE */
			while (!isspace((unsigned char) *p))
				p++;	/* ignore TITLE */
			while (isspace((unsigned char) *p))
				p++;	/* ignore white space until text */
			if (*p == STRDEL || *p == STRDEL2) {
				s = p + 1;
				while (1) {
					/* check for double delim */
					if ((*s == *p) && (*++s != *p))
						break;
					if (*s == '\n') {
						asmerr(E_MISDEL);
						break;
					}
					*d++ = *s++;
				}
			}
			else
				while (*p != '\n' && *p != COMMENT)
					*d++ = *p++;
			*d = '\0';
		}
		break;
	default:
		fatal(F_INTERN, "invalid opcode for function op_misc");
		break;
	}
	return(0);
}

/*
 *	IFDEF, IFNDEF, IFEQ, IFNEQ, COND, IF, IFT, IFE, IFF, ELSE, ENDIF, ENDC
 */
int op_cond(int op_code, int dummy)
{
	register char *p, *p1, *p2;
	static int condnest[IFNEST];

	UNUSED(dummy);

	switch(op_code) {
	case 1:				/* IFDEF */
		if (iflevel >= IFNEST) {
			asmerr(E_IFNEST);
			break;
		}
		condnest[iflevel++] = gencode;
		if (gencode)
			if (get_sym(operand) == NULL)
				gencode = 0;
		break;
	case 2:				/* IFNDEF */
		if (iflevel >= IFNEST) {
			asmerr(E_IFNEST);
			break;
		}
		condnest[iflevel++] = gencode;
		if (gencode)
			if (get_sym(operand) != NULL)
				gencode = 0;
		break;
	case 3:				/* IFEQ */
		if (iflevel >= IFNEST) {
			asmerr(E_IFNEST);
			break;
		}
		condnest[iflevel++] = gencode;
		p = operand;
		if (!*p || !(p1 = strchr(operand, ','))) {
			asmerr(E_MISOPE);
			break;
		}
		if (gencode) {
			p2 = tmp;
			while (*p != ',')
				*p2++ = *p++;
			*p2 = '\0';
			if (eval(tmp) != eval(++p1))
				gencode = 0;
		}
		break;
	case 4:				/* IFNEQ */
		if (iflevel >= IFNEST) {
			asmerr(E_IFNEST);
			break;
		}
		condnest[iflevel++] = gencode;
		p = operand;
		if (!*p || !(p1 = strchr(operand, ','))) {
			asmerr(E_MISOPE);
			break;
		}
		if (gencode) {
			p2 = tmp;
			while (*p != ',')
				*p2++ = *p++;
			*p2 = '\0';
			if (eval(tmp) == eval(++p1))
				gencode = 0;
		}
		break;
	case 5:				/* COND, IF, and IFT */
		if (iflevel >= IFNEST) {
			asmerr(E_IFNEST);
			break;
		}
		condnest[iflevel++] = gencode;
		if (gencode)
			if (eval(operand) == 0)
				gencode = 0;
		break;
	case 6:				/* IFE and IFF */
		if (iflevel >= IFNEST) {
			asmerr(E_IFNEST);
			break;
		}
		condnest[iflevel++] = gencode;
		if (gencode)
			if (eval(operand) != 0)
				gencode = 0;
		break;
	case 98:			/* ELSE */
		if (!iflevel)
			asmerr(E_MISIFF);
		else
			if ((iflevel == 0) || (condnest[iflevel - 1] == 1))
				gencode = !gencode;
		break;
	case 99:			/* ENDIF and ENDC */
		if (!iflevel)
			asmerr(E_MISIFF);
		else
			gencode = condnest[--iflevel];
		break;
	default:
		fatal(F_INTERN, "invalid opcode for function op_cond");
		break;
	}
	ad_mode = AD_NONE;
	return(0);
}

/*
 *	EXTRN, EXTERNAL, EXT and PUBLIC, ENT, ENTRY, GLOBAL
 */
int op_glob(int op_code, int dummy)
{
	UNUSED(dummy);

	ad_mode = AD_NONE;
	switch(op_code) {
	case 1:				/* EXTRN, EXTERNAL, EXT */
		break;
	case 2:				/* PUBLIC, ENT, ENTRY, GLOBAL */
		break;
	default:
		fatal(F_INTERN, "invalid opcode for function op_glob");
		break;
	}
	return(0);
}

/*
 *	END
 */
int op_end(int dummy1, int dummy2)
{
	UNUSED(dummy1);
	UNUSED(dummy2);

	if (pass == 2 && *operand)
		start_addr = eval(operand);
	return(0);
}
