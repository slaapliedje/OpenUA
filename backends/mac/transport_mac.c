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
} mac_transport;

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
    u32 deadline;

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
    deadline = plat_ticks_ms() + timeout_ms;
    while (pb.ioParam.ioResult == WP_MAC_IO_IN_PROGRESS) {
        if (plat_ticks_ms() >= deadline) {
            mac_kill_io(mt->in_ref);
            while (pb.ioParam.ioResult == WP_MAC_IO_IN_PROGRESS) { }
            return WP_TIMEOUT;
        }
    }

    if (pb.ioParam.ioResult != noErr) return WP_ERR;
    if (out_len) *out_len = (u32)pb.ioParam.ioActCount;
    return WP_OK;
}

wp_status plat_transport_write(plat_transport *t, const u8 *data, u32 len,
                                u32 timeout_ms)
{
    mac_transport *mt = (mac_transport *)t;
    ParamBlockRec pb;
    u32 deadline;

    if (mt == NULL) return WP_ERR;
    if (len == 0) return WP_OK;

    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioRefNum = mt->out_ref;
    pb.ioParam.ioBuffer = (Ptr)data;
    pb.ioParam.ioReqCount = (long)len;
    pb.ioParam.ioResult = WP_MAC_IO_IN_PROGRESS;

    PBWriteAsync(&pb);

    deadline = plat_ticks_ms() + timeout_ms;
    while (pb.ioParam.ioResult == WP_MAC_IO_IN_PROGRESS) {
        if (plat_ticks_ms() >= deadline) {
            mac_kill_io(mt->out_ref);
            while (pb.ioParam.ioResult == WP_MAC_IO_IN_PROGRESS) { }
            return WP_TIMEOUT;
        }
    }

    if (pb.ioParam.ioResult != noErr) return WP_ERR;
    return WP_OK;
}

void plat_transport_close(plat_transport *t)
{
    close_transport((mac_transport *)t);
}
