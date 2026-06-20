# Treasure / reward-picker event subsystem — worklist

The HEIRS intro caravan "conversation" ends with a picker for **100 Platinum +
a ring**. That picker doesn't exist in the port yet: the give-treasure event
handler is a stub, and the treasure-distribution UI is unwired. This file maps
the whole subsystem so it can be lifted bottom-up, one focused commit per slice.

Recon date: 2026-06-20 (after the l4336/l42c2 once-only fix, 9f634d1).

## Event-dispatch entry (l709e, CODE 20)

`l709e`'s type switch routes the give/take-treasure event types to **`l28b0`**:

| l709e case | call | meaning |
|-----------:|------|---------|
| 3  | `l28b0(ev, 1)` | give treasure **+ refresh screen** (flag path) |
| 25 | `l28b0(ev, 0)` | give treasure, **no refresh** |

(The doc `play-loop-wall.md` mislabeled case 32 `l38bc` as "Vault" — that was the
inferred guess it warned about. `l38bc` is actually a party-member *selection*
event: it walks `-27928` matching `member[397]==ev[8]`, sets `-27932`, redraws.
NOT treasure.)

## l28b0 — the give/take-treasure event handler  (CODE 20 + 0x28b0)  ⛔ STUB

Faithful behaviour from the disasm:

1. `rec[30] = 0`  (rec = `-28006` design header).
2. `-25314 = L427c(&ev[4])`  then `bclr #7` (clear bit 7).   ← money long, LE
3. `-25310 = (u16)L4256(&ev[8])`                              ← money word, LE
4. `-25306 = (u16)L4256(&ev[10])`                             ← money word, LE
5. loop `i = 9..1`: if `ev[i+11] != 0` → `jt187(ev[i+11], ev[7] & 0x80)`
   (9 treasure-category quantities at `ev[12..20]`; the item ids).
6. if flag (`f != 0`, i.e. case 3):
   `-27987=0`; save+clear `-22268`; **`jt930()`**; compute `-27990` mode
   (3 if `rec[34]==0 && rec[36]==1`, else 4); toggle `rec[25]` bit0;
   `jt399(&-22302, 2, 0)`; `-22281=0`; **`jt23()`**; restore `-22268`;
   `-4918=0`.

"100 Platinum + a ring" fits exactly: money in `ev[4..11]`, the ring's item id
in `ev[12]`.

### l28b0 dependencies

| dep | addr | role | status |
|-----|------|------|--------|
| `L427c` | CODE20+0x427c | read LE u32 from `ptr[0..3]` | ⛔ trivial, lift |
| `L4256` | CODE20+0x4256 | read LE u16 from `ptr[0..1]` | ⛔ trivial, lift |
| `jt187` | CODE7+0x4910  | treasure-pile builder (per category) | ⛔ lift (see below) |
| `jt399` | CODE3+0x39d2  | fill/zero buffer | ✅ lifted |
| `jt23`  | CODE6+0x2890  | play-frame stand-up | ✅ lifted |
| `jt930` | CODE12+0x4110 | **leave-area / party cleanup** | ⛔ BIG, leaf-stub for now |

## jt187 — treasure-pile builder  (CODE 7 + 0x4910)  ⛔ NOT IN PORT

`jt187(id, flag)`:
- `id == 0`  → nothing.
- `id == 255` → `L483e(&local22)`  (build the "money" pseudo-item).
- else → `jt479(itemTemplate[-27920 + id*18], &local22, 18)` (copy 18B template).
- if `local22[0] == 255` → nothing.
- if `flag` → `local22[-11] &= ~7`  (clear low 3 bits — frame offset, recheck).
- build a **62-byte node**, copy the assembled record in, **prepend** to the
  list head at `-25302` (`node->next = oldHead`).

### ABI QUESTION — RESOLVED (2026-06-20)

The Mac `jt477` (CODE3+0x214) reads its **bucket** struct from `fp@(14)` (arg2:
`max_count@0`, `bitmap@8`) and writes the reserved slot address to `fp@(8)`
(arg0). So the Mac order is `jt477(out, tag, bucket)`. The port lifted the same
body but **named the params swapped** — `jt477(bucket, tag, out)` reads its
arg0 as the bucket and writes `*out` (arg2) — and lifted its 65 call sites the
same way, so the port is internally consistent.

⇒ In the port, `jt187` must call **`jt477(&-21508 /*bucket*/, 62, &-25302
/*out*/)`**: `-21508` is the node-allocator pool struct, `-25302` receives the
new node pointer. (Verify `-21508`'s bucket is initialised — record_size 62 —
during the design/play init before relying on it.)

### jt187 dependencies

| dep | addr | role | status |
|-----|------|------|--------|
| `jt479` | CODE3+0x36c | memcpy 18B (src,dst,count) | ✅ lifted (unused) |
| `jt477` | CODE3+0x214 | slot reserve from bucket | ✅ lifted — but see ABI Q |
| `L483e` | CODE7+0x483e | build money pseudo-item | ⛔ lift (below) |

## L483e — money pseudo-item builder  (CODE 7 + 0x483e)  ⛔ NOT IN PORT

`L483e(ptr)` (ptr = the 22-byte local in jt187):
- `jt65(ptr, 18)`  zero 18 bytes  (`jt65 == l5f4e`, ✅ lifted).
- fixed fields: `ptr[0]=39 ptr[2]=39 ptr[1]=102 ptr[8]=1 ptr[4..5]=1 (w)
  ptr[6..7]=3000 (w) ptr[12]=0 ptr[3]=40 ptr[11]=4`.
- `-22307 = 1`.
- for count 1..3: loop `idx = jt870(1,126)` until `(-16906)[idx*16] == 2`
  (a type-2 entry in the 16-byte-record table at `-16906`); store
  `ptr[13 + (-22307)] = idx`; `-22307++`.
  (jt870 random ✅ lifted; `-16906` table populated by the design load.)

## The PICKER UI (the visible Take/Pool/Exit screen) — SEPARATE SLICE, NOT LOCATED

l28b0 only **stages** the loot (money → `-25314/-25310/-25306`, items → the
`-25302` list). The interactive distribution screen is a different subsystem:

- `jt926` (CODE12+0x2504) is the "treasure pending?" predicate (out_a = money
  set, out_b = `-25302` set). It is **lifted but `__attribute__((unused))`** —
  i.e. nothing in the port calls it yet → the picker is never triggered.
- The actual distribution **dialog** (Take/Pool/Detect/Exit over `-25302` +
  the money longs) has NOT been located in the port. CODE 6 (≈0x656a..0x65a2)
  and CODE 9 (≈0x34d2) reference `-25302`; one of those is the likely picker.
- `jt73` (CODE6+0x6114, frees `-25302`) and the money-pool adder (≈boot.c
  44606, "+76 into the -25314 pool longs") are the take/pool consumers — both
  ✅ lifted.

## Lift order

1. **Slice A — give-treasure staging** ✅ DONE 2026-06-20 (652c85c):
   `l427c`, `l4256`, `l483e`, `jt187`, `l28b0` lifted faithfully (jt477 ABI
   resolved); `jt930` left as a documented leaf PROBE stub. The give-treasure
   event (l709e cases 3/25) now builds the loot into `-25314/-25310/-25306`
   (money) + the `-25302` item list. Clean build. NOT yet live-verified — it's
   event-gated (runs only when a give-treasure event fires) and has no visible
   output until Slice B; verify via a PROBE trace or once the picker exists.
2. **Slice B — the picker UI** ⛔ IN PROGRESS (recon done 2026-06-20; see
   "Slice B map" below). The picker is the SHARED FRUA treasure/exchange UI, a
   multi-function subsystem behind three stubbed event handlers — NOT a single
   dialog. Foundation leaf `l11a8` lifted; the rest is a multi-session lift.
3. **Slice C — jt930** (leave-area / party cleanup) full lift, closing the
   case-3 refresh path and the death-message tail.

---

## Slice B map — the treasure-picker UI subsystem (recon 2026-06-20)

The "picker" is the shared FRUA treasure/exchange interface, reached from three
l709e event handlers (all currently STUBS in the port):

| l709e case | handler (CODE 20) | event | picker driver |
|-----------:|-------------------|-------|---------------|
| 8  | `l5586` (0x5586, ~240B) | **Shop** ("The party enters a local shop") | `jt183` |
| 9  | `l216a` (0x216a, ~1.8KB, frame -60) | **give-treasure + take** (polls jt926 ×2) | `jt183` (likely) |
| 24 | `l3a32` (0x3a32, ~150B) | **Vault** ("The party enters the vault.") | `jt185` |

The "take 100 Platinum + a ring" intro reward is most likely **case 9 (l216a)** —
the give-treasure-with-take handler — not the silent give (case 3, l28b0, Slice
A). l216a is the largest single piece.

### The two picker drivers (CODE 7)

- **`jt183`** (JT[183] = CODE7+0x3e68, ~790B) — the **party treasure-distribution
  dialog** (mode -27990=1). Builds Money/Gems/Jewelry/Items rows (l11a8), runs
  the dialog loop (L2ebc), and dispatches a 7-way button switch:
  `jt904`(view) / `jt924` / `jt925` / `jt921` / `jt922` / `jt926`+take. Loops
  until exit. Called by l5586 (Shop) + l216a (give-treasure).
- **`jt185`** (JT[185] = CODE7+0x417a, ~700B) — the **per-character take screen**
  (mode -27990=10). Builds the rows for one character (jt73 free + jt583), runs
  L2ebc, dispatches its own button switch. Called by l3a32 (Vault).

### Shared helpers (CODE 7 locals)

| sym | addr | size | role | status |
|-----|------|-----:|------|--------|
| `l11a8` | 0x11a8 | 70B | append a display row to the -24126 array | ✅ LIFTED (B1) |
| `l2ebc` | 0x2ebc | ~232B (frame -82) | the picker DIALOG runner (draw + input loop) | ✅ LIFTED (B2) |
| `L16ea` | 0x16ea | ? | (take-confirm helper, jt183) | ⛔ |
| `L17f8` | 0x17f8 | ? | (money-row draw helper, jt183) | ⛔ |
| `L2858` | 0x2858 | — | pen-mode setup | ✅ already lifted (l2858) |

`l2ebc`'s helper cluster (`l206e`/`l25b6`/`l23b4`/`l2170`/`l1f3e`/`l177a`/
`l2858`/`jt396`) was ALL already lifted — its sibling dialog runner `jt163`
(CODE7+0x2e30) uses the identical cluster — so B2 was a clean structural lift,
not the multi-session item first feared.

### Dependency lift-status (port)

- ✅ already lifted: `jt96` (button/text), `jt20`, `jt103`, `jt181`, `jt188`,
  `jt925`, `jt934`, `jt936`, `jt73`, `jt65`(l5f4e), `jt399`, `jt23`, `jt937`,
  `l442e` (event-picture painter), `l0b20`? (text print — VERIFY).
- ⛔ MISSING: `jt183`, `jt185`, `jt921`, `jt922`, `jt924`, `jt583`, `L2ebc`,
  `L16ea`, `L17f8`, `L2858`, the three trigger handlers `l5586`/`l216a`/`l3a32`.

### Proposed Slice-B sub-order (each its own commit/session)

- **B1** ✅ `l11a8` row-builder leaf (done).
- **B2** ✅ `l2ebc` dialog runner (done — its helper cluster was already lifted;
  `l2858` is the only one of the original "draw helper" guesses, also already
  lifted). Remaining shared draw helpers are `L16ea`/`L17f8`, used by jt183.
- **B3** the missing CODE-12 button helpers `jt921`/`jt922`/`jt924`.
- **B4** ✅ `jt185` (per-char/vault screen) + `l3a32` (Vault trigger, l709e
  case 24) lifted as a faithful structural skeleton. The Vault event now paints
  its picture, prints "The party enters the vault.", and runs the take screen:
  the menu build, l2ebc dialog, JT[3] dispatch, Esc->Exit, and arrow-key
  character switch are all faithful. DEFERRED as documented leaf stubs (each its
  own lift): the action handlers `jt929` (take shared treasure, ~468B), `jt894`
  (pool/sell money, ~902B), `jt893` (the CODE-19 item-management dispatcher,
  ~1962B), and the vault file I/O `jt583`/`jt586` (Vault<c>.DAT load/save). So
  View / Exit / char-switch work; Take / Pool / Items + vault persistence are
  the remaining B4 follow-ups.
- **B5** `jt183` (distribution dialog) + `l5586` (Shop) + `l216a` (give-treasure
  + take) — the visible "take 100 Platinum + a ring".

### B4 follow-ups (the deferred action handlers)
| sym | addr | size | role |
|-----|------|-----:|------|
| `jt929` | CODE12+0x3b4a | ~468B | "Take: Money/Items/Exit" driver (jt185 case 1) |
| `jt894` | CODE19+0x46e0 | ~902B | pool / sell the active char's money |
| `jt893` | CODE19+0x25ce | ~1962B | item-management dispatcher (merchant/inventory) |
| `jt583` | CODE15+0x1c92 | 64B | load Vault<c>.DAT into the pending list (file I/O) |
| `jt586` | CODE15+0x1cd2 | 54B | save the pending list to Vault<c>.DAT (file I/O) |

### jt929 "take treasure" cluster — bottom-up lift (in progress 2026-06-20)

jt929 (jt185 case 1) is the root of a transfer tree. Order + status:

ITEMS PATH (✅ COMPLETE):
- ✅ `l39ac` (CODE12+0x39ac) — item-list picker (jt28/jt179/jt169).
- ✅ `jt24` + `jt887` — Strength carry-capacity table + overload check.
- ✅ `jt186` (CODE7+0x3aba) — give a copy of an item to the active char
  (`jt889` CODE19+0x35a0 bundle-join is a leaf stub; only hit for already-
  bundled items).
- ✅ `jt62` + `l3a3c` (CODE12+0x3a3c) — take-items loop (pick -> give -> unlink
  -> free; vault count fixup via jt71/jt72).

MONEY PATH:
- ✅ `jt884` (CODE19+0x3f16) — map the picked money row -> type index (JT[1]).
- ✅ `jt901`/`l1baa`/`l21d6` — capacity + overload + the actual add-to-char /
  drop-from-pool transfer (commit 6b0cd2c).
- ✅ `jt891` LEAVES (commit 801bc24): `l3e3c` (strchr->ptr), `jt409` (index-of-
  char), `jt487` (atol of trailing decimal), `jt60` (get-key, drains the event
  queue). jt891's whole dep tree is now satisfied.
- ⛔ `jt891` (CODE19+0x3fd2, 572B) — the "how much?" numeric-entry widget. Deps
  ALL lifted (jt176/jt94/jt117/jt93/jt482/jt1080/jt394/jt384/jt423/jt389/jt409/
  jt487/jt60). DECODE (verified via jt1_extract @ 0x4036):
    sig: `long jt891(long maxval, char *prompt, short width)` -> amount entered.
    jt176(); jt94(0,24,width,0,prompt); len=jt423(prompt); cur(fp-5)=len;
    jt394(fp-14,"%ld",maxval) [the cap string]; input buf fp-22 = ""; jt117();
    loop { key=jt60(); JT[1]:
      48..57 (digit) -> L40aa: if strlen(fp-22)>=6 jt1080() beep; else append
        (jt394 "%s%c"), parse (jt487 -> fp-28); if val>maxval strcpy the cap
        string (jt384 fp-22<-fp-14) + cur=promptlen+strlen(cap) else cur++;
        redraw jt94(promptlen? ,24,11,0,fp-22).
      8 (bksp) -> L4148: if len>0 substring drop-last (jt482 + jt384), cur--,
        redraw jt93(cur,24,0,1,...).
      13/27/96 -> L419a (exit). default -> L4076: jt409(")!@#$%^&*(",10,key)+48
        remaps a shifted symbol to its digit, then the isdigit/append path. }
    L41ca: jt176(); if key==27||96 result=0 (cancel) else jt487(fp-22)->result.
    VERIFY jt94/jt93/jt482 arg order before writing (the only soft spots).
- ⛔ `jt924` (CODE12+0x229e, 614B) — build the money rows (jt477/-21156 nodes,
  jt394/jt488/jt384 labels), run jt169, then jt884/jt891/l21d6. Deps jt147 ✓.
  Takes NO args (callers push nothing); the per-letter row mapper is jt884.

DRIVER (✅ DONE):
- ✅ `jt929` (CODE12+0x3b4a) — money-only -> jt924; items-only -> l3a3c; both ->
  a "Take: Money/Items/Exit" l2ebc sub-dialog (Money=jt924, Items=l3a3c, Exit,
  arrow keys = char-switch jt936/jt934); single-type menu once one runs out.
  The two JT[3] arms were VERIFIED against the raw bytes: the .s "asrb #3" at
  0x3c9e was a table-boundary misalignment — case 0 is `jsr L229e` = jt924.
  Replaces the leaf stub; jt185 case 1 now calls it.

## CLUSTER COMPLETE 2026-06-20 — "take treasure" works end to end
jt185 case 1 -> jt929 -> jt924 (money) / l3a3c (items) -> the full leaf tree.
Wiring jt929 into jt185 made the whole cluster LIVE (binary +39KB, no longer
dead-stripped). Order delivered: l39ac, jt24/jt887, jt186/jt889-stub, jt62/
l3a3c (items); jt884/jt901/l1baa/l21d6 (money mechanics); l3e3c/jt409/jt487/
jt60 (input leaves); jt891 (amount widget); jt924 (money driver); jt929
(top driver, wired). jt889 (bundle-join, item[53]>0 only) stays a documented
stub. NOT yet live-tested in Hatari (reach it via the Vault event or a give-
treasure reward); the lifts are faithful + build clean.

OPEN QUESTION carried into B: confirm l216a (case 9) is the intro reward's path
(vs case 3 l28b0 + a play-loop jt926 poll). The CODE20 jt926 callers at 0x234a /
0x24ca are both inside l216a, supporting case 9.

Related: [[bigpic-composer-129]] (event subsystem), [[combat-encounter-gateway]]
(l709e cases 10/21), docs/play-loop-wall.md (event-type table).
