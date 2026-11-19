/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string.h>
#include <Types.h>
#include <Devices.h>
#include <Serial.h>
#include "platform.h"

/* Phase 4: real RS-232 transport over the classic Mac Serial Driver
   (.AIn/.AOut, the modem port -- port B's .BIn/.BOut, the printer
   port on most real hardware, isn't used here), mirroring
   transport_atari.c's/transport_amiga.c's role and
   "serial:<device>:<baud>" endpoint convention exactly (<device> is
   parsed but unvalidated beyond non-empty -- there is exactly one
   modem serial port to pick on a stock Mac of this era, same
   forward-compatibility reasoning both other backends already give
   for keeping the field anyway). "tcp:" (MacTCP/Open Transport) is
   out of scope, matching Atari's own "serial only, no networking
   stack" precedent for offload transport, and matching Retro68's own
   missing Multiversal Interfaces coverage for either (see
   docs/mac-platform.md's toolchain section).

   Unlike serial.device (one IOExtSer handle for both directions) or
   GEMDOS's Bconstat/Bconin/Bconout (one polled, byte-at-a-time
   interface), the classic Mac Device Manager opens INPUT and OUTPUT
   as two SEPARATE driver references (OpenDriver(".AIn", &inRef),
   OpenDriver(".AOut", &outRef)) -- both name the same physical port;
   the Device Manager's read/write model is per-direction, not
   per-device, unlike serial.device's single IORequest. */

typedef struct {
    short in_ref;
    short out_ref;
    wp_bool in_open;
    wp_bool out_open;
    u32   baud;        /* the line's speed now */
} mac_transport;

/* The Serial Driver's own input buffer is 64 bytes; at 57600 that is a
   tenth of a second of a busy Mac Plus not reading. SerSetBuf gives it
   this instead (Inside Macintosh II, "The Serial Drivers"). One port,
   one buffer, so it lives here. */
#define WP_MAC_SERBUF 2048
static char g_mac_serbuf[WP_MAC_SERBUF];

/* SerReset's config word packs baud rate, data bits, parity, and stop
   bits into one value meant to be OR'd together (verified against
   Serial.h's own constants: the baudNNN values fit in the low bits,
   dataN in bits 10-11, parity in bits 12-13, stop bits in bit 14/15 --
   non-overlapping by construction). Unlike serial.device's io_Baud
   (an arbitrary numeric baud accepted directly) or GEMDOS's Rsconf
   (an index into a small table), the SCC hardware behind the classic
   Serial Driver only supports this fixed, enumerated set of rates --
   a real hardware limitation, not a narrower API choice, so an
   unlisted baud value is a genuine WP_ERR, not a rounding question. */
static wp_bool baud_to_config(unsigned long baud, short *out_config)
{
    switch (baud) {
    case 300:   *out_config = baud300;   return WP_TRUE;
    case 600:   *out_config = baud600;   return WP_TRUE;
    case 1200:  *out_config = baud1200;  return WP_TRUE;
    case 1800:  *out_config = baud1800;  return WP_TRUE;
    case 2400:  *out_config = baud2400;  return WP_TRUE;
    case 3600:  *out_config = baud3600;  return WP_TRUE;
    case 4800:  *out_config = baud4800;  return WP_TRUE;
    case 7200:  *out_config = baud7200;  return WP_TRUE;
    case 9600:  *out_config = baud9600;  return WP_TRUE;
    case 14400: *out_config = baud14400; return WP_TRUE;
    case 19200: *out_config = baud19200; return WP_TRUE;
    case 28800: *out_config = baud28800; return WP_TRUE;
    case 38400: *out_config = baud38400; return WP_TRUE;
    case 57600: *out_config = baud57600; return WP_TRUE;
    default: return WP_FALSE;
    }
}

static wp_bool parse_endpoint(const char *endpoint, unsigned long *out_baud)
{
    const char *rest;
    const char *colon;
    unsigned long value = 0;
    wp_bool any_digit = WP_FALSE;
    const char *s;

    if (strncmp(endpoint, "serial:", 7) != 0) return WP_FALSE;
    rest = endpoint + 7;

    colon = strrchr(rest, ':');
    if (colon == NULL || colon == rest || colon[1] == '\0') return WP_FALSE;

    s = colon + 1;
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (unsigned long)(*s - '0');
        s++;
        any_digit = WP_TRUE;
    }
    if (!any_digit || *s != '\0' || value == 0) return WP_FALSE;

    *out_baud = value;
    return WP_TRUE;
}

static void close_transport(mac_transport *t)
{
    if (t == NULL) return;
    if (t->in_open) CloseDriver(t->in_ref);
    if (t->out_open) CloseDriver(t->out_ref);
    mem_free(t);
}

wp_status plat_transport_open(const char *endpoint, u32 connect_timeout_ms,
                               plat_transport **out)
{
    mac_transport *t;
    unsigned long baud;
    short config;
    SerShk handshake;

    WP_UNUSED(connect_timeout_ms); /* SerReset takes effect immediately --
                                       no connection handshake at this
                                       layer, same reasoning
                                       transport_atari.c's/
                                       transport_amiga.c's own comments
                                       give for their respective targets */

    if (!parse_endpoint(endpoint, &baud)) return WP_UNSUPPORTED;
    if (!baud_to_config(baud, &config)) return WP_ERR;

    t = (mac_transport *)mem_alloc((u32)sizeof(mac_transport));
    if (t == NULL) return WP_NOMEM;
    t->in_ref = 0;
    t->out_ref = 0;
    t->in_open = WP_FALSE;
    t->out_open = WP_FALSE;
    t->baud = (u32)baud;

    if (OpenDriver((ConstStringPtr) "\p.AIn", &t->in_ref) != noErr) {
        close_transport(t);
        return WP_ERR;
    }
    t->in_open = WP_TRUE;

    if (OpenDriver((ConstStringPtr) "\p.AOut", &t->out_ref) != noErr) {
        close_transport(t);
        return WP_ERR;
    }
    t->out_open = WP_TRUE;

    /* 8N1, no parity -- matches transport_atari.c's FLOW_NONE and
       transport_amiga.c's SERF_XDISABLED choice of no flow control at
       all. SerReset only needs to be called once (either direction's
       refNum reaches the same physical SCC channel); the output ref
       is used here, matching common real-world Mac serial sample code.
       Its return isn't treated as fatal, for the identical reason
       transport_amiga.c's own SDCMD_SETPARAMS comment gives: an
       emulator's serial port may have no real host-side backing to
       apply line settings to, and refusing to proceed at all would
       make this transport permanently unverifiable in that
       environment -- the wire protocol's own CRC framing
       (offload/transport/wire.c) is the real, live arbiter of whether
       communication actually works, not this call's return code. */
    SerReset(t->out_ref, (short)(config | data8 | noParity | stop10));
    (void)SerSetBuf(t->in_ref, (Ptr)g_mac_serbuf, (short)WP_MAC_SERBUF);

    handshake.fXOn = 0;
    handshake.fCTS = 0;
    handshake.xOn = 0;
    handshake.xOff = 0;
    handshake.errs = 0;
    handshake.evts = 0;
    handshake.fInX = 0;
    handshake.null = 0;
    SerHShake(t->out_ref, &handshake);

    *out = (plat_transport *)t;
    return WP_OK;
}

/* KillIO is declared in Retro68's headers but, like GetVInfo (see
   file_mac.c/app_shell.c's own build_full_path comment), has no
   linkable glue implementation -- the linker rejected `KILLIO` as
   undefined, found by building, not guessed. PBKillIOSync (the raw
   parameter-block call it would otherwise wrap) works directly. */
static void mac_kill_io(short refNum)
{
    ParamBlockRec pb;

    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioRefNum = refNum;
    PBKillIOSync(&pb);
}

/* ioInProgress (Inside Macintosh: Devices' own documented convention):
   an async request's ioResult field is pinned to exactly 1 while
   still pending, then overwritten with the real completion code
   (noErr or a negative OSErr) -- not declared as a named constant in
   Retro68's headers, so named locally instead of a bare magic number. */
#define WP_MAC_IO_IN_PROGRESS 1

wp_status plat_transport_read(plat_transport *t, u8 *out, u32 out_cap,
                               u32 timeout_ms, u32 *out_len)
{
    mac_transport *mt = (mac_transport *)t;
    ParamBlockRec pb;
    u32 t0;

    if (out_len) *out_len = 0;
    if (out_cap == 0) return WP_OK;
    if (mt == NULL) return WP_ERR;

    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioRefNum = mt->in_ref;
    pb.ioParam.ioBuffer = (Ptr)out;
    pb.ioParam.ioReqCount = (long)out_cap;
    pb.ioParam.ioResult = WP_MAC_IO_IN_PROGRESS;

    PBReadAsync(&pb);

    /* The Serial Driver's own read completes as soon as at least one
       byte has arrived, reporting the real count read (up to
       ioReqCount) in ioActCount -- it does not wait for the buffer to
       fill, the same "return with whatever's there" semantics
       transport_atari.c's Bconstat-gated read and
       transport_amiga.c's CMD_READ-then-SDCMD_QUERY drain both give
       platform.h's documented contract. Structurally verified (a real
       async request queues and completes without hanging); the exact
       partial-read timing isn't independently confirmed against real
       hardware or a live gateway this pass -- see docs/mac-platform.md. */
    t0 = plat_ticks_ms();
    while (pb.ioParam.ioResult == WP_MAC_IO_IN_PROGRESS) {
        if (PLAT_TICKS_PASSED(t0, timeout_ms)) {
            mac_kill_io(mt->in_ref);
            while (pb.ioParam.ioResult == WP_MAC_IO_IN_PROGRESS) { }
            return WP_TIMEOUT;
        }
    }

    if (pb.ioParam.ioResult != noErr) return WP_ERR;
    if (out_len) *out_len = (u32)pb.ioParam.ioActCount;
    return WP_OK;
}

/* platform.h's write timeout means NO PROGRESS for timeout_ms, as on
   every backend; one PBWriteAsync for the whole buffer made it one
   deadline for the whole frame instead, which a big frame on a slow line
   cannot meet. So pieces of WP_MAC_WRITE_PIECE, each with its own
   timeout_ms: a stalled line still times out, a slow one does not. */
#define WP_MAC_WRITE_PIECE 128u

wp_status plat_transport_write(plat_transport *t, const u8 *data, u32 len,
                                u32 timeout_ms)
{
    mac_transport *mt = (mac_transport *)t;
    ParamBlockRec pb;
    u32 done = 0;

    if (mt == NULL) return WP_ERR;

    while (done < len) {
        u32 piece = len - done;
        u32 t0;

        if (piece > WP_MAC_WRITE_PIECE) piece = WP_MAC_WRITE_PIECE;
        memset(&pb, 0, sizeof(pb));
        pb.ioParam.ioRefNum = mt->out_ref;
        pb.ioParam.ioBuffer = (Ptr)(data + done);
        pb.ioParam.ioReqCount = (long)piece;
        pb.ioParam.ioResult = WP_MAC_IO_IN_PROGRESS;

        PBWriteAsync(&pb);

        t0 = plat_ticks_ms();
        while (pb.ioParam.ioResult == WP_MAC_IO_IN_PROGRESS) {
            if (PLAT_TICKS_PASSED(t0, timeout_ms)) {
                mac_kill_io(mt->out_ref);
                while (pb.ioParam.ioResult == WP_MAC_IO_IN_PROGRESS) { }
                return WP_TIMEOUT;
            }
        }
        if (pb.ioParam.ioResult != noErr) return WP_ERR;
        done += piece;
    }
    return WP_OK;
}

/* Line speed (platform.h): the Serial Driver's table, up to 57600 --
   what SerReset names (baud57600). Faster needs the SCC clocked from
   outside, which this backend does not do. */
static const u32 k_mac_rates[] = { 57600uL, 38400uL, 19200uL, 9600uL };
#define MAC_RATE_COUNT ((u32)(sizeof(k_mac_rates) / sizeof(k_mac_rates[0])))

u32 plat_transport_speeds(plat_transport *t, u32 *rates, u32 cap, u32 *current)
{
    mac_transport *mt = (mac_transport *)t;
    u32 i, n = 0;

    if (current) *current = (mt != NULL) ? mt->baud : 0;
    if (mt == NULL || mt->baud == 0) return 0;
    for (i = 0; i < MAC_RATE_COUNT && n < cap; i++) rates[n++] = k_mac_rates[i];
    return n;
}

/* Every write here has completed before it returns; a character's time
   more for the last one in the SCC, then SerReset at the new speed. */
wp_status plat_transport_set_speed(plat_transport *t, u32 baud)
{
    mac_transport *mt = (mac_transport *)t;
    short config;
    u32 i, t0;

    if (mt == NULL || mt->baud == 0) return WP_UNSUPPORTED;
    for (i = 0; i < MAC_RATE_COUNT; i++) if (k_mac_rates[i] == baud) break;
    if (i == MAC_RATE_COUNT || !baud_to_config((unsigned long)baud, &config)) return WP_RANGE;
    t0 = plat_ticks_ms();
    while (!PLAT_TICKS_PASSED(t0, 20u)) { }
    if (SerReset(mt->out_ref, (short)(config | data8 | noParity | stop10)) != noErr) return WP_ERR;
    mt->baud = baud;
    return WP_OK;
}

void plat_transport_close(plat_transport *t)
{
    close_transport((mac_transport *)t);
}
