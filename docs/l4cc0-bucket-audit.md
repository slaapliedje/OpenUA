# l4cc0 deferred-bucket audit (2026-06-20)

`l4cc0` (CODE6+0x4cc0, design subsystem bring-up) allocates every per-design
list-node pool / scratch buffer at design open. The port lifted it as a
**structural skeleton** and DEFERRED most of the sub-allocators "until their
consumers are lifted". Several consumers are now lifted, so an uninitialised
bucket = a **silent failure** (jt477 returns 0 → the caller drops the node with
no error). The lost caravan ring (item id 225) was exactly this on -21508.

This audits every l4cc0 allocation.

## Two allocator classes

- **Heap** — `jt387` (NewPtr). Independent of combat.
- **Combat arena** — `L531e`, a bump allocator over `-17450`, itself allocated
  by `JT[1004]` (CODE5, combat). Everything `L531e`-backed needs the combat
  subsystem up, so it is correctly tied to combat (#115).

## The allocations

| A5 slot | rec size | helper (arg) | alloc | consumer(s) | reachable now | status |
|--------:|---------:|--------------|-------|-------------|---------------|--------|
| -22208 | 398 | L30cc(8) | heap | record staging | yes | ✅ lifted (l30cc) |
| -21508 | 62 | L311c(640) | heap | items / pending treasure (-25302) / inventory / vault | **yes** | ✅ FIXED ce89e0d (ring) |
| -21156 | →-21508 | L3144 | (pointer) | money rows: jt167 / jt924 / jt894 | **yes** | ✅ FIXED this commit |
| -21148 | 10 | L3154(400) | heap | STRG table | yes | ✅ lifted (l3154) |
| -21860 | 398 | L30f4(60) | heap | l2b40 record-free (jt471) | latent (record destruction) | ⏸ deferred — heap, safe to add when reached |
| -20800 | 34 | L317c(70) | **combat arena** | jt602 effect-node reserve | latent (combat) | ⏸ defer w/ #115 |
| -20448 | 26 | L31a4(68) | **combat arena** | l2b40 [64] sub-free | latent (combat) | ⏸ defer w/ #115 |
| -22306 | 2738 B | L531e(2738) | **combat arena** | combat sprite buffer | latent (combat) | ⏸ defer w/ #115 |
| -25318 | 1260 B | L531e(1260) | **combat arena** | live combat map (CODE13/14 area-map, jt501/jt521) | latent (combat) | ⏸ defer w/ #115 |
| — | 6656 B | L59ca | combat (JT[981]/JT[986]/JT[1004]…) | combat init | latent (combat) | ⏸ defer w/ #115 |

## The -21156 tangle (fixed this commit)

`-21156` is **not its own descriptor** — the Mac `L3144` sets it to `&-21508`,
and every faithful consumer (`jt167` L183a, `jt924`, `jt894`) reads it as a
**pointer** (`movel -21156`) and reserves from the bucket it points at. `jt477`
ignores the caller's size arg and uses the descriptor's own `record_size` (62),
so money rows are simply 62-byte nodes from the shared item bucket.

While `L3144` was deferred, `-21156` was NULL → `jt477(NULL,…)` early-returns →
**the entire money-row path was dead** (take-money from the pool, deposit). Two
port sites also used the wrong convention (`&g_a5_byte(-21156)` as a standalone
descriptor) in the recently-lifted `jt924` / `jt894`; corrected to the pointer
form to match `jt167` and the Mac.

Fix = lift `l3144` + un-defer it (after `l311c`), and fix the two sites.
hrdb-verified at boot: `-21156` = `&-21508`, `-21508` header = `0280 / 003E /
<ptr>` (640 / 62 / valid).

## Still deferred — and why it's OK (for now)

Everything else is **combat-arena-backed** (`L531e`/`JT[1004]`) or a
combat/area-map consumer (`-25318` is the live combat map; the dungeon 3D walk
uses wall sets, not this). None is reachable in normal exploration, so they are
correctly deferred until the combat subsystem (#115) brings up the arena. When
that lands, un-defer `L317c` / `L31a4` / the two `L531e` buffers / `L59ca`
together (they share the `-17450` arena) and verify against a live encounter.

`-21860` (L30f4) is the one heap-backed straggler; its only consumer is the
record-free path `l2b40`, not hit in basic play. Safe to un-defer when a
record-destruction path is exercised (jt471 on an uninitialised bucket would
divide by `record_size == 0`).

## Method note

`jt477` bucket layout: `[count:w][record_size:w][base_ptr:l][free-bit bitmap…]`
at `bucket+8`. The bitmap starts clear from the A5 zero-fill, so an init only
needs to set count / size / ptr. hrdb gotcha: `_g_a5_below` moves every rebuild
— re-read `m _g_a5_below` each session; `g_a5_byte(-N)` = `base + (32768 - N)`.
