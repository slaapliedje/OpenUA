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

## Lift order (proposed)

1. **Slice A — give-treasure staging** (this subsystem's foundation):
   `L427c`, `L4256`, `L483e`, `jt187` (after the jt477 ABI Q), then `l28b0`
   with `jt930` as a documented leaf PROBE stub. Result: the give-treasure
   event faithfully builds the treasure pile in the engine's real globals.
   Verify via probes (loot staged), not yet visible.
2. **Slice B — the picker UI**: locate + lift the treasure-distribution dialog,
   wire its `jt926`-gated trigger into the play/event flow. This is the visible
   "take 100 Platinum + a ring" screen.
3. **Slice C — jt930** (leave-area / party cleanup) full lift, closing the
   case-3 refresh path and the death-message tail.

Related: [[bigpic-composer-129]] (event subsystem), [[combat-encounter-gateway]]
(l709e cases 10/21), docs/play-loop-wall.md (event-type table).
