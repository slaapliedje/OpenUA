# Stub-vs-alias audit — lXXXX PROBE stubs that ARE lifted JT exports

Two runtime crashes came from the same defect class in one day
(2026-07-03): a caller was lifted against a fresh PROBE stub for an
lXXXX local that IS a JT export already lifted elsewhere (the
docs/lxxxx-jt-aliases.md rule). The jt502/l1888 arrow crash and the
l3d1e treasure-verb stubs (l3b4a/l1c8a/l1d90/l0082) were both this.

This audit swept ALL lXXXX PROBE stubs in boot.c against the alias
table (same-segment (CODE, offset) match, twin must have a real body).
Method: tools-free python sweep over boot.c + the alias doc; each hit
then hand-verified against the stub's own header comment.

## Executed 2026-07-03 (same session)

| stub | CODE+off | = jtN (lifted) | caller | status |
|---|---|---|---|---|
| l3b4a/l1c8a/l1d90/l0082 | 12 | jt929/jt925/jt921/jt936 | l3d1e | DONE (treasure) |
| l1888 | 13+0x1888 | jt495 | jt502 | DONE (arrow fix) |
| l035e | 6+0x035e | jt131 | jt56 | DONE — repointed |
| l4910 | 7+0x4910 | jt187 | jt188 | DONE — repointed (cell treasure triggers now BUILD items) |
| l475e | 22+0x475e | jt276 | jt290 tool 3 | DONE — repointed |
| l07be | 22+0x07be | jt305 | jt290 tool 3 | DONE — repointed |
| l423e | 22+0x423e | jt279 | jt290 brush | DONE — repointed |
| l3998 | 22+0x3998 | jt295 | jt290 brush | DONE — repointed |

## Deferred — signature reconciliation needed first

| stub | CODE+off | = jtN | why deferred |
|---|---|---|---|
| l23ee(long ctx, short a) | 22+0x23ee | jt312(unsigned char *page) | the port jt312 is the play-screen present with an ADAPTED signature; read the CODE 22 asm at 0x23ee and reconcile — the Mac caller (jt290) pushes (ctx, 0) |

## l5f04 / jt363 — RESOLVED 2026-07-03 (was the "signature clash" card)

jt363 (the STRG kind-classed table loader, CODE 8+0x5f04) is the
AUTHORITATIVE lift: it matches the Mac asm instruction-for-instruction
(holder-clear prologue, "STR@n" cache key at -10370, jt394 name build,
jt132(51) + jt127("STRG") load into the -21148 scratch buffer,
(count+1)*14 size validation), and its jt361 TEST block validated it
against real STRG.GLB data (STRG003=574, STRG001=3668). The -21148
buffer is 4000 bytes (the Mac's L3154 multiplies its 400 arg by 10) —
STRG001 fits. l5f04 was a PARTIAL stub (prologue + hardwired return 0)
that dead-ended the whole spell-table subsystem: jt349/jt347/jt352
(spell list/pick/count), jt355 (school lookup), jt350 (monster
records), jt366 (monster art). Deleted; all six callers repointed.

Verification notes: the six paths are NOT reachable in today's flows
(probe-verified: zero jt363 calls across boot, menu, load, dungeon
entry, combat entry, DELAY turns) — the repoint is behaviorally inert
until they light up. Two blockers for exercising them, filed:
- the manual CAST combat command isn't key-routed (the bar shows CAST
  on a caster's turn — Hatari-verified — but 'c' does nothing; the
  earlier session's casting was the QUICK AI path);
- ENCAMP's memorize flow (jt352's consumer) untested.

While verifying, an INTERMITTENT crash-to-desktop on the QUICKed
archer's first turn reproduced ~1/3 of runs — INCLUDING at bd7e0c8
with this change absent (and with jt363 probes proving these paths
never ran). It is a pre-existing bug in the arrow-turn path (same
family as the earlier one-off "waited 0" freeze), now the top open
combat card — see docs/play-loop-wall.md.

## Duplicate FULL lifts — dedup/compare cards (both sides have real bodies)

Not bugs per se (both bodies exist), but two independent lifts of the
same asm can drift; compare against the disasm, keep one, repoint.

| local (full lift) | CODE+off | = jtN (also full) | note |
|---|---|---|---|
| l2504 | 12+0x2504 | jt926 | staged-loot probe; trivially comparable |
| l0848 | 12+0x0848 | jt934 | roster selection; l0848 is the Hall-verified one (#139) |
| l0b88 / l0ba2 | 20+0x0b88/0x0ba2 | jt951 / jt952 | hdr[34]/[36] flips — compare byte order |
| l5752 | 7+0x5752 | jt216 | HUD panel painter |
| l11a8 | 7+0x11a8 | jt155 | verb-slot append (l3d1e uses jt155, l63xx-era callers use l11a8) |
| l276c | 13+0x276c | jt496 | combat-field recenter |
| l68ae | 6+0x68ae | jt80 | armed-path gate |
| l5f84 | 6+0x5f84 | jt60 | key read; return width differs (short vs unsigned char) |
| l6bbe | 14+0x6bbe | jt519 | zone lookup; return width differs |
| ~~l5f04~~ | 8+0x5f04 | jt363 | RESOLVED — see the l5f04/jt363 section above (jt363 authoritative; the "full lift" was actually a partial stub) |

## Rule going forward

Before stubbing ANY lXXXX callee during a lift: check the alias table
for the callee too, not just the lift target. If it maps to a jtN with
a real body, forward-declare and call the jtN — never mint a stub.

Re-run the sweep after big lift batches (the python one-liner lives in
the session log; regenerate the alias table first with
`tools/gen_jt_aliases.sh > docs/lxxxx-jt-aliases.md`).
