/*
 * FRUA memory allocation — lifted from CODE 3 + 0x36bc (jump-table entry 387).
 *
 * Original 68k:
 *     link    a6,#0
 *     moveq   #0,d0
 *     move.w  8(a6),d0        ; d0 = (zero-extended) 16-bit byte count
 *     clr.l   -(sp)           ; Pascal-convention result slot
 *     move.l  d0,-(sp)        ; widened size
 *     jsr     JT[1028](a5)    ; THINK C _NewPtr trap glue
 *     move.l  (sp)+,a0        ; result pointer
 *     unlk    a6
 *     rts
 *
 * The engine's allocation wrapper: it widens a 16-bit byte count and calls
 * the Memory Manager. The original reached _NewPtr through THINK C's trap
 * glue (JT[1028]); that glue is pure compiler runtime and collapses away —
 * here the call goes straight to the compat/ Memory Manager shim.
 */

#include "alloc.h"
#include "macmemory.h"

void *ua_alloc(unsigned short size)
{
	return NewPtr((Size)size);
}
