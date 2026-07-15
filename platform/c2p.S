| Fast chunky-to-planar for the Falcon 8-plane (256-colour) mode, 68030.
|
| void c2p_group_asm(const unsigned long *src, unsigned short *dst);
|   src: 4 longs = 16 chunky pixels (pixel 0 in the MSB of src[0]).
|   dst: 8 plane words (big-endian); plane b bit (15-p) = src pixel p bit b.
|
| A hand translation of the host-verified C in display_videl.c (the
| transpose8rS32 bit-transpose + the plane-word pack). It beats the
| compiler on two counts: it cycles output bytes to the LSB with
| `ror.l #8` (one fast rotate) instead of the 16/24-bit shifts gcc emits
| for byte extraction, and it keeps all four transposed longs in
| registers. videl_init self-tests it against the C reference and only
| uses it if they agree, so a mistake here degrades to the C path rather
| than corrupting the screen.

	.text
	.even
	.globl	_c2p_group_asm

	| 8x8 bit-matrix transpose of the byte-rows in \x (bytes 0..3) and
	| \y (bytes 4..7), in place; \t and \u are scratch. (>>14 / <<14 are
	| done as two #7 shifts since the immediate form maxes at 8.)
	.macro	TR8 x, y, t, u
	move.l	\x,\t
	lsr.l	#7,\t
	eor.l	\x,\t
	and.l	#0x00AA00AA,\t
	eor.l	\t,\x
	lsl.l	#7,\t
	eor.l	\t,\x
	move.l	\y,\t
	lsr.l	#7,\t
	eor.l	\y,\t
	and.l	#0x00AA00AA,\t
	eor.l	\t,\y
	lsl.l	#7,\t
	eor.l	\t,\y
	move.l	\x,\t
	lsr.l	#7,\t
	lsr.l	#7,\t
	eor.l	\x,\t
	and.l	#0x0000CCCC,\t
	eor.l	\t,\x
	lsl.l	#7,\t
	lsl.l	#7,\t
	eor.l	\t,\x
	move.l	\y,\t
	lsr.l	#7,\t
	lsr.l	#7,\t
	eor.l	\y,\t
	and.l	#0x0000CCCC,\t
	eor.l	\t,\y
	lsl.l	#7,\t
	lsl.l	#7,\t
	eor.l	\t,\y
	move.l	\x,\t
	and.l	#0xF0F0F0F0,\t
	move.l	\y,\u
	lsr.l	#4,\u
	and.l	#0x0F0F0F0F,\u
	or.l	\u,\t
	move.l	\x,\u
	lsl.l	#4,\u
	and.l	#0xF0F0F0F0,\u
	and.l	#0x0F0F0F0F,\y
	or.l	\u,\y
	move.l	\t,\x
	.endm

_c2p_group_asm:
	move.l	4(%sp),%a0		| src (4 longs)
	move.l	8(%sp),%a1		| dst (8 words)
	movem.l	%d2-%d5,-(%sp)		| preserve callee-saved scratch
	move.l	(%a0)+,%d0		| d0 = hx (pixels 0..3)
	move.l	(%a0)+,%d1		| d1 = hy (pixels 4..7)
	move.l	(%a0)+,%d2		| d2 = lx (pixels 8..11)
	move.l	(%a0)+,%d3		| d3 = ly (pixels 12..15)
	TR8	%d0,%d1,%d4,%d5		| transpose the high half
	TR8	%d2,%d3,%d4,%d5		| transpose the low half
	| pack plane words. dst bytes (big-endian): hi byte then lo byte,
	| plane 0 (= pixels' bit 7 row) first. hi[4..7] live in hy (d1),
	| hi[0..3] in hx (d0); lo likewise in ly (d3) / lx (d2). ror.l #8
	| walks each long's LSB through b3,b2,b1,b0.
	move.b	%d1,(%a1)		| hi[7]
	move.b	%d3,1(%a1)		| lo[7]
	ror.l	#8,%d1
	ror.l	#8,%d3
	move.b	%d1,2(%a1)		| hi[6]
	move.b	%d3,3(%a1)		| lo[6]
	ror.l	#8,%d1
	ror.l	#8,%d3
	move.b	%d1,4(%a1)		| hi[5]
	move.b	%d3,5(%a1)		| lo[5]
	ror.l	#8,%d1
	ror.l	#8,%d3
	move.b	%d1,6(%a1)		| hi[4]
	move.b	%d3,7(%a1)		| lo[4]
	move.b	%d0,8(%a1)		| hi[3]
	move.b	%d2,9(%a1)		| lo[3]
	ror.l	#8,%d0
	ror.l	#8,%d2
	move.b	%d0,10(%a1)		| hi[2]
	move.b	%d2,11(%a1)		| lo[2]
	ror.l	#8,%d0
	ror.l	#8,%d2
	move.b	%d0,12(%a1)		| hi[1]
	move.b	%d2,13(%a1)		| lo[1]
	ror.l	#8,%d0
	ror.l	#8,%d2
	move.b	%d0,14(%a1)		| hi[0]
	move.b	%d2,15(%a1)		| lo[0]
	movem.l	(%sp)+,%d2-%d5
	rts

| --------------------------------------------------------------------------
| Fast 8->16 LUT blit for the 16bpp TrueColor present path, 68030.
|
| void lutblit_span(const unsigned char *src, unsigned short *dst,
|                   const unsigned short *lut, long count);
|   src:   4-aligned chunky bytes (one palette index per pixel).
|   dst:   16bpp screen words.
|   lut:   256-entry RGB565 table (one word per index).
|   count: pixel count, a multiple of 4.
|
| Reads a longword (4 pixels, pixel 0 in the MSB), cycles each byte to the
| LSB with `rol.l #8`, indexes the LUT with the 030 scaled index
| (a2,d2.w*2), and writes one word per pixel. videl_init self-tests this
| against the C reference and only uses it if they agree.
	.text
	.even
	.globl	_lutblit_span
_lutblit_span:
	movem.l	%d2/%a2,-(%sp)		| callee-saved
	move.l	12(%sp),%a0		| src
	move.l	16(%sp),%a1		| dst
	move.l	20(%sp),%a2		| lut
	move.l	24(%sp),%d0		| count (pixels)
	lsr.l	#2,%d0			| groups of 4 pixels
	beq.s	2f
	subq.l	#1,%d0			| dbra bias
	moveq	#0,%d2			| keep the upper bytes zero
1:
	move.l	(%a0)+,%d1		| p0 p1 p2 p3  (p0 = MSB)
	rol.l	#8,%d1			| .b = p0
	move.b	%d1,%d2
	move.w	0(%a2,%d2.w*2),(%a1)+	| lut[p0]
	rol.l	#8,%d1			| .b = p1
	move.b	%d1,%d2
	move.w	0(%a2,%d2.w*2),(%a1)+	| lut[p1]
	rol.l	#8,%d1			| .b = p2
	move.b	%d1,%d2
	move.w	0(%a2,%d2.w*2),(%a1)+	| lut[p2]
	rol.l	#8,%d1			| .b = p3
	move.b	%d1,%d2
	move.w	0(%a2,%d2.w*2),(%a1)+	| lut[p3]
	dbra	%d0,1b
2:
	movem.l	(%sp)+,%d2/%a2
	rts

| --------------------------------------------------------------------------
| ATARI-ONLY from here down: the TOS vblqueue trampolines. They name Falcon
| backend handlers (vbl_flip_handler lives in display_videl.c), so they must
| not assemble for other machines — the Amiga hooks its vertical blank
| through exec's INTB_VERTB server chain in platform/amiga instead.
#ifndef FRUA_AMIGA

| VBL queue trampoline. The OS calls vblqueue routines at every vertical
| blank in supervisor mode; this saves the scratch registers the C handler
| may clobber (d0-d2/a0-a2; callee-saved regs the C handler preserves
| itself), calls vbl_flip_handler, and returns.
	.text
	.even
	.globl	_vbl_trampoline
_vbl_trampoline:
	movem.l	%d0-%d2/%a0-%a2,-(%sp)
	jbsr	_vbl_flip_handler
	movem.l	(%sp)+,%d0-%d2/%a0-%a2
	rts

| --------------------------------------------------------------------------
| Sound VBL trampoline. The four-tone synth's DMA loop is refilled every
| vblank (plat_sound_vbl). It gets its OWN vblqueue slot rather than riding
| on the display's: that one only exists when the VIDEL backend is triple-
| buffering, and the music must not depend on how the screen is buffered.
| Same register discipline as _vbl_trampoline above.
	.text
	.even
	.globl	_snd_vbl_trampoline
_snd_vbl_trampoline:
	movem.l	%d0-%d2/%a0-%a2,-(%sp)
	jbsr	_plat_sound_vbl
	movem.l	(%sp)+,%d0-%d2/%a0-%a2
	rts

#endif
