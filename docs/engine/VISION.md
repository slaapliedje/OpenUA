# Greybox — All-in-One Gold Box Engine & Authoring Suite (Vision)

> **Product name: Greybox** (chosen 2026-06-21). A trademark-safe nod to "Gold Box"
> (and to *graybox*, the game-dev term for a prototype level). "Gold Box / D&D /
> Dragonlance / Forgotten Realms / SSI / Krynn" are avoided in the product name/branding;
> describing it as "plays Gold Box games" is fine. Open-source freeware. The live demo
> is at **https://dionysus.dk/greybox/** (renamed from `/goldbox/`, which 301-redirects).

> **Status: active primary direction (2026-06-21).** This supersedes the earlier
> "two DOS executables" framing as the *primary* deliverable. The DOS-rebuild idea
> is retained as a sibling track that shares the format library (see
> `../../CLAUDE.md` and `findings.md`). This doc is the north star; the concrete
> build order lives in `ROADMAP.md`, and the autonomous build process in
> `ORCHESTRATION.md`.

## One-line

A single, extensible **Gold Box engine + authoring suite** that can load and play
*any* SSI Gold Box game from the player's own original assets, let you **switch
graphics modes live** (e.g. DOS EGA ↔ Amiga ↔ VGA) while playing, and **author new
games** in a FRUA-superset editor — delivered both as a **website** and an
**offline-installable Electron app** from one codebase.

## Pillars

1. **Universal player.** Launch any Gold Box title (**Champions of Krynn first**, then
   the rest of the Krynn trilogy; architected for the Pool of Radiance line, Savage
   Frontier, Buck Rogers, etc.). The engine is ruleset- and asset-agnostic.
   - **Game Library / auto-detection (how games load):** the user clicks **"Add asset
     folder reference,"** points at one of their own game installs, and the engine
     **scans the folder, fingerprints it** against known game profiles (filename sets +
     magic-byte signatures), identifies the game + platform + which graphic sets are
     present, and registers it as a playable entry. No hand-authored paths; per-game
     **detection profiles** replace static manifests. Web uses the File System Access
     API; Electron uses a native folder dialog + `fs`.
2. **Live graphics-mode switching.** Each logical asset (a portrait, a tile, a sprite)
   can resolve to multiple *graphic sets* (DOS EGA/CGA/VGA, Amiga, C64, Apple II, Mac,
   …). The player picks the look at runtime; the engine swaps the active set without
   restarting. This is the user's "best-of art" idea generalized into a feature.
3. **Authoring (FRUA, broadened).** Everything Unlimited Adventures / FRUA allowed as
   the **baseline**, then expanded: multiple rulesets (AD&D 1e/2e Gold Box variants,
   Dragonlance, others), and the ability to **add new rulesets**. Richer creation of
   spells, monsters, NPCs, items, classes, maps, events. Modern, but original-faithful
   by default. (Track the open-source FRUA successor **Dungeon Craft / UAF** as prior
   art — see research.)
4. **Gold Box Companion, built in.** Fold the GBC tool's features (party/character
   tooling, auto-map, journal, monster/item/spell databases, helpers) into the engine
   as first-class panels — researching the exact feature set now.
5. **Two shells, one core.** A DOM/canvas/WebGL UI over an engine-neutral core. The
   **web** shell fetches assets over HTTP; the **Electron** shell reads the user's
   installed game files off disk and runs fully offline. Shared TypeScript everywhere.

## Distribution & legal stance

Ship the **engine + tools only**. Original game data is **user-supplied** (they own
the games); nothing copyrighted is committed to the repo or hosted on the website.
This mirrors FRUA, ScummVM, and Dungeon Craft. The deployed `dionysus.dk/goldbox/`
demo currently bundles the user's own assets for *their* verification — a public
release must gate assets behind a "point me at your install" flow.

## Relationship to existing work

- The shipped **format library + asset viewer** (`web-engine/`, live at
  `dionysus.dk/goldbox/`) is the seed of pillar 1's loader layer and pillar 5's web
  shell. It already decodes DAX/DAA/HLIB/IFF to an engine-neutral model.
- The big undecoded layers — **ECL bytecode VM, GEO maps, MON/ITEM tables, combat** —
  are the gameplay work this vision now commits to (previously deferred). COAB
  reimplementation + Dungeon Craft are the references.

## What "done enough to share" looks like (first public milestone)

Play **Champions of Krynn** start-to-finish in browser **and** Electron — loaded via the
"add asset folder reference" flow from the user's own install — with at least two
switchable graphic sets (**DOS EGA ↔ Amiga**), plus a read-only Companion panel (auto-map
+ databases). Authoring follows. (Distributed as **open-source freeware, unsigned builds**.)
