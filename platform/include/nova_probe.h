/*
 * Nova / graphics-card discovery probe (ATW800/2 and any NVDI/Nova card).
 *
 * OpenUA can only be developed for a graphics card on the actual hardware —
 * no emulator here models a Nova/VME card. This probe runs ONCE on the real
 * machine (with the card active as the boot screen) and dumps everything a
 * backend author needs — cookie jar, VDI workstation caps, plane depth, and
 * the framebuffer base — to C:\NOVA.LOG, so the display_nova backend can be
 * coded from real numbers rather than guessed.
 *
 * Compiled only into a diagnostic build (-DFRUA_NOVAPROBE); the shipping
 * binary links this translation unit as an empty object. main() calls it
 * (under the same guard) just before dsp_detect(), while the boot screen is
 * still whatever the card driver left up.
 */

#ifndef PLATFORM_NOVA_PROBE_H
#define PLATFORM_NOVA_PROBE_H

/* Read the cookie jar + screen VDI caps and write C:\NOVA.LOG. No-op unless
 * built with -DFRUA_NOVAPROBE. Safe to call on a plain ST/STe with no card
 * (it then records the built-in ST screen, which is a useful control). */
void nova_probe_dump(void);

#endif /* PLATFORM_NOVA_PROBE_H */
