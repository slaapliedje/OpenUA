# Char-gen render trace — BasiliskII (real Mac), JT[1089] text draws

Ground-truth capture of every `JT[1089]` (the text painter) call through the
whole character-generation flow on the real Macintosh build, via BasiliskII.
Each line is `#seq v=<row> h=<col> colour=<n> fmt="<template>"`. Use it to
validate the port's char-gen render (coords, colours, draw order, the exact
label set + ordering). 8000-based coords are the GLIB pen space; bare coords
(e.g. v=24) are native screen pixels.

Captured 2026-06-17 by the user. Columns: `v` = row (pen Y), `h` = col (pen X),
`colour` = the pen colour byte (see "Colour key" below), `fmt` = the THINK-C
template (`%s`/`%c` are filled at runtime).

## Colour key (observed)

- **135** = normal option text (light grey) — the pick lists, sheet values.
- **140** = the PICK headers (RACE/ALIGNMENT/GENDER/CLASS).
- **139 / 143** = button highlight / accelerator-letter overlay (the `%c`).
- **131** = a pressed/selected button face.
- **128** = **disabled / "not available"** text (black) — confirms the Training
  Hall greys unavailable commands in colour 128 (see task-2 menu availability:
  `jt137`/`jt382` draw disabled items in 128). See #311–#315.

## Screen 1 — the PICK screen (L3666)

Headers (colour 140): `PICK RACE` v8006/h8006, `PICK ALIGNMENT` v8040/h8006,
`PICK GENDER` v8076/h8006, `PICK CLASS` v8012/h8068.

RACE list (colour 135, h=24, native px), **in Mac order**:
```
ELF v24, HALF-ELF v32, DWARF v40, GNOME v48, HALFLING v56, HUMAN v64
```
GENDER: `MALE` v164, `FEMALE` v172 (h=24).

CLASS list (colour 135, h=152), **Mac order** — note the single classes then
the multi-class combos:
```
CLERIC v36, FIGHTER v44, PALADIN v52, RANGER v60, MAGIC-USER v68, THIEF v76,
CLERIC/FIGHTER v84, CLERIC/FIGHTER/M-U v92, CLERIC/RANGER v100,
CLERIC/MAGIC-USER v108, FIGHTER/MAGIC-USER v116, FIGHTER/THIEF v124,
FIGHTER/M-U/THIEF v132, MAGIC-USER/THIEF v140
```
ALIGNMENT (colour 135, h=24): axis-2 `LAWFUL` v92 / `NEUTRAL` v100 / `CHAOTIC`
v108; axis-1 `GOOD` v120 / `NEUTRAL` v128 / `EVIL` v136.

Buttons: `Done` v189/h8, `Exit` v189/h48 (+ the `%c` accelerator overlay in 143).

The class/alignment list **re-renders as the race/class pick changes** (#176–
#184): e.g. picking a race that forbids some classes redraws PALADIN/FIGHTER and
re-filters the alignment rows. This is the `jt566`/`jt568` action-proc redraw.

## Screen 2 — the stat SHEET (jt886 / cg_char_sheet)

`%s` fields at the 8000 panel coords (#185–#310), repainted on each **Reroll**.
The sheet action bar (#256–#263):
```
Done v189/h168, Reroll Stats v189/h8, Modify v189/h112, Exit v189/h208
```
(colour 135 normal, 143 the accelerator `%c`; 131 the pressed face on #264.)

## Screen 3 — the body / final review (jt573 / l11ac / l09dc)

The two-pane body-sprite screen's TEXT layer (#311–#324). The body sprites
themselves are NOT jt1089 — they blit through `jt57`/GLIB (DUNGCOM1), so they do
not appear here. What jt1089 draws:

```
#311 v189 h8004 colour128 "%s"      ← bottom command bar, BLACK (disabled)
#312-315 v189 h8068 colour128 "%s " ← disabled command items (black, x4)
#316 v8032 h8092 colour135 "%s"     ← info panel (right column, h=8092)
#317 v8036 h8092 colour135 "%s"
#318 v8040 h8092 colour135 "%s"
#319 v8048 h8092 colour135 "%s"
#320 v8008 h8094 colour135 "ready    action"   ← column header
#321 Done v189/h8, #323 Exit v189/h48 (+ %c in 143)
```

KEY: the colour-128 (black) draws at #311–#315 are the **disabled / "not
available" command-bar items** — direct ground truth for the task-2 menu
availability work (the Mac draws unavailable commands in black, it does not hide
them).

## Full capture

```
#138 v=119 h=168 colour=131 fmt="Create Character"
#139 v=119 h=168 colour=139 fmt="%c"
#140 v=8006 h=8006 colour=140 fmt="PICK RACE"
#141 v=8040 h=8006 colour=140 fmt="PICK ALIGNMENT"
#142 v=8076 h=8006 colour=140 fmt="PICK GENDER"
#143 v=8012 h=8068 colour=140 fmt="PICK CLASS"
#144 v=24 h=24 colour=135 fmt="ELF"
#145 v=32 h=24 colour=135 fmt="HALF-ELF"
#146 v=40 h=24 colour=135 fmt="DWARF"
#147 v=48 h=24 colour=135 fmt="GNOME"
#148 v=56 h=24 colour=135 fmt="HALFLING"
#149 v=64 h=24 colour=135 fmt="HUMAN"
#150 v=164 h=24 colour=135 fmt="MALE"
#151 v=172 h=24 colour=135 fmt="FEMALE"
#152 v=36 h=152 colour=135 fmt="CLERIC"
#153 v=44 h=152 colour=135 fmt="FIGHTER"
#154 v=52 h=152 colour=135 fmt="PALADIN"
#155 v=60 h=152 colour=135 fmt="RANGER"
#156 v=68 h=152 colour=135 fmt="MAGIC-USER"
#157 v=76 h=152 colour=135 fmt="THIEF"
#158 v=84 h=152 colour=135 fmt="CLERIC/FIGHTER"
#159 v=92 h=152 colour=135 fmt="CLERIC/FIGHTER/M-U"
#160 v=100 h=152 colour=135 fmt="CLERIC/RANGER"
#161 v=108 h=152 colour=135 fmt="CLERIC/MAGIC-USER"
#162 v=116 h=152 colour=135 fmt="FIGHTER/MAGIC-USER"
#163 v=124 h=152 colour=135 fmt="FIGHTER/THIEF"
#164 v=132 h=152 colour=135 fmt="FIGHTER/M-U/THIEF"
#165 v=140 h=152 colour=135 fmt="MAGIC-USER/THIEF"
#166 v=92 h=24 colour=135 fmt="LAWFUL"
#167 v=100 h=24 colour=135 fmt="NEUTRAL"
#168 v=108 h=24 colour=135 fmt="CHAOTIC"
#169 v=120 h=24 colour=135 fmt="GOOD"
#170 v=128 h=24 colour=135 fmt="NEUTRAL"
#171 v=136 h=24 colour=135 fmt="EVIL"
#172 v=189 h=8 colour=135 fmt="Done"
#173 v=189 h=8 colour=143 fmt="%c"
#174 v=189 h=48 colour=135 fmt="Exit"
#175 v=189 h=48 colour=143 fmt="%c"
#176 v=52 h=152 colour=135 fmt="PALADIN"
#177 v=44 h=152 colour=135 fmt="FIGHTER"
#178 v=52 h=152 colour=135 fmt="PALADIN"
#179 v=100 h=24 colour=135 fmt="NEUTRAL"
#180 v=108 h=24 colour=135 fmt="CHAOTIC"
#181 v=128 h=24 colour=135 fmt="NEUTRAL"
#182 v=136 h=24 colour=135 fmt="EVIL"
#183 v=189 h=8 colour=131 fmt="Done"
#184 v=189 h=8 colour=139 fmt="%c"
#185 v=8004 h=8004 colour=135 fmt="%s"
#186 v=8004 h=8080 colour=139 fmt="%s"
#187 v=8004 h=8108 colour=135 fmt="%s"
#188 v=8012 h=8004 colour=139 fmt="%s"
#189 v=8012 h=8032 colour=139 fmt="%s"
#190 v=8016 h=8004 colour=139 fmt="%s"
#191 v=8016 h=8080 colour=139 fmt="%s"
#192 v=8020 h=8004 colour=139 fmt="%s"
#193 v=8028 h=8004 colour=139 fmt="%s"
#194 v=8028 h=8028 colour=139 fmt="%s"
#195 v=8024 h=8096 colour=139 fmt="%s"
#196 v=8028 h=8108 colour=135 fmt="%s"
#197 v=8036 h=8004 colour=135 fmt="%s"
#198 v=8036 h=8024 colour=135 fmt="%s"
#199 v=8036 h=8048 colour=135 fmt="%s"
#200 v=8040 h=8004 colour=135 fmt="%s"
#201 v=8040 h=8024 colour=135 fmt="%s"
#202 v=8040 h=8048 colour=135 fmt="%s"
#203 v=8044 h=8004 colour=135 fmt="%s"
#204 v=8044 h=8024 colour=135 fmt="%s"
#205 v=8044 h=8048 colour=135 fmt="%s"
#206 v=8048 h=8004 colour=135 fmt="%s"
#207 v=8048 h=8024 colour=135 fmt="%s"
#208 v=8048 h=8048 colour=135 fmt="%s"
#209 v=8052 h=8004 colour=135 fmt="%s"
#210 v=8052 h=8024 colour=135 fmt="%s"
#211 v=8052 h=8048 colour=135 fmt="%s"
#212 v=8056 h=8004 colour=135 fmt="%s"
#213 v=8056 h=8024 colour=135 fmt="%s"
#214 v=8056 h=8048 colour=135 fmt="%s"
#215 v=8012 h=8080 colour=139 fmt="%s"
#216 v=8012 h=8124 colour=135 fmt="%s"
#217 v=8012 h=8128 colour=135 fmt="%s"
#218 v=8068 h=8004 colour=139 fmt="%s"
#219 v=8068 h=8064 colour=135 fmt="%s"
#220 v=8068 h=8080 colour=139 fmt="%s"
#221 v=8068 h=8132 colour=135 fmt="%s"
#222 v=8072 h=8004 colour=139 fmt="%s"
#223 v=8072 h=8064 colour=135 fmt="%s"
#224 v=8072 h=8080 colour=139 fmt="%s"
#225 v=8072 h=8140 colour=135 fmt="%s"
#226 v=8076 h=8004 colour=139 fmt="%s"
#227 v=8076 h=8044 colour=135 fmt="%s"
#228 v=8036 h=8020 colour=135 fmt="%s"
#229 v=8036 h=8048 colour=135 fmt="%s"
#230 v=8040 h=8020 colour=135 fmt="%s"
#231 v=8040 h=8048 colour=135 fmt="%s"
#232 v=8044 h=8020 colour=135 fmt="%s"
#233 v=8044 h=8048 colour=135 fmt="%s"
#234 v=8048 h=8020 colour=135 fmt="%s"
#235 v=8048 h=8048 colour=135 fmt="%s"
#236 v=8052 h=8020 colour=135 fmt="%s"
#237 v=8052 h=8048 colour=135 fmt="%s"
#238 v=8056 h=8020 colour=135 fmt="%s"
#239 v=8056 h=8048 colour=135 fmt="%s"
#240 v=8028 h=8028 colour=139 fmt="%s"
#241 v=8012 h=8080 colour=139 fmt="%s"
#242 v=8012 h=8124 colour=135 fmt="%s"
#243 v=8012 h=8132 colour=135 fmt="%s"
#244 v=8068 h=8004 colour=139 fmt="%s"
#245 v=8068 h=8064 colour=135 fmt="%s"
#246 v=8068 h=8080 colour=139 fmt="%s"
#247 v=8068 h=8132 colour=135 fmt="%s"
#248 v=8072 h=8004 colour=139 fmt="%s"
#249 v=8072 h=8064 colour=135 fmt="%s"
#250 v=8072 h=8080 colour=139 fmt="%s"
#251 v=8072 h=8140 colour=135 fmt="%s"
#252 v=8076 h=8004 colour=139 fmt="%s"
#253 v=8076 h=8044 colour=135 fmt="%s"
#254 v=8036 h=8080 colour=135 fmt="%s"
#255 v=8036 h=8140 colour=135 fmt="%s"
#256 v=189 h=168 colour=135 fmt="Done"
#257 v=189 h=168 colour=143 fmt="%c"
#258 v=189 h=8 colour=135 fmt="Reroll Stats"
#259 v=189 h=8 colour=143 fmt="%c"
#260 v=189 h=112 colour=135 fmt="Modify"
#261 v=189 h=112 colour=143 fmt="%c"
#262 v=189 h=208 colour=135 fmt="Exit"
#263 v=189 h=208 colour=143 fmt="%c"
#264 v=189 h=168 colour=131 fmt="Done"
#265 v=189 h=168 colour=139 fmt="%c"
#266 v=8004 h=8004 colour=135 fmt="%s"
#267 v=8004 h=8080 colour=139 fmt="%s"
#268 v=8004 h=8108 colour=135 fmt="%s"
#269 v=8012 h=8004 colour=139 fmt="%s"
#270 v=8012 h=8032 colour=139 fmt="%s"
#271 v=8016 h=8004 colour=139 fmt="%s"
#272 v=8016 h=8080 colour=139 fmt="%s"
#273 v=8020 h=8004 colour=139 fmt="%s"
#274 v=8028 h=8004 colour=139 fmt="%s"
#275 v=8028 h=8028 colour=139 fmt="%s"
#276 v=8024 h=8096 colour=139 fmt="%s"
#277 v=8028 h=8108 colour=135 fmt="%s"
#278 v=8036 h=8004 colour=135 fmt="%s"
#279 v=8036 h=8020 colour=135 fmt="%s"
#280 v=8036 h=8048 colour=135 fmt="%s"
#281 v=8040 h=8004 colour=135 fmt="%s"
#282 v=8040 h=8020 colour=135 fmt="%s"
#283 v=8040 h=8048 colour=135 fmt="%s"
#284 v=8044 h=8004 colour=135 fmt="%s"
#285 v=8044 h=8020 colour=135 fmt="%s"
#286 v=8044 h=8048 colour=135 fmt="%s"
#287 v=8048 h=8004 colour=135 fmt="%s"
#288 v=8048 h=8020 colour=135 fmt="%s"
#289 v=8048 h=8048 colour=135 fmt="%s"
#290 v=8052 h=8004 colour=135 fmt="%s"
#291 v=8052 h=8020 colour=135 fmt="%s"
#292 v=8052 h=8048 colour=135 fmt="%s"
#293 v=8056 h=8004 colour=135 fmt="%s"
#294 v=8056 h=8020 colour=135 fmt="%s"
#295 v=8056 h=8048 colour=135 fmt="%s"
#296 v=8036 h=8080 colour=135 fmt="%s"
#297 v=8036 h=8140 colour=135 fmt="%s"
#298 v=8012 h=8080 colour=139 fmt="%s"
#299 v=8012 h=8124 colour=135 fmt="%s"
#300 v=8012 h=8132 colour=135 fmt="%s"
#301 v=8068 h=8004 colour=139 fmt="%s"
#302 v=8068 h=8064 colour=135 fmt="%s"
#303 v=8068 h=8080 colour=139 fmt="%s"
#304 v=8068 h=8132 colour=135 fmt="%s"
#305 v=8072 h=8004 colour=139 fmt="%s"
#306 v=8072 h=8064 colour=135 fmt="%s"
#307 v=8072 h=8080 colour=139 fmt="%s"
#308 v=8072 h=8140 colour=135 fmt="%s"
#309 v=8076 h=8004 colour=139 fmt="%s"
#310 v=8076 h=8044 colour=135 fmt="%s"
#311 v=189 h=8004 colour=128 fmt="%s"
#312 v=189 h=8068 colour=128 fmt="%s "
#313 v=189 h=8068 colour=128 fmt="%s "
#314 v=189 h=8068 colour=128 fmt="%s "
#315 v=189 h=8068 colour=128 fmt="%s "
#316 v=8032 h=8092 colour=135 fmt="%s"
#317 v=8036 h=8092 colour=135 fmt="%s"
#318 v=8040 h=8092 colour=135 fmt="%s"
#319 v=8048 h=8092 colour=135 fmt="%s"
#320 v=8008 h=8094 colour=135 fmt="ready    action"
#321 v=189 h=8 colour=135 fmt="Done"
#322 v=189 h=8 colour=143 fmt="%c"
#323 v=189 h=48 colour=135 fmt="Exit"
#324 v=189 h=48 colour=143 fmt="%c"
```
