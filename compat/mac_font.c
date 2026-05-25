/*
 * Mac FONT resource loader — see mac_font.h.
 *
 * The Mac FONT format (Inside Macintosh: Text, classic edition):
 *
 *   +0   short  fontType        0x9000 / 0x9001 propFont, 0xB000+ fixedFont
 *   +2   short  firstChar       low character code in the strike
 *   +4   short  lastChar        high character code (last real glyph)
 *   +6   short  widMax          maximum char width
 *   +8   short  kernMax         left side bearing of widest char (<= 0)
 *   +10  short  nDescent        -1 or font's descent (legacy field)
 *   +12  short  fRectWidth      "font rect" max bounding-box width
 *   +14  short  fRectHeight     "font rect" max bounding-box height
 *   +16  short  owTLoc          offset to offset/width table, in words
 *                                from the start of the bitmap image table
 *                                (i.e. offset 16, the field's own location)
 *   +18  short  ascent
 *   +20  short  descent
 *   +22  short  leading
 *   +24  short  rowWords        row width of the bitmap image, in 16-bit words
 *   +26  ...    bitmap image    rowWords*2 * fRectHeight bytes
 *                                (one big strip with every glyph laid out
 *                                horizontally, then locTable indexes columns)
 *   ...        location table   (lastChar-firstChar+3) shorts, each the
 *                                bit-column of a glyph's left edge in the
 *                                strike (+1 sentinel for the missing-glyph
 *                                slot, +1 for the end-of-last)
 *   ...        offset/width tbl (lastChar-firstChar+2) shorts, each high
 *                                byte = left-side bearing (signed) and
 *                                low byte = advance width
 *
 * The missing-glyph occupies slot lastChar+1 (so n_glyphs = lastChar-
 * firstChar+2 in both tables); the location table has one extra entry
 * so the final glyph's width can be computed by subtraction.
 *
 * Bytes in tables / strike are big-endian. The shim's host might be
 * either-endian; we extract via byte arithmetic to stay portable.
 */

#include <stddef.h>             /* NULL */

#include "mac_font.h"
#include "resources.h"

int          g_mac_font_loaded;
mac_font_t   g_mac_font;

static unsigned short be16(const unsigned char *p)
{
	return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

int mac_font_load(short id)
{
	Handle               h;
	const unsigned char *base;
	long                 size;
	short                fType, firstC, lastC;
	short                fRH, owTLoc, asc, desc, rowW;
	short                rowBytes, n_chars;
	long                 strike_bytes, ow_table_off;

	g_mac_font_loaded = 0;

	h = GetResource(0x464F4E54L /* 'FONT' */, id);
	if (h == NULL || *h == NULL)
		return -1;
	size = GetHandleSize(h);
	if (size < 26)
		return -1;
	base = (const unsigned char *)*h;

	fType   = (short)be16(base + 0);
	firstC  = (short)be16(base + 2);
	lastC   = (short)be16(base + 4);
	fRH     = (short)be16(base + 14);
	owTLoc  = (short)be16(base + 16);
	asc     = (short)be16(base + 18);
	desc    = (short)be16(base + 20);
	rowW    = (short)be16(base + 24);

	if (lastC < firstC || rowW <= 0 || fRH <= 0)
		return -1;

	/* The shim only handles plain bitmap fonts (type 0x9000 / 0x9001
	 * propFont, 0xB000+ fixedFont). The high bit selects "image height
	 * referenced" which we don't honour yet — return failure for it. */
	if ((fType & 0x8000) == 0)
		return -1;

	rowBytes     = (short)(rowW * 2);
	strike_bytes = (long)rowBytes * fRH;
	if (26 + strike_bytes > size)
		return -1;

	/* owTLoc is "in words from the field itself". The owTLoc field lives
	 * at offset 16 in the header, so the table's byte offset from the
	 * start of the resource is 16 + 2 * owTLoc. */
	ow_table_off = 16 + (long)owTLoc * 2;
	n_chars      = (short)(lastC - firstC + 2);    /* +1 for missing-glyph */
	if (ow_table_off + (long)n_chars * 2 > size)
		return -1;

	g_mac_font.resource  = h;
	g_mac_font.strike    = base + 26;
	g_mac_font.locTable  = base + 26 + strike_bytes;
	g_mac_font.owTable   = base + ow_table_off;
	g_mac_font.firstChar = firstC;
	g_mac_font.lastChar  = lastC;
	g_mac_font.ascent    = asc;
	g_mac_font.descent   = desc;
	g_mac_font.height    = fRH;
	g_mac_font.rowBytes  = rowBytes;
	g_mac_font.maxWidth  = (short)be16(base + 6);
	g_mac_font_loaded    = 1;
	return 0;
}

/* Glyph table slot for char c: c - firstChar if in range, else the
 * missing-glyph slot at the end. */
static short slot_for(short c)
{
	short idx = (short)(c - g_mac_font.firstChar);

	if (idx < 0 || idx > (short)(g_mac_font.lastChar - g_mac_font.firstChar))
		idx = (short)(g_mac_font.lastChar - g_mac_font.firstChar + 1);
	return idx;
}

short mac_font_strike_width(short c)
{
	short                idx, sw;
	const unsigned char *lt;
	unsigned short       loc, next;

	if (!g_mac_font_loaded)
		return 0;
	idx  = slot_for(c);
	lt   = g_mac_font.locTable + (long)idx * 2;
	loc  = be16(lt);
	next = be16(lt + 2);
	sw   = (short)(next - loc);
	if (sw < 0)
		sw = 0;
	return sw;
}

short mac_font_advance(short c)
{
	short                idx;
	const unsigned char *ow;

	if (!g_mac_font_loaded)
		return 0;
	idx = slot_for(c);
	ow  = g_mac_font.owTable + (long)idx * 2;
	if (ow[0] == 0xFF && ow[1] == 0xFF)
		return mac_font_strike_width(c);    /* no entry → strike width */
	return (short)ow[1];                    /* low byte = advance      */
}

int mac_font_pixel(short c, short col, short row)
{
	short                idx;
	unsigned short       loc;
	long                 bit_col, byte_off, bit_in_byte;

	if (!g_mac_font_loaded || row < 0 || row >= g_mac_font.height || col < 0)
		return 0;
	idx = slot_for(c);
	loc = be16(g_mac_font.locTable + (long)idx * 2);

	bit_col     = (long)loc + col;
	byte_off    = (long)row * g_mac_font.rowBytes + bit_col / 8;
	bit_in_byte = 7 - (bit_col & 7);
	return (g_mac_font.strike[byte_off] >> bit_in_byte) & 1;
}
