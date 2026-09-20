# Cutting a release

Every OpenUA release so far is a **beta, published as a GitHub prerelease**.
1.0.0 is reserved for the port being confirmed on real hardware across its
targets; do not spend it on features.

A cut means FIVE machines to reflash, so batch: one cut per testing round.
Tool-only and docs-only changes ride with the next engine cut.

## 1. Before building

```sh
git fetch && git status            # main, clean, in sync (small fixes land on main directly)
```

Commit any docs first. `README.md`, `HARDWARE.md` and friends are PACKED INTO
THE ZIPS from the working tree, so a docs commit that lands mid-build leaves the
earlier zips stale.

Bump `VERSION ?=` in the `Makefile` and commit it (`build: vX.Y.Z-beta — default
the VERSION`). The recipe below passes `VERSION` explicitly, so nothing else
catches a stale default.

## 2. Build, check, smoke

```sh
make release-all VERSION=X.Y.Z-beta      # ~5 min; runs the test suite per target; four zips under dist/
tools/release_integrity.sh X.Y.Z-beta    # the SHIPPED binaries: CPU level, stripped, version stamp
tools/mkhwdist.sh X.Y.Z-beta             # dist/hw/: engine-only .st / .adf / .lha for real machines
```

`release_integrity.sh` judges the CPU level by the **ratio** of 68020-only
opcodes between the 020 and 68000 binaries (thousands vs a handful of phantoms
from data disassembled as code) — never by an absolute count — and fails loudly
when a tool or a binary is missing, because every such zero looks like a pass.

Boot the shipped binaries, not a dev build (release flags differ):

- **Falcon zip** in Hatari: copy its `frua.prg` to the repo root and
  `.claude/skills/run-falcon-port/driver.sh start` (it does not rebuild), then
  `shots` the menu. `unset DISPLAY` first.
- **AGA zip** in amiberry: copy its `frua` into `data/work/amiga-mount/` and
  `FRUA_AMIGA_DISPLAY=:98 SDL_AUDIODRIVER=dummy .claude/skills/run-amiga-port/driver.sh start`.
  This exercises the Paula FALLBACK arm, the one that could hang a stock machine.

Check the hardware images carry the same engine as the zips: read `frua` back
out of the AGA `.adf` and both `.lha`s, rejoin `frua.00`+`frua.01` from the ECS
disk pair, and `cmp` each against the zip's binary (`tools/.venv/bin/xdftool`;
it is not on PATH).

## 3. Publish

```sh
git tag -a vX.Y.Z-beta -m "OpenUA vX.Y.Z-beta — <one line>"
git push origin main vX.Y.Z-beta
gh release create vX.Y.Z-beta dist/*.zip dist/hw/*.st dist/hw/*.adf dist/hw/*.lha \
   --prerelease --title "OpenUA vX.Y.Z-beta — <headline>" --notes-file notes.md
```

Always `--prerelease`, never `--latest` (GitHub rejects the combination, and a
non-prerelease beta makes `/releases/latest` point at it for ever after). Twelve
assets. Notes: what changed and how it was proved, a downloads table, what real
hardware still owes; no AI footers. Quote only the deltas this cut measured. A
release with no engine change says so in its first line.

**Never attach or commit game DATA images** (`tools/mkdatadisks.sh` output):
they contain SSI's copyrighted assets. The engine images are fine.

A broken release is retitled (`⛔ vX — DO NOT USE`) and its notes point at the
fix; there is no unpublish.

## 4. The machines

A release is not "on the hardware" until the media is refreshed — engine pieces
only, never the user's data, saves, `frua.rsc`/`frua.cur` or `video.cfg`.
Read every copy back and `cmp` it, then unmount and power off before saying it
is safe to pull.

| Machine | Media | Refresh |
|---|---|---|
| A1200 (AGA) | Gotek USB stick, `O/OpenUA/` | the AGA + ECS `.adf` / `.lha` images |
| A500 (ECS) | ACA500plus CF card, `OpenUA/` | `frua` from the ECS `.lha`, `uainst`, `uaconv`, `RELEASE.TXT`; append a dated section to `NOTES.txt` |
| Falcon | ZuluSCSI microSD, `HD1.bin` partition 1 via mtools `@@1048576` | `FRUA.PRG` (Falcon zip), `RELEASE.TXT`. A cold power-off leaves the FAT dirty flag set and mtools refuses the partition: clear entry 1 (`FF7F` -> `FFFF`) in BOTH FAT copies first |
| Mega STe | SD, eight partitions whose labels shuffle — find `OPENUA/` by its marker file `F` | `FRUA.PRG` (ST zip), `FRUA_020.PRG` (Falcon zip), `RELEASE.TXT`, `UAINST.PRG` + `.TTP` |
| TT030 | own SD, `E3/OPENUA/` | `FRUA.PRG` (Falcon zip), `RELEASE.TXT`, `UAINST.PRG` |

A Gotek reads disk IMAGES, not files: anything meant for the Amiga through it
must be an `.adf` (`tools/mkdatadisks.sh`, `tools/tree2adf.py`).
