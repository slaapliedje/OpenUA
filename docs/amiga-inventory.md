# Amiga Krynn Trilogy — Disk Extraction Inventory

Generated: 2026-06-20

---

## Summary Table

| Game | Variant Used | Disks | Files Extracted | Unreadable Disks |
|------|-------------|-------|-----------------|-----------------|
| Champions of Krynn (CoK) | Clean set `ChampionsOfKrynn_Nof3.Adf` | 3 of 3 | 109 | None |
| Death Knights of Krynn (DoK) | `dkk01.adz` / `dkk02.adz` (Skid Row crack) | 2 of 2 | 137 | None |
| Dark Queen of Krynn (DQK) | Skid Row `.adf` set | 1+2 readable; 3 unreadable | 53 | Disk 3 (crash), Disk 2 (data errors but extracted) |

**Extraction root:** `C:\Modding\GoldBox\amiga_extracted\`

---

## Champions of Krynn — Full File Inventory

**Set:** `ChampionsOfKrynn_1of3.Adf`, `ChampionsOfKrynn_2of3.Adf`, `ChampionsOfKrynn_3of3.Adf`
**Filesystem:** OFS (Old Filing System) — all 3 disks standard, readable with minor boot-block checksum warning (not an error).

### Disk 1 (`amiga_extracted/ChampionsOfKrynn/disk1/`)

| Path | Size (bytes) | Type Guess |
|------|-------------|-----------|
| `.info` | 46 | Workbench icon |
| `8X8D0.DAA` | 25,916 | Graphics — 8x8 tile font/character set (set 0) |
| `BORDER.DAA` | 287 | Graphics — UI border definition |
| `DUNGCOM.DAA` | 8,935 | Graphics — dungeon common sprites/tiles |
| `Disk.info` | 306 | Workbench disk icon |
| `EmptyDirectory/.info` | 16 | Workbench icon |
| `RANDCOM.DAA` | 1,757 | Graphics — random common element |
| `SKY.DAA` | 2,981 | Graphics — sky/outdoor backdrop |
| `Sound/arrow` | 778 | Audio sample |
| `Sound/cast2` | 2,468 | Audio sample |
| `Sound/cast3` | 59 | Audio data/header |
| `Sound/cast3Samp` | 6,516 | Audio sample |
| `Sound/cast4` | 64 | Audio data/header |
| `Sound/dead` | 4,898 | Audio sample |
| `Sound/fireb` | 1,804 | Audio sample |
| `Sound/gate` | 2,468 | Audio sample |
| `Sound/hit` | 2,004 | Audio sample |
| `Sound/krynn` | 2,306 | Music module/data |
| `Sound/lightSamp` | 846 | Audio sample |
| `Sound/pad` | 324 | Audio sample |
| `Sound/pad2` | 1,772 | Audio sample |
| `Sound/periodTable` | 640 | Audio period table (pitch lookup) |
| `Sound/soundData` | 4,013 | Audio data blob |
| `Sound/swish` | 1,344 | Audio sample |
| `Sound/tuningfork` | 1,860 | Audio sample |
| `Sound/whislSamp2` | 558 | Audio sample |
| `WILDCOM.DAA` | 8,410 | Graphics — wildcard/combat common sprites |
| `c/assign` | 3,008 | AmigaOS executable (system) |
| `c/endcli` | 696 | AmigaOS executable (system) |
| `c/run` | 2,568 | AmigaOS executable (system) |
| `c/stack` | 872 | AmigaOS executable (system) |
| `charS.LBM` | 16,504 | **GRAPHICS** — IFF/LBM character screen (portrait or charset) |
| `charT.LBM` | 25,652 | **GRAPHICS** — IFF/LBM character screen/tileset |
| `comspr.daa` | 12,865 | Graphics — common sprite sheet |
| `emptydirectory.info` | 894 | Workbench icon |
| `game` | 479,844 | **Executable** — main game binary |
| `game.info` | 12,338 | Workbench icon (large — likely includes icon image) |
| `gothic.lbm` | 1,166 | **GRAPHICS** — IFF/LBM (small — gothic font or decorative) |
| `hdinstall` | 16,784 | Executable — HD installer |
| `hdinstall.info` | 518 | Workbench icon |
| `items` | 2,050 | Data — item definitions |
| `l/disk-validator` | 1,848 | AmigaOS library/filesystem |
| `pointers.daa` | 804 | Data — pointer table |
| `s/startup-sequence` | 40 | AmigaOS script |
| `scrn1b.lbm` | 33,134 | **GRAPHICS** — IFF/LBM full screen image (intro/cutscene) |
| `scrn2.lbm` | 34,626 | **GRAPHICS** — IFF/LBM full screen image (intro/cutscene) |
| `text1.lbm` | 11,030 | **GRAPHICS** — IFF/LBM text background screen |
| `text2.lbm` | 14,462 | **GRAPHICS** — IFF/LBM text background screen |

### Disk 2 (`amiga_extracted/ChampionsOfKrynn/disk2/`)

| Path | Size (bytes) | Type Guess |
|------|-------------|-----------|
| `8X8D1.DAA` | 25,916 | Graphics — 8x8 tile font/character set (set 1, area 1) |
| `8X8D1.DAX` | 14,346 | Graphics — compressed 8x8 tile set (area 1 variant) |
| `BIGPIC1.DAA` | 103,275 | **GRAPHICS** — large picture pack (area 1 big pics) |
| `BODY1.DAA` | 2,927 | Graphics — body/torso sprite data (area 1) |
| `CPIC1.DAA` | 37,459 | **GRAPHICS** — combat pictures pack (area 1) |
| `DUNGCOM.DAA` | 8,935 | Graphics — dungeon common (duplicate from disk 1) |
| `ECL1.DAX` | 54,401 | Data — encounter/event script data (area 1) |
| `GEO1.DAX` | 4,661 | Data — geometry/map data (area 1) |
| `HEAD1.DAA` | 4,669 | **GRAPHICS** — character portrait heads (area 1) |
| `ITEM1.DAX` | 2,633 | Data — item data (area 1) |
| `MON1CHA.DAX` | 3,683 | Data — monster character data (area 1) |
| `MON1ITM.DAX` | 2,118 | Data — monster item data (area 1) |
| `MON1SPC.DAX` | 390 | Data — monster special ability data (area 1) |
| `RANDCOM.DAA` | 1,757 | Graphics — random common (duplicate) |
| `SKY.DAA` | 2,981 | Graphics — sky (duplicate) |
| `SPRIT1.DAA` | 128,875 | **GRAPHICS** — sprite sheet pack (area 1) — LARGE |
| `WALLDEF1.DAX` | 7,558 | Data — wall definitions (area 1) |
| `WILDCOM.DAA` | 8,410 | Graphics — wildcard common (duplicate) |
| `charS.LBM` | 16,504 | **GRAPHICS** — IFF/LBM character screen (duplicate) |
| `charT.LBM` | 25,652 | **GRAPHICS** — IFF/LBM character tileset (duplicate) |
| `comspr.daa` | 12,865 | Graphics — common sprites (duplicate) |
| `pic1.daa` | 165,455 | **GRAPHICS** — picture pack (area 1) — LARGE |

### Disk 3 (`amiga_extracted/ChampionsOfKrynn/disk3/`)

| Path | Size (bytes) | Type Guess |
|------|-------------|-----------|
| `8X8D2.DAA` | 28,734 | Graphics — 8x8 tile font/character set (area 2) |
| `BODY2.DAA` | 2,927 | Graphics — body sprite data (area 2) |
| `DUNGCOM.DAA` | 8,935 | Graphics — dungeon common (duplicate) |
| `ECL2.DAX` | 77,265 | Data — encounter/event script data (area 2) |
| `GEO2.DAX` | 9,020 | Data — geometry/map data (area 2) |
| `HEAD2.DAA` | 4,669 | **GRAPHICS** — character portrait heads (area 2) |
| `ITEM2.DAX` | 1,658 | Data — item data (area 2) |
| `MON2CHA.DAX` | 4,459 | Data — monster character data (area 2) |
| `MON2ITM.DAX` | 1,889 | Data — monster item data (area 2) |
| `MON2SPC.DAX` | 749 | Data — monster special ability data (area 2) |
| `RANDCOM.DAA` | 1,757 | Graphics — random common (duplicate) |
| `SKY.DAA` | 2,981 | Graphics — sky (duplicate) |
| `SPRIT2.DAA` | 133,658 | **GRAPHICS** — sprite sheet pack (area 2) — LARGE |
| `WALLDEF2.DAX` | 9,517 | Data — wall definitions (area 2) |
| `WILDCOM.DAA` | 8,410 | Graphics — wildcard common (duplicate) |
| `bigpic2.daa` | 101,561 | **GRAPHICS** — large picture pack (area 2) |
| `charS.LBM` | 16,504 | **GRAPHICS** — IFF/LBM character screen (duplicate) |
| `charT.LBM` | 25,652 | **GRAPHICS** — IFF/LBM character tileset (duplicate) |
| `comspr.daa` | 12,865 | Graphics — common sprites (duplicate) |
| `cpic2.daa` | 43,679 | **GRAPHICS** — combat picture pack (area 2) |
| `pic2.daa` | 223,194 | **GRAPHICS** — picture pack (area 2) — LARGEST FILE |
| `save/CHRDATA*.sav/.swg/.fx` | 10–426 | Save game character data (pre-loaded party) |
| `save/savgamA.dat` | 5,481 | Save game state |

---

## Death Knights of Krynn — Full File Inventory

**Set:** `dkk01.adz` / `dkk02.adz` (Skid Row crack, decompressed from .adz)
**Filesystem:** OFS — both disks standard and readable.

### Disk 1 (`amiga_extracted/DeathKnightsOfKrynn/disk1/`)

| Path | Size (bytes) | Type Guess |
|------|-------------|-----------|
| `.info` | 41 | Workbench icon |
| `8x8d1.daa` | 63,376 | Graphics — 8x8 tile/font set |
| `AssignCOK` | 15 | AmigaOS script — COK disk path assignment |
| `AssignCOK.info` | 666 | Workbench icon |
| `BACK1.DAA` | 39,875 | **GRAPHICS** — outdoor/map background tiles |
| `BIGPIC1.DAA` | 17,602 | **GRAPHICS** — large picture pack (area 1) |
| `BORDER.DAA` | 287 | Graphics — UI border |
| `COMSPR.DAA` | 19,362 | **GRAPHICS** — common sprite sheet |
| `CPIC1.daa` | 74,929 | **GRAPHICS** — combat pictures (area 1) — LARGE |
| `DUNGCOM.daa` | 6,942 | Graphics — dungeon common sprites |
| `Directoryinfo` | 894 | Workbench icon file |
| `Disk.info` | 1,094 | Workbench disk icon |
| `ECL1.DAX` | 42,500 | Data — encounter script data (area 1) |
| `GEO1.DAX` | 3,738 | Data — geometry/map data |
| `Game.info` | 11,098 | Workbench icon |
| `HDInstall` | 17,548 | Executable — HD installer |
| `HDInstall.info` | 654 | Workbench icon |
| `ITEM0.DAX` | 1,745 | Data — base item definitions |
| `ITEMS` | 2,050 | Data — item list |
| `MON1WIZ.DAX` | 752 | Data — monster/wizard data |
| `PIC1.DAA` | 33,567 | **GRAPHICS** — picture pack (area 1) |
| `Title0.LBM` | 12,430 | **GRAPHICS** — IFF/LBM title screen (variant 0) |
| `Title1.LBM` | 37,836 | **GRAPHICS** — IFF/LBM title screen (main) |
| `Title1A.LBM` | 27,406 | **GRAPHICS** — IFF/LBM title screen (variant A) |
| `Title1B.LBM` | 27,406 | **GRAPHICS** — IFF/LBM title screen (variant B) |
| `WILDCOM.daa` | 12,525 | Graphics — wildcard/combat common |
| `c/Assign` | 3,008 | AmigaOS executable |
| `c/IconX` | 3,884 | AmigaOS executable |
| `c/LoadWB` | 2,804 | AmigaOS executable |
| `c/endcli` | 696 | AmigaOS executable |
| `c/stack` | 872 | AmigaOS executable |
| `chars.lbm` | 14,532 | **GRAPHICS** — IFF/LBM character screen |
| `chart.lbm` | 24,858 | **GRAPHICS** — IFF/LBM character tileset |
| `game` | 193,208 | **Executable** — main game binary |
| `globals.daa` | 11,398 | Data — global game state/constants |
| `gothic.lbm` | 1,166 | **GRAPHICS** — IFF/LBM gothic font/decorative |
| `l/disk-validator` | 1,848 | AmigaOS library |
| `mon1cha.dax` | 6,747 | Data — monster character data |
| `mon1itm.dax` | 2,774 | Data — monster item data |
| `mon1spc.dax` | 2,346 | Data — monster special data |
| `pointers.daa` | 804 | Data — pointer table |
| `s/startup-sequence` | 17 | AmigaOS script |
| `sound/action.smu` | 1,084 | Music module (.smu = SSI music) |
| `sound/arrow` | 778 | Audio sample |
| `sound/cast2` | 2,468 | Audio sample |
| `sound/cast3` | 59 | Audio header |
| `sound/cast3Samp` | 6,516 | Audio sample |
| `sound/cast4` | 64 | Audio header |
| `sound/dead` | 4,898 | Audio sample |
| `sound/fireb` | 1,804 | Audio sample |
| `sound/gate` | 2,468 | Audio sample |
| `sound/hit` | 2,004 | Audio sample |
| `sound/horror.smu` | 284 | Music module |
| `sound/krynn.smu` | 4,658 | Music module |
| `sound/lightSamp` | 846 | Audio sample |
| `sound/lordsoth.smu` | 550 | Music module |
| `sound/pad` | 324 | Audio sample |
| `sound/pad2` | 1,772 | Audio sample |
| `sound/periodTable` | 640 | Audio pitch table |
| `sound/sorrow.smu` | 410 | Music module |
| `sound/soundData` | 4,013 | Audio data |
| `sound/swish` | 1,344 | Audio sample |
| `sound/tuningfork` | 1,860 | Audio sample |
| `sound/undead.smu` | 632 | Music module |
| `sound/victory.smu` | 1,810 | Music module |
| `sound/whislSamp2` | 558 | Audio sample |
| `walldef1.dax` | 13,319 | Data — wall texture definitions |

### Disk 2 (`amiga_extracted/DeathKnightsOfKrynn/disk2/`)

| Path | Size (bytes) | Type Guess |
|------|-------------|-----------|
| `.info` | 18 | Workbench icon |
| `8x8d1.daa` | 63,376 | Graphics — 8x8 tile set (duplicate) |
| `BIGPIC2.DAA` | 64,044 | **GRAPHICS** — large picture pack (area 2) |
| `COMSPR.DAA` | 19,362 | **GRAPHICS** — common sprite sheet (duplicate) |
| `CPIC1.daa` | 74,929 | **GRAPHICS** — combat pictures (duplicate) |
| `DUNGCOM.daa` | 6,942 | Graphics — dungeon common (duplicate) |
| `Disk.info` | 1,104 | Workbench disk icon |
| `ECL2.DAX` | 48,630 | Data — encounter script (area 2) |
| `ECL3.DAX` | 55,412 | Data — encounter script (area 3) |
| `GEO2.DAX` | 6,808 | Data — geometry (area 2) |
| `GEO3.DAX` | 6,277 | Data — geometry (area 3) |
| `ITEM0.DAX` | 1,745 | Data — item definitions (duplicate) |
| `ITEMS` | 2,050 | Data — item list (duplicate) |
| `MON1WIZ.DAX` | 752 | Data — monster/wizard (duplicate) |
| `PIC2.DAA` | 282,317 | **GRAPHICS** — picture pack (area 2) — LARGEST FILE |
| `SPRIT1.DAA` | 78,075 | **GRAPHICS** — sprite pack (area 1) |
| `Save/CHRDATA*.gam/.eff/.jnk/.wiz` | 10–288 | Save game character data |
| `Save/savgamA.pty` | 2,925 | Save game party data |
| `Save/vaultA.dat` | 12 | Save game vault |
| `WILDCOM.daa` | 12,525 | Graphics — wildcard common (duplicate) |
| `mon1cha.dax` | 6,747 | Data — monster character (duplicate) |
| `mon1itm.dax` | 2,774 | Data — monster items (duplicate) |
| `mon1spc.dax` | 2,346 | Data — monster specials (duplicate) |
| `pointers.daa` | 804 | Data — pointer table (duplicate) |
| `sound/` (all sound files) | (same as disk 1) | Audio — full sound set duplicated |
| `walldef1.dax` | 13,319 | Data — wall definitions (duplicate) |

---

## Dark Queen of Krynn — Full File Inventory

**Set:** Skid Row `.adf` files (disk 1 clean; disk 2 extracted with data errors; disk 3 unreadable)
**Filesystem:** OFS — disk 1 standard; disk 2 OFS with multiple data block checksum errors (likely copy protection remnants or image damage); disk 3 causes unadf segfault.

### Disk 1 (`amiga_extracted/DarkQueenOfKrynn/disk1/`)

| Path | Size (bytes) | Type Guess |
|------|-------------|-----------|
| `.info` | 85 | Workbench icon |
| `DISK1.info` | 894 | Workbench icon |
| `Disk.info` | 606 | Workbench disk icon |
| `Disk1/ALWAYS.TLB` | 1,506 | Data — always-present data table |
| `Disk1/COMSPR.TLB` | 8,826 | **GRAPHICS** — common sprite table (TLB = table library) |
| `Disk1/DQK.MX` | 68,580 | **Executable** — main game module (.MX format) |
| `Disk1/GAME.FON` | 768 | **GRAPHICS** — game font bitmap |
| `Disk1/ITEM.DAT` | 4,572 | Data — item definitions |
| `Disk1/ITEMS.DAT` | 2,048 | Data — item list |
| `Disk1/MUSIC.SLB` | 6,548 | Music — sound library (SLB format) |
| `Disk1/SOUNDS.GLB` | 61,602 | Audio — sound library (GLB = global library) |
| `Disk1/TITLE.TLB` | 145,614 | **GRAPHICS** — title screen table library — LARGEST |
| `Disk1/TOPVIEW.TLB` | 386 | Data/Graphics — top-view map table |
| `Drawer_info` | 894 | Workbench icon |
| `Install_DH0` | 600 | Executable — HD install script |
| `Install_DH0.info` | 568 | Workbench icon |
| `Install_DH1` | 600 | Executable — HD install script |
| `Install_DH1.info` | 568 | Workbench icon |
| `Save.info` | 894 | Workbench icon |
| `Save/FLINTFIR.qch` | 526 | Save game character (.qch format) |
| `Save/GILTHANA.qch` | 506 | Save game character |
| `Save/HUMABRIG.qch` | 470 | Save game character |
| `Save/ISHARVLA.qch` | 470 | Save game character |
| `Save/LAURANA.qch` | 488 | Save game character |
| `Save/LUCIFER.qch` | 478 | Save game character |
| `Save/ONANNOIV.qch` | 470 | Save game character |
| `Somewhere` | 27,264 | **Executable** — loader / intro executable |
| `Somewhere.info` | 830 | Workbench icon |
| `c/SetPatch` | 5,088 | AmigaOS executable |
| `c/echo` | 560 | AmigaOS executable |
| `c/loop.ago` | 7,128 | Executable — cracker loader loop |
| `devs/system-configuration` | 232 | AmigaOS config |
| `l/Disk-Validator` | 1,848 | AmigaOS library |
| `s/SkidRow` | 14,428 | Executable — Skid Row crack intro |
| `s/startup-sequence` | 111 | AmigaOS startup script |

### Disk 2 (`amiga_extracted/DarkQueenOfKrynn/disk2/`) — EXTRACTED WITH ERRORS

**Note:** Multiple data block checksum failures during extraction. Files were written but content integrity is suspect — binary verification recommended before use.

| Path | Size (bytes) | Type Guess |
|------|-------------|-----------|
| `.info` | 22 | Workbench icon |
| `DISK2.info` | 894 | Workbench icon |
| `Disk.info` | 606 | Workbench disk icon |
| `Disk2/8X8DB.TLB` | 82,238 | **GRAPHICS** — 8x8 tile set B (large) |
| `Disk2/8X8DC.TLB` | 83,184 | **GRAPHICS** — 8x8 tile set C (large) |
| `Disk2/BACK.TLB` | 43,788 | **GRAPHICS** — background tile library |
| `Disk2/BIGPIC.TLB` | 56,184 | **GRAPHICS** — large picture library |
| `Disk2/CPIC.TLB` | 162,984 | **GRAPHICS** — combat picture library — LARGEST |
| `Disk2/DUNGCOM.TLB` | 7,422 | **GRAPHICS** — dungeon common table library |
| `Disk2/MONCHA.GLB` | 14,226 | **GRAPHICS** — monster character portraits (global library) |
| `Disk2/PICA.TLB` | 93,192 | **GRAPHICS** — picture set A |
| `Disk2/PICB.TLB` | 90,668 | **GRAPHICS** — picture set B |
| `Disk2/PICC.TLB` | 90,012 | **GRAPHICS** — picture set C |
| `Disk2/SPRIT.TLB` | 99,854 | **GRAPHICS** — sprite table library — LARGE |
| `Disk2/WILDCOM.TLB` | 9,816 | **GRAPHICS** — wildcard/combat common table |
| `Journal` | 0 | Empty file (journal log?) |

### Disk 3 — UNREADABLE

Both the `.adz` decompressed image and the Skid Row `.adf` image cause `unadf` to segfault immediately after the boot block. No files extracted. This disk likely uses a non-standard track format or is image-damaged.

---

## Candidate Graphics / Art Resources

### Champions of Krynn — Priority Art Files

| File | Location | Reason |
|------|----------|--------|
| `pic2.daa` | disk3 | 223 KB — largest single asset; likely a multi-frame picture/cutscene pack for area 2 |
| `pic1.daa` | disk2 | 165 KB — picture pack area 1; scene/story images |
| `SPRIT1.DAA` / `SPRIT2.DAA` | disk2/disk3 | 129 KB / 134 KB — main sprite sheets; all combat and character sprites |
| `BIGPIC1.DAA` / `bigpic2.daa` | disk2/disk3 | 103 KB / 102 KB — "big picture" packs; full-screen NPC/location art |
| `CPIC1.DAA` | disk2 | 37 KB — combat pictures; monster/battle portrait art |
| `cpic2.daa` | disk3 | 44 KB — combat pictures area 2 |
| `scrn1b.lbm` / `scrn2.lbm` | disk1 | 33 / 35 KB — IFF LBM full-screen images; intro/title screens, directly viewable |
| `charT.LBM` | disk1/2/3 | 26 KB — IFF LBM; likely character creation/roster screen background |
| `charS.LBM` | disk1/2/3 | 17 KB — IFF LBM; character sheet/stats screen graphic |
| `HEAD1.DAA` / `HEAD2.DAA` | disk2/disk3 | 4.7 KB each — character portrait heads by area |
| `comspr.daa` | disk1 | 13 KB — common sprites shared across game |
| `8X8D0.DAA` | disk1 | 26 KB — 8x8 pixel font/icon tiles |

**IFF/LBM files** (`.lbm`) are directly readable by standard IFF-ILBM viewers. The `.DAA`/`.DAX` files use a custom SSI packing format that requires format-specific unpacking.

### Death Knights of Krynn — Priority Art Files

| File | Location | Reason |
|------|----------|--------|
| `PIC2.DAA` | disk2 | 282 KB — largest file in entire DoK set; major picture pack |
| `CPIC1.daa` | disk1/disk2 | 75 KB — combat pictures; all battle monster art |
| `BACK1.DAA` | disk1 | 40 KB — outdoor/overworld background tiles |
| `BIGPIC2.DAA` | disk2 | 64 KB — large pictures area 2 |
| `8x8d1.daa` | disk1/disk2 | 63 KB — 8x8 tile set; larger than CoK version |
| `SPRIT1.DAA` | disk2 | 78 KB — sprite pack; combat/overworld character sprites |
| `PIC1.DAA` | disk1 | 34 KB — picture pack area 1 |
| `COMSPR.DAA` | disk1/disk2 | 19 KB — common sprites shared across game |
| `Title1.LBM` | disk1 | 38 KB — IFF LBM; main title screen, directly viewable |
| `Title1A.LBM` / `Title1B.LBM` | disk1 | 27 KB each — IFF LBM title screen variants |
| `Title0.LBM` | disk1 | 12 KB — IFF LBM; earlier title variant |
| `chart.lbm` | disk1 | 25 KB — IFF LBM; character screen background |
| `chars.lbm` | disk1 | 15 KB — IFF LBM; character portraits/charset |
| `BIGPIC1.DAA` | disk1 | 18 KB — large pictures area 1 |
| `globals.daa` | disk1 | 11 KB — may contain shared palette/global graphics data |

**Notable:** DoK has 4 title screen LBM variants (Title0, Title1, Title1A, Title1B), making it a richer source for key art than CoK.

### Dark Queen of Krynn — Priority Art Files

| File | Location | Reason |
|------|----------|--------|
| `Disk1/TITLE.TLB` | disk1 | 146 KB — title screen table library; largest file on disk 1, likely holds the main title artwork |
| `Disk2/CPIC.TLB` | disk2 (errors) | 163 KB — combat picture library; all monster/battle art |
| `Disk2/SPRIT.TLB` | disk2 (errors) | 100 KB — sprite table library |
| `Disk2/PICA.TLB` | disk2 (errors) | 93 KB — picture set A |
| `Disk2/PICB.TLB` | disk2 (errors) | 91 KB — picture set B |
| `Disk2/PICC.TLB` | disk2 (errors) | 90 KB — picture set C |
| `Disk2/BIGPIC.TLB` | disk2 (errors) | 56 KB — large picture library |
| `Disk2/8X8DB.TLB` / `8X8DC.TLB` | disk2 (errors) | 82–83 KB each — tile sets |
| `Disk2/BACK.TLB` | disk2 (errors) | 44 KB — background tiles |
| `Disk2/MONCHA.GLB` | disk2 (errors) | 14 KB — monster character graphics |
| `Disk1/COMSPR.TLB` | disk1 | 9 KB — common sprites |
| `Disk1/GAME.FON` | disk1 | 768 bytes — game font bitmap |

**Caveat:** All disk 2 files have checksum errors and may be partially corrupted. Disk 3 (which likely holds additional picture sets and the rest of the game data) is completely unrecoverable with current tools.

**TLB/GLB format:** DQK uses a different file format than CoK/DoK (`.TLB` = table library, `.GLB` = global library), suggesting a newer/revised SSI engine. These require different unpacking than the `.DAA`/`.DAX` format.

---

## Problems / Unreadable Disks

| Game | Disk | Image(s) Tried | Problem | Impact |
|------|------|----------------|---------|--------|
| Dark Queen of Krynn | Disk 2 | `dqk2.adz` (decompressed) AND `Dark Queen of Krynn...(Disk 2 of 3).adf` | Multiple `adfReadDataBlock` checksum errors; blocks reference out-of-range sectors | Files extracted but data integrity suspect — binary verification required before use |
| Dark Queen of Krynn | Disk 3 | `dqk3.adz` (decompressed) AND `Dark Queen of Krynn...(Disk 3 of 3).adf` | `unadf` segfaults immediately; both images identical behaviour | Zero files extracted; disk 3 content completely lost. Likely holds additional picture sets (PICA/PICB area 2?, SPRIT area 2?, GEO/ECL areas 2–3) |
| Champions of Krynn | All disks | `ChampionsOfKrynn_Nof3.Adf` | Boot block checksum warning only (not an error); content intact | No impact — all 109 files extracted cleanly |
| Death Knights of Krynn | All disks | `dkk01.adz` / `dkk02.adz` | Boot block checksum warning only; content intact | No impact — all 137 files extracted cleanly |

### Recovery Options for DQK Disk 3

- Source a different image (another crack group, a different dump) — the Skid Row `.adf` and `.adz` are effectively the same bad image.
- Use a Kryoflux/SuperCard Pro raw flux dump if original disks are available.
- The DQK Skid Row `.adf` Disk 2 data, while extracted, should be checksum-validated — file sizes look plausible (CPIC.TLB at 163 KB, SPRIT.TLB at 100 KB) but individual byte ranges may be corrupted.

---

## File Extension Reference

| Extension | Meaning (SSI Gold Box Amiga) |
|-----------|------------------------------|
| `.DAA` | SSI data archive — uncompressed or lightly packed graphics/data block |
| `.DAX` | SSI data archive — compressed variant (game data: maps, scripts, items) |
| `.LBM` | IFF ILBM — standard Amiga interleaved bitmap, directly viewable |
| `.TLB` | Table library — DQK-era SSI graphics/data container |
| `.GLB` | Global library — DQK-era SSI sound/graphics container |
| `.SMU` | SSI music module format |
| `.SLB` | Sound library — DQK-era container |
| `.qch` | DQK character save file |
| `.sav/.swg/.fx` | CoK character save file variants |
| `.gam/.eff/.jnk/.wiz` | DoK character save file variants |
