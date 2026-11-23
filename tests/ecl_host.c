/*
 * ecl_host.c — a host-side driver for src/engine/ecl.c, used only by
 * tests/test_ecl_c.py. Not part of any engine build.
 *
 *   ecl_host decode <dialect> <block>         offsets on stdin, one per line;
 *                                             prints "off size opcode argc"
 *   ecl_host run <dialect> <block> <entry> <budget> [addr=value ...]
 *                                             runs one entry point with a host
 *                                             that logs every callback
 *
 * The run log is line-oriented so the test can compare it exactly:
 *   HOST <opcode> <argc> <rvalue>... [| text | text ...]
 *   STATUS <status> pc=<offset> steps=<n> unhandled=<n> chain=<n> fault=<offset>
 *   MEM <addr> <value>                        every non-zero cell, ascending
 *   HVAR <addr> <value>                       the same for host variables
 *
 * A run argument "h<addr>=<value>" turns on a host variable hook that serves
 * 0x401..0x800 from its own table (as the Dark Queen's character cells are
 * served), preset to <value>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/ecl.h"

static void *host_alloc(void *ctx, unsigned long bytes)
{
	(void)ctx;
	return calloc(1, bytes);
}

static unsigned short hvar[0x400];
static int             hvar_on;

static int host_var(void *ctx, unsigned short addr, unsigned short *v, int write)
{
	(void)ctx;
	if (!hvar_on || addr < 0x401 || addr > 0x800)
		return 0;
	if (write)
		hvar[addr - 0x401] = *v;
	else
		*v = hvar[addr - 0x401];
	return 1;
}

static int host_op(void *ctx, ecl_vm *vm, const ecl_insn *ins)
{
	unsigned i;
	int texts = 0;

	(void)ctx;
	printf("HOST %u %u", ins->opcode, ins->argc);
	for (i = 0; i < ins->argc; i++)
		printf(" %u", ecl_rvalue(vm, &ins->arg[i]));
	for (i = 0; i < ins->argc; i++) {
		char buf[512];

		if (ins->arg[i].code != ECL_C_TEXT)
			continue;
		ecl_text(vm->raw, ins->arg[i].text_off, ins->arg[i].text_len,
		         buf, sizeof buf);
		printf("%s%s", texts++ ? " | " : " | ", buf);
	}
	printf("\n");
	if (ins->opcode == ECL_OP_COMBAT)
		return ECL_HOST_COMBAT;
	if (ins->opcode == 0x23)        /* the short dialects' "clear and exit" */
		return ECL_HOST_EXIT;
	if (ins->opcode == 0x22) {      /* ... and their yes/no: answer yes     */
		ecl_set_cmp(vm, 0, 0);
		vm->cmp[2] = vm->cmp[3] = vm->cmp[4] = vm->cmp[5] = 0;
	}
	return ECL_HOST_OK;
}

static const ecl_dialect *dialect(const char *name)
{
	int i;

	for (i = 0; i < ECL_NDIALECTS; i++)
		if (strcmp(ecl_dialect_get(i)->name, name) == 0)
			return ecl_dialect_get(i);
	fprintf(stderr, "unknown dialect %s\n", name);
	exit(2);
}

static unsigned char *slurp(const char *path, unsigned *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf = malloc(0x10000);
	size_t n;

	if (f == NULL || buf == NULL) {
		fprintf(stderr, "cannot read %s\n", path);
		exit(2);
	}
	n = fread(buf, 1, 0x10000, f);
	fclose(f);
	*len = (unsigned)n;
	return buf;
}

int main(int argc, char **argv)
{
	static ecl_vm vm;
	static ecl_insn ins;
	ecl_host host = { NULL, host_alloc, host_op, host_var };
	const ecl_dialect *d;
	unsigned char *raw;
	unsigned len;

	if (argc < 4)
		return 2;
	d   = dialect(argv[2]);
	raw = slurp(argv[3], &len);

	if (strcmp(argv[1], "decode") == 0) {
		unsigned off;

		while (scanf("%u", &off) == 1) {
			unsigned size = ecl_decode(raw, len, d, off, &ins);

			if (size == 0)
				printf("%u 0 0 0\n", off);
			else
				printf("%u %u %u %u\n", off, size, ins.opcode, ins.argc);
			/* the size-only path must agree with the full decode */
			if (ecl_decode(raw, len, d, off, NULL) != size) {
				fprintf(stderr, "size-only decode disagrees at %u\n", off);
				return 1;
			}
		}
		return 0;
	}

	if (strcmp(argv[1], "run") == 0 && argc >= 6) {
		int status, i;
		unsigned long a;

		ecl_init(&vm, &host);
		if (!ecl_load(&vm, raw, len, d)) {
			printf("STATUS load-failed\n");
			return 0;
		}
		for (i = 6; i < argc; i++) {
			unsigned addr, value;

			if (sscanf(argv[i], "h%u=%u", &addr, &value) == 2
			 && addr >= 0x401 && addr <= 0x800) {
				hvar_on = 1;
				hvar[addr - 0x401] = (unsigned short)value;
			} else if (sscanf(argv[i], "%u=%u", &addr, &value) == 2)
				ecl_poke(&vm, (unsigned short)addr, (unsigned short)value);
		}
		if (!ecl_enter(&vm, atoi(argv[4]))) {
			printf("STATUS enter-failed\n");
			return 0;
		}
		status = ecl_run(&vm, atol(argv[5]));
		/* A paused script resumes where it stopped. Bounded: a real script can
		 * loop back to its fight for ever (it is waiting on the outcome this
		 * driver never supplies), and the budget is per ecl_run() call. */
		for (i = 0; i < 16 && (status == ECL_COMBAT || status == ECL_WAIT); i++) {
			printf("PAUSE %d pc=%u\n", status, vm.pc);
			status = ecl_run(&vm, atol(argv[5]));
		}
		printf("STATUS %d pc=%u steps=%lu unhandled=%lu chain=%u fault=%u\n",
		       status, vm.pc, vm.steps, vm.unhandled, vm.chain_block,
		       vm.fault_pc);
		for (a = 0; a < 0x10000UL; a++)
			if (ecl_peek(&vm, (unsigned short)a))
				printf("MEM %lu %u\n", a, ecl_peek(&vm, (unsigned short)a));
		for (a = 0; a < 0x400; a++)
			if (hvar[a])
				printf("HVAR %lu %u\n", a + 0x401, hvar[a]);
		return 0;
	}
	return 2;
}
