/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_ENDIAN_H
#define WP_ENDIAN_H

#include "types.h"

/* Canonical on-disk / on-wire byte order is big-endian (CLAUDE.md rule 3).
   These helpers read/write exactly the documented number of bytes from a
   raw byte buffer. They are implemented with shifts and masks, not by
   reinterpreting memory, so they produce the same result regardless of the
   host's native endianness or integer width -- never cast a struct onto a
   buffer and never memcpy a multi-byte value directly. */

u16 be_read16(const u8 *p);
u32 be_read32(const u8 *p);

void be_write16(u8 *p, u16 v);
void be_write32(u8 *p, u32 v);

/* Pure byte-swap helpers, for the rare case something already holds a
   value in a known-native register form and needs it reversed. Prefer
   be_read/be_write for all buffer I/O. */
u16 be_swap16(u16 v);
u32 be_swap32(u32 v);

/* True if the host CPU is little-endian. Provided for diagnostics and
   tests only -- be_read and be_write never need to branch on this. */
wp_bool host_is_little_endian(void);

#endif /* WP_ENDIAN_H */
