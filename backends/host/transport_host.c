/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <stdlib.h>
#include "platform.h"

/* plat_transport is an opaque handle per platform.h; this struct is
   private to this backend. Endpoint strings select the underlying
   mechanism by prefix:
     "pipe:<path>"       a named FIFO, opened O_RDWR (a Linux extension
                          that lets one process use a single fd as both
                          ends -- host-only test scaffolding, never
                          shipped to a real target, just enough to prove
                          the plat_transport_* seam compiles and links).
     "tcp:<host>:<port>" a real loopback/LAN TCP client socket, used to
                          reach the mock (and later, real) gateway.
     "serial:<device>:<baud>" a tty -- a USB serial adapter, or one end
                          of a pty pair in tests -- raw, 8N1, no flow
                          control; its speed can be switched
                          (plat_transport_set_speed).
   A later Atari backend adds "serial:<device>:<baud>" as a fifth,
   independent implementation of these same four functions -- no change
   to callers is required when that lands. Both mechanisms here share one
   host_transport (an fd plus fifo-specific cleanup state) because
   plat_transport_read/write/close's poll-then-read/write pattern works
   identically over a FIFO fd and a TCP socket fd. */
typedef struct {
    int  fd;
    int  owns_fifo;
    char fifo_path[256];
    u32  baud;          /* a tty's speed now; 0 for a pipe or socket */
} host_transport;

static wp_status open_pipe(const char *path, host_transport **out)
{
    host_transport *t;
    int fd;
    int created = 0;

    if (mkfifo(path, 0600) == 0) {
        created = 1;
    } else if (errno != EEXIST) {
        return WP_ERR;
    }

    fd = open(path, O_RDWR);
    if (fd < 0) return WP_ERR;

    t = (host_transport *)mem_alloc((u32)sizeof(host_transport));
    if (t == NULL) { close(fd); return WP_NOMEM; }

    t->fd = fd;
    t->baud = 0;
    t->owns_fifo = created;
    strncpy(t->fifo_path, path, sizeof(t->fifo_path) - 1);
    t->fifo_path[sizeof(t->fifo_path) - 1] = '\0';

    *out = t;
    return WP_OK;
}

static wp_status open_tcp(const char *hostport, u32 connect_timeout_ms,
                           host_transport **out)
{
    char host[128];
    char port[16];
    const char *colon;
    size_t host_len, port_len;
    struct addrinfo hints, *res, *rp;
    int fd = -1;
    host_transport *t;

    /* Split on the LAST ':' so a bare hostname/IPv4 address before it is
       taken verbatim; this backend does not need to parse IPv6 literals
       for host-only test scaffolding. */
    colon = strrchr(hostport, ':');
    if (colon == NULL) return WP_ERR;

    host_len = (size_t)(colon - hostport);
    if (host_len == 0 || host_len >= sizeof(host)) return WP_ERR;
    memcpy(host, hostport, host_len);
    host[host_len] = '\0';

    port_len = strlen(colon + 1);
    if (port_len == 0 || port_len >= sizeof(port)) return WP_ERR;
    memcpy(port, colon + 1, port_len + 1);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) return WP_ERR;

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        int flags;
        int rc;

        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        /* Non-blocking connect with a poll-based timeout, matching the
           poll-then-op pattern the read/write functions below use. */
        flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc < 0 && errno != EINPROGRESS) {
            close(fd);
            fd = -1;
            continue;
        }
        if (rc < 0) {
            struct pollfd pfd;
            int pr;
            int sock_err = 0;
            socklen_t elen = sizeof(sock_err);

            pfd.fd = fd;
            pfd.events = POLLOUT;
            pr = poll(&pfd, 1, (int)connect_timeout_ms);
            if (pr <= 0) { close(fd); fd = -1; continue; }

            getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_err, &elen);
            if (sock_err != 0) { close(fd); fd = -1; continue; }
        }
        fcntl(fd, F_SETFL, flags); /* restore blocking mode */
        break; /* connected */
    }
    freeaddrinfo(res);

    if (fd < 0) return WP_ERR;

    t = (host_transport *)mem_alloc((u32)sizeof(host_transport));
    if (t == NULL) { close(fd); return WP_NOMEM; }

    t->fd = fd;
    t->baud = 0;
    t->owns_fifo = 0;
    t->fifo_path[0] = '\0';

    *out = t;
    return WP_OK;
}

/* The rates a host tty is offered at, fastest first: the common ones a
   USB adapter and a Pi's UART both take. */
static const u32 k_host_rates[] = { 115200uL, 57600uL, 38400uL, 19200uL, 9600uL };
#define HOST_RATE_COUNT ((u32)(sizeof(k_host_rates) / sizeof(k_host_rates[0])))

static speed_t host_speed_of(u32 baud)
{
    switch (baud) {
    case 115200uL: return B115200;
    case 57600uL:  return B57600;
    case 38400uL:  return B38400;
    case 19200uL:  return B19200;
    case 9600uL:   return B9600;
    case 4800uL:   return B4800;
    case 2400uL:   return B2400;
    case 1200uL:   return B1200;
    default:       return (speed_t)0;
    }
}

/* Raw 8N1 at `baud`, CLOCAL, no flow control; output drained first so a
   speed change never cuts off bytes already written. */
static wp_status host_tty_setup(int fd, u32 baud)
{
    struct termios tio;
    speed_t sp = host_speed_of(baud);

    if (sp == (speed_t)0) return WP_RANGE;
    if (tcgetattr(fd, &tio) != 0) return WP_ERR;
    (void)tcdrain(fd);
    cfmakeraw(&tio);
    tio.c_cflag |= (tcflag_t)(CLOCAL | CREAD);
    tio.c_cflag &= (tcflag_t)~CRTSCTS;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    return tcsetattr(fd, TCSANOW, &tio) == 0 ? WP_OK : WP_ERR;
}

/* "serial:<device>:<baud>": the device is everything before the last
   colon (a path may hold colons), the baud after it. */
static wp_status open_serial(const char *spec, host_transport **out)
{
    char dev[256];
    const char *colon = strrchr(spec, ':');
    host_transport *t;
    unsigned long baud;
    char *end;
    size_t n;
    int fd;

    if (colon == NULL || colon == spec) return WP_ERR;
    n = (size_t)(colon - spec);
    if (n >= sizeof(dev)) return WP_ERR;
    memcpy(dev, spec, n);
    dev[n] = '\0';
    baud = strtoul(colon + 1, &end, 10);
    if (*end != '\0' || host_speed_of((u32)baud) == (speed_t)0) return WP_ERR;

    fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) return WP_ERR;
    if (!isatty(fd) || host_tty_setup(fd, (u32)baud) != WP_OK) { close(fd); return WP_ERR; }

    t = (host_transport *)mem_alloc((u32)sizeof(host_transport));
    if (t == NULL) { close(fd); return WP_NOMEM; }
    t->fd = fd;
    t->baud = (u32)baud;
    t->owns_fifo = 0;
    t->fifo_path[0] = '\0';
    *out = t;
    return WP_OK;
}

u32 plat_transport_speeds(plat_transport *t, u32 *rates, u32 cap, u32 *current)
{
    host_transport *ht = (host_transport *)t;
    u32 i, n = 0;

    if (current) *current = (ht != NULL) ? ht->baud : 0;
    if (ht == NULL || ht->baud == 0) return 0;
    for (i = 0; i < HOST_RATE_COUNT && n < cap; i++) rates[n++] = k_host_rates[i];
    return n;
}

wp_status plat_transport_set_speed(plat_transport *t, u32 baud)
{
    host_transport *ht = (host_transport *)t;
    u32 i;
    wp_status st;

    if (ht == NULL || ht->baud == 0) return WP_UNSUPPORTED;
    for (i = 0; i < HOST_RATE_COUNT; i++) if (k_host_rates[i] == baud) break;
    if (i == HOST_RATE_COUNT) return WP_RANGE;
    st = host_tty_setup(ht->fd, baud);
    if (st == WP_OK) ht->baud = baud;
    return st;
}

wp_status plat_transport_open(const char *endpoint, u32 connect_timeout_ms,
                               plat_transport **out)
{
    if (strncmp(endpoint, "serial:", 7) == 0) {
        WP_UNUSED(connect_timeout_ms);
        return open_serial(endpoint + 7, (host_transport **)out);
    }
    if (strncmp(endpoint, "pipe:", 5) == 0) {
        WP_UNUSED(connect_timeout_ms); /* FIFO/loopback open is instantaneous */
        return open_pipe(endpoint + 5, (host_transport **)out);
    }
    if (strncmp(endpoint, "tcp:", 4) == 0) {
        return open_tcp(endpoint + 4, connect_timeout_ms, (host_transport **)out);
    }
    return WP_UNSUPPORTED;
}

wp_status plat_transport_read(plat_transport *t, u8 *out, u32 out_cap,
                               u32 timeout_ms, u32 *out_len)
{
    host_transport *ht = (host_transport *)t;
    struct pollfd pfd;
    int pr;
    ssize_t n;

    pfd.fd = ht->fd;
    pfd.events = POLLIN;

    pr = poll(&pfd, 1, (int)timeout_ms);
    if (pr == 0) return WP_TIMEOUT;
    if (pr < 0) return WP_ERR;
    /* Data before hang-up (platform.h): a peer that sends its last bytes
       and closes -- every HTTP/1.0 server, a gateway answering and
       exiting -- gives POLLIN|POLLHUP together, and those bytes are
       still to be read. Only a hang-up with nothing left is WP_ERR. */
    if (!(pfd.revents & POLLIN)) {
        if (pfd.revents & (POLLHUP | POLLERR)) return WP_ERR;
        return WP_TIMEOUT;
    }

    n = read(ht->fd, out, (size_t)out_cap);
    if (n < 0) return WP_ERR;
    if (n == 0) return WP_ERR; /* peer closed */

    if (out_len) *out_len = (u32)n;
    return WP_OK;
}

/* A write to a socket or FIFO whose reader has gone raises SIGPIPE, and
   its default action ends the process -- the whole test run, or a host
   build of the program, killed by a gateway exiting. platform.h promises
   WP_ERR instead. A socket can say so per call (MSG_NOSIGNAL); a FIFO
   cannot, so the first write sets SIGPIPE to ignored -- only if the
   program left it at the default, so a program that chose a handler
   keeps it -- and write() then fails with EPIPE. */
static void ignore_sigpipe_once(void)
{
    static int done = 0;
    struct sigaction now;

    if (done) return;
    done = 1;
    if (sigaction(SIGPIPE, NULL, &now) == 0 && now.sa_handler == SIG_DFL) {
        struct sigaction ign;
        memset(&ign, 0, sizeof(ign));
        ign.sa_handler = SIG_IGN;
        sigemptyset(&ign.sa_mask);
        sigaction(SIGPIPE, &ign, NULL);
    }
}

wp_status plat_transport_write(plat_transport *t, const u8 *data, u32 len,
                                u32 timeout_ms)
{
    host_transport *ht = (host_transport *)t;
    struct pollfd pfd;
    int pr;
    ssize_t n;
    u32 written = 0;

    ignore_sigpipe_once();
    while (written < len) {
        pfd.fd = ht->fd;
        pfd.events = POLLOUT;

        pr = poll(&pfd, 1, (int)timeout_ms);
        if (pr == 0) return WP_TIMEOUT;
        if (pr < 0) return WP_ERR;
        if (pfd.revents & (POLLHUP | POLLERR)) return WP_ERR;

        n = write(ht->fd, data + written, (size_t)(len - written));
        if (n < 0) return WP_ERR;
        written += (u32)n;
    }
    return WP_OK;
}

void plat_transport_close(plat_transport *t)
{
    host_transport *ht = (host_transport *)t;

    if (ht == NULL) return;
    close(ht->fd);
    if (ht->owns_fifo) unlink(ht->fifo_path);
    mem_free(ht);
}
