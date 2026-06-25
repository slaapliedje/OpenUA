# Palettes & Rendering

The cross-platform color model (layer 2 output) and the render/audio pipeline (layer 5).
Grounded in [`../dax-format.md`](../dax-format.md) §5, [`../daa-format.md`](../daa-format.md)
§5, [`../hlib-format.md`](../hlib-format.md) §2, and `hackdocs_extracted/UAPALETT.TXT`.

The hard rule from the format docs: **every generation scales differently, and HLIB is
*already* 8-bit.** Get the scaling wrong and colors are subtly (EGA/Amiga) or grossly (a
spurious `<<2` on HLIB) wrong.

---

## 1. The three color models

### EGA-16 (DOS CoK / DoK — DAX)
Not stored in the file. Use the fixed IBM EGA-16 default with the corrected brown at index 6
(from `dax-format.md` §5):

```ts
export const EGA16: RGB[] = [
  {r:0x00,g:0x00,b:0x00},{r:0x00,g:0x00,b:0xAA},{r:0x00,g:0xAA,b:0x00},{r:0x00,g:0xAA,b:0xAA},
  {r:0xAA,g:0x00,b:0x00},{r:0xAA,g:0x00,b:0xAA},{r:0xAA,g:0x55,b:0x00},{r:0xAA,g:0xAA,b:0xAA},
  {r:0x55,g:0x55,b:0x55},{r:0x55,g:0x55,b:0xFF},{r:0x55,g:0xFF,b:0x55},{r:0x55,g:0xFF,b:0xFF},
  {r:0xFF,g:0x55,b:0x55},{r:0xFF,g:0x55,b:0xFF},{r:0xFF,g:0xFF,b:0x55},{r:0xFF,g:0xFF,b:0xFF},
];
// → Palette{ colors: [...EGA16, ...160×null... but only 0..15 used], source:'ega-fixed' }
```
Pixel nibble (high = left) indexes 0..15 directly.

### Amiga 12-bit ×17 (CoK / DoK — DAA)
Embedded **per frame**: 32 × `u16 BE` words, each `0x0RGB` (only the low nibble of each
channel used). Scale each 4-bit channel to 8-bit by **×17** (equivalently `(c<<4)|c`):

```ts
const r = ((v >> 8) & 0xF) * 17, g = ((v >> 4) & 0xF) * 17, b = (v & 0xF) * 17;
```
`Palette{ colors[0..31], source:'amiga-12bit' }`. The same 64 palette bytes repeat in every
block of a file — a per-file palette. The 8 EGA-like ramp entries `(0,0,0)…(170,170,170)`
cross-validate against the game's own `scrn1b.lbm` CMAP, confirming the ×17 scaling.

### VGA 8-bit per-leaf ranges (DQK — HLIB)
**Already 8-bit; never scale.** Each leaf stores only a *slice*: `first_col` + `ncolors`
triplets fill `colors[first_col .. first_col+ncolors)`; everything else stays `null` and is
inherited at render time. `Palette{ source:'hlib-range', firstColor, count }`.

The slices follow `UAPALETT.TXT`:

| Art | Palette range |
| --- | ------------- |
| ALWAYS (UI icons) | 0–15 |
| FRAME / GEN | 16–31 |
| WALLS | 32–68 |
| COMBAT SPRITES | 64–95 |
| CBODY/CPIC combat icons | none (use loaded WILDCOM/DUNGCOM) |
| BACKDROPS | 144–175 |
| SPRITES | ~176–255 |
| DUNGEONS / WILDERNESS / SMALLPICS / BIGPICS / OVERLAND | 32–255 |
| TITLE | 0–255 (pic 1), 32–255 (pics 2–11) |

### Palette inheritance & layering (HLIB, important)
A single in-game screen layers several leaves' partial palettes; "UA always uses the most
recently loaded palette." Consequence noted in `hlib-format.md` §2: a **standalone** backdrop
renders dim/near-grayscale (only its 144–175 ramp defined) — that is **correct data**, not a
bug.

Engine render rule:
```ts
function resolve(idx: number, frame: Palette | null, ctx: Palette): RGB {
  const c = frame?.colors[idx] ?? ctx.colors[idx];
  return c ?? {r:0,g:0,b:0};   // undefined → black, as on hardware
}
```
The engine maintains a **context palette** (256 slots) updated as leaves load — e.g. load
WILDCOM/DUNGCOM, then overlay a backdrop's 144–175, then sprites' 176–255. The bare asset
viewer can offer a "merge with context palette X" toggle to preview correct colors.

### Color cycling (HLIB only)
`nranges` bands `{dir, speed, start, count}` rotate `colors[start .. start+count)` every
`speed` ticks. Drive from a render-loop timer; rotate the palette slice, not the pixels (the
indices are stable). Used for water/fire/torch effects.

---

## 2. Render pipeline (indexed → RGBA → screen)

Native model is **320×200**, top-left origin. Two stages:

**Stage A — indexed → RGBA `ImageData`** (CPU, per frame that changes):
```ts
function blit(img: IndexedImage, ctx: Palette, out: ImageData) {
  const d = out.data;
  for (let i = 0; i < img.indices.length; i++) {
    const idx = img.indices[i];
    if (img.transparentIndex !== null && idx === img.transparentIndex) continue; // leave dst
    const c = img.palette?.colors[idx] ?? ctx.colors[idx] ?? BLACK;
    const o = i * 4; d[o]=c.r; d[o+1]=c.g; d[o+2]=c.b; d[o+3]=255;
  }
}
```
Composite the screen by blitting frames in z-order (backdrop → walls → sprites → UI frame),
honoring `xOffset`/`yOffset` placement hints and `transparentIndex`.

**Stage B — present at integer scale:**
- **Simple:** a 320×200 backing `<canvas>` via `putImageData`, then CSS
  `image-rendering: pixelated` on a scaled display canvas (or `drawImage` with
  `imageSmoothingEnabled = false` to an integer-multiple target).
- **WebGL (preferred for scaling/CRT):** upload the 320×200 RGBA as a texture, draw a
  fullscreen quad with `NEAREST` filtering at integer scale; optional CRT/scanline shader.
  Keeps the crisp pixel grid and offloads scaling to the GPU.

Pick the largest integer scale that fits the viewport (`floor(min(vw/320, vh/200))`),
letterbox the remainder. Never fractional-scale the pixel art.

---

## 3. Audio

Two classes, both decodable to WebAudio-playable forms (`Sound` in
[asset-model.md](./asset-model.md)). Inventory: DQK ships per-device `.XMI` music +
`SFXDQ.VOC` + `SOUNDS.GLB` (DIG4); CoK/DoK ship `MIDI.DAX`/`MUSIC.DAX` + `SOUND.DAX`
(PC-speaker/SFX) and, on Amiga, raw 8-bit samples + `.smu` modules.

### Music — XMI (XMIDI) → MIDI → WebAudio
DQK music is **XMIDI** (`AD/RO/TY/PCDQ*.XMI`, one set per sound device). Path:
1. Parse XMIDI (`FORM XDIR` / `CAT … XMID` → `EVNT`). XMIDI uses delay-based timing and an
   interval/duration note model; convert to standard MIDI delta-time events → `MidiTrack`.
2. Play `MidiTrack` through a soundfont synth in WebAudio (e.g. a JS SF2 player) — MT-32 or
   GM soundfont depending on which `*DQ*.XMI` variant the manifest selects.

`TUNES.TXT` confirms track ordering (??DQ1 Overture; ??DQ2 Treasure/Foes/Battle/Mystery/
Uh-Oh/Evil-March; ??DQ3 Victory). XMIDI→MIDI conversion is **future work**; document the
shape now.

### Digitized SFX — VOC / DIG4 → PCM
- `SFXDQ.VOC` is a standard **Creative VOC** file with marker blocks separating 13 effects
  (markers 0–12: Cast, Flame, Sorcery, Die, Sling, Hit, Lightning, Swing, Walk, Fireball,
  Bow, Sploosh, Crackle — per `UASOUND.TXT`). Parse VOC data blocks (type 1 = sound data:
  sample-rate divisor + codec byte + 8-bit unsigned PCM) → split on markers → `Sound[]` PCM.
- `DIG4` members inside `SOUNDS.GLB` (HLIB container, member tag `DIG4`): container walk works
  (see [loaders.md](./loaders.md) §3), **payload codec UNSOLVED** — likely 4-bit ADPCM
  ("DIG4"). Decode to PCM once reversed; for now expose raw bytes.
- Amiga SFX are raw 8-bit signed samples (the `Sound/*` files) with a `periodTable` for pitch;
  `.smu` are SSI music modules (format not yet decoded).

WebAudio playback: decode to `Int16Array` mono, push into an `AudioBuffer` at the source
sample rate, play through an `AudioBufferSourceNode`. PC-speaker `SOUND.DAX` effects are
frequency/duration pairs → synthesize a square wave (low priority).

---

## Verification anchors
- DAX colors: title screen renders with cyan sky / green foliage / brown structures / white
  text (corroborates EGA table + high-nibble-first).
- DAA scaling: BIGPIC1 b114 ×17 palette matches `scrn1b.lbm` CMAP and the DOS DAX render of
  the same block.
- HLIB no-scaling: ALWAYS.TLB palette begins `00 00 00 / 00 00 AB / 00 AB 00 …` (0xAB=171),
  max byte 255 — proves full 8-bit, no DAC `<<2`.
