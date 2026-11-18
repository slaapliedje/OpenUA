/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef RP_WIN32_METRICS_INTERNAL_H
#define RP_WIN32_METRICS_INTERNAL_H

#include <windows.h>

#include "types.h"

/* Private to backends/win32 -- shared between metrics_win32.c (which owns
   the twips<->device conversion and the HFONT) and draw_win32.c (the
   plat_gc implementation, painting through an HDC). The same shared-
   internal-header pattern every other backend in this library uses.

   THE CONVERSION IS THE ONE THING WORTH READING HERE. Every other backend
   in this library gets twips for nearly free: QuickDraw is 72 dpi by
   construction so a point IS a pixel, Qt and Pango both count in points,
   and the Atari VDI is told a point size directly. Windows is the odd one
   -- GDI works in LOGICAL UNITS whose relationship to a point is a
   property of the DEVICE, and the screen is conventionally 96 dpi but is
   not required to be and is not on a scaled display.

   So the ratio is asked for rather than assumed:

       pixels = MulDiv(twips, GetDeviceCaps(dc, LOGPIXELSY), 1440)
       twips  = MulDiv(pixels, 1440, GetDeviceCaps(dc, LOGPIXELSY))

   MulDiv rather than a multiply and a divide because it carries the
   intermediate in 64 bits and rounds to nearest, which is exactly the
   `(v * milli + 500) / 1000` discipline GACS's own engine uses for its
   fractional rule multipliers. Doing it by hand in 32 bits overflows on a
   large point size at a high DPI.

   1440 = 20 twips per point x 72 points per inch.

   A font's metrics are measured ONCE at open time, against the screen DC,
   and cached. GetTextMetrics needs the font selected into a DC, and
   opening one on every plat_font_metrics_get would make a query that every
   other backend answers from a struct into a round trip to GDI. */

#define RP_TWIPS_PER_INCH 1440

typedef struct {
    HFONT hfont;
    i32   size_twips;
    i32   ascent_twips;
    i32   descent_twips;
    int   dpi_y;          /* LOGPIXELSY at open time */
} w32_font;

/* plat_gc for this backend: the device context being painted -- a window's
   from BeginPaint, a memory DC for an off-screen render, or a printer's.
   Callers construct one directly, as they do on the Amiga, Mac and Qt
   backends, because the shell already owns its window. */
typedef struct {
    HDC dc;
} w32_gc;

/* Shared with draw_win32.c: GDI is UTF-16, this seam is UTF-8, and both
   files need the conversion. Returns the number of WCHARs written, or -1
   if the run does not fit. */
int rp_w32_widen(const u8 *utf8, u32 len, WCHAR *out, int out_cap);

#endif /* RP_WIN32_METRICS_INTERNAL_H */
