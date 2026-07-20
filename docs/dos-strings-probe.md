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

## What this does and does not settle

**Settles:** the string half of DOS-independence is viable. The risk that killed
the plan outright is gone.

**Does not settle:** the A5 world (#67) — still the substantial grind, and
unaffected by this result. Nor does it change the Mac 1.0 vs 1.2 question:
these offsets are derived against the 1.0 fork, and a 1.2 retarget would
require re-deriving them.
