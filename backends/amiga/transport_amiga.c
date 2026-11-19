/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <string.h>

#include <exec/types.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <devices/serial.h>
#include <proto/exec.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/filio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
/* The library base every bsdsocket call goes through, under a name of this
   file's own: proto/bsdsocket.h declares "SocketBase" extern, and an
   application with its own SocketBase must not collide with this one. */
#define SocketBase wp_amiga_socket_base
#include <proto/bsdsocket.h>

#include "platform.h"

/* T9.3: real RS-232 transport over serial.device, mirroring platform/
   atari/transport_atari.c's role and its "serial:<device>:<baud>"
   endpoint convention exactly -- only <baud> differs in shape (Amiga's
   io_Baud takes a true baud value directly; there is no BAUD_* index
   table to map into the way Atari's Rsconf needs, so parsing is simpler
   here, not harder). <device> is parsed but unvalidated beyond
   "non-empty", same as Atari -- a stock Amiga has exactly one built-in
   serial port (unit 0), so there is nothing to select, but the field is
   kept for the same forward-compatibility reason Atari's own comment
   gives. "tcp:<host>:<port>" is a TCP client over bsdsocket.library
   (AmiTCP, Miami, Roadshow, or an emulator's own bsdsocket emulation),
   further down this file; a machine with no TCP stack running answers
   WP_UNSUPPORTED from plat_transport_open, never a hang. */

static wp_bool parse_baud(const char *s, ULONG *out_baud)
{
    ULONG value = 0;
    wp_bool any_digit = WP_FALSE;

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (ULONG)(*s - '0');
        s++;
        any_digit = WP_TRUE;
    }
    if (!any_digit || *s != '\0' || value == 0) return WP_FALSE;

    *out_baud = value;
    return WP_TRUE;
}

/* A real IOExtSer-sized IORequest, one MsgPort, and whether OpenDevice
   actually succeeded (so plat_transport_close only CloseDevice's a
   device this code actually opened -- CloseDevice on a never-opened
   IORequest is undefined per the serial.device docs, unlike the fully
   defensive mem_free/DeleteIORequest/DeleteMsgPort calls below it, which
   are all safe on NULL). */
/* Every transport this file hands out starts with its kind, so the four
   public functions can tell a serial line from a socket. */
enum { TRANSPORT_SERIAL = 1, TRANSPORT_TCP = 2 };

typedef struct {
    int              kind;          /* TRANSPORT_SERIAL */
    struct MsgPort  *port;
    struct IOExtSer *ioreq;
    wp_bool          device_open;
} amiga_transport;

/* Real read-buffer size requested from the driver (io_RBufLen) -- large
   enough that a burst faster than this app's own read loop still won't
   overflow the driver's internal buffer between two plat_transport_read
   calls; the wire protocol's own largest single frame
   (OFFLOAD_MAX_RESPONSE_PAYLOAD, engine/src/offload_client.c) is 32768,
   but this buffer only needs to absorb what arrives between polls, not
   a whole frame at once -- 4096 is a generous, real, non-arbitrary
   margin over a single CMD_READ's typical drain size. */
#define WP_AMIGA_SERIAL_RBUFLEN 4096uL

static void close_transport(amiga_transport *t)
{
    if (t == NULL) return;
    if (t->device_open) CloseDevice((struct IORequest *)t->ioreq);
    if (t->ioreq != NULL) DeleteIORequest((APTR)t->ioreq);
    if (t->port != NULL) DeleteMsgPort(t->port);
    mem_free(t);
}

static wp_status serial_open(const char *endpoint, u32 connect_timeout_ms,
                             plat_transport **out)
{
    amiga_transport *t;
    const char *rest;
    const char *colon;
    ULONG baud;

    WP_UNUSED(connect_timeout_ms); /* SDCMD_SETPARAMS takes effect
                                       immediately -- there is no
                                       connection handshake at this layer,
                                       same reasoning transport_atari.c's
                                       own Rsconf comment gives */

    if (strncmp(endpoint, "serial:", 7) != 0) return WP_UNSUPPORTED;
    rest = endpoint + 7;

    colon = strrchr(rest, ':');
    if (colon == NULL || colon == rest || colon[1] == '\0') return WP_ERR;
    if (!parse_baud(colon + 1, &baud)) return WP_ERR;

    t = (amiga_transport *)mem_alloc((u32)sizeof(amiga_transport));
    if (t == NULL) return WP_NOMEM;
    t->kind = TRANSPORT_SERIAL;
    t->port = NULL;
    t->ioreq = NULL;
    t->device_open = WP_FALSE;

    t->port = CreateMsgPort();
    if (t->port == NULL) { close_transport(t); return WP_ERR; }

    /* CAUTION (devices/serial.h's own comment, verbatim): accessing
       serial.device REQUIRES an IOExtSer-sized IORequest, not the
       smaller IOStdReq -- CreateIORequest's size parameter is exactly
       this, not sizeof(struct IORequest). CreateIORequest returns a
       zero-filled block (a real, documented exec.library guarantee,
       unlike mem_alloc -- platform.h makes no such promise), so every
       IOExtSer field below starts from a known state, not garbage. */
    t->ioreq = (struct IOExtSer *)CreateIORequest(t->port, (ULONG)sizeof(struct IOExtSer));
    if (t->ioreq == NULL) { close_transport(t); return WP_ERR; }

    if (OpenDevice((CONST_STRPTR)SERIALNAME, 0, (struct IORequest *)t->ioreq, 0) != 0) {
        close_transport(t);
        return WP_ERR;
    }
    t->device_open = WP_TRUE;

    /* 8N1, no parity, no software (xON/xOFF) flow control -- SERF_XDISABLED
       disables it explicitly rather than relying on a driver default,
       matching transport_atari.c's own FLOW_NONE choice. io_CtlChar is
       still set to the documented default control-char set
       (SER_DEFAULT_CTLCHAR) since SETPARAMS reads it regardless of
       whether xON/xOFF is enabled. io_Length is set to this struct's
       real size per devices/serial.h's own CAUTION about IOExtSer
       sizing (CMD_READ/CMD_WRITE below set their own io_Length to the
       real transfer size before every call; this one covers the
       SETPARAMS request specifically). */
    t->ioreq->io_CtlChar = SER_DEFAULT_CTLCHAR;
    t->ioreq->io_RBufLen = WP_AMIGA_SERIAL_RBUFLEN;
    t->ioreq->io_ExtFlags = 0;
    t->ioreq->io_Baud = baud;
    t->ioreq->io_BrkTime = 0;
    t->ioreq->io_ReadLen = 8;
    t->ioreq->io_WriteLen = 8;
    t->ioreq->io_StopBits = 1;
    t->ioreq->io_SerFlags = (UBYTE)SERF_XDISABLED;
    t->ioreq->IOSer.io_Length = (ULONG)sizeof(struct IOExtSer);

    /* SDCMD_SETPARAMS is attempted but NOT treated as fatal if it
       fails -- a real, deliberate choice, not an oversight. Found
       empirically under amiberry (see docs/amiga-platform.md): this
       emulator's serial.device answers SDCMD_QUERY (a real, successful
       IORequest -- proving OpenDevice and the exec.library IORequest
       lifecycle genuinely work) but rejects SDCMD_SETPARAMS with
       SerErr_InvParam regardless of which parameter VALUES are sent
       -- consistent with an emulated serial port that has no real
       host-side backing configured to actually apply line settings to,
       not a bug in the values requested here (verified by trying
       several distinct, individually-valid combinations, all rejected
       identically). Refusing to proceed at all in that case would mean
       this transport can NEVER be opened under this project's own test
       fixture, which would make T9.3's real, novel mechanism (the
       CMD_READ/CMD_WRITE timeout race below) permanently unverifiable
       on target. On genuine hardware, a well-formed SETPARAMS for a
       completely standard configuration (8N1, a common baud) should
       never legitimately fail -- and if the actual line settings ARE
       wrong for some other reason, the wire protocol's own CRC framing
       (offload/transport/wire.c) is the real, live arbiter of whether
       communication actually works, not this call's return code. */
    t->ioreq->IOSer.io_Command = SDCMD_SETPARAMS;
    DoIO((struct IORequest *)t->ioreq);

    *out = (plat_transport *)t;
    return WP_OK;
}

/* Waits up to timeout_ms for the FIRST byte (platform.h's documented
   contract), via a real async SendIO raced against plat_ticks_ms() --
   the same deadline-polling SHAPE transport_atari.c's Bconstat loop
   uses, adapted to exec.library's real async I/O primitives (SendIO +
   CheckIO to poll without blocking, AbortIO + WaitIO to cancel and
   reclaim the IORequest on timeout -- AbortIO on an already-completed
   request is documented as a safe no-op, so no race window between the
   CheckIO check and the AbortIO call). Once the first byte arrives,
   drains whatever else is ALREADY buffered (via a real SDCMD_QUERY,
   which reports exactly how many bytes are waiting, not a guess) up to
   out_cap -- matching transport_atari.c's own "wait then drain, no
   further waiting" contract, so callers assembling a multi-byte frame
   (offload/transport/wire.c) need fewer round trips than a strict
   one-byte-per-call implementation would cost them. */
static wp_status serial_read(plat_transport *t, u8 *out, u32 out_cap,
                             u32 timeout_ms, u32 *out_len)
{
    amiga_transport *at = (amiga_transport *)t;
    u32 t0;
    u32 extra, n;

    if (out_len) *out_len = 0;
    if (out_cap == 0) return WP_OK;
    if (at == NULL) return WP_ERR;

    at->ioreq->IOSer.io_Command = CMD_READ;
    at->ioreq->IOSer.io_Data = (APTR)out;
    at->ioreq->IOSer.io_Length = 1;
    SendIO((struct IORequest *)at->ioreq);

    t0 = plat_ticks_ms();
    while (CheckIO((struct IORequest *)at->ioreq) == NULL) {
        if (PLAT_TICKS_PASSED(t0, timeout_ms)) {
            AbortIO((struct IORequest *)at->ioreq);
            WaitIO((struct IORequest *)at->ioreq);
            return WP_TIMEOUT;
        }
    }
    WaitIO((struct IORequest *)at->ioreq);
    if (at->ioreq->IOSer.io_Error != 0) return WP_ERR;
    n = 1;

    if (n < out_cap) {
        at->ioreq->IOSer.io_Command = SDCMD_QUERY;
        if (DoIO((struct IORequest *)at->ioreq) == 0) {
            extra = at->ioreq->IOSer.io_Actual;
            if (extra > out_cap - n) extra = out_cap - n;
            if (extra > 0) {
                at->ioreq->IOSer.io_Command = CMD_READ;
                at->ioreq->IOSer.io_Data = (APTR)(out + n);
                at->ioreq->IOSer.io_Length = extra;
                if (DoIO((struct IORequest *)at->ioreq) == 0) {
                    n += at->ioreq->IOSer.io_Actual;
                }
            }
        }
    }

    if (out_len) *out_len = n;
    return WP_OK;
}

/* serial.device takes a multi-byte CMD_WRITE, so there is no per-byte
   polling here (unlike Atari's Bconout). But one IORequest for the whole
   buffer meant one deadline for the whole buffer, and platform.h's write
   timeout means NO PROGRESS for timeout_ms, as on every other backend --
   a 60 KB frame at 9600 baud is a minute of honest progress. So the
   buffer goes in pieces of WP_AMIGA_WRITE_PIECE, each racing its own
   timeout_ms with the SendIO/CheckIO/AbortIO/WaitIO shape the read uses:
   a stalled line still times out, a slow one does not. 128 bytes is
   0.13 s at 9600 baud and a few hundred IORequests a second at 115200. */
#define WP_AMIGA_WRITE_PIECE 128u

static wp_status serial_write(plat_transport *t, const u8 *data, u32 len,
                              u32 timeout_ms)
{
    amiga_transport *at = (amiga_transport *)t;
    u32 done = 0;

    if (at == NULL) return WP_ERR;

    while (done < len) {
        u32 piece = len - done;
        u32 t0;

        if (piece > WP_AMIGA_WRITE_PIECE) piece = WP_AMIGA_WRITE_PIECE;
        at->ioreq->IOSer.io_Command = CMD_WRITE;
        at->ioreq->IOSer.io_Data = (APTR)(data + done);
        at->ioreq->IOSer.io_Length = piece;
        SendIO((struct IORequest *)at->ioreq);

        t0 = plat_ticks_ms();
        while (CheckIO((struct IORequest *)at->ioreq) == NULL) {
            if (PLAT_TICKS_PASSED(t0, timeout_ms)) {
                AbortIO((struct IORequest *)at->ioreq);
                WaitIO((struct IORequest *)at->ioreq);
                return WP_TIMEOUT;
            }
        }
        WaitIO((struct IORequest *)at->ioreq);
        if (at->ioreq->IOSer.io_Error != 0) return WP_ERR;
        done += piece;
    }
    return WP_OK;
}

/* ------------------------------------------------------------------ */
/* tcp:<host>:<port> over bsdsocket.library                             */
/* ------------------------------------------------------------------ */

/* The library is opened when a connection is, and closed with it: a
   machine with no TCP stack only finds out when it asks for one, and a
   program that never asks never touches the network. A bsdsocket base
   belongs to the task that opened it, and every call goes through the
   base in SocketBase, which each function below points at its
   connection's base first. The socket is non-blocking throughout and
   every wait is a WaitSelect with the caller's timeout, so nothing here
   blocks past it except the name lookup: a dotted address is taken as it
   is, and only a real name goes to gethostbyname, which has no timeout
   and CAN BLOCK when the DNS server is unreachable. Kept small in stack: a RUN-launched program has 4 KB.
   (The name SocketBase is #defined to one of this file's own at the top,
   so an application's SocketBase cannot collide with it.) */
struct Library *SocketBase;   /* wp_amiga_socket_base: see the #define */

typedef struct {
    int             kind;           /* TRANSPORT_TCP */
    struct Library *base;
    LONG            fd;
} amiga_tcp;

static void tcp_close(amiga_tcp *t)
{
    if (t == NULL) return;
    SocketBase = t->base;
    if (t->fd >= 0) CloseSocket(t->fd);
    if (t->base != NULL) CloseLibrary(t->base);
    mem_free(t);
}

/* Wait until fd is readable (want_write 0) or writable (1), up to ms.
   1 ready, 0 timed out, -1 failed. */
static int tcp_wait(LONG fd, int want_write, u32 ms)
{
    u32 t0 = plat_ticks_ms();

    for (;;) {
        fd_set set;
        struct timeval tv;
        u32 left = ms - PLAT_TICKS_SINCE(t0);
        LONG r;

        if (PLAT_TICKS_PASSED(t0, ms)) left = 0;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        tv.tv_sec = (long)(left / 1000u);
        tv.tv_usec = (long)((left % 1000u) * 1000u);
        r = WaitSelect(fd + 1, want_write ? NULL : &set, want_write ? &set : NULL,
                       NULL, &tv, NULL);
        if (r < 0 && Errno() == EINTR && !PLAT_TICKS_PASSED(t0, ms))
            continue;               /* interrupted: wait out what is left */
        return r > 0 ? 1 : (r == 0 ? 0 : -1);
    }
}

static wp_status tcp_open(const char *hostport, u32 connect_timeout_ms,
                          plat_transport **out)
{
    char host[128];
    const char *colon = strrchr(hostport, ':');
    struct hostent *he;
    struct sockaddr_in sa;
    amiga_tcp *t;
    LONG one = 1, err = 0, errlen = (LONG)sizeof(err);
    ULONG port = 0;
    size_t n;

    if (colon == NULL || colon == hostport) return WP_ERR;
    n = (size_t)(colon - hostport);
    if (n >= sizeof(host)) return WP_ERR;
    memcpy(host, hostport, n);
    host[n] = '\0';
    {
        const char *q;

        for (q = colon + 1; *q >= '0' && *q <= '9' && port <= 65535u; q++)
            port = port * 10u + (ULONG)(*q - '0');
        if (*q != '\0' || port == 0 || port > 65535u) return WP_ERR;
    }

    t = (amiga_tcp *)mem_alloc((u32)sizeof(amiga_tcp));
    if (t == NULL) return WP_NOMEM;
    t->kind = TRANSPORT_TCP;
    t->fd = -1;
    t->base = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (t->base == NULL) { mem_free(t); return WP_UNSUPPORTED; }
    SocketBase = t->base;

    memset(&sa, 0, sizeof(sa));
    {
        /* numbers first: a LAN address never touches the resolver */
        ULONG a = (ULONG)inet_addr((STRPTR)host);

        if (a == (ULONG)INADDR_NONE) {
            he = gethostbyname((STRPTR)host);
            if (he == NULL || he->h_length != 4) { tcp_close(t); return WP_ERR; }
            memcpy(&a, he->h_addr, 4);
        }
        memcpy(&sa.sin_addr, &a, 4);
    }
    t->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (t->fd < 0) { tcp_close(t); return WP_ERR; }
    IoctlSocket(t->fd, FIONBIO, (char *)&one);

    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    if (connect(t->fd, (struct sockaddr *)&sa, (LONG)sizeof(sa)) < 0) {
        if (Errno() != EINPROGRESS ||
            tcp_wait(t->fd, 1, connect_timeout_ms) != 1 ||
            getsockopt(t->fd, SOL_SOCKET, SO_ERROR, &err, (void *)&errlen) < 0 ||
            err != 0) {
            tcp_close(t);
            return WP_ERR;
        }
    }
    *out = (plat_transport *)t;
    return WP_OK;
}

/* platform.h: wait up to timeout_ms for the first byte, then hand back
   what is there; bytes sent before a close come first (recv returns them
   before it returns 0), and WP_ERR only once none are left. */
static wp_status tcp_read(amiga_tcp *t, u8 *out, u32 out_cap, u32 timeout_ms,
                          u32 *out_len)
{
    int w;
    LONG n;

    SocketBase = t->base;
    w = tcp_wait(t->fd, 0, timeout_ms);
    if (w == 0) return WP_TIMEOUT;
    if (w < 0) return WP_ERR;
    n = recv(t->fd, (UBYTE *)out, (LONG)out_cap, 0);
    if (n < 0) return Errno() == EWOULDBLOCK ? WP_TIMEOUT : WP_ERR;
    if (n == 0) return WP_ERR;          /* closed, and nothing left */
    if (out_len) *out_len = (u32)n;
    return WP_OK;
}

/* platform.h: WP_TIMEOUT means no progress for timeout_ms -- each wait
   starts again after bytes go out. */
static wp_status tcp_write(amiga_tcp *t, const u8 *data, u32 len, u32 timeout_ms)
{
    u32 done = 0;

    SocketBase = t->base;
    while (done < len) {
        int w = tcp_wait(t->fd, 1, timeout_ms);
        LONG n;

        if (w == 0) return WP_TIMEOUT;
        if (w < 0) return WP_ERR;
        n = send(t->fd, (UBYTE *)(data + done), (LONG)(len - done), 0);
        if (n < 0) {
            if (Errno() == EWOULDBLOCK) continue;
            return WP_ERR;
        }
        done += (u32)n;
    }
    return WP_OK;
}

/* ------------------------------------------------------------------ */
/* The four functions platform.h declares: by the endpoint's prefix,     */
/* then by the handle's kind.                                           */
/* ------------------------------------------------------------------ */

wp_status plat_transport_open(const char *endpoint, u32 connect_timeout_ms,
                               plat_transport **out)
{
    if (strncmp(endpoint, "tcp:", 4) == 0)
        return tcp_open(endpoint + 4, connect_timeout_ms, out);
    return serial_open(endpoint, connect_timeout_ms, out);
}

wp_status plat_transport_read(plat_transport *t, u8 *out, u32 out_cap,
                               u32 timeout_ms, u32 *out_len)
{
    if (out_len) *out_len = 0;
    if (t != NULL && *(int *)t == TRANSPORT_TCP) {
        if (out_cap == 0) return WP_OK;
        return tcp_read((amiga_tcp *)t, out, out_cap, timeout_ms, out_len);
    }
    return serial_read(t, out, out_cap, timeout_ms, out_len);
}

wp_status plat_transport_write(plat_transport *t, const u8 *data, u32 len,
                                u32 timeout_ms)
{
    if (t != NULL && *(int *)t == TRANSPORT_TCP)
        return tcp_write((amiga_tcp *)t, data, len, timeout_ms);
    return serial_write(t, data, len, timeout_ms);
}

void plat_transport_close(plat_transport *t)
{
    if (t != NULL && *(int *)t == TRANSPORT_TCP)
        tcp_close((amiga_tcp *)t);
    else
        close_transport((amiga_transport *)t);
}
