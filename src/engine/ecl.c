/*
 * ecl.c — the Gold Box ECL interpreter core. See ecl.h.
 *
 * Semantics follow the Greybox reference VM (packages/engine/src/ecl/vm.ts in
 * kblood/Greybox_CombinedGoldbox, 0BSD), which in turn checked each one below
 * against the games' own handlers. The points where a plausible implementation
 * goes wrong are marked ★ — each cost that project a debugging round.
 */
#include <string.h>

#include "ecl.h"

/* ---- dialects ---------------------------------------------------------- */

#define F(n)      { (n), -1 }
#define FC(n, c)  { (n), (c) }

/* Champions of Krynn — the base table, opcode 0x00 upward. */
#define ECL_BASE_FRAMES \
	F(0), F(1), F(1), F(2), F(3), F(3), F(3), F(3),       /* 00 EXIT..MULTIPLY     */ \
	F(2), F(2), F(1), F(3), F(3), F(0), F(1), F(2),       /* 08 RANDOM..INPUT_NUM  */ \
	F(2), F(1), F(1), F(0), F(4), FC(3, 2), F(0), F(0),   /* 10 INPUT_STR..IF_NE   */ \
	F(0), F(0), F(0), F(0), F(0), F(1), F(6), F(2),       /* 18 IF_LT..UNKNOWN_1F  */ \
	F(1), F(3), F(2), F(4), F(0), FC(2, 1), FC(2, 1), F(8), /* 20 NEWECL..TREASURE */ \
	F(3), F(14), F(3), FC(2, 1), F(6), F(1), F(5), F(3),  /* 28 ROB..AND           */ \
	F(3), F(0), F(1), F(0), F(2), F(3), F(2), F(3),       /* 30 OR..LOAD_PIECES    */ \
	F(1), F(1), F(0), F(3), F(1), F(0), F(0), F(1),       /* 38 PROGRAM..FIND_SPEC */ \
	F(1), F(2)                                            /* 40 DESTROY, AWARD_XP  */

static ecl_dialect g_dialect[ECL_NDIALECTS] = {
	{ "gen1",     22, 1, 0x7FFE, 0x7F3F, { ECL_BASE_FRAMES } },
	{ "gen1-dok", 22, 1, 0x7FFE, 0x7F3F, { ECL_BASE_FRAMES } },
	{ "gen2",     20, 0, 0x8000, 0x0029, { ECL_BASE_FRAMES } },
};
static unsigned char g_dialect_ready;

static void frame_set(ecl_dialect *d, unsigned op, unsigned fixed, int count_at)
{
	d->frame[op].fixed    = (unsigned char)fixed;
	d->frame[op].count_at = (signed char)count_at;
}

/* The "short" operand table Death Knights and The Dark Queen share, each read
 * out of that game's own handlers (Greybox: ECL_GEN1_SHORT_DOK, ECL_GEN2). */
static void frames_short(ecl_dialect *d)
{
	frame_set(d, 0x0c, 4, -1);      /* SETUP_MONSTER                         */
	frame_set(d, 0x1d, 0, -1);      /* "press Return" here, not PARTYSTRENGTH */
	frame_set(d, 0x1f, 1, -1);
	frame_set(d, 0x22, 0, -1);      /* a yes/no question that sets the flags */
	frame_set(d, 0x23, 0, -1);      /* clear the text box and EXIT           */
	frame_set(d, 0x27, 4, 3);       /* TREASURE: three coin piles + N items  */
	frame_set(d, 0x29, 0, -1);      /* does nothing in The Dark Queen        */
	frame_set(d, 0x2c, 1, -1);      /* ★ PARLAY: 6 in Champions. Read wrong, */
	                                /*   it swallows the COMBAT behind it.   */
	frame_set(d, 0x31, 2, 1);       /* a menu here, SPRITE_OFF in Champions  */
}

const ecl_dialect *ecl_dialect_get(int id)
{
	if (!g_dialect_ready) {
		frames_short(&g_dialect[ECL_GEN1_DOK]);
		frames_short(&g_dialect[ECL_GEN2]);
		frame_set(&g_dialect[ECL_GEN2], 0x21, 2, -1);   /* LOAD_FILES   */
		frame_set(&g_dialect[ECL_GEN2], 0x37, 2, -1);   /* FIND_NPC     */
		g_dialect_ready = 1;
	}
	if (id < 0 || id >= ECL_NDIALECTS)
		return 0;
	return &g_dialect[id];
}

/* ---- decoding ---------------------------------------------------------- */

/* One operand at *pos. Two bytes, three for the word codes; inline text is
 * skipped over and located, not copied. */
static int opnd_read(const unsigned char *raw, unsigned len, unsigned *pos,
                     ecl_opnd *out)
{
	unsigned p = *pos;
	unsigned char code;
	unsigned value;

	if (p + 2 > len)
		return 0;
	code  = raw[p];
	value = raw[p + 1];
	p += 2;
	if (out) {
		out->code     = code;
		out->text_len = 0;
		out->text_off = 0;
	}
	if (code == ECL_C_MEM || code == ECL_C_IMM16 || code == ECL_C_MEM3
	 || code == ECL_C_STRPTR) {
		if (p >= len)
			return 0;
		value |= (unsigned)raw[p] << 8;
		p++;
	} else if (code == ECL_C_TEXT) {
		if (p + value > len)
			return 0;
		if (out) {
			out->text_len = (unsigned char)value;
			out->text_off = (unsigned short)p;
		}
		p += value;
	}
	if (out)
		out->word = (unsigned short)value;
	*pos = p;
	return 1;
}

unsigned ecl_decode(const unsigned char *raw, unsigned len,
                    const ecl_dialect *d, unsigned off, ecl_insn *out)
{
	const ecl_frame *f;
	unsigned pos, argc = 0, extra = 0, i;
	unsigned char opcode;
	ecl_opnd tmp;

	if (off >= len)
		return 0;
	opcode = raw[off];
	if (opcode >= ECL_NOPS)
		return 0;
	f = &d->frame[opcode];
	if (f->fixed == ECL_FRAME_NONE)
		return 0;
	pos = off + 1;
	for (i = 0; i < f->fixed; i++, argc++) {
		ecl_opnd *slot = out ? &out->arg[argc] : &tmp;

		if (!opnd_read(raw, len, &pos, slot))
			return 0;
		if (f->count_at == (int)i)
			extra = slot->word;
	}
	if (argc + extra > ECL_MAX_ARGS)
		return 0;
	for (i = 0; i < extra; i++, argc++)
		if (!opnd_read(raw, len, &pos, out ? &out->arg[argc] : 0))
			return 0;
	if (out) {
		out->offset = (unsigned short)off;
		out->size   = (unsigned short)(pos - off);
		out->argc   = (unsigned short)argc;
		out->opcode = opcode;
	}
	return pos - off;
}

unsigned ecl_text(const unsigned char *raw, unsigned off, unsigned packed_len,
                  char *out, unsigned outmax)
{
	unsigned long acc = 0;
	unsigned bits = 0, n = 0, i;

	if (outmax == 0)
		return 0;
	for (i = 0; i < packed_len; i++) {
		acc = ((acc << 8) | raw[off + i]) & 0xFFFFFFUL;
		bits += 8;
		while (bits >= 6) {
			unsigned v;

			bits -= 6;
			v = (unsigned)(acc >> bits) & 0x3F;
			if (v == 0) {                   /* terminator */
				out[n] = 0;
				return n;
			}
			if (n + 1 < outmax)             /* 1..31 are '@'+v, the rest as-is */
				out[n++] = (char)(v <= 31 ? v + 64 : v);
		}
	}
	out[n] = 0;
	return n;
}

/* ---- script memory ----------------------------------------------------- */

unsigned short ecl_peek(const ecl_vm *vm, unsigned short addr)
{
	const unsigned short *pg = vm->page[addr >> 8];

	return pg ? pg[addr & 0xFF] : 0;        /* never written reads as 0 */
}

int ecl_poke(ecl_vm *vm, unsigned short addr, unsigned short v)
{
	unsigned short *pg = vm->page[addr >> 8];

	if (pg == 0) {
		if (vm->host == 0 || vm->host->alloc == 0)
			return 0;
		pg = (unsigned short *)vm->host->alloc(vm->host->ctx,
		                                       256UL * sizeof *pg);
		if (pg == 0)
			return 0;
		vm->page[addr >> 8] = pg;
	}
	pg[addr & 0xFF] = v;
	return 1;
}

unsigned short ecl_rvalue(const ecl_vm *vm, const ecl_opnd *a)
{
	switch (a->code) {
	case ECL_C_MEM:
	case ECL_C_MEM3:
	case ECL_C_GAMEVAR:
		return ecl_peek(vm, a->word);       /* a variable: its contents */
	default:
		return a->word;                     /* a literal, or a marker   */
	}
}

void ecl_set_cmp(ecl_vm *vm, long left, long right)
{
	vm->cmp[0] = (unsigned char)(left == right);
	vm->cmp[1] = (unsigned char)(left != right);
	vm->cmp[2] = (unsigned char)(left <  right);
	vm->cmp[3] = (unsigned char)(left >  right);
	vm->cmp[4] = (unsigned char)(left <= right);
	vm->cmp[5] = (unsigned char)(left >= right);
}

/* ---- the machine ------------------------------------------------------- */

void ecl_init(ecl_vm *vm, const ecl_host *host)
{
	unsigned char *p = (unsigned char *)vm;
	unsigned long  n = sizeof *vm;

	while (n--)
		*p++ = 0;
	vm->host = host;
	vm->rng  = 0x1234ABCDUL;
}

int ecl_load(ecl_vm *vm, const unsigned char *raw, unsigned len,
             const ecl_dialect *d)
{
	if (d == 0 || raw == 0 || len < d->header_len || len > 0xFFFFU)
		return 0;
	if (d->magic && !(raw[0] == 0x88 && raw[1] == 0x13))
		return 0;
	vm->raw = raw;
	vm->len = (unsigned short)len;
	vm->d   = d;
	vm->pc  = d->header_len;
	vm->sp  = 0;
	return 1;
}

/* A jump operand names a virtual address; turn it into a block offset. */
static unsigned target_of(const ecl_vm *vm, const ecl_opnd *a)
{
	unsigned w = a->word;

	return w >= vm->d->vbase ? w - vm->d->vbase : w;
}

static int in_code(const ecl_vm *vm, unsigned off)
{
	return off >= vm->d->header_len && off < vm->len;
}

int ecl_enter(ecl_vm *vm, int entry)
{
	unsigned base, vaddr;

	if (vm->raw == 0 || entry < 0 || entry > 4)
		return 0;
	base  = (vm->d->magic ? 2U : 0U) + 4U * (unsigned)entry;
	vaddr = vm->raw[base + 2] | ((unsigned)vm->raw[base + 3] << 8);
	if (vaddr < vm->d->vbase || !in_code(vm, vaddr - vm->d->vbase))
		return 0;
	vm->pc = (unsigned short)(vaddr - vm->d->vbase);
	vm->sp = 0;
	return 1;
}

static unsigned long rng_next(ecl_vm *vm)
{
	/* Not the original generator, which is not recoverable from the scripts:
	 * a plain LCG, so a seeded run is repeatable. */
	vm->rng = vm->rng * 1664525UL + 1013904223UL;
	return (vm->rng >> 16) & 0x7FFFUL;
}

static int fault(ecl_vm *vm, unsigned at)
{
	vm->fault_pc = (unsigned short)at;
	return ECL_FAULT;
}

unsigned ecl_str_get(const ecl_vm *vm, unsigned short addr, char *out,
                     unsigned outmax)
{
	unsigned n = 0;

	if (outmax == 0)
		return 0;
	while (n + 1 < outmax && n < ECL_STR_MAX) {
		unsigned short c = ecl_peek(vm, (unsigned short)(addr + n));

		if (c == 0)
			break;
		out[n++] = (char)c;
	}
	out[n] = 0;
	return n;
}

int ecl_str_put(ecl_vm *vm, unsigned short addr, const char *s)
{
	unsigned n;

	for (n = 0; n < ECL_STR_MAX && s[n]; n++)
		if (!ecl_poke(vm, (unsigned short)(addr + n), (unsigned char)s[n]))
			return 0;
	return ecl_poke(vm, (unsigned short)(addr + n), 0);
}

/* The text an operand stands for, when it is text: inline, or a string cell.
 * Returns 0 when it is a number. */
static int opnd_str(const ecl_vm *vm, const ecl_opnd *a, char *out, unsigned outmax)
{
	if (a->code == ECL_C_TEXT) {
		ecl_text(vm->raw, a->text_off, a->text_len, out, outmax);
		return 1;
	}
	if (a->code == ECL_C_STRPTR) {
		ecl_str_get(vm, a->word, out, outmax);
		return 1;
	}
	return 0;
}

static int call_host(ecl_vm *vm, const ecl_insn *ins, int *status)
{
	int r;

	if (vm->host == 0 || vm->host->op == 0) {
		vm->unhandled++;
		return 0;
	}
	r = vm->host->op(vm->host->ctx, vm, ins);
	switch (r) {
	case ECL_HOST_OK:        return 0;
	case ECL_HOST_UNHANDLED: vm->unhandled++; return 0;
	case ECL_HOST_WAIT:      *status = ECL_WAIT;   return 1;
	case ECL_HOST_COMBAT:    *status = ECL_COMBAT; return 1;
	case ECL_HOST_EXIT:      *status = ECL_EXITED; return 1;
	default:                 *status = fault(vm, ins->offset); return 1;
	}
}

static int has_text(const ecl_insn *ins)
{
	unsigned i;

	for (i = 0; i < ins->argc; i++)
		if (ins->arg[i].code == ECL_C_TEXT || ins->arg[i].code == ECL_C_STRPTR)
			return 1;
	return 0;
}

int ecl_run(ecl_vm *vm, long budget)
{
	ecl_insn *ins = &vm->ins;
	const ecl_opnd *a = ins->arg;

	if (vm->raw == 0)
		return ECL_FAULT;
	while (budget-- > 0) {
		unsigned at = vm->pc, next, t;
		int status;

		if (!in_code(vm, at) || !ecl_decode(vm->raw, vm->len, vm->d, at, ins))
			return fault(vm, at);
		next = at + ins->size;
		vm->steps++;
		vm->pc = (unsigned short)next;

		switch (ins->opcode) {
		case ECL_OP_EXIT:
			return ECL_EXITED;

		case ECL_OP_GOTO:
		case ECL_OP_GOSUB:
			t = target_of(vm, &a[0]);
			if (!in_code(vm, t)) {
				/* another block's address: not ours to follow */
				vm->unhandled++;
				break;
			}
			if (ins->opcode == ECL_OP_GOSUB) {
				if (vm->sp >= ECL_STACK)
					return fault(vm, at);
				vm->stack[vm->sp++] = (unsigned short)next;
			}
			vm->pc = (unsigned short)t;
			break;

		case ECL_OP_RETURN:
			if (vm->sp == 0)
				return ECL_EXITED;
			vm->pc = vm->stack[--vm->sp];
			break;

		case ECL_OP_COMPARE: {
			char l[ECL_STR_MAX * 4 + 1], r[ECL_STR_MAX * 4 + 1];
			int  lt = opnd_str(vm, &a[0], l, sizeof l);
			int  rt = opnd_str(vm, &a[1], r, sizeof r);

			if (lt || rt) {
				/* ★ Text compares as text: equal or not, and the four
				 * ordering flags follow (<= and >= pass when equal). A
				 * number against text is never equal. */
				int eq = lt && rt && strcmp(l, r) == 0;

				vm->cmp[0] = (unsigned char)eq;
				vm->cmp[1] = (unsigned char)!eq;
				vm->cmp[2] = vm->cmp[3] = 0;
				vm->cmp[4] = vm->cmp[5] = (unsigned char)eq;
			} else {
				ecl_set_cmp(vm, ecl_rvalue(vm, &a[0]), ecl_rvalue(vm, &a[1]));
			}
			if (has_text(ins) && call_host(vm, ins, &status))
				return status;
			break;
		}

		case ECL_OP_COMPARE_AND: {
			/* ★ Sets ONLY == and !=. The other four flags go FALSE, so an
			 * IF_LE or IF_GE after one of these never passes. Treating it as
			 * an ordinary compare makes those branch spuriously. */
			int both = ecl_rvalue(vm, &a[0]) == ecl_rvalue(vm, &a[1])
			        && ecl_rvalue(vm, &a[2]) == ecl_rvalue(vm, &a[3]);

			vm->cmp[0] = (unsigned char)both;
			vm->cmp[1] = (unsigned char)!both;
			vm->cmp[2] = vm->cmp[3] = vm->cmp[4] = vm->cmp[5] = 0;
			break;
		}

		case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1b:
			/* IF_*: guards exactly the NEXT instruction. When the flag is
			 * clear, decode that one only to step over it. */
			if (!vm->cmp[ins->opcode - ECL_OP_IF_EQ]) {
				unsigned skip = ecl_decode(vm->raw, vm->len, vm->d, next, 0);

				if (skip == 0)
					return fault(vm, next);
				vm->pc = (unsigned short)(next + skip);
			}
			break;

		case ECL_OP_SAVE: {             /* SAVE value, destination */
			char t[ECL_STR_MAX * 4 + 1];

			if (a[1].code == ECL_C_STRPTR && opnd_str(vm, &a[0], t, sizeof t)) {
				/* text into a string cell: the characters, not a marker */
				if (!ecl_str_put(vm, a[1].word, t))
					return fault(vm, at);
			} else if (!ecl_poke(vm, a[1].word, ecl_rvalue(vm, &a[0]))) {
				return fault(vm, at);
			}
			if (has_text(ins) && call_host(vm, ins, &status))
				return status;
			break;
		}

		case ECL_OP_ADD: case ECL_OP_SUBTRACT: case ECL_OP_DIVIDE:
		case ECL_OP_MULTIPLY: case ECL_OP_AND: case ECL_OP_OR: {
			/* ★ The DESTINATION is the THIRD operand, not the first. */
			unsigned long x = ecl_rvalue(vm, &a[0]);
			unsigned long y = ecl_rvalue(vm, &a[1]);
			unsigned long r;

			switch (ins->opcode) {
			case ECL_OP_ADD:      r = x + y; break;
			case ECL_OP_SUBTRACT: r = y - x; break;  /* ★ second minus first */
			case ECL_OP_MULTIPLY: r = x * y; break;
			case ECL_OP_AND:      r = x & y; break;
			case ECL_OP_OR:       r = x | y; break;
			default:
				if (y == 0)             /* the original faults too */
					return fault(vm, at);
				r = x / y;
				if (!ecl_poke(vm, vm->d->div_rem, (unsigned short)(x % y)))
					return fault(vm, at);
				break;
			}
			r &= 0xFFFFUL;
			if (!ecl_poke(vm, a[2].word, (unsigned short)r))
				return fault(vm, at);
			/* ★ AND/OR also set the compare flags against zero, which is
			 * what makes `AND mask, var, tmp ; IF_NE` a bit test. */
			if (ins->opcode == ECL_OP_AND || ins->opcode == ECL_OP_OR)
				ecl_set_cmp(vm, (long)r, 0);
			break;
		}

		case ECL_OP_RANDOM: {           /* RANDOM range, destination */
			unsigned range = ecl_rvalue(vm, &a[0]);

			if (range == 0)
				range = 1;
			if (!ecl_poke(vm, a[1].word,
			              (unsigned short)(rng_next(vm) % range)))
				return fault(vm, at);
			break;
		}

		case ECL_OP_ON_GOTO:
		case ECL_OP_ON_GOSUB: {
			/* selector, count, then `count` targets. ZERO-based: selector 0
			 * takes the first target. Out of range takes none. */
			unsigned sel = ecl_rvalue(vm, &a[0]);
			unsigned count = a[1].word;

			if (sel >= count || 2 + sel >= ins->argc)
				break;
			t = target_of(vm, &a[2 + sel]);
			if (!in_code(vm, t)) {
				vm->unhandled++;
				break;
			}
			if (ins->opcode == ECL_OP_ON_GOSUB) {
				if (vm->sp >= ECL_STACK)
					return fault(vm, at);
				vm->stack[vm->sp++] = (unsigned short)next;
			}
			vm->pc = (unsigned short)t;
			break;
		}

		case ECL_OP_GETTABLE: {         /* GETTABLE base, index, destination */
			/* A table is either script memory or — when its address falls
			 * inside this block — bytes of the block itself, which is how a
			 * script carries constant data. The original has one address
			 * space, so a SAVE_TABLE into the block's own range would change
			 * what a later GETTABLE reads; here the write lands in script
			 * memory, and a NON-ZERO cell there is taken in preference. (A
			 * zero written over a non-zero block byte is the one case this
			 * cannot tell from "never written".) */
			unsigned addr = (a[0].word + (ecl_rvalue(vm, &a[1]) & 0xFF)) & 0xFFFFU;
			unsigned short v = ecl_peek(vm, (unsigned short)addr);

			if (v == 0 && addr >= vm->d->vbase && in_code(vm, addr - vm->d->vbase))
				v = vm->raw[addr - vm->d->vbase];
			if (!ecl_poke(vm, a[2].word, v))
				return fault(vm, at);
			break;
		}

		case ECL_OP_SAVE_TABLE:         /* SAVE_TABLE value, base, index */
			if (!ecl_poke(vm, (unsigned short)(a[1].word + ecl_rvalue(vm, &a[2])),
			              ecl_rvalue(vm, &a[0])))
				return fault(vm, at);
			break;

		case ECL_OP_NEWECL:
			vm->chain_block = ecl_rvalue(vm, &a[0]);
			return ECL_CHAIN;

		default:
			if (call_host(vm, ins, &status))
				return status;
			break;
		}
	}
	return ECL_BUDGET;
}
