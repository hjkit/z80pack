/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk
 *
 * This is the configuration for a Raspberry Pico (W) board
 */

#define PICO_W		/* board we use, comment for Pico */

#define DEF_CPU Z80	/* default CPU (Z80 or I8080) */
#define CPU_SPEED 0	/* default CPU speed 0=unlimited */
#define UNDOC_INST	/* compile undocumented instructions */
/*#define CORE_LOG*/	/* don't use LOG() logging in core simulator */
#define EXCLUDE_I8080	/* don't include 8080 emulation support */

extern void sleep_ms();
#define SLEEP_MS(t)	sleep_ms(t)

#define USR_COM "Raspberry Pi Pico Z80 Simulation"
#define USR_REL "1.0"
#define USR_CPR "Copyright (C) 2024 by Udo Munk"
 
#include "simcore.h"
