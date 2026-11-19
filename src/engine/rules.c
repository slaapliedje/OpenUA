/*
 * rules.c — FRUA's own ruleset (ADR-0026): pointers to the A5 tables the
 * engine has always read. See rules.h.
 */
#include "rules.h"
#include "data_pool_replay.h"

#define A5(off) ((const unsigned char *)(g_a5_below + A5_BELOW_SIZE + (off)))

const ruleset_t g_rules_frua = {
	"FRUA",
	6,                   /* elf, half-elf, dwarf, gnome, halfling, human */
	6,                   /* monster                                      */

	A5(-14636), A5(-14564),

	A5(-30864), A5(-30450), A5(-30552), A5(-30960), A5(-30612), A5(-30780),
	A5(-28048),

	A5(-30212), A5(-31228), A5(-23184), A5(-28818), A5(-23030), A5(-23023),
	A5(-23007), A5(-23000), A5(-22993), A5(-30220),

	A5(-22980), A5(-22836),
};

const ruleset_t *g_rules = &g_rules_frua;
