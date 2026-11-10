/*
 * s16-pi - the 68000's state for save states: the part of Musashi's context
 * that is plain data (registers, flags, interrupt and stop state), without the
 * pointers that follow it.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "m68kcpu.h"
#include <stddef.h>
#include <string.h>

#define PLAIN	offsetof (m68ki_cpu_core, cyc_instruction)

size_t s16_m68k_state_size (void)		{ return PLAIN; }
void s16_m68k_state_save (void *pDst)		{ memcpy (pDst, &m68ki_cpu, PLAIN); }
void s16_m68k_state_load (const void *pSrc)	{ memcpy (&m68ki_cpu, pSrc, PLAIN); }
