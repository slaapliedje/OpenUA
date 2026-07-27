# Chunky → planar on 68k: what worked, and what it actually cost

Notes from porting a 1993 Macintosh game engine to the Atari ST/STE/TT and the
Amiga. The Mac drew into a **chunky** framebuffer — one byte per pixel, the
byte *is* the colour index. The ST and the Amiga are **bitplane** machines with
no such mode, so every frame has to be transposed before it can be shown. On a
7–8 MHz 68000 that transpose is most of the cost of drawing anything.

This is a writeup of what we tried, in order, with the numbers. Several of the
things that sounded best did least, so the failures are here too — they are
the part that is hard to find elsewhere.

Everything below was measured in emulation (Hatari, amiberry). Where a number
looks too good, that is called out rather than quietly enjoyed.

---

## 1. Know your target's actual layout

"Planar" is not one format. Getting this wrong costs a day.

| machine | mode | layout |
|---|---|---|
| Atari ST/STE low | 320×200, 16 colours | 4 planes, **word-interleaved**: plane words for a pixel group sit adjacent — `p0 p1 p2 p3` then the next 16 pixels. 160 bytes/line, 32000/screen |
| Atari TT low | 320×480, 256 colours | 8 planes, word-interleaved |
| Amiga OCS/ECS | 320×200, 32 colours | 5 planes, **separate** — each plane is its own contiguous bitmap |
| Amiga AGA | 320×200, 256 colours | 8 separate planes |
| Atari Falcon (VIDEL) | 320×200, 16 bpp | **chunky** — no conversion needed at all |
| Amiga RTG | chunky | no conversion needed |

Word-interleaved and separate-plane need genuinely different inner loops. The
*bit transpose* is shared; only the scatter differs. Structure the code that
way — one transpose network, per-machine scatter — and you write the hard part
once. Ours is 81 lines with no dependencies at all.

The Falcon is the interesting entry: it is a 68030 Atari that is **chunky**, so
it needs none of this. Do not assume "Atari" means "planar". Equally, do not
assume "the fast machine doesn't need it" — the TT is a 32 MHz 030 and *is*
planar.

## 2. The transpose itself is not where your time goes

Write a decent 32-pixels-at-a-time bit transpose, test it against a naïve
reference, and move on. It matters less than everything below.

The two optimisations that *did* pay, both content-driven rather than clever:

**Flat-run detection: −36%.** Real game frames are full of flat colour — UI
panels, backdrops, black borders. Checking whether the next 32 pixels are all
the same index and, if so, writing the four plane words directly from a
precomputed pattern skipped the transpose entirely for a large fraction of the
screen. This was the single biggest win in the whole effort and it is about ten
lines.

**Row diffing.** Keep a shadow copy of the last presented chunky frame; only
convert rows that changed. Obvious, and effective, but see §6 — it interacts
badly with palette changes in a way that took a long time to see.

## 3. The next idea: stop converting at all ("draw-time planar")

If the transpose is the cost, delete it: have the drawing code write bitplanes
*directly* instead of writing chunky bytes that later get converted.

The design that survived contact:

- Keep the chunky buffer. It stays the source of truth and the fallback.
- Add a parallel plane buffer plus **per-row ownership**: a coverage count and
  a copy of the chunky bytes each row was stamped from.
- Converted writers stamp *both* — chunky (for coherence) and planes (for
  speed).
- At present time, a row is skipped if it is fully covered **and** its recorded
  chunky bytes still match the live chunky buffer. Otherwise it converts as
  before. The invariant is `stamp == remap[chunky]`.

That last condition is what makes it safe to adopt incrementally: an
unconverted writer, or one that scribbles over a converted writer's pixels, is
*detected* and falls back rather than corrupting the display. You can convert
writers one at a time and never have a broken frame.

**What it bought: about half.** Measured over a boot → dungeon → walk on the
ST: 3373 of 6349 presented rows were writer-owned and skipped; **2976 (47%)
still converted.**

That is a real win and it is nowhere near "no conversion". Budget accordingly.

## 4. Three walls we hit (this is the useful part)

Every one of these looked tractable from a distance.

**Wall 1 — z-order.** Converting "the chrome" (static UI) first seems safe:
it's static. But chrome is drawn *under* things that are still chunky, and a
plane-stamping writer and a chunky writer cannot interleave in z-order without
one of them reading the other's output. Only self-contained rectangles that
nothing overlaps are safely convertible.

**Wall 2 — text lifecycle.** Text overlay was proven to work as a mechanism and
still failed: the engine draws, erases and redraws text at points scattered
through a modal's lifecycle, and the plane stamps have to be invalidated at
each one. The bookkeeping to know *when* exceeded the cost it saved.

**Wall 3 — the remap bootstrap.** The one that actually caps the idea. A
draw-time writer must convert `index → slot` at the moment it draws, so the
palette remap has to exist *before* the frame is drawn. Ours was computed **at
present time, from the drawn frame's histogram** (median-cut over the indices
that actually appear).

We tried deriving it from the palette instead, ahead of drawing. It was a clear
**quality regression** — the palette carried ~32 meaningful entries plus ~224
stale ones left over from previous scenes, so reducing over all 256 spent the
16 hardware slots on colours that never appear on screen. The frame histogram
knows which indices are real; the palette does not.

**So a new scene's first frame needs a remap that cannot exist until that frame
has been drawn.** The scene-transition conversion is structurally unavoidable.
Draw-time can remove the *steady-state* per-frame conversion; it cannot remove
the transition.

If you are planning this work: the remap bootstrap is the thing to think about
on day one, not month two.

## 5. The blitter is worth less than you expect

The Atari BLiTTER (Mega ST, STE, TT) and the Amiga blitter are the obvious
accelerators. We measured, on an emulated STE, the three copy shapes the
present actually issues:

| shape | CPU | BLiTTER | speed-up |
|---|---|---|---|
| one 32000-byte screen copy | 20.5 ms | 5.5 ms | **3.76×** |
| 200 × 160-byte row copies | 38.3 ms | 27.2 ms | 1.41× |
| 200 × 320-byte row copies | 60.5 ms | 44.2 ms | 1.37× |

Two lessons, and only one is about the blitter.

**Setup dominates small blits.** ~15 register writes per blit, plus — on Atari —
the I/O page is supervisor-only, so each blit needs a `Supexec` trap unless you
batch. At 160 bytes the setup eats most of the gain. Batch an entire frame's
blits into one supervisor call or don't bother.

**Look again at the second row of that table.** 200 separate 160-byte `memcpy`s
cost **1.9× more** than one 32000-byte `memcpy` of exactly the same bytes. Half
the row-path copy cost was per-call overhead. Coalescing contiguous changed rows
into runs recovers that — with no special hardware, on every machine.

**End-to-end result: 3.4%.** Not 3.76×. Because the copies were never the
bottleneck — the conversion and the row compare were. A first cut that blitted
only the row path and left the full-screen path on `memcpy` measured **0.9%**,
i.e. noise; adding the full-screen seed took it to 3.4%.

If you take one thing from this section: **measure the whole present before
optimising any part of it.** The microbenchmark said 3.76× and it was honest;
it just wasn't answering the question that mattered.

## 6. Where the time actually went (the punchline)

Having built ownership tracking, we instrumented *why* each row still converted:
a **coverage hole** (nobody stamped it — go convert another writer) or a **stamp
mismatch** (someone clobbered stamped pixels — go fix that writer).

Expected mostly holes. Got neither, cleanly:

```
presents = 16    rebands = 11    reband skips = 0
rows: 0 hole-only, 0 mismatch-only, 200 flagged BOTH
every mismatch's first differing pixel at x = 0
```

`x = 0` on every row is the tell. A real writer clobber starts at *that
writer's* left edge, so mismatches would spread across varying x. x=0 everywhere
means the comparison baseline is wholesale stale — the entire surface is being
invalidated, repeatedly.

**Eleven palette rebands in sixteen presents.** Every one re-quantises, which
forces a full redraw and resets the ownership epoch. The draw-time model never
gets two consecutive frames in which to own anything. The 47% of rows still
converting are overwhelmingly rows whose ownership a *palette change* threw
away — not rows nobody wrote natively.

So the real lever was never "convert more writers", which is where all the
effort had been going. It is "stop invalidating everything". Two obvious
threads: make palette-only changes reload the hardware palette without
re-quantising (we have that path — it fired zero times, which is its own bug),
and make the epoch reset *partial*, so a reband that moves a few slots doesn't
invalidate rows whose pixels map to unmoved slots.

## 7. What I'd tell someone starting

1. **Identify the layout precisely** — interleaved vs separate, plane count.
   One transpose, per-machine scatter.
2. **Flat-run detection first.** Best ratio of win to effort by a distance.
3. **Row-diff against a shadow**, and coalesce changed rows into runs before
   copying. Per-call overhead is real at these clock speeds.
4. **Think about the palette before the pixels.** Where does the remap come
   from, and can it exist before the frame is drawn? If it can't, transitions
   will always convert, and palette churn will wreck any caching you build.
5. **Only then** consider draw-time plane stamping, and expect ~half, not all.
   Build the ownership check so unconverted writers degrade instead of
   corrupting — that is what makes incremental adoption possible.
6. **The blitter is a late, small optimisation.** Batch your supervisor calls.
7. **Instrument attribution, not just totals.** "47% still converting" told us
   nothing actionable. "Why each row converted" overturned the plan.

## 8. A note on measurement honesty

Every figure here is from an emulator. One was clearly too generous: the
blitter came out at 5.8 MB/s on the full-screen copy, which is above what the
hardware can plausibly sustain given one word per two bus cycles for a
read-plus-write at 8 MHz. The *ratios* held up across shapes and the end-to-end
number is small enough not to hinge on it, but it is an upper bound and we say
so rather than quoting it as fact.

Two harness traps that cost real time, both worth internalising:

- **A diagnostic that was never compiled in.** A build flag missing from the
  dependency stamp meant `make FLAG=1` after a normal build printed "nothing to
  be done" and produced a binary with no instrumentation. Its silence read
  exactly like "this code did not run". Verify the instrument is *in the
  binary* before trusting what it doesn't say.
- **A dump gated on a window that never elapsed.** The attribution above was
  written months before it ever produced output: it fired every 64 frames, and
  a scripted test run produces under 20. It had reported nothing, ever, and
  nobody noticed because nothing was missing.
