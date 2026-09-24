/*
 * sysv_rt.c - run-time pieces a statically linked AMIX program built by
 * toolchain/sysv4-cc needs and no static AMIX library provides.
 *
 * Machine: AMIX (Amiga UNIX) and Atari System V; the binary is AMIX's, and
 * runs on ASV through atari-sysv-sp1's amx module.
 */
#include <string.h>
#include <sys/utsname.h>
#include <netconfig.h>

/* GCC for an a.out target calls __main at the top of main() to run
 * constructors; the C code here has none. */
void __main(void) { }

/*
 * libsocket.a's socket() finds its transport device (/dev/tcp ...) in the
 * netconfig database, whose reader is only in the shared libnsl.so. These
 * are the inet entries of /etc/netconfig. Name lookups (their resolvers are
 * shared objects too) are not available to a static program: DISPLAY must
 * name the X server by address (192.168.1.2:0), not by host name.
 */
static struct netconfig nc_inet[] = {
	{ "tcp", NC_TPI_COTS_ORD, NC_VISIBLE, NC_INET, "tcp", "/dev/tcp", 0, 0, { 0 } },
	{ "udp", NC_TPI_CLTS,     NC_VISIBLE, NC_INET, "udp", "/dev/udp", 0, 0, { 0 } },
};
#define NNC	(int)(sizeof nc_inet / sizeof nc_inet[0])

void *setnetconfig_c(void)
{
	static int pos[8];		/* 1 + index of the next entry */
	int i;

	for (i = 0; i < 8; i++)
		if (pos[i] == 0) {
			pos[i] = 1;
			return &pos[i];
		}
	return 0;
}

struct netconfig *getnetconfig_c(void *h)
{
	int *p = h;

	if (p == 0 || *p < 1 || *p > NNC)
		return 0;
	return &nc_inet[(*p)++ - 1];
}

int endnetconfig(void *h)
{
	if (h)
		*(int *)h = 0;
	return 0;
}

/*
 * Called by the SVR4 libraries, and returning pointers: SVR4 code reads a
 * pointer result from a0, while GCC for an a.out target leaves it in d0
 * alone. So the entry points are these, which copy d0 to a0.
 */
__asm__(
"	.text\n"
"	.globl	setnetconfig\n"
"setnetconfig:\n"
"	jsr	setnetconfig_c\n"
"	move.l	%d0,%a0\n"
"	rts\n"
"	.globl	getnetconfig\n"
"getnetconfig:\n"
"	move.l	4(%sp),-(%sp)\n"
"	jsr	getnetconfig_c\n"
"	lea	4(%sp),%sp\n"
"	move.l	%d0,%a0\n"
"	rts\n");

int gethostname(char *name, int len)
{
	struct utsname u;

	if (uname(&u) < 0)
		return -1;
	strncpy(name, u.nodename, len);
	if (len > 0)
		name[len - 1] = '\0';
	return 0;
}
