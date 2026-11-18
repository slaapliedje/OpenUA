/*
 * ecl.h — the Gold Box ECL interpreter core (ADR-0023).
 *
 * Every Gold Box game but FRUA drives an area from ECL, a bytecode script.
 * This is the part of an interpreter that is the same for all of them: the
 * instruction framing, the operand model, the script's 16-bit memory, the
 * compare flags, arithmetic, and control flow. Everything a script does TO
 * THE GAME — print, show a picture, load a map, run a menu, start a fight —
 * is handed to the host through one callback, so the core knows nothing about
 * the engine it runs in and builds unchanged on a host machine for its tests.
 *
 * The format and the semantics are the Greybox project's findings
 * (kblood/Greybox_CombinedGoldbox, 0BSD), cross-checked here against the
 * games' own script files by tools/ecl.py. See that module's docstring for
 * the format, and tests/test_ecl_c.py for how this file is held to it.
 *
 * Plain C99, no engine, shim or platform headers: keep it that way.
 */
#ifndef FRUA_ECL_H
#define FRUA_ECL_H

#define ECL_NOPS        0x42    /* opcodes 0x00..0x41                        */
#define ECL_MAX_ARGS    264     /* 8 fixed + a 255-entry jump table, rounded */
#define ECL_STACK       32      /* GOSUB depth                               */
#define ECL_FRAME_NONE  0xFF    /* ecl_frame.fixed: not an opcode            */

/* Dialects. A DOS archive does not say which Gen1 game it came from; the
 * caller knows (tools/ecl.py's misframing() is how to find out). */
enum { ECL_GEN1 = 0,            /* Champions of Krynn            */
       ECL_GEN1_DOK,            /* Death Knights of Krynn        */
       ECL_GEN2,                /* The Dark Queen of Krynn       */
       ECL_NDIALECTS };

/* The five entry points in a block's header, in header order. */
enum { ECL_ENTRY_STEP = 0,      /* vmRun1: every party step      */
       ECL_ENTRY_SEARCH,        /* searchLocation: per-cell      */
       ECL_ENTRY_PRECAMP,
       ECL_ENTRY_CAMPINT,
       ECL_ENTRY_INIT };        /* the area's initial entry      */

/* What ecl_run() returns. */
enum { ECL_EXITED = 0,          /* EXIT, or RETURN with an empty stack       */
       ECL_CHAIN,               /* NEWECL: load vm->chain_block and re-enter */
       ECL_WAIT,                /* the host asked to pause (menu, input)     */
       ECL_COMBAT,              /* the host asked to run a fight             */
       ECL_FAULT,               /* undecodable byte, stack, divide by zero   */
       ECL_BUDGET };            /* instruction budget spent (runaway guard)  */

/* What a host callback returns. */
enum { ECL_HOST_OK = 0,         /* done, carry on                            */
       ECL_HOST_UNHANDLED,      /* not implemented: skipped, and counted     */
       ECL_HOST_WAIT,           /* pause; ecl_run() again to resume          */
       ECL_HOST_COMBAT,
       ECL_HOST_FAULT,
       ECL_HOST_EXIT };         /* the instruction also ends the script      */

/* Opcodes the core itself executes, plus the few a host has to recognise. */
enum { ECL_OP_EXIT = 0x00, ECL_OP_GOTO = 0x01, ECL_OP_GOSUB = 0x02,
       ECL_OP_COMPARE = 0x03, ECL_OP_ADD = 0x04, ECL_OP_SUBTRACT = 0x05,
       ECL_OP_DIVIDE = 0x06, ECL_OP_MULTIPLY = 0x07, ECL_OP_RANDOM = 0x08,
       ECL_OP_SAVE = 0x09, ECL_OP_PICTURE = 0x0e, ECL_OP_PRINT = 0x11,
       ECL_OP_PRINTCLEAR = 0x12, ECL_OP_RETURN = 0x13,
       ECL_OP_COMPARE_AND = 0x14, ECL_OP_IF_EQ = 0x16, ECL_OP_IF_GE = 0x1b,
       ECL_OP_NEWECL = 0x20, ECL_OP_LOAD_FILES = 0x21, ECL_OP_COMBAT = 0x24,
       ECL_OP_ON_GOTO = 0x25, ECL_OP_ON_GOSUB = 0x26, ECL_OP_GETTABLE = 0x2a,
       ECL_OP_AND = 0x2f, ECL_OP_OR = 0x30, ECL_OP_PRINT_RETURN = 0x33,
       ECL_OP_SAVE_TABLE = 0x35, ECL_OP_CLEAR_BOX = 0x3d };

/* Operand codes (the first byte of an operand). */
enum { ECL_C_IMM8 = 0x00,       /* literal 0..255                            */
       ECL_C_MEM = 0x01,        /* a script variable, 16-bit address         */
       ECL_C_IMM16 = 0x02,      /* literal word                              */
       ECL_C_MEM3 = 0x03,       /* a script variable (variant)               */
       ECL_C_GAMEVAR = 0x7f,    /* a script variable, 8-bit address          */
       ECL_C_TEXT = 0x80,       /* inline 6-bit packed string                */
       ECL_C_STRPTR = 0x81 };   /* address of a runtime string cell          */

typedef struct ecl_frame {
	unsigned char fixed;        /* operands that always follow; ECL_FRAME_NONE */
	signed char   count_at;     /* index of the operand counting the rest, -1  */
} ecl_frame;

typedef struct ecl_dialect {
	const char    *name;
	unsigned char  header_len;  /* 22 with the 0x1388 magic word, 20 without   */
	unsigned char  magic;
	unsigned short vbase;       /* virtual address of block byte 0             */
	unsigned short div_rem;     /* where DIVIDE leaves its remainder           */
	ecl_frame      frame[ECL_NOPS];
} ecl_dialect;

typedef struct ecl_opnd {
	unsigned char  code;
	unsigned char  text_len;    /* ECL_C_TEXT: packed bytes ...                */
	unsigned short text_off;    /* ... starting at this block offset           */
	unsigned short word;        /* the value, or the address it names          */
} ecl_opnd;

typedef struct ecl_insn {
	unsigned short offset;      /* of the opcode byte, within the block        */
	unsigned short size;        /* opcode + operands + inline text             */
	unsigned short argc;
	unsigned char  opcode;
	ecl_opnd       arg[ECL_MAX_ARGS];
} ecl_insn;

struct ecl_vm;

typedef struct ecl_host {
	void *ctx;
	/* A zeroed block of `bytes` for a page of script memory, or 0. */
	void *(*alloc)(void *ctx, unsigned long bytes);
	/* Every instruction the core does not execute itself arrives here with the
	 * program counter already past it. Also called AFTER the core's own work
	 * for SAVE and COMPARE when an operand is text, so the host can keep the
	 * string (the core stores only its numeric marker). May be null. */
	int (*op)(void *ctx, struct ecl_vm *vm, const ecl_insn *ins);
} ecl_host;

typedef struct ecl_vm {
	const unsigned char *raw;
	const ecl_dialect   *d;
	const ecl_host      *host;
	unsigned short       len;
	unsigned short       pc;            /* block offset of the next opcode   */
	unsigned short       chain_block;   /* after ECL_CHAIN                   */
	unsigned short       fault_pc;      /* after ECL_FAULT                   */
	unsigned char        cmp[6];        /* ==  !=  <  >  <=  >=              */
	unsigned char        sp;
	unsigned short       stack[ECL_STACK];
	unsigned long        rng;
	unsigned long        steps;         /* instructions executed, ever       */
	unsigned long        unhandled;     /* host callbacks that declined      */
	unsigned short      *page[256];     /* script memory, 256 words a page   */
	ecl_insn             ins;           /* the instruction being executed    */
} ecl_vm;

const ecl_dialect *ecl_dialect_get(int id);

/* Decode the instruction at block offset `off`. Returns its size, or 0 if the
 * byte is not an opcode, the block ends inside it, or it has too many
 * operands. `out` may be null when only the size is wanted. */
unsigned ecl_decode(const unsigned char *raw, unsigned len,
                    const ecl_dialect *d, unsigned off, ecl_insn *out);

/* Unpack 6-bit text into `out` (always NUL-terminated). Returns its length. */
unsigned ecl_text(const unsigned char *raw, unsigned off, unsigned packed_len,
                  char *out, unsigned outmax);

/* Zero a VM and give it a host. Script memory starts empty. */
void ecl_init(ecl_vm *vm, const ecl_host *host);

/* Point the VM at a block. Script memory is KEPT — that is how state carries
 * across NEWECL and between an area's entry points. Returns 0 if the block is
 * too short for its header or lacks the dialect's magic word. */
int  ecl_load(ecl_vm *vm, const unsigned char *raw, unsigned len,
              const ecl_dialect *d);

/* Start at one of the five header entry points (clears the call stack). */
int  ecl_enter(ecl_vm *vm, int entry);

/* Execute until the script stops, pauses or spends `budget` instructions. */
int  ecl_run(ecl_vm *vm, long budget);

/* String cells. A 0x81 operand names a cell in script memory where a string
 * lives one CHARACTER A WORD, NUL-terminated (Greybox, from CoAB's own
 * CMD_InputString). That is what makes SAVE "NAME", 0x81:cell, COMPARE
 * 0x81:cell, "WORD" and PRINT 0x81:cell work, and it is why a saved game that
 * keeps script memory keeps the strings too. */
#define ECL_STR_MAX     16      /* the cells the Dark Queen uses are 17 apart */

/* The string in the cell at `addr` (always NUL-terminated). Returns its length. */
unsigned ecl_str_get(const ecl_vm *vm, unsigned short addr, char *out, unsigned outmax);
/* Store `s` (at most ECL_STR_MAX characters) in the cell at `addr`. */
int      ecl_str_put(ecl_vm *vm, unsigned short addr, const char *s);

unsigned short ecl_peek(const ecl_vm *vm, unsigned short addr);
int            ecl_poke(ecl_vm *vm, unsigned short addr, unsigned short v);
unsigned short ecl_rvalue(const ecl_vm *vm, const ecl_opnd *a);
void           ecl_set_cmp(ecl_vm *vm, long left, long right);

#endif
