/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* transport_gem4xe.c -- the offload seam on gem4xe: no device, and said so.
 *
 * The ST reaches a gateway over the modem port (transport_atari.c, Rsconf
 * and the Bconin/Bconout pair).  gem4xe serves the VDI, the AES and
 * GEMDOS and nothing else -- there is no serial call in its ABI to reach,
 * and the Atari 8-bit's own serial lives behind SIO, which is a driver
 * this port does not have and does not need in order to be a word
 * processor.
 *
 * So this is a deliberate, complete implementation of "no offload device
 * is attached", which is a state the engine is required to handle on
 * every target anyway: the engine must be fully functional without one,
 * and offload features are enhancements requested through this one
 * interface.  Answering WP_UNSUPPORTED at open() is the same answer the
 * ST gives for a "tcp:" endpoint, and the offload client already knows
 * what to do with it.
 *
 * The three calls after open exist because the seam has four functions.
 * None can be reached: nothing here ever produces a handle for them to be
 * called with.  They answer rather than assert so that a caller with a
 * bug gets an error instead of a machine that stops. */

#include <gem.h>

#ifdef GEM4XE_APP_GEM_H

#include "platform.h"

wp_status plat_transport_open(const char *endpoint, u32 connect_timeout_ms,
                              plat_transport **out)
{
    WP_UNUSED(endpoint);
    WP_UNUSED(connect_timeout_ms);
    if (out) *out = (plat_transport *)0;
    return WP_UNSUPPORTED;
}

wp_status plat_transport_read(plat_transport *t, u8 *out, u32 out_cap,
                              u32 timeout_ms, u32 *out_len)
{
    WP_UNUSED(t);
    WP_UNUSED(out);
    WP_UNUSED(out_cap);
    WP_UNUSED(timeout_ms);
    if (out_len) *out_len = 0;
    return WP_ERR;
}

wp_status plat_transport_write(plat_transport *t, const u8 *data, u32 len,
                               u32 timeout_ms)
{
    WP_UNUSED(t);
    WP_UNUSED(data);
    WP_UNUSED(len);
    WP_UNUSED(timeout_ms);
    return WP_ERR;
}

void plat_transport_close(plat_transport *t)
{
    WP_UNUSED(t);
}

#else

typedef int rp_transport_gem4xe_not_this_target;

#endif
