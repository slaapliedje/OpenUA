/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "utf8.h"

u32 utf8_seq_len(u8 lead)
{
    if ((lead & 0x80u) == 0x00u) return 1; /* 0xxxxxxx */
    if ((lead & 0xE0u) == 0xC0u) return 2; /* 110xxxxx */
    if ((lead & 0xF0u) == 0xE0u) return 3; /* 1110xxxx */
    if ((lead & 0xF8u) == 0xF0u) return 4; /* 11110xxx */
    return 0; /* continuation byte, or an obsolete 5/6-byte lead */
}

u32 utf8_decode(const u8 *s, u32 len, u32 *cp_out)
{
    u32 seqlen, cp, i, min_cp;

    if (len == 0) return 0;
    seqlen = utf8_seq_len(s[0]);
    if (seqlen == 0 || seqlen > len) return 0;

    if (seqlen == 1) {
        cp = s[0];
    } else {
        cp = s[0] & (u8)(0xFFu >> (seqlen + 1));
        for (i = 1; i < seqlen; i++) {
            if ((s[i] & 0xC0u) != 0x80u) return 0; /* bad continuation byte */
            cp = (cp << 6) | (s[i] & 0x3Fu);
        }
    }

    /* Reject overlong encodings: a codepoint must use the shortest
       sequence length that can represent it. */
    switch (seqlen) {
        case 1: min_cp = 0x0u; break;
        case 2: min_cp = 0x80u; break;
        case 3: min_cp = 0x800u; break;
        default: min_cp = 0x10000u; break;
    }
    if (cp < min_cp) return 0;

    /* Reject surrogate halves and anything above the max scalar value. */
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;
    if (cp > 0x10FFFFu) return 0;

    if (cp_out) *cp_out = cp;
    return seqlen;
}

u32 utf8_encode(u32 cp, u8 *out)
{
    if ((cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) return 0;

    if (cp < 0x80u) {
        out[0] = (u8)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (u8)(0xC0u | (cp >> 6));
        out[1] = (u8)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (u8)(0xE0u | (cp >> 12));
        out[1] = (u8)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (u8)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (u8)(0xF0u | (cp >> 18));
    out[1] = (u8)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (u8)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (u8)(0x80u | (cp & 0x3Fu));
    return 4;
}

u32 utf8_next(const u8 *s, u32 len, u32 offset)
{
    u32 seqlen;

    if (offset >= len) return len;
    seqlen = utf8_seq_len(s[offset]);
    if (seqlen == 0) seqlen = 1;
    if (offset + seqlen > len) seqlen = len - offset;
    return offset + seqlen;
}

u32 utf8_prev(const u8 *s, u32 len, u32 offset)
{
    u32 start;

    WP_UNUSED(len);
    if (offset == 0) return 0;
    start = offset - 1;
    while (start > 0 && (s[start] & 0xC0u) == 0x80u && (offset - start) < 4) {
        start--;
    }
    return start;
}

u32 utf8_cp_count(const u8 *s, u32 len)
{
    u32 count = 0;
    u32 offset = 0;

    while (offset < len) {
        offset = utf8_next(s, len, offset);
        count++;
    }
    return count;
}

wp_status utf8_validate(const u8 *s, u32 len, u32 *bad_offset)
{
    u32 offset = 0;

    while (offset < len) {
        u32 cp;
        u32 consumed = utf8_decode(s + offset, len - offset, &cp);
        if (consumed == 0) {
            if (bad_offset) *bad_offset = offset;
            return WP_BADFMT;
        }
        offset += consumed;
    }
    return WP_OK;
}

/* U+00A0..U+00FF, one ASCII character each; ' ' where none is better. */
static const char g_latin1_ascii[97] =
    " !cL*Y|S\"ca\"--r-o+23'uP.,1o\"423?"     /* A0..BF */
    "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"      /* C0..DF */
    "aaaaaaaceeeeiiiidnooooo/ouuuuyty";     /* E0..FF */

u8 utf8_ascii_fallback(u32 cp)
{
    if (cp < 0x80) return (u8)cp;
    if (cp >= 0xA0 && cp <= 0xFF) return (u8)g_latin1_ascii[cp - 0xA0];
    switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: return (u8)'\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return (u8)'"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015:
    case 0x2212: return (u8)'-';
    case 0x2026: return (u8)'.';
    case 0x2022: case 0x2023: case 0x25CF: return (u8)'*';
    case 0x2002: case 0x2003: case 0x2009: case 0x200A: case 0x202F: return (u8)' ';
    case 0x20AC: return (u8)'E';
    case 0x2122: return (u8)'T';
    default: return 0;
    }
}
