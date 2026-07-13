# Shop / Merchant subsystem — findings + worklist

Goal: a working in-game merchant — the player reaches a shop event, sees the
stock, and can sell / identify / take-money. **The subsystem is far more complete
than earlier notes claimed:** the event flow and the shop screen are fully
lifted; the only stubs on the actual transaction path are **`jt189` SELL** and
**`jt190` IDENTIFY**.

> Two corrections to the old [[shop-subsystem-scope]] memory:
> - **`jt923` is NOT a missing "add-money" handler.** `JT[923] = CODE12+0x1baa`
>   = `l1baa`, the encumbrance/overload gate, and it is **LIFTED** (boot.c:52324).
> - ~~**FRUA has no "buy" verb.**~~ **WRONG — RETRACTED 2026-07-13.** FRUA has a
>   real BUY verb: it is **arm 0** of the jt183 menu, `L3c7c`, and the merchant
>   bar is the classic Gold Box **BUY / VIEW / TAKE / POOL / SHARE / APPRAISE /
>   EXIT**. STRS even carries its messages: `"%s buys %s "` / `"%s buys a %s "`.
>   The claim survived because dis68k's JT[3] straddle bug had eaten the
>   `jsr L3c7c` out of case 0 (see docs/jt3-straddle-audit.md), so the arm looked
>   empty — and L3c7c had never been lifted at all. **Now lifted + live-verified**
>   (see "Live verification" below). Take/Pool/Share/Appraise are the *other*
>   arms, not a substitute for buying.

Cross-subsystem dep: the shop shares the **inventory `-21508`/`-21504` 62-byte
item-node pool** ([[inventory-subsystem-wall]]) — drop/give/sell allocate & free
those nodes via jt477/jt30. That pool is 0 at boot, allocated lazily on
design-load; since the shop is a dungeon event (post design-load), the call sites
are naturally safe, but any standalone harness must allocate it first.

## The merchant event flow (traced)

`l709e` (boot.c:3295, the 39-arm event dispatcher, fired per-cell during the
walk) **case 8 → `l5586(ev)`**:

`l5586` (boot.c:53919, **LIFTED**):
1. First visit seeds `ev[6]=210` (picture id) + flag bit7.
2. `l442e(ev)` paints the merchant picture; `jt20()` stand-up.
3. Greeting: `ev[0]==22` → "The party enters a local shop." else "May I help you?".
4. Loop `i=3..0`: fire the 4 three-byte stock slots `ev[8/11/14/17]` via
   **`jt188`** into `-25302` (the staged stock list).
5. Store shop kind `ev[5]` → `rec[40]`.
6. **`jt183()`** — the interactive merchant screen.
7. `jt73()` free; `ev[7]&0x04` arms deferred re-eval `-4946`.

`jt183` (boot.c:53779, **LIFTED** except cosmetic leaf `l17f8`): zeroes the
12-byte money pool `-25314..-25306`, renders staged `-25302` rows via `jt28`,
then loops: `jt926` poll → build menu `l11a8` arms 0..6 → `l2ebc` dialog →
JT[3] dispatch: **0** BUY (`l3c7c`), **1** View (jt904), **2** Take-money
(jt924), **3** Pool-coin (jt925), **4** Share (jt921), **5** Appraise (jt922),
**6** Exit. Arrow keys switch active char. Arms **2** and **4** are hidden while
the pool holds no coin, and arm **0** while `-25302` is empty — which is why the
bar has a different width on a stocked shop than on an empty one.
**The shop screen itself is complete** — sell/identify live one level down in
`jt893` (the per-item screen).

### BUY — `l3c7c` (CODE 7 + 0x3c7c), lifted 2026-07-13

The last arm. Loops: `l3bcc` repaints the two funds lines → `l38fe` composes the
stock rows and runs the picker → the pick is priced and paid for.

- **`l3bcc`** — `Personal funds:<rec[76]>` (row 20) and `Pooled funds:<-25314>`
  (row 21), each jt95-padded to a 10-wide field so the columns line up.
- **`l38fe`** — per stock item: price = `jt932(rec[46] base, hdr[40] price class,
  rec[53] count)`, right-justified to END at column 34, name laid over from
  column 0, and the composed row written **back over the item's own name field
  (rec+5)** — the picker paints rec+5, so the row text has to live there. The
  name field is rec[5..39] = 35 bytes and the row is 34 chars + NUL: it fits
  exactly, and re-composing is idempotent (the name it re-reads already carries
  the price in the same columns). Then `jt169` runs the list.
  **NOT `jt112`** — the alias map's `l38fe = jt112` is CODE **6**'s l38fe; this
  is a CODE 7 private sharing the offset, sitting after the 6-byte `entry_jt180`.
- **The payment gate is two-tier.** If the buyer's own coin (`rec[76]`, a WORD)
  covers the price it pays alone; otherwise the party pool (`-25314`, a LONG)
  tops it up — the buyer is drained to **zero** and the pool absorbs the whole
  remainder. Only if coin + pool still falls short does the `-14064` "can't
  afford" line print. The item transfers **first** (`jt186`), and if it bounces
  there (no room / overloaded) **nothing is charged**.
- The two success arms print through *separate* STRS copies of the same strings
  (0x2800/0x280c vs 0x281a/0x2826) — THINK C did not pool the duplicates.

**Blocked on a formatter bug, now fixed:** l38fe prices through `"%l"`, and
THINK C's `%l` is a **first-class conversion** (long), not C's length modifier —
so the port's vsprintf-based `jt394`/`jt488` emitted **nothing** and the price
column would have come out blank. See `ua_fmt_pct_l()` in boot.c; the same bug
had been silently blanking `l3806_c12`'s end-of-combat XP line.

### Live verification (2026-07-13, Hatari)

**HEIRS shop cells, GEO005** (`special = ENCR event index + 1`; type 8 = shop):

| event idx | special | row, col |
|---|---|---|
| 20 | 21 | **5, 9** |
| 21 | 22 | 6, 11 |
| 24 | 25 | 5, 10 |

```sh
make EXTRA_CFLAGS="-DFRUA_ENTRY_LEVEL=5 -DFRUA_ENTRY_ROW=5 -DFRUA_ENTRY_COL=9 -DFRUA_ENTRY_FACING=2"
# headless: click 150 298 (PLAY) -> key l -> key a -> key b -> lands ON the shop
```
(Cross-checked against the known type-9 temple at idx 4 → special 5 → row 5,
col 5, which is where [[completeness-jt933-live-gap]] found it.)

Confirmed: merchant portrait + "MAY I HELP YOU?" + bar `BUY VIEW POOL APPRAISE
EXIT` → **B** → stock list `BELT 4 / BOOTS 4 / CLOAK 4 / ROBE 4 / MIRROR 16`
with prices right-justified, `PERSONAL FUNDS: 100 / POOLED FUNDS: 0`.
- Buy BELT → **100 → 96** (personal-coin arm).
- POOL → the bar **grows TAKE and SHARE** (the money-only arms unhide);
  `PERSONAL 0 / POOLED 596`.
- Buy BELT → **596 → 592** (pool top-up arm; buyer already at zero).
- ITEMS on the buyer shows **two BELTs** — jt186 transferred both.

**Still unexercised:** the "can't afford" arm (needs price > coin + pool; HEIRS
stock tops out at 16pp against a 592pp pool, so it takes a contrived design).

## `jt893` anatomy (the per-item action menu — the keystone)

`jt893(out)` (boot.c:53177) is a **level-2 structural skeleton**: full faithful
dialog loop, PROBE still present. Reached from the vault (jt185 case 4), the
shop, and trade. Loop while `choice!=9 && *out==0 && chr[193]!=0`.

Menu arms (gated by play mode `-27990`: vault=10, shop=1; readied slot `chr[382]`;
class `chr[147]`; design header `-28006`):

| Arm | Gate | Handler | Status |
|----:|------|---------|--------|
| 0 examine | always | `l30bc` | **STUB** |
| 1 ready | chr[382] + design/mode | `l3b6e` | **STUB** |
| 2 use | reach + mode≠5 | `l3228` (after `l23d2_c19`) | **STUB** |
| 3 drop/vault | mode==10 | **inline** (53320–53382) | **LIVE** |
| 4 trade/give | mode≠10 | **inline** (53383–53404) | **LIVE** |
| 5 halve | chr[193]<16 | `l32c4` | **STUB** |
| 6 join | always | `jt889` | **STUB** |
| **7 sell** | **shop mode==1 & hdr[20]!=1** | **`jt189`** | **STUB** |
| **8 identify** | **shop mode==1 & hdr[20]!=1** | **`jt190`** | **STUB** |
| 9 Exit | always | sets choice=9 | — |

Cases 2/3/4/7 first pass the **`l23d2_c19`** gate (LIFTED, boot.c:53097) which
blocks readied items and warns on scroll-charge loss. The two **inline arms are
fully lifted and touch the pool**: arm 3 (drop/vault) walks `-22216`, allocs a
62-byte node via jt477, `memcpy(node,itp,62)`, `jt30` removes from char; arm 4
(trade/give) prompts + `jt159` confirm + `jt30`. **Arms 7/8 call the still-stub
`jt189`/`jt190` — that is the whole remaining gap.**

## Data model (money / value / transactions)

- **Char money fields** (words, little-endian on disk): platinum `rec[76]`, gems
  `rec[78]`, jewelry `rec[80]` — generalized `rec[76 + type*2]`, type 0/1/2.
- **Shared money pool** (transaction scratch): platinum `-25314`, gems `-25310`,
  jewelry `-25306` (12 B, `-25314 + type*4`). Zeroed on jt183 entry; polled by
  jt926.
- **Item value** = the word at item-record offset 6 = **node+46** (the 62-byte
  pool node: `+0` next, `+40` 18-byte item record start, `+46` value, `+58`
  sub-list). Same pool the inventory subsystem uses.
- **SELL** (`jt189`, full Mac body CODE_07.s:5700–5902): price =
  `jt932(value@46, type@40, item@53)`, halved if >1; prompt "I'll give you %ld
  platinum pieces for your %s"; on confirm `jt30` removes the node, `l1baa`
  overload-checks, then `addw price, rec[76]` (spills overflow to pool `-25314`).
- **IDENTIFY** (`jt190`, CODE_07.s:5904+): fixed **20 platinum** debit —
  `rec[76]>=20` then `addiw #-20, rec[76]`.
- **TAKE money** (jt924→`l21d6`, boot.c:52348): clamp to pool, subtract pool, add
  to `rec[76+type*2]`, book coin weight (jt883), overload-gate (l1baa).

## Status table

| Fn | addr | Status | boot.c | Role |
|----|------|--------|--------|------|
| `l5586` | CODE20+0x5586 | **LIFTED** | 53919 | Shop event: picture + greeting + stock + jt183 |
| `jt183` | CODE7+0x3e68 | **LIFTED** | 53779 | Merchant screen dialog loop |
| **`l3c7c`** | CODE7+0x3c7c | **LIFTED** 07-13 | — | **BUY** (jt183 arm 0) — price + two-tier payment |
| `l38fe` | CODE7+0x38fe | **LIFTED** 07-13 | — | BUY stock rows (name + right-justified price) + picker. NOT jt112 |
| `l3bcc` | CODE7+0x3bcc | **LIFTED** 07-13 | — | BUY "Personal funds / Pooled funds" header |
| `jt188` | CODE7+0x49aa | **LIFTED** | 46245 | Fire ≤4 shop-stock slots into `-25302` |
| `jt926`/`jt925`/`jt924`/`jt921`/`jt922` | — | **LIFTED** | 46398/45324/52580/53497/53671 | poll / pool / take / share / appraise |
| `jt894` | CODE19+0x46e0 | **LIFTED** | 52976 | Deposit/drop char money |
| `jt932` | CODE12+0x45ca | **LIFTED** | 45210 | Scale-by-kind table (sale price) |
| `l1baa` (`jt923`) | CODE12+0x1baa | **LIFTED** | 52324 | Overload/encumbrance gate |
| `l23d2_c19` | CODE19+0x23d2 | **LIFTED** | 53097 | "may this item be parted with?" gate |
| `jt893` | CODE19+0x25ce | **SKELETON** (2 inline arms live, 7 leaf arms stub) | 53177 | Per-item action menu |
| `jt28`/`jt30`/`jt477` | CODE6 | **LIFTED** | 29788/24388/23016 | item-row paint / remove node / alloc 62B node |
| **`jt189`** | CODE7+0x43a4 | **STUB** (Mac body ~640B CODE_07.s:5700) | 53086 | **SELL** |
| **`jt190`** | CODE7+0x4644 | **STUB** (Mac body ~500B CODE_07.s:5904) | 53088 | **IDENTIFY** |
| `l30bc`/`l3228`/`l3b6e`/`l32c4`/`jt889` | CODE19 | **STUB** | 53081–53085 / 52184 | examine / use / ready / halve / join (non-shop item verbs) |
| `l17f8` | CODE7 | **STUB** | 53761 | jt183 exit-prompt text (cosmetic) |

## Testability

**Not a standalone screen** — the shop is an in-game dungeon event (`l709e`
case 8 → `l5586`), reached only by walking onto a shop-event cell. No harness
opens jt183 directly. **SOLVED 2026-07-13: the shop is live on the PORT** — see
"Live verification" above for the GEO005 cell table and the `FRUA_ENTRY_*`
recipe that spawns the party straight onto it.

> Superseded note (2026-06-21): a Mac blit trace was read as saying the HEIRS
> opening "caravan" IS the case-8 shop, so "no cell hunting needed". In practice
> a **loaded save resumes past** the intro event, so the caravan never re-fires
> and the shop is NOT reachable that way — the cell table above is how you get
> there. The verb list in that note (`View/Take/Pool/Share/Exit`, no BUY) is also
> what seeded the wrong "FRUA has no buy verb" claim: BUY hides when the stock
> list is empty, so an empty-stock visit shows a short bar.

## Plan (smallest-first)

1. ~~Confirm the HEIRS shop cell is a case-8 event~~ **DONE 07-13** — three of
   them on GEO005 (table above); the party spawns on one via `FRUA_ENTRY_*`.
2. **`jt190` identify** (~500B) — simplest transaction: fixed 20-pt debit on
   rec[76], mostly UI + the yes/no helper. Smallest faithful win.
3. **`jt189` sell** (~640B) — the keystone verb. All deps lifted (jt932, jt30,
   l1baa, jt28, UI). No new pool work; credits rec[76] + pool spill.
4. **`l30bc`/`l3228`/`l3b6e`/`l32c4`/`jt889`** — the non-shop item verbs
   (examine/use/ready/halve/join); independent, any order. These overlap the
   inventory subsystem (the ITEMS menu uses the same jt893).
5. **`l17f8`** exit-prompt text (cosmetic, last).

Biggest single blocker to a live shop demo: ~~step 1~~ — **gone.** The shop
demo runs: walk on, buy, pay, carry it away. What is left (SELL, IDENTIFY, the
item verbs) is incremental leaf lifts onto an already-faithful scaffold.

Related: [[inventory-subsystem-wall]] (shares jt893 + the 62-byte pool),
[[treasure-event-wall]] (the case-8 trigger sits next to give-treasure).
