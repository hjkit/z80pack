/*
 *	Z80 - Macro - Assembler
 *	Copyright (C) 1987-2022 by Udo Munk
 *	Copyright (C) 2022 by Thomas Eberhardt
 *
 *	History:
 *	17-SEP-1987 Development under Digital Research CP/M 2.2
 *	28-JUN-1988 Switched to Unix System V.3
 *	21-OCT-2006 changed to ANSI C for modern POSIX OS's
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
 *	25-OCT-2022 Intel-like macros (TE)
 */

/*
 *	this module contains all global variables other
 *	than CPU specific tables
 */

#include <stdio.h>
#include "z80a.h"

char *infiles[MAXFN],		/* source filenames */
     *srcfn,			/* filename of current processed source file */
     objfn[LENFN + 1],		/* object filename */
     lstfn[LENFN + 1],		/* listing filename */
     line[MAXLINE + 2],		/* buffer for one line of source */
     label[MAXLINE + 1],	/* buffer for label */
     opcode[MAXLINE + 1],	/* buffer for opcode */
     operand[MAXLINE + 1],	/* buffer for working with operand */
     title[MAXLINE + 1];	/* buffer for title of source */

BYTE ops[OPCARRAY],		/* buffer for generated object code */
     ctype[256];		/* table for character classification */

WORD rpc,			/* real program counter */
     pc,			/* logical program counter, normally equal */
				/* to rpc, except when inside a .PHASE block */
     a_addr,			/* output value for A_ADDR/A_VALUE mode */
     load_addr,			/* load address of program */
     start_addr,		/* start address of program */
     hexlen = MAXHEX;		/* hex record length */

int  list_flag,			/* flag for option -l */
     sym_flag,			/* flag for option -s */
     undoc_flag,		/* flag for option -u */
     ver_flag,			/* flag for option -v */
     nofill_flag,		/* flag for option -x */
     upcase_flag,		/* flag for option -U */
     mac_list_flag,		/* flag for option -m */
     i8080_flag,		/* flag for option -8 */
     radix,			/* current radix, set to 10 at start of pass */
     phs_flag,			/* flag for being inside a .PHASE block */
     pass,			/* processed pass */
     iflevel,			/* IF nesting level */
     act_iflevel,		/* active IF nesting level */
     act_elselevel,		/* active ELSE nesting level */
     gencode = 1,		/* flag for conditional code */
     nofalselist,		/* flag for false conditional listing */
     mac_def_nest,		/* macro definition nesting level */
     mac_exp_nest,		/* macro expansion nesting level */
     mac_symmax,		/* max. macro symbol length encountered */
     errors,			/* error counter */
     errnum,			/* error number in pass 2 */
     a_mode,			/* address output mode for pseudo ops */
     load_flag,			/* flag for load_addr valid */
     obj_fmt = OBJ_HEX,		/* format of object file (default Intel hex) */
     symlen = SYMLEN,		/* significant characters in symbols */
     symmax,			/* max. symbol name length encountered */
     p_line,			/* no. printed lines on page (can be < 0) */
     ppl = PLENGTH,		/* page length */
     page;			/* no. of pages for listing */

unsigned long
     c_line;			/* current line no. in current source */

FILE *srcfp,			/* file pointer for current source */
     *objfp,			/* file pointer for object code */
     *lstfp,			/* file pointer for listing */
     *errfp;			/* file pointer for error output */
