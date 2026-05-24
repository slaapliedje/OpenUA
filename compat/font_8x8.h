/*
 * Embedded 8x8 fallback font for the QuickDraw shim.
 *
 * Private to compat/ (the public include/ directory is the engine's
 * surface). Real NFNT fonts replace this once the Resource Manager is
 * lifting them off the resource fork — see compat/font_8x8.c.
 */

#ifndef COMPAT_FONT_8X8_H
#define COMPAT_FONT_8X8_H

extern const unsigned char qd_font_8x8[256][8];
extern const unsigned char qd_font_8x8_width;   /* cell width, columns */
extern const unsigned char qd_font_8x8_height;  /* cell height, rows   */
extern const unsigned char qd_font_8x8_ascent;  /* rows above baseline */

#endif /* COMPAT_FONT_8X8_H */
