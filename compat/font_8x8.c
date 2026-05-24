/*
 * Embedded 8x8 fallback font for the QuickDraw shim (ADR-0003).
 *
 * Scaffolding so DrawChar / DrawString / CharWidth / StringWidth have
 * something to render before the Resource Manager lifts the Mac NFNT
 * fonts off the resource fork. The table is dense (256 entries) but the
 * data is sparse — only the glyphs the bring-up demo exercises are spelt
 * out; every other code point falls back to a hollow 7x7 box so missing
 * glyphs are visible rather than silent. C99 range-designated init keeps
 * the missing-glyph plumbing off the runtime path.
 *
 * Each glyph is eight bytes, one row per byte, top to bottom; bit 7 (the
 * mask 0x80) is the leftmost column. Row 7 (the bottom byte) is the
 * descender row — zero for these glyphs since none of them descend.
 * QuickDraw's pen sits on the baseline, which the renderer treats as the
 * row below the glyph body, so the eight rows draw at pnLoc.v - 7 ..
 * pnLoc.v with no clipping for descenders.
 *
 * Glyph design favours legibility on the 320x400 VIDEL surface — 2-pixel
 * stems, 7-row ascent — over compactness. Real fonts will replace this
 * subsystem.
 */

#include "font_8x8.h"

/* The [0 ... 255] catch-all installs the fallback into every slot; the
 * named-glyph initialisers below intentionally override individual slots.
 * GCC's -Woverride-init flags that intent as a warning — silence it for
 * just this table. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
const unsigned char qd_font_8x8[256][8] = {
	[0 ... 255] = { 0xFE, 0x82, 0x82, 0x82, 0x82, 0x82, 0xFE, 0x00 },

	[' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },

	['H'] = { 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00 },
	['E'] = { 0xFE, 0xC0, 0xC0, 0xFC, 0xC0, 0xC0, 0xFE, 0x00 },
	['L'] = { 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00 },
	['O'] = { 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00 },
};
#pragma GCC diagnostic pop

const unsigned char qd_font_8x8_width  = 8;
const unsigned char qd_font_8x8_height = 8;
const unsigned char qd_font_8x8_ascent = 7;
