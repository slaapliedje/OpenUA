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
2. **Slice B — the picker UI** ✅ COMPLETE 2026-06-20. The shared FRUA
   treasure/exchange UI: B1 l11a8, B2 l2ebc, B3 jt921/jt922/jt924, B4 jt185 +
   l3a32 + the action handlers (jt929/jt894/jt893) + vault I/O, B5 jt183 +
   l5586 (Shop, case 8) + l216a (give-treasure/temple, case 9). All three event
   triggers (cases 8/9/24) now open their picker. Remaining leaf stubs are
   documented per-arm (jt889 bundle-join, jt189/jt190 shop sell/identify, l4218/
   jt933 temple commit) — each its own follow-up.
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
- **B5** ✅ DONE 2026-06-20 — `jt183` (distribution dialog, ff71681) + `l5586`
  (Shop event, l709e case 8, ff71681) + `l216a` (give-treasure/TEMPLE take, l709e
  case 9, this commit). l216a is a faithful structural lift: intro text (jt232/
  l0b20, "Welcome to our temple."/"What is your wish?"), the take loop (l11a8
  menu gated by ev[7] bit3 + jt926, jt160 dialog, arrow-key char-switch, 7-arm
  JT[3] -> View/Pool/Take/Share/Exit), temple donation-on-exit, mode-restore +
  chain (return -> ev[12]). STUBS: `l4218` (donation money write) + `jt933`
  (CODE12+0x5720 take-commit). NOTE: l216a is case 9 — NOT the HEIRS caravan
  (that's case 3 / l28b0, lifted in Slice A); not on the HEIRS walk path, so not
  Hatari-tested. **Slice B COMPLETE.**

### B4 follow-ups (the deferred action handlers)
| sym | addr | size | role | status |
|-----|------|-----:|------|--------|
| `jt929` | CODE12+0x3b4a | ~468B | "Take: Money/Items/Exit" driver (jt185 case 1) | ✅ DONE |
| `jt894` | CODE19+0x46e0 | ~902B | deposit/drop the active char's money (jt185 case 3) | ✅ DONE (+l465c) |
| `jt893` | CODE19+0x25ce | ~1962B | item-management dispatcher (jt185 case 4) | ✅ DONE 2026-06-20 — dispatcher + 2 inline arms faithful; 7 sub-arms stubbed |
| `jt583` | CODE15+0x1c92 | 64B | load Vault<c>.DAT into the pending list (file I/O) | ✅ DONE |
| `jt586` | CODE15+0x1cd2 | 54B | save the pending list to Vault<c>.DAT (file I/O) | ✅ DONE |

VAULT I/O COMPLETE 2026-06-20 — round-trips: jt185 entry -> jt583 -> l00e0_load
+ jt74 (read Vault<c>.DAT); jt185 exit -> jt586 -> l00e0 + jt75 (write it). The
record format (jt74/jt75) is the LE-swapped money block + 4-byte header + 18-byte
templates (bundles via the +58 chain, padded to 200). The port reuses its File
Manager shim drivers l00e0/l00e0_load instead of the faithful CODE15 L0006 path
builder (jt431/jt987) — same choice the existing l00e0 lift made. jt74's only
unique dep l61c6 (= store count -> -13048) lifted too. NOTE: not Hatari-tested.

### jt583/jt586 scope (vault file persistence) — recon 2026-06-20
TRAP: jt583/jt586 are trivial 64B/54B wrappers (build "Vault<c>.DAT" via jt394,
call a file driver with a per-record callback), but the real work is the vault
FILE FORMAT, a ~1.5KB byte-swapping I/O subsystem:
- `jt75` (CODE6+0x61da, ~670B) — the record WRITER: money longs (-25314..) then
  byte-swap (jt1180/jt1199) + write (jt410) each -25302 item + its 18-byte
  template, bundles (item[40]==73), padded to 200. MISSING.
- `jt74` (CODE6+0x6476, ~600B) — the record READER: inverse, builds the -25302
  list (jt477 from -21508). MISSING.
- `l61c6` (CODE6) — MISSING (jt74/jt75 helper).
- the CODE15 `L0006` reader-driver — NOT lifted (the port's `l0006` is a
  cross-segment NAME COLLISION = CODE13 combat teardown; lift the real one under
  a distinct name). The WRITER driver `l00e0(fn,cb)` IS lifted (matches).
- deps already lifted: jt401/jt410/jt1180/jt1199/l6114/jt394/jt73/l00e0.
Its own focused session (a save format; a bug = corrupted vault files). Lifting
the jt583/jt586 wrappers with jt74/jt75 stubbed would be a no-op (don't).

### jt893 scope (item-management screen, jt185 case 4) — recon 2026-06-20
The full per-character inventory/item screen. ~1962B (CODE19 0x25ce..0x2d78).
GOOD NEWS: its external deps are ALL LIFTED — a call survey of the whole body
shows only `jt155` (=L11a8 row builder), `jt96` (button/text), `jt488` (sprintf),
`jt30`, `jt28`; everything else (the ~40 L2xxx) is internal control flow. So it's
a big but SELF-CONTAINED lift, no missing sub-tree. It conditionally builds the
action menu (jt155 rows gated on item flags: View/Ready/Trade/Drop/Give/... per
rec[193] item count, rec[382] readied, rec[147] class, -27990 mode) then runs its
own dispatch. Deserves its own focused session (intricate inventory state; a bug
= item dup/loss). jt583/jt586 (vault file I/O) are the only other B4 follow-ups.

#### jt893 DONE 2026-06-20 — dispatcher + the 2 inline arms (Level-2 lift)
Lifted faithfully as a dialog-loop dispatcher (the Level-2 exemplar): init ->
menu-row build loop (l11a8, arm values 0..9 gated on -27990 mode / rec[382]
readied / rec[147] class / rec[94] status / the -28006 design header) ->
item-row render (jt28) -> conditional sheet repaint (jt79/jt25/jt94) -> list
dialog (jt169) -> Esc=Exit(9) -> JT[3] dispatch -> jt21. The loop exits on
Exit, on *out (a transfer flag the ready arm sets), or when the char runs out
of items. Binary +16KB (the body + its strings became reachable). Lifted with
it:
- `l23d2_c19` (CODE19 L23d2, ~500B) — the shared "may this item be parted
  with?" gate (cases 2/3/4/7). Readied -> "Must be unreadied"; scroll/bundle
  with a pending-scribe charge bit -> "Okay to lose it?" confirm. NAMED to
  dodge the render-helper `l23d2` collision (lXXXX-collision trap). Reproduces
  one faithful quirk: the bundle-walk loop re-tests the bundle HEAD's charge
  bits, not the running cursor, while the cursor advances to the chain end.
- INLINE arm 3 (drop / into-vault): when -27990==10 appends a 62-byte node
  copy to the -25302 vault list (jt477 from the -21508 bucket, jt71/jt72
  count fixup, bundle [58] chain detach), else "gone forever" + "Drop It?"
  confirm. arm 4 (trade/give): "%s%s%s" prompt + jt159 + jt30. Both use the
  already-lifted jt28/jt96/jt488/jt30/jt103/jt159 and were the high-value
  arms (they touch the vault I/O just built).
STUBBED (each its own follow-up sub-function): `l30bc` examine, `l3b6e`
ready/unready, `l3228` use, `l32c4` halve/split, `jt889` join-bundle (already
stubbed), `jt189` sell, `jt190` identify. NOT yet Hatari-tested (reach it via
jt185 case 4 "manage items" inside a Vault event). This closes the LAST jt185
arm — all six cases now route faithfully.

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

## jt183 + l5586 SHOP/MERCHANT screen DONE 2026-06-20
RESOLVED the open question above: **l216a (case 9) is NOT a reward picker** — it's
a text/temple-message event that even CLEARS the money pool (jt65 on -25314). So
the only treasure-presenting screens are jt185 (vault, case 24) and jt183.

**jt183 (CODE7+0x3e68) is the MERCHANT/SHOP treasure screen, NOT a give-treasure
distribution screen.** Its sole caller is **l5586** (l709e **case 8**, the shop
event: "The party enters a local shop." / "May I help you?"). Lifted both:
- `jt183` — faithful dialog-loop sibling of jt185: enter mode 1, zero the 12-byte
  money pool (jt65), render the staged -25302 item rows (jt28), then loop {jt926
  poll -> l11a8 menu (arms 0..6, hide money arms 2/4 when no coin) -> l2ebc dialog
  -> JT[3]: 1 View(jt904) 2 Take-money(jt924) 3 Pool-coin(jt925) 4 jt921 5 jt922
  6 Exit(warn if coin remains) -> JT[3] repaint}. Arrow keys = char-switch
  (jt936/jt934), Esc = exit.
- `l5586` — the shop/merchant event: l442e picture + greeting + jt188 stock build
  (4x 3-byte slots ev[8/11/14/17]) -> jt183. Wired live into l709e case 8.

#### jt183 arms 4 + 5 DONE 2026-06-20 (9a306e4 + this commit)
- **jt921 (CODE12+0x1d90)** — SHARE: divide the -25314/-25310/-25306 pool equally
  among eligible members (l1d42 count; jt5/jt6 div/mod; l1baa encumbrance gate
  spilling to a remainder pass; jt901/rec[86] carry slack; leftover back to the
  pool). l1d42 lifted with it. (9a306e4)
- **jt922 (CODE12+0x2554)** — APPRAISE gems (rec[78]) / jewelry (rec[80]): per
  stone roll d100 (jt870) -> gem fixed tiers 5/25/50/250/500/2500 pt, jewelry
  jt485(range)+base; settle = keep as a kind-47 item (icon 122 gem / 123 jewelry,
  worth value*2) or sell for platinum (l1c1c, overflow to the pool). l1c1c + the
  shared jt922_settle tail lifted with it.
Only **l17f8** (the arm-6 exit-prompt text helper) is still stubbed in jt183 —
cosmetic. The merchant/vault treasure screen is now functionally complete.

CAVEAT for the caravan reward: this works IF the caravan/merchant cell is a
case-8 SHOP event (likely — it shows a merchant picture). If instead it's a
give-treasure event (l28b0, case 3/25), that path STAGES the pool but still has
no presenter (l28b0 verified faithful = stage-only; no jt183/jt185 call). Confirm
the HEIRS caravan cell's event type. NOT yet Hatari-tested.

Related: [[bigpic-composer-129]] (event subsystem), [[combat-encounter-gateway]]
(l709e cases 10/21), docs/play-loop-wall.md (event-type table).

## JT[933] — the TEMPLE SERVICES screen is LIVE (2026-07-12)

`jt933` was the project's **last live gap**. It was also mislabelled: the port's
comment called it "the take-commit (the actual transfer)". It is nothing of the
sort — it is the temple's own **modal SERVICES screen**, and with it stubbed the
whole of FRUA's temple was dead.

### What it actually is

`L216a` (the temple event) runs the take/pool/share picker. On any turn where the
player neither navigated the roster nor chose Exit, it hands off to `jt933`, which
builds its OWN verb bar (JT[155] arms 0..8, run by JT[160]) and dispatches:

| arm | target | |
|--:|---|---|
| 0 | `L5120` | the 13-entry SERVICES list |
| 1 | `jt904` | View character |
| 2 | `L54c8` | **"Fix"** — the whole-party heal/cure/raise |
| 3/4/5/6 | `jt925`/`jt922`/`jt924`/`jt921` | Pool / take / money / Share |
| 7 | `jt896` | the donation-limit commit (jt933's return value) |
| 8 | — | Exit (ends the loop) |

Every purchasable service funnels through **`L46f6`, the PAYMENT GATE**: price it
with JT[932], show "<service> will only cost <N> platinum pieces.", confirm with
JT[159] ("pay for cure"), then debit the active character's purse (rec[76], a
WORD) or fall back to the party pool (-25314, a LONG); if neither covers it,
"Not enough money." and the service is refused. On success it announces
"<NAME> is cured." — or "the whole party is healed." when the caller is Fix (the
service name compares equal to "Fix" under JT[393], a signed strcmp).

The 12 services and their Mac list prices (platinum), each banded by JT[932]:

| service | base | helper |
|---|--:|---|
| Cure Light / Serious / Critical Wounds | 20 / 70 / 120 | `L4a38` kinds 1-3 (1d8 / 2d8+1 / 3d8+3) |
| Heal | 1000 | `L4a38` kind 4 — AD&D Heal: restores **all but 1d4** HP |
| Cure Blindness / Cure Disease | 200 / 200 | `L488a` / `L4916` |
| Neutralize Poison / Stone to Flesh | 200 / 400 | `L4d24_c12` / `L4ee4` |
| Remove Curse / Raise Dead | 700 / 1100 | `L4e28` / `L4c18` |
| Restore / Resurrection | 1500 / 2000 | `L4f6a` / `L5012` |

A service the character does not need routes through **`L4586`** first — paint
"<NAME> is not Diseased" / "is not poisoned." etc. and ask whether to buy anyway.

### THE BUG IT EXPOSED — l216a's gate was INVERTED

The Mac computes `did_exit || acted` and **branches AWAY on it** (0x26e0
`bnew L2764`): jt933 runs only when the player did NEITHER. The port had
`if (did_exit || acted)` — exactly backwards, so the services screen was
**unreachable** and every roster keypress tried to open it. Also restored in the
same commit: the -28006 header's `rec[20] = 1` stamp (0x26e8), and the 4th
argument, which the Mac derives as `(ev[12] != 0)` where the port hardcoded 0.

### The alias trap (again)

Of jt933's ten CODE-12 callees, **six were already lifted under their JT names** —
`l0082`=jt936, `l1c8a`=jt925, `l1d90`=jt921, `l2554`=jt922, `l229e`=jt924,
`l2504`=jt926. Only `L5120` and `L54c8` were genuinely missing. **Check
docs/lxxxx-jt-aliases.md BEFORE lifting any lXXXX** — it would have been six
duplicate lifts. (Two twins remain in the tree: `l2504`/`jt926` and
`l0848`/`jt934` are the same function lifted twice.)

### Verified live (Hatari, HEIRS)

Reaching a temple needs a harness — HEIRS' type-9 events are not on the default
walk. Method: scan each level's `ENCR` chunk (20-byte event records, `ev[0]` =
type) for type 9, then scan the LIVE level buffer (-12300; 6-byte cell records at
+290, special at +4, `idx = lvl[3]*y + x`) for the cell whose special is that
event index. HEIRS' **entry level is GEO005**, and its temple sits at
**row 5, col 5**:

```sh
make EXTRA_CFLAGS="-DFRUA_ENTRY_LEVEL=5 -DFRUA_ENTRY_ROW=5 -DFRUA_ENTRY_COL=5 -DFRUA_ENTRY_FACING=2"
# Play -> Add Character -> Begin Adventuring
```

Confirmed end to end, **zero faults in engine address space**:
1. **"TEMPLE OF TYMORA."** at 5,5 — the priest bigpic + event text
2. **"MAY I HELP YOU?"** with `l216a`'s bar: HEAL / DONATE / VIEW / POOL / LEAVE
3. HEAL -> **jt933**'s own bar appears: HEAL / VIEW / POOL / APPR. / EXIT
4. HEAL -> **L5120**: *"BARBARUS, HOW CAN WE HELP YOU?"* with **7 of the 12**
   services offered (the -5286 table gating on ev[4]) and "BARBARUS 78/78" below
5. Buy Cure Light Wounds -> **L46f6**: *"CURE LIGHT WOUNDS WILL ONLY COST 6
   PLATINUM PIECES."* / **PAY FOR CURE [YES] [NO]** (JT[932] banded 20 -> 6)
6. YES -> debited, cure applied, back to the list

**Harness note:** the list commits on the **verb-word LETTER accelerator**
(`driver.sh key h` for HEAL) — clicking the verb button does NOT commit.

**~~NOT yet exercised~~ — BOTH VERIFIED (2026-07-12, post-reboot):**
- **Wounded cure:** MALTIER **9/77 → 14/77** via Cure Light Wounds (a real 1d8=5)
  through the payment gate; the services screen's HP readout drew in CYAN (the
  `cur==max ? 7 : 11` colour branch) while wounded.
- **The party-wide Fix (`L54c8`):** at GEO009's temple (row 17, col 20 —
  "GREETINGS IN THE NAME OF SUNE") the verb bar showed **HEAL/VIEW/FIX/EXIT**
  (arm 2's `ev[4]>=7 && ev[5]==0` gate passing for the first time); `key f` →
  **"THE WHOLE PARTY IS HEALED."** with NO payment prompt (faithful — Fix's base
  price is 0, so l46f6 takes the free path) → BARBARUS 12→78, LADY ILLIS 20→84,
  MALTIER 9→77, all to max (`rec[395]=rec[129]`), HP colour back to grey.

### Hunting temple cells — hard-won corrections to the recipe

1. **Parse the CELLSCAN log PER-LEVEL-BLOCK, never in aggregate.** Every play
   entry loads the header level (GEO005) BEFORE the harness level, so the log
   contains multiple scan blocks — and GEO005 happens to have cells at (2,12)
   and (7,8), the exact coordinates hunted on other levels. Three separate
   mis-attributions came from deduping across blocks.
2. **The scan must clamp to ds[2] (width) as well as ds[3] (height)** — the MAP
   chunk is a fixed 576-cell region and the rows beyond the declared dims hold
   stale editor data; jt201/l5baa reject those cells, so an unclamped scan
   reports PHANTOM temples. (Fixed in the committed FRUA_CELLSCAN.)
3. **An event in ENCR is not necessarily on any cell.** GEO008/GEO018 carry
   Fix-capable temples in ENCR that no in-bounds cell references (chain-target
   rewards, hence once-only). The OFFLINE cross-reference (parse HDR/MAP/ENCR
   straight from the .DAT — see the session's python) beats emulator round-trips.
4. **The walk-in Fix temples in HEIRS are GEO009 and GEO010 at (17,20)**, both
   gated on `rec[8] outside 6..19` (l694e ct=3) — and `rec[8]=0` at game start,
   so they fire on entry.
5. **Drive the verb bars by LETTER only** (`key h`, `key f`): clicking a verb
   sometimes commits the WRONG arm (a FIX click landed on EXIT); the letter
   accelerators are deterministic.
