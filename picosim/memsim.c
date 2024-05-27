/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk
 *
 * This module implements the memory for the Z80/8080 CPU
 *
 * History:
 * 23-APR-2024 derived from z80sim
 * 27-MAY-2024 implemented load file
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "f_util.h"
#include "ff.h"
#include "sim.h"
#include "simglb.h"
#include "memsim.h"

//extern sd_card_t *SD;
extern FIL sd_file;
extern FRESULT sd_res;

/* 64KB non banked memory */
#define MEMSIZE 65536
unsigned char code[MEMSIZE];

void init_memory(void)
{
	register int i;

	// fill top page of memory with 0xff, write protected ROM
	for (i = 0xff00; i <= 0xffff; i++)
		code[i] = 0xff;
}

void complain(void)
{
	printf("File not found\n");
}

/*
 * load a file 'name' into memory
 */
void load_file(char *name)
{
	int i = 0;
	unsigned int br;
	unsigned char c;
	char SFN[25];

	strcpy(SFN, "/CODE80/");
	strcat(SFN, name);
	strcat(SFN, ".BIN");

	/* try to open file */
	sd_res = f_open(&sd_file, SFN, FA_READ);
	if (sd_res != FR_OK) {
		complain();
		return;
	}

	/* read file into memory */
	while ((sd_res = f_read(&sd_file, &code[i], 128, &br)) == FR_OK) {
		if (br < 128)	/* last record reached */
			break;
		i += 128;
	}
	if (sd_res != FR_OK)
		printf("f_read error: %s (%d)\n", FRESULT_str(sd_res), sd_res);

	f_close(&sd_file);
}

/*
 * read from drive a sector on track into FRAM addr
 * dummy, need to get SD working first
 */
BYTE read_sec(int drive, int track, int sector, WORD addr)
{
}

/*
 * write to drive a sector on track from FRAM addr
 * dummy, need to get SD working first
 */
BYTE write_sec(int drive, int track, int sector, WORD addr)
{
}

/*
 * get FDC command from FRAM
 * dummy, need to get SD working first
 */
void get_fdccmd(BYTE *cmd, WORD addr)
{
}
