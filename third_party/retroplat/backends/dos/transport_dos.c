/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <string.h>
#include <conio.h>
#include <i86.h>
#include "platform.h"

/* Real serial transport (T14.4), driving the COM port's 8250/16550 UART
   directly by port I/O -- what DOS communications programs do -- with
   the same "serial only" scope platform/atari/transport_atari.c has.
   `endpoint` prefixes other than "serial:" (including "tcp:") are
   WP_UNSUPPORTED here, honestly, not silently accepted.

   endpoint format: "serial:<device>:<baud>" (platform.h). <device> is
   the COM port: 0=COM1, 1=COM2, 2=COM3, 3=COM4. Its I/O base comes from
   the BIOS data area's table at 0040:0000 (what POST found), else the
   standard addresses. <baud> is any rate whose divisor of 115200 is a
   whole number, 300 to 115200.

   Not BIOS INT 14h, which this used to be. Its send (AH=01h) raises DTR
   and RTS and then waits for the modem's DSR and CTS before it will
   transmit -- the IBM PC BIOS's own definition -- so on a line whose
   far end does not drive those (a MiSTer ao486's COM1, whose modem lines
   come from the MiSTer's Linux side) every byte timed out unsent and
   the gateway never heard a HELLO. It also stopped at 9600 baud. The
   UART itself needs neither: 8N1 at the divisor, DTR and RTS raised for
   a far end that does watch them, and the line status register polled
   for "may send" and "byte waiting". */

#define UART_RBR 0  /* receive buffer (read) / transmit holding (write) */
#define UART_DLL 0  /* divisor latch, low  -- while LCR bit 7 is set */
#define UART_DLM 1  /* divisor latch, high -- while LCR bit 7 is set */
#define UART_IER 1  /* interrupt enable */
#define UART_FCR 2  /* FIFO control (write) */
#define UART_LCR 3  /* line control */
#define UART_MCR 4  /* modem control */
#define UART_LSR 5  /* line status */

#define LSR_DATA_READY 0x01
#define LSR_THR_EMPTY  0x20

static wp_bool parse_port(const char *s, const char *end, int *out_port)
{
    long value = 0;
    wp_bool any_digit = WP_FALSE;

    while (s < end && *s >= '0' && *s <= '9') {
        value = value * 10 + (long)(*s - '0');
        s++;
        any_digit = WP_TRUE;
    }
    if (!any_digit || s != end) return WP_FALSE;
    if (value < 0 || value > 3) return WP_FALSE;
    *out_port = (int)value;
    return WP_TRUE;
}

/* The UART's divisor for `baud`: 115200 / baud, which must divide. */
static wp_bool parse_baud(const char *s, unsigned *out_divisor)
{
    long value = 0;
    wp_bool any_digit = WP_FALSE;

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (long)(*s - '0');
        s++;
        any_digit = WP_TRUE;
    }
    if (!any_digit || *s != '\0') return WP_FALSE;
    if (value < 300 || value > 115200L || 115200L % value != 0) return WP_FALSE;
    *out_divisor = (unsigned)(115200L / value);
    return WP_TRUE;
}

typedef struct {
    unsigned base;   /* the UART's I/O base */
} dos_transport;

static unsigned port_base(int port)
{
    static const unsigned standard[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };
    unsigned short __far *bda = (unsigned short __far *)MK_FP(0x0040, 0x0000);
    unsigned base = bda[port];

    return base != 0 ? base : standard[port];
}

wp_status plat_transport_open(const char *endpoint, u32 connect_timeout_ms,
                               plat_transport **out)
{
    dos_transport *t;
    const char *rest;
    const char *colon;
    int port;
    unsigned divisor, base;

    WP_UNUSED(connect_timeout_ms); /* the UART's settings take effect at
                                       once -- no connection handshake at
                                       this layer, as transport_atari.c */

    if (strncmp(endpoint, "serial:", 7) != 0) return WP_UNSUPPORTED;
    rest = endpoint + 7;

    colon = strrchr(rest, ':');
    if (colon == NULL || colon == rest || colon[1] == '\0') return WP_ERR;
    if (!parse_port(rest, colon, &port)) return WP_ERR;
    if (!parse_baud(colon + 1, &divisor)) return WP_ERR;

    t = (dos_transport *)mem_alloc((u32)sizeof(dos_transport));
    if (t == NULL) return WP_NOMEM;
    base = port_base(port);
    t->base = base;

    outp(base + UART_IER, 0x00);                 /* polled: no interrupts */
    outp(base + UART_LCR, 0x80);                 /* divisor latch */
    outp(base + UART_DLL, divisor & 0xFF);
    outp(base + UART_DLM, (divisor >> 8) & 0xFF);
    outp(base + UART_LCR, 0x03);                 /* 8N1 */
    outp(base + UART_FCR, 0x07);                 /* FIFOs on and cleared (a 16550; an 8250 ignores it) */
    outp(base + UART_MCR, 0x03);                 /* DTR and RTS, for a far end that watches them */
    while (inp(base + UART_LSR) & LSR_DATA_READY) (void)inp(base + UART_RBR);

    *out = (plat_transport *)t;
    return WP_OK;
}

/* Waits up to timeout_ms for the FIRST byte (platform.h's contract), then
   drains whatever else is already there up to out_cap without waiting --
   the "wait once, then drain" shape transport_atari.c's Bconstat/Bconin
   pair has, so offload/transport/wire.c's frame reads take fewer calls. */
wp_status plat_transport_read(plat_transport *t, u8 *out, u32 out_cap,
                               u32 timeout_ms, u32 *out_len)
{
    dos_transport *dt = (dos_transport *)t;
    u32 deadline;
    u32 n = 0;

    if (out_len) *out_len = 0;
    if (out_cap == 0) return WP_OK;
    if (dt == NULL) return WP_ERR;

    deadline = plat_ticks_ms() + timeout_ms;
    while ((inp(dt->base + UART_LSR) & LSR_DATA_READY) == 0) {
        if (plat_ticks_ms() >= deadline) return WP_TIMEOUT;
    }
    while (n < out_cap && (inp(dt->base + UART_LSR) & LSR_DATA_READY) != 0) {
        out[n++] = (u8)inp(dt->base + UART_RBR);
    }

    if (out_len) *out_len = n;
    return WP_OK;
}

/* Each byte waits for "transmit holding register empty" with its own
   timeout_ms, as transport_atari.c's per-byte Bcostat loop does. */
wp_status plat_transport_write(plat_transport *t, const u8 *data, u32 len,
                                u32 timeout_ms)
{
    dos_transport *dt = (dos_transport *)t;
    u32 i;

    if (dt == NULL) return WP_ERR;
    for (i = 0; i < len; i++) {
        u32 deadline = plat_ticks_ms() + timeout_ms;

        while ((inp(dt->base + UART_LSR) & LSR_THR_EMPTY) == 0) {
            if (plat_ticks_ms() >= deadline) return WP_TIMEOUT;
        }
        outp(dt->base + UART_RBR, data[i]);
    }
    return WP_OK;
}

void plat_transport_close(plat_transport *t)
{
    mem_free(t);
}
