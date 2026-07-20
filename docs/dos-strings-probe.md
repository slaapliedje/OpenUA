# Can the string pool come from the DOS release?

**Verdict: yes — 96.4% of the Mac `STRS` pool is recoverable byte-exact from
the DOS `CKIT.EXE`, and nothing creative is missing.**

## Why this was asked

The port's one hard Mac dependency is `frua.rsc` (see
[redistributable-binary.md](redistributable-binary.md), [../GAMEDATA.md](../GAMEDATA.md)).
Design data is already byte-identical between releases and art already converts
([art_convert.py](../tools/art_convert.py)), so a player who owns only the DOS
release — the one still sold, via *Forgotten Realms: The Archives – Collection
Two* — is blocked on exactly two things:

1. the **A5 world** (`DATA`/`ZERO`/`DREL`) — the THINK C initialised globals, and
2. the **string pool** (`STRS`) — 2,145 strings, 29,148 bytes.

(2) is the cheaper one to falsify. Mac and DOS were built by different
compilers for different CPUs, so it was entirely possible their string pools
could not be reconciled at all — in which case DOS-independence dies regardless
of any A5 work. So it was settled first.

For (1), see task #67 and the `FRUA_NO_REPLAY` knob in `src/main.c`: the A5
world is load-bearing and cannot simply be dropped. That remains the real cost.

## Method

Split `STRS` on NUL into `(pool_offset, bytes)` entries and look for each one
inside `CKIT.EXE`. An entry counts as **recovered** only when it appears as a
whole NUL-delimited string in DOS too. The loose "are these bytes present
anywhere" test scores 98.3%, but admits false positives where a short Mac
string is really a fragment of a longer DOS one (`Pod` inside `Podium`), so the
strict number is the one quoted.

```sh
python3 tools/strs_dos_probe.py data/work/UnlimitedAdventures.rfork \
        data/dos-frua/CKIT.EXE --from-rfork
```

## Result

Mac 1.0 resource fork vs the GOG DOS release (`CKIT.EXE`, 587,843 bytes):

| | Count | Share |
|---|---:|---:|
| Recovered (whole NUL-delimited string) | 2,068 | 96.4% |
| Substring-only | 40 | 1.9% |
| Absent | 37 | 1.7% |
| **Total** | **2,145** | |

470 recovered entries occur at more than one DOS offset. That is **not**
ambiguity: every occurrence is the identical byte sequence, so any one of them
serves as the extraction offset.

Ordering is also strongly preserved — walking the uniquely-located entries in
Mac pool order gives 128 monotonically-increasing DOS clusters, the longest
running 122 entries. Not needed for extraction, but it is good evidence the two
pools really are the same content emitted by two compilers.

## The 37 absent strings are all Mac-platform-specific

This is what makes the result trustworthy — the gaps are not random:

| Group | Strings |
|---|---|
| Mac art-name templates (`.ctl`; DOS uses `.TLB`) | `%s.ctl` ×2, `%s%d%03d.ctl`, `PIC%c1%03d.ctl`, `SPRI0%03d.ctl`, `CPIC1%03d.ctl`, `BIGP0%03d.ctl`, `CTL` ×2 |
| Mac paths / Finder | `:DISK4:ALWAYS.CTL`, `system`, `desktop`, `pref.dat` |
| Mac art import | `MacPaint File:`, `PICT File:` |
| Mac memory partition | `Insufficent Memory` *(SSI's typo)*, `Try increasing the amount of memory`, `allocated for the program.`, `Insufficient FAR Memory!`, `Out of Memory!` |
| Toolbox StandardFile | `File to save`, `File to open`, `Yes\n`, `No\n` |
| Mac sound engine | `Insufficient memory in MLoad` ×2, `Insufficient memory in DNPInit` ×2, `Song out of range (%d/%d)`, `music` |
| Version banner | `Version 1.0       April 27,1993` |
| Misc | `Quit Game? `, `Please Insert %s`, `Moebius`, `slb`, `topview` |

Every one is either a Mac platform concept the Atari/Amiga port does not have,
or a generic diagnostic phrase the port can author itself with no copyright
question. **No creative game content is missing** — no spell effect, monster
name, or message text.

The Mac memory-partition warning is the same one the *UA Manual Addendum*
documents ("set the amount of memory for the program to 2000"), which is also
why 1.2 adds a `SIZE` resource the 1.0 fork lacks.

**Caveat:** the probe is case-sensitive. A case-insensitive pass additionally
finds `topview` and `MENU`, so the genuinely-absent count is 35, not 37. The
strict figures above are kept as the conservative number.

## The 40 substring-only entries

Cases where the Mac string is a fragment of a longer DOS string — mostly short
format specifiers and words that also appear inside longer phrases: `%s%03d.dat`,
`%s.tlb`, `of`, `gen`, `Staff`, `Long Bow`, `Missiles`, `Fighter/Magic-User`.
Recoverable, but a naive extractor would take the wrong span, so they need a
length-aware pass. 1.9% of the pool.

## What ships

The distributable artifact is a table of `pool_offset -> (dos_offset, length)`
pairs — **positions only, never text**, so it carries no copyrighted bytes. The
words come from the user's own `CKIT.EXE` at install time, exactly as
`art_convert.py` takes their own art:

```sh
python3 tools/strs_dos_probe.py <rfork> <CKIT.EXE> --from-rfork --emit-map strs_map.json
```

Deriving the map still needs a Mac fork once, on the developer side. It is the
*player* who is freed from needing one.

### Which `CKIT.EXE` these offsets are derived against

An offset table is only valid for the exact build it was derived from, so the
reference is recorded here:

| | |
|---|---|
| Source | GOG *Forgotten Realms: The Archives – Collection Two*, `gameId` 1432650732 |
| Size | 587,843 bytes |
| SHA-256 | `467451a770e3275df409c524e6f7cfc714716bf88ef96f8fbeb2ca9afb146959` |
| Version banner | `Version 1.2        June 28,1993` |

**Open: is the Steam release byte-identical?** Not validated — only the GOG copy
was available. Both re-releases are the same DOSBox repackaging from the same
publisher, so identical is *likely*, but likely is not measured. Check with:

```sh
sha256sum <steam>/CKIT.EXE      # compare against the hash above
```

If it differs, the extractor needs a per-build table keyed by hash rather than a
single one — which is why ADR-0017 requires install-time anchor verification
that refuses loudly instead of extracting from unverified offsets. Any other
provenance (floppy original, a fan repack, a different patch level) is subject
to the same caveat.

## What this does and does not settle

**Settles:** the string half of DOS-independence is viable. The risk that killed
the plan outright is gone.

**Does not settle:** the A5 world (#67) — still the substantial grind, and
unaffected by this result.

## Does retargeting the lift to Mac 1.2 help? No.

The appealing argument: GOG/Steam ship DOS **1.2**, Mac 1.0 is as rare as any
other Mac copy, so surely the lift should retarget to Mac 1.2 to line the two
up. Measured, it does not hold — for three independent reasons.

### 1. "1.2" is not the same version on the two platforms

    Mac 1.0    April 27, 1993
    DOS 1.2    June 28, 1993      <- the GOG/Steam release
    Mac 1.2    February 28, 1994

DOS 1.2 *predates* Mac 1.2 by eight months and postdates Mac 1.0 by two. These
are independent version lines that reuse a numeral; the matching digits are a
labelling coincidence, not a shared code state. Retargeting Mac 1.0 -> Mac 1.2
moves the lift eight months *away* from the DOS release, not toward it.

### 2. Coverage against DOS 1.2 is a dead heat

Same probe, same `CKIT.EXE`:

| Mac source | Entries | Recovered | Substring | Absent |
|---|---:|---:|---:|---:|
| Mac 1.0 | 2145 | 2068 (**96.4%**) | 40 | 37 |
| Mac 1.2 | 2147 | 2070 (**96.4%**) | 40 | 37 |

Identical, because the gaps are **platform** differences (Toolbox StandardFile,
`.ctl` vs `.TLB`, the Mac memory model), not **version** differences. No Mac
build will ever carry DOS's spelling of those. A retarget buys exactly nothing
for DOS string sourcing.

### 3. Mac-copy rarity is irrelevant

The Mac fork is a **build-time** input, used once, on the developer side, to
derive the offset table. No player needs a Mac copy of any version — that is
the entire point of shipping positions rather than text. Scarcity of Mac 1.0
does not reach anyone downstream.

### What 1.2 *is* worth

Wanting 1.2 for its bug fixes is reasonable; it just has nothing to do with
DOS. The complete user-visible text delta between Mac 1.0 and 1.2 is:

    + "There is no way to go in that direction."
    + "Transfer module ends testing!"
    + "Version 1.2    February 28,1994"
    - "Version 1.0       April 27,1993"

Two new messages — 1.2 is a pure bug-fix release, no new features or UI.
**Caveat: a small string delta does not imply a small code delta.** 22 of 23
CODE segments changed, and behaviour fixes need not add text; what 1.2 actually
fixes is still unknown.

The recommendation is therefore to keep 1.2 as an **oracle** — when chasing a
specific bug, diff that one function 1.0 vs 1.2 and port just the fix — rather
than paying for a full retarget (jump table 1208 -> 1207, reorganised from
index 13, every `jtNNN` re-derived) to churn a working port for benefits nobody
has yet demonstrated.
