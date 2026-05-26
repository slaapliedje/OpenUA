# JT entry callsite frequency

Static count of every `JT[N]` callsite across the regenerated
`data/work/disasm/CODE_*.s` listings, cross-referenced against
`src/engine/boot.c` to flag which entries are already lifted, still
PROBE-only stubs, or absent from the C tree entirely.

A runtime trace would be more precise, but the Mac build's busiest
JT entries are stable leaf helpers (jt3 / jt384 / jt488 / etc.) so
the static count tracks runtime frequency closely enough to drive
lift priority.

Regenerate this table with:

```sh
python3 tools/jt_freq.py --format markdown --limit 50 \
    > /tmp/jt_freq.md
# then refresh the table below from /tmp/jt_freq.md
```

The columns:

- **JT** — A5 jump-table index.
- **Callsites** — number of `jsr JT[N]` instances across all CODE
  segments.
- **State** — `lifted` (jtN body has more than PROBE / void-casts /
  constant return), `stub` (PROBE-only declaration), `unknown` (no
  jtN decl in `boot.c` yet — future lifts will need to add one).

The classifier is heuristic and matches on the function body shape;
declarations that span unusual layouts can mis-classify. Re-check
by hand before declaring an entry done.

## Top 50 by callsite count

| JT  | Callsites | State |
|----:|----------:|:------|
|   3 |       307 | lifted |
| 384 |       287 | lifted |
| 1200 |       187 | stub |
| 488 |       156 | lifted |
| 394 |       156 | unknown |
|  94 |       155 | stub |
| 406 |       153 | unknown |
| 1161 |       147 | unknown |
| 1089 |       143 | unknown |
| 399 |       126 | lifted |
|  41 |       116 | stub |
|  18 |       115 | unknown |
| 413 |       107 | unknown |
| 155 |        99 | unknown |
| 444 |        96 | stub |
|   1 |        95 | unknown |
| 870 |        95 | unknown |
|  20 |        94 | unknown |
| 397 |        92 | unknown |
| 423 |        88 | unknown |
|   4 |        87 | unknown |
| 1135 |        83 | unknown |
| 452 |        81 | lifted |
| 1001 |        69 | lifted |
|  42 |        69 | stub |
| 1180 |        67 | unknown |
| 477 |        65 | stub |
| 468 |        64 | stub |
| 117 |        56 | stub |
| 525 |        52 | unknown |
| 531 |        52 | unknown |
| 179 |        51 | lifted |
| 1080 |        50 | unknown |
| 112 |        43 | stub |
|   7 |        43 | unknown |
| 1134 |        43 | stub |
|  96 |        43 | unknown |
| 431 |        42 | stub |
| 103 |        42 | stub |
| 1004 |        40 | unknown |
| 878 |        39 | stub |
| 1061 |        38 | unknown |
| 159 |        38 | stub |
|  40 |        38 | unknown |
|  23 |        37 | unknown |
| 1163 |        36 | unknown |
| 176 |        36 | stub |
| 483 |        36 | unknown |
| 401 |        35 | unknown |
| 404 |        34 | unknown |

## Reading the table

The hot entries cluster as follows:

- **Already lifted, no action**: JT[3] / JT[384] / JT[488] / JT[399]
  / JT[452] / JT[1001] / JT[179].
- **Lift candidates** (high traffic, PROBE-only): JT[1200] (187x),
  JT[94] (155x), JT[41] (116x), JT[444] (96x), JT[42] (69x),
  JT[477] (65x), JT[468] (64x), JT[117] (56x). Every PROBE line on
  a boot trace is one of these dominating the noise floor.
- **Unknown** (no caller has needed a wrapper yet): JT[394] /
  JT[406] / JT[1161] / JT[1089] / JT[18] / JT[413] / etc. When a
  future lift hits one, add the wrapper + PROBE stub.
