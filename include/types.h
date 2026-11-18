/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_TYPES_H
#define WP_TYPES_H

/* Fixed-width integer types for the engine. u32/i32 are guaranteed to be
   AT LEAST 32 bits, but may be wider on some hosts (e.g. LP64: `long` is
   64 bits). Never rely on sizeof(u32)/sizeof(i32) for wire or disk sizing --
   all persistence and protocol I/O goes through the be_read and be_write
   helpers in endian.h, which always read/write exactly the documented
   number of bytes regardless of the in-memory type width. */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
typedef signed short   i16;
typedef signed long    i32;

/* 0/1 boolean; stored as a byte so it has a fixed size in structs. */
typedef u8 wp_bool;
#define WP_FALSE 0
#define WP_TRUE  1

typedef enum {
    WP_OK          = 0,
    WP_ERR         = 1,
    WP_NOMEM       = 2,
    WP_RANGE       = 3,
    WP_UNSUPPORTED = 4,
    WP_TIMEOUT     = 5,
    WP_BADFMT      = 6
} wp_status;

/* Silence unused-parameter warnings under -Wextra without disabling the
   warning globally (stub backends have many intentionally-unused params). */
#define WP_UNUSED(x) ((void)(x))

#endif /* WP_TYPES_H */
