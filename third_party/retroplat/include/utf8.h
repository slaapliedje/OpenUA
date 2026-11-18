/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_UTF8_H
#define WP_UTF8_H

#include "types.h"

/* Pure UTF-8 helpers per skills/encoding.md. No platform calls, no
   allocation. The engine and services store and manipulate text as UTF-8
   exclusively; these are the ONLY functions permitted to reason about
   codepoint boundaries. Everything else treats spans as opaque byte runs. */

/* Decode one codepoint starting at s[0]. Returns bytes consumed (1..4) on
   success and writes the codepoint to *cp_out. Returns 0 on any invalid
   sequence (truncated input, bad continuation bytes, overlong encoding,
   surrogate half, or a codepoint above 0x10FFFF) -- the caller decides the
   replacement policy; this function never guesses. */
u32 utf8_decode(const u8 *s, u32 len, u32 *cp_out);

/* The nearest plain ASCII character to cp, for a machine whose character
   set has no glyph for it: curly quotes to straight ones, dashes to a
   hyphen, an ellipsis to a full stop, an accented Latin-1 letter to its
   letter (e for e-acute) -- what a reader would write by hand. 0 where
   there is no sensible one, and the caller prints its '?'. */
u8 utf8_ascii_fallback(u32 cp);

/* Encode cp into out, which must have room for up to 4 bytes. Returns
   bytes written (1..4), or 0 if cp is not a valid Unicode scalar value
   (a surrogate half, or greater than 0x10FFFF). */
u32 utf8_encode(u32 cp, u8 *out);

/* Number of bytes in the sequence that starts with lead byte `lead`,
   1..4. Returns 0 if `lead` is a continuation byte or otherwise cannot
   start a sequence. This is a structural check on the lead byte's bit
   pattern only -- it does not validate the bytes that would follow. */
u32 utf8_seq_len(u8 lead);

/* Step a byte offset forward by one codepoint within [0, len]. If the byte
   at `offset` is not a valid lead byte, or the indicated sequence would
   run past `len`, steps forward by exactly one byte instead, so callers
   can never get stuck. Clamps at len. */
u32 utf8_next(const u8 *s, u32 len, u32 offset);

/* Step a byte offset backward by one codepoint within [0, len]. Clamps
   at 0. */
u32 utf8_prev(const u8 *s, u32 len, u32 offset);

/* Codepoint count of a span -- for word/cursor logic, never for byte
   math. Counts one "step" per utf8_next call, so it is well-defined even
   over input that contains invalid sequences. */
u32 utf8_cp_count(const u8 *s, u32 len);

/* Validate a buffer is well-formed UTF-8 end to end. Returns WP_OK, or
   WP_BADFMT with *bad_offset set to the byte offset of the first invalid
   sequence. */
wp_status utf8_validate(const u8 *s, u32 len, u32 *bad_offset);

#endif /* WP_UTF8_H */
