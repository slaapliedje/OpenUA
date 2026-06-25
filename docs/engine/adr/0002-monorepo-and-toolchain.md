# ADR-0002 — Monorepo & toolchain (pnpm + electron-vite + electron-builder)

**Status:** Accepted
**Date:** 2026-06-21

## Context

We must deliver one TypeScript codebase as both a website and an offline Electron app
(VISION.md pillar 5). The shipped `web-engine/` is a single Vite+Vitest package with a pure,
injection-based format library and a viewer UI. We need a structure that (a) keeps the engine
shell-agnostic, (b) lets web and Electron share all UI, (c) supports independent testing, and
(d) doesn't break the live `dionysus.dk/goldbox/` deploy during migration.

## Options

1. **Single package, conditional shell code** — simplest now, but mixes Node/Electron types into
   the engine, breaks purity, and makes the "engine never imports a shell" rule unenforceable.
2. **pnpm-workspaces monorepo** (electron-web research recommendation): `packages/engine` (pure
   core), `packages/ui-*`, `packages/ruleset-*`, `apps/web`, `apps/electron/{main,preload}`.
   Dev via **electron-vite** (HMR across main/preload/renderer); package via **electron-builder**
   (NSIS/DMG/AppImage + electron-updater); optional **Turborepo** for build caching.
3. **Nx / Turbopack / Tauri** — Nx is heavier than needed; Tauri (Rust + system WebView) halves
   install size but risks WebView rendering inconsistency for our pixel pipeline — defer as a
   post-alpha evaluation, not the baseline.

## Decision

**pnpm-workspaces monorepo with electron-vite (dev) + electron-builder (dist)**, optional
Turborepo caching. Layout and dependency rules per ARCHITECTURE.md §4. The engine package
excludes `@types/node`/DOM libs and is gated by `tsc --noEmit` in CI to enforce purity.

## Consequences

- Clean enforcement of "engine imports no shell"; engine is independently Vitest-testable.
- Web is the primary dev target (fast iteration); Electron shell stays thin (renderer = the web
  bundle, swapped behind the `AssetSource` seam).
- Migration is mechanical and staged (ARCHITECTURE.md §4 steps 1–5), each step a verifiable PR
  that keeps the deploy green.
- electron-builder gives cross-platform installers + auto-update; bytecode compilation of the
  main process is available pre-release (minor source protection).
- Tauri remains an open evaluation if install size (~150 MB Electron) becomes a user complaint.
- **Code-signing: NONE (user decision, 2026-06-21).** This is open-source freeware with no
  revenue goal, so builds ship **unsigned**. Consequence: first-run Windows SmartScreen and
  macOS Gatekeeper warnings — we document the "More info → Run anyway" / right-click-Open steps
  in the install guide rather than paying for an EV cert ($350–500/yr) or Apple notarization
  ($99/yr). Revisit only if a signed channel is ever wanted.
