/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "endian.h"

u16 be_read16(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | (u16)p[1]);
}

u32 be_read32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
           ((u32)p[2] <<  8) | (u32)p[3];
}

void be_write16(u8 *p, u16 v)
{
    p[0] = (u8)((v >> 8) & 0xFFu);
    p[1] = (u8)(v & 0xFFu);
}

void be_write32(u8 *p, u32 v)
{
    p[0] = (u8)((v >> 24) & 0xFFu);
    p[1] = (u8)((v >> 16) & 0xFFu);
    p[2] = (u8)((v >>  8) & 0xFFu);
    p[3] = (u8)(v & 0xFFu);
}

u16 be_swap16(u16 v)
{
    return (u16)(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8));
}

u32 be_swap32(u32 v)
{
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) | ((v & 0xFF000000u) >> 24);
}

wp_bool host_is_little_endian(void)
{
    union { u16 u; u8 b[2]; } probe;
    probe.u = 1;
    return (wp_bool)(probe.b[0] == 1 ? WP_TRUE : WP_FALSE);
}
