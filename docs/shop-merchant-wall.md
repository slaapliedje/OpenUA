# Shop / Merchant subsystem — findings + worklist

Goal: a working in-game merchant — the player reaches a shop event, sees the
stock, and can sell / identify / take-money. **The subsystem is far more complete
than earlier notes claimed:** the event flow and the shop screen are fully
lifted; the only stubs on the actual transaction path are **`jt189` SELL** and
**`jt190` IDENTIFY**.

> Two corrections to the old [[shop-subsystem-scope]] memory:
> - **`jt923` is NOT a missing "add-money" handler.** `JT[923] = CODE12+0x1baa`
>   = `l1baa`, the encumbrance/overload gate, and it is **LIFTED** (boot.c:52324).
> - **FRUA has no "buy" verb.** The merchant model is: stocked treasure the party
>   *takes* (staged into `-25302` by jt188) + *sell* (jt189) + *identify* (jt190)
>   + *appraise* gems/jewelry (jt922) + pool/share/take coin. "Buy" = take from
>   the staged stock via the `jt183`/`jt28` row UI.

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
JT[3] dispatch: **1** View (jt904), **2** Take-money (jt924), **3** Pool-coin
(jt925), **4** Share (jt921), **5** Appraise (jt922), **6** Exit. Arrow keys
switch active char. **The shop screen itself is complete** — sell/identify live
one level down in `jt893` (the per-item screen).

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
opens jt183 directly. **HEIRS shop cell is UNCONFIRMED** (treasure-event-wall.md
flags this): the HEIRS merchant cell shows a merchant picture but might be a
give-treasure event (case 3/25, presenter-less) rather than a case-8 SHOP event.
The l5586/jt183 lift is **not yet Hatari-tested**. Confirming this gates whether
the shop is reachable live at all.

## Plan (smallest-first)

1. **Confirm the HEIRS shop cell is a case-8 event** (data check, no code) — gates
   live reachability. Also verify the design supports a trade recipient.
2. **`jt190` identify** (~500B) — simplest transaction: fixed 20-pt debit on
   rec[76], mostly UI + the yes/no helper. Smallest faithful win.
3. **`jt189` sell** (~640B) — the keystone verb. All deps lifted (jt932, jt30,
   l1baa, jt28, UI). No new pool work; credits rec[76] + pool spill.
4. **`l30bc`/`l3228`/`l3b6e`/`l32c4`/`jt889`** — the non-shop item verbs
   (examine/use/ready/halve/join); independent, any order. These overlap the
   inventory subsystem (the ITEMS menu uses the same jt893).
5. **`l17f8`** exit-prompt text (cosmetic, last).

Biggest single blocker to a live shop demo: step 1. Everything else is
incremental leaf lifts onto an already-faithful scaffold.

Related: [[inventory-subsystem-wall]] (shares jt893 + the 62-byte pool),
[[treasure-event-wall]] (the case-8 trigger sits next to give-treasure).
