# jt94 faithful-remap campaign — the audit + migration plan

**Status: AUDITED 2026-07-03, execution pending (its own session).**

## The divergence

The port's jt94 (JT[94], CODE 6+0x3fd6) carries a DELIBERATELY inverted
style-remap block ("the engine's non-prompt text is tuned around it"):

```c
/* PORT (inverted): */
if (style != 0) {
        if (page < 23) {
                if (col == 7 || col == 8)
                        col = 15;
        }
        style = 8;
}
```

The Mac (L3fd6 0x3ff8..0x4040, fully decoded — fp@9/11/13/15 = the four
byte args page/row/col/style):

```c
/* MAC (faithful): */
if (style == 0) {
        style = 8;
        if (row >= 23 && (col == 7 || col == 8))
                col = 15;
}
/* style != 0 passes through UNCHANGED. */
```

Three inversions: the gate tests style==0 (not !=0), the inner test is
row>=23 (not page<23), and the col whitening applies on the OTHER side.
After the remap all arms merge (faithful arm order):

1. `row == 24` -> the box/plate arm (0x4050..0x4196) -> the common tail.
2. `-27990 == 5 && row == 23` -> the combat bottom-line arm (fixed
   8000-space Y anchor 8089, page-step X, l3f88 band erase behind
   jt423(len)) -> the common tail. (LIFTED faithfully in 56d6227; the
   old page==23 gate + stub ate the combat panel labels.)
3. default: Y = (row<<2)+8000; `jt1200()==3 && col==11` -> col=0,
   style=15.
Common tail: `jt1089(Y, (page<<2)+8000, (style<<4)|col, "%s", buf)`.

NOTE: the port's row-24 branch performs its own LOCAL faithful remap
(added when the global one was known-inverted). When the global remap is
made faithful, DELETE the row-24 branch's local copy or it double-applies.

## Why it can't be flipped alone: CALLER DRIFT (the audit)

132 jt94 call sites. By style arg: 92x style 0, 30x style 8, 2x style 7,
rest variables. Samples vs the Mac:

| caller | port literals | Mac literals | verdict |
|---|---|---|---|
| jt938 clock/pos (CODE 12) | (26,15,7,0) / (26,13,7,0) | (26,15,7,0) / (26,13,7,0) | FAITHFUL |
| l02dc roster header (CODE 12+0x2dc, 0x348/0x36c) | (t0,t1,7,8) / (…,11,8) | (fp-5, 2, **12**, **0**, "Name") / (33, 2, 12, 0, "AC HP") | **TUNED** |
| l177a press-Return (CODE 7) | (7,24,0,7) | (7,24,0,7) | FAITHFUL |
| jt38 panel labels (CODE 6) | (23,row,7,0) | (23,row,7,0) | FAITHFUL |

The tuned population is concentrated in the **style-8 clusters**:
- l02dc / the Hall-roster row painters (boot.c ~21692-21851): cols
  7/11/12 + style 8 — the Mac uses col 12 + style 0 for the headers;
  each row call needs its Mac literal read out of CODE 12 L02dc/L0528+.
- jt886's char-sheet block (~23905-24000, "23,N,7,8" x ~25): verify vs
  CODE 19's jt886 (its Mac literals were probably style 0).

## Rendering rules that arbitrate the visual outcome (jt1089 colour pairs)

colour word = (bg<<4)|fg. jt1089 paints the cell bg only when
(bg==15 || fg==0) && bg != 8; bg 8 = the transparent window-grey
default (see the prompt-plate memory / 06cd0b5). Consequences of the
faithful flip:
- style-0 sites: bg 0 -> 8. No visual change (both transparent) EXCEPT
  rows >= 23 where col 7/8 text WHITENS to 15 (the Mac's bottom-row
  look — likely fixes grey-on-dark prompts).
- style-8 sites (once restored to their Mac style-0+col forms): the
  headers go back to the Mac's colours (e.g. roster "Name"/"AC HP" =
  col 12 light-blue-ish, not white-15).
- style-7 sites (l177a): bg 7 fg 0 -> the fg==0 erase rule fires with
  bg 7 = the grey plate + black text ("Press [Return]"), which the
  inverted remap today squashes to bg 8 transparent. The flip should
  IMPROVE the prompt look (closer to 06cd0b5's model).

## The migration plan (one session, screen-by-screen)

1. Flip the remap to the Mac form; delete the row-24 local remap.
2. Restore the tuned callers' Mac literals: l02dc cluster (CODE 12
   asm 0x2dc..0x600), jt886 cluster (CODE 19), any other style-8 site
   whose Mac original differs (grep + per-site asm check).
3. Visual regression sweep (FRUA_ENTRY harness + FIFO keys): main menu,
   Training Hall + roster, char sheet, design picker, dungeon HUD,
   combat (bar/panel/messages), the XP page, press-Return prompts,
   Save/Load pickers.
4. Commit per cluster; keep each step revertable.
