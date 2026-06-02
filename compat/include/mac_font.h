/*
 * Mac FONT resource loader for the QuickDraw shim.
 *
 * Parses the classic-Mac FONT format (16-byte header + bitmap strike +
 * location table + offset/width table) and exposes a tiny bit-plot
 * interface DrawChar can consult. When a font is loaded the renderer
 * prefers it over the embedded 8x8 fallback in compat/font_8x8.c.
 *
 * NFNT (the colour-aware variant) and FOND-driven font families are
 * follow-ups; FRUA's FONT -27001 / -26998 are plain bitmap fonts and
 * land first.
 */

#ifndef COMPAT_MAC_FONT_H
#define COMPAT_MAC_FONT_H

#include "macmemory.h"          /* Handle */

typedef struct {
	Handle                resource;     /* the GetResource handle (kept) */
	const unsigned char  *strike;        /* row-major bitmap, big-endian */
	const unsigned char  *locTable;      /* (n+1) 16-bit BE bit-offsets   */
	const unsigned char  *owTable;       /* n × (offset, width) bytes     */
	short                 firstChar;
	short                 lastChar;
	short                 ascent;
	short                 descent;
	short                 height;        /* fRectHeight                   */
	short                 rowBytes;      /* strike row width in bytes     */
	short                 maxWidth;
	short                 kernMax;       /* max left-side bearing (<= 0)  */
} mac_font_t;

extern int          g_mac_font_loaded;
extern mac_font_t   g_mac_font;

/* Load FONT resource `id` into g_mac_font. Returns 0 on success, -1
 * otherwise (resource missing, corrupt, or unsupported type). */
int  mac_font_load(short id);

/* Per-char strike bitmap width (the pixel span between consecutive
 * locTable entries). For the missing-glyph it returns the missing-glyph
 * width. */
short mac_font_strike_width(short c);

/* Per-char advance — how far the pen moves after drawing this glyph.
 * Comes from the offset/width table, low byte. */
short mac_font_advance(short c);

/* Per-char left-side bearing — how far right of the pen the glyph's bit
 * image starts (offset/width table high byte + kernMax). Glyphs must be
 * drawn at pen.h + this, or narrow chars left-align in their advance cell
 * and the spacing looks uneven. */
short mac_font_offset(short c);

/* Pixel at (col, row) inside the glyph for char c. col is 0..strike_width,
 * row is 0..height. Returns 1 if set, 0 otherwise. */
int   mac_font_pixel(short c, short col, short row);

#endif /* COMPAT_MAC_FONT_H */
