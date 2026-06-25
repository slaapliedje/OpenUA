# Electron + Web: One Codebase, Two Shells — Architecture Research

> **Status:** Research findings, June 2026.
> **Author:** goldbox-researcher agent.
> **Scope:** Architecture and toolchain for delivering the Gold Box all-in-one engine
> as both a website (Vite) and an offline-installable Electron desktop app from a
> single TypeScript codebase.

---

## 1. Summary / TL;DR

The recommended approach is a **pnpm-workspaces monorepo** with three apps and one
shared package:

- `packages/engine` — the existing `web-engine/` core, zero shell dependencies.
- `apps/web` — Vite SPA shell (web deploy + PWA offline).
- `apps/electron/main` — Electron main process (Node, file I/O, window management).
- `apps/electron/preload` — Typed `contextBridge` bridge (the security seam).

The renderer inside Electron **is the same Vite bundle** as `apps/web`, loaded via
`loadURL` in dev or from packed `dist/` in production. The shell is swapped; the core
is untouched.

Build tooling: **electron-vite** for the Electron app (HMR in all three Electron
processes during dev), **electron-builder** for cross-platform installers and
auto-update (the electron-vite templates already assume this pairing).

---

## 2. Recommended Monorepo Layout

```
goldbox/                        ← repo root
├── pnpm-workspace.yaml
├── package.json                (root: scripts for "dev", "build:all", "release")
├── turbo.json                  (optional, recommended for caching)
│
├── packages/
│   └── engine/                 ← @goldbox/engine  (was web-engine/)
│       ├── src/
│       │   ├── index.ts        (re-exports; no DOM, no Node, no Electron)
│       │   ├── loaders/        (dax, daa, hlib, ilbm — pure Uint8Array functions)
│       │   ├── model/          (engine-neutral types: IndexedImage, FrameSet, …)
│       │   ├── render/         (palette, rgba — returns typed arrays, not canvas)
│       │   └── game/
│       │       └── loadGame.ts (FetchBytes injected — shell-agnostic ← KEY SEAM)
│       ├── tsconfig.json
│       └── package.json        (exports: { ".": "src/index.ts" }, no main-process deps)
│
├── apps/
│   ├── web/                    ← @goldbox/web-shell
│   │   ├── src/
│   │   │   ├── main.ts         (Vite entrypoint; detects window.electronAPI at runtime)
│   │   │   ├── shell/
│   │   │   │   ├── webAssetSource.ts   (FetchBytes via fetch())
│   │   │   │   └── electronAssetSource.ts (FetchBytes via window.electronAPI.readFile)
│   │   │   └── ui/             (all UI components — identical for both shells)
│   │   ├── vite.config.ts
│   │   └── package.json        (depends on @goldbox/engine workspace:*)
│   │
│   └── electron/
│       ├── electron.vite.config.ts    (electron-vite config; points renderer at apps/web/src)
│       ├── main/
│       │   ├── index.ts        (BrowserWindow, IPC handlers, fs.readFile)
│       │   └── ipc.ts          (typed channel definitions)
│       ├── preload/
│       │   └── index.ts        (contextBridge — exposes typed AssetAPI to renderer)
│       └── package.json        (depends on @goldbox/engine, @goldbox/web-shell workspace:*)
│
├── web-engine/                 ← current location; migrate to packages/engine/ in step 1
├── docs/
└── tools/
```

### Key dependency rules

| Package | May import | Must NOT import |
|---|---|---|
| `packages/engine` | nothing outside itself | DOM APIs, Node `fs`, Electron, any shell |
| `apps/web` | `@goldbox/engine`, browser APIs | Node `fs`, Electron APIs directly |
| `apps/electron/main` | `@goldbox/engine`, Node `fs`, `electron` | DOM, browser fetch |
| `apps/electron/preload` | `electron` (`contextBridge`, `ipcRenderer`) | DOM APIs, direct fs |

The engine package is enforced pure by keeping `@types/node` out of its `tsconfig` and
running `tsc --noEmit` as a CI gate.

---

## 3. The Asset-Source Abstraction (the shell seam)

The engine already has the correct seam. In `packages/engine/src/game/loadGame.ts`:

```typescript
/** Injected fetcher: resolve a manifest path to its raw bytes (throws/rejects on 404). */
export type FetchBytes = (path: string) => Promise<Uint8Array>;
```

`loadGame(manifest, fetchBytes)` calls `fetchBytes` and nothing else for I/O. This
must be kept permanently. The two implementations are:

### 3a. Web shell — HTTP fetch

```typescript
// apps/web/src/shell/webAssetSource.ts
import type { FetchBytes } from '@goldbox/engine';

/**
 * Resolve manifest paths against a base URL (the game folder on the server,
 * or a local Vite dev-server /@fs/ route).
 */
export function makeWebFetcher(baseUrl: string): FetchBytes {
  return async (path: string): Promise<Uint8Array> => {
    const resp = await fetch(new URL(path, baseUrl));
    if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${path}`);
    return new Uint8Array(await resp.arrayBuffer());
  };
}
```

### 3b. Electron shell — IPC bridge to Node fs

The renderer cannot call `fs` directly (contextIsolation blocks it). The flow is:

```
renderer                     preload (bridge)              main process
  │                               │                             │
  │  window.electronAPI           │                             │
  │  .readFile(absPath)           │                             │
  │ ─────────────────────────────>│                             │
  │                               │  ipcRenderer.invoke         │
  │                               │  ('fs:readFile', absPath)   │
  │                               │ ──────────────────────────> │
  │                               │                             │ fs.readFile(absPath)
  │                               │                             │ (validates path first)
  │                               │         Uint8Array          │
  │                               │ <─────────────────────────  │
  │      Uint8Array               │                             │
  │ <─────────────────────────────│                             │
```

**Preload (`apps/electron/preload/index.ts`):**

```typescript
import { contextBridge, ipcRenderer } from 'electron';

export interface ElectronAssetAPI {
  /** Read an absolute path off the local disk. Rejects on ENOENT or outside game root. */
  readFile: (absPath: string) => Promise<Uint8Array>;
  /** Let the user pick their game folder via the native folder-picker. */
  pickGameFolder: () => Promise<string | null>;
}

contextBridge.exposeInMainWorld('electronAPI', {
  readFile: (absPath: string) =>
    ipcRenderer.invoke('fs:readFile', absPath),
  pickGameFolder: () =>
    ipcRenderer.invoke('dialog:pickGameFolder'),
} satisfies ElectronAssetAPI);
```

**Main process IPC handler (`apps/electron/main/ipc.ts`):**

```typescript
import { ipcMain, dialog } from 'electron';
import fs from 'node:fs/promises';
import path from 'node:path';

let gameRoot: string | null = null;  // set after user picks folder

ipcMain.handle('dialog:pickGameFolder', async () => {
  const result = await dialog.showOpenDialog({ properties: ['openDirectory'] });
  if (result.canceled) return null;
  gameRoot = result.filePaths[0];
  return gameRoot;
});

ipcMain.handle('fs:readFile', async (_event, absPath: string) => {
  // Security: reject any path that escapes the chosen game root.
  if (!gameRoot) throw new Error('No game folder selected');
  const resolved = path.resolve(absPath);
  if (!resolved.startsWith(gameRoot + path.sep)) {
    throw new Error(`Path traversal rejected: ${absPath}`);
  }
  const buf = await fs.readFile(resolved);
  return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);
});
```

**Electron FetchBytes adapter (renderer-side, in `apps/web/src/shell/electronAssetSource.ts`):**

```typescript
import type { FetchBytes } from '@goldbox/engine';

declare global {
  interface Window {
    electronAPI?: {
      readFile: (absPath: string) => Promise<Uint8Array>;
      pickGameFolder: () => Promise<string | null>;
    };
  }
}

export function makeElectronFetcher(gameRoot: string): FetchBytes {
  return async (relPath: string): Promise<Uint8Array> => {
    if (!window.electronAPI) throw new Error('Not running in Electron');
    const absPath = `${gameRoot}/${relPath}`;
    return window.electronAPI.readFile(absPath);
  };
}

/** True when running inside the Electron shell. */
export const isElectron = (): boolean => Boolean(window.electronAPI);
```

**Runtime dispatch in the web shell's entrypoint:**

```typescript
// apps/web/src/main.ts
import { isElectron, makeElectronFetcher } from './shell/electronAssetSource.js';
import { makeWebFetcher } from './shell/webAssetSource.js';
import { loadGame } from '@goldbox/engine';

const gameRoot = await resolveGameRoot();   // from URL param or electronAPI.pickGameFolder
const fetchBytes = isElectron()
  ? makeElectronFetcher(gameRoot)
  : makeWebFetcher(gameRoot);

const game = await loadGame(manifest, fetchBytes);
```

The engine (`loadGame`, all decoders) never knows which shell it is in.

---

## 4. Security Configuration (BrowserWindow)

```typescript
// apps/electron/main/index.ts
new BrowserWindow({
  webPreferences: {
    preload: path.join(__dirname, '../preload/index.js'),
    contextIsolation: true,   // default since Electron 12; keep explicit
    nodeIntegration: false,   // MUST be false — renderer must not import Node
    sandbox: true,            // extra layer; compatible with contextBridge
    webSecurity: true,
  },
});
```

Never expose `ipcRenderer` directly; never expose raw `require`; only expose one named
function per IPC channel (prevents renderer from sending arbitrary messages).

Source: [Electron Security docs](https://www.electronjs.org/docs/latest/tutorial/security/),
[contextBridge API](https://www.electronjs.org/docs/latest/api/context-bridge),
[RealCoding contextIsolation pattern](https://realcoding.blog/en/2025/12/05/electron-context-isolation-preload/).

---

## 5. Build Tooling: Recommended Stack

### Dev workflow: electron-vite

`electron-vite` v5 (MIT, Alex Wei) wraps Vite and provides:
- Separate Vite configs for main / preload / renderer, each with the right target
  (`node`, `electron-main`, `electron-preload`, browser).
- HMR for the renderer + hot-reload (not full HMR) for main and preload.
- Source code protection (bytecode compilation for main) — useful for shipping.
- Framework-agnostic (no UI framework commitment yet for this project).

Run: `npx electron-vite dev` — starts the Vite dev server and launches Electron.

Source: [electron-vite.org](https://electron-vite.org/),
[Best Electron Boilerplates 2026](https://starterpick.com/guides/best-electron-boilerplates-2026).

### Production packaging: electron-builder

electron-builder (MIT) handles:
- Cross-platform installers: NSIS (.exe) for Windows, DMG + pkg for macOS, AppImage /
  deb / rpm for Linux.
- `electron-updater` for auto-update (differential, staged rollouts, GitHub Releases /
  S3 / generic providers).
- Native module rebuilding (`electron-rebuild`).
- Code signing integration (see section 7).

The electron-vite templates ship with electron-builder as the packaging layer. Electron
Forge is the "official" Electron recommendation, but its auto-update story is weak
without electron-builder; in practice, the ecosystem consensus in 2025/2026 is
**electron-vite (dev) + electron-builder (dist)**.

Source: [electron-builder.io](https://www.electron.build/),
[electron-vite FAQ on Forge](https://electron-vite.github.io/faq/electron-forge.html).

### Monorepo orchestration: pnpm workspaces + Turborepo (optional)

`pnpm-workspace.yaml`:
```yaml
packages:
  - 'packages/*'
  - 'apps/*'
  - 'apps/electron/*'
```

Reference the engine from apps: `"@goldbox/engine": "workspace:*"`.

Turborepo adds build caching (`turbo.json`) so `packages/engine` is not rebuilt when
only UI code changes. Optional but cheap to add up front.

Prior art: [buqiyuan/electron-vite-monorepo](https://github.com/buqiyuan/electron-vite-monorepo)
(pnpm + Turborepo + electron-vite).

---

## 6. Web Shell Offline (PWA vs Electron)

### PWA / service worker

The web shell (`apps/web`) can be made offline-installable via a service worker
(`vite-plugin-pwa`, Workbox). The service worker pre-caches the engine bundle and UI;
game assets (the user's game files served over HTTP) are cached on first load.

Trade-offs vs Electron:

| Dimension | PWA | Electron |
|---|---|---|
| Install friction | Zero (browser install prompt) | ~150 MB download + OS installer |
| File system access | None directly (user must upload/serve files) | Full `fs` access via IPC |
| Offline asset access | Only if assets were previously cached from HTTP | Always (reads local disk) |
| App size on disk | < 5 MB (engine bundle only) | ~150–200 MB (Chromium bundled) |
| Update delivery | Automatic (server pushes new service worker) | electron-updater + user consent |
| Browser support (2026) | All modern browsers incl. Firefox 143+ | Windows/Mac/Linux native |
| Local game files | Requires File System Access API (Chrome/Edge only) | Transparent via IPC |

**Recommendation:** Ship the PWA for the website demo / "try before you install" path
where assets are served by the website (user's own data behind a server-side upload, or
the dev's curated demo set). Ship Electron for the primary offline use case where users
point at their installed Gold Box folder.

The [File System Access API](https://developer.mozilla.org/en-US/docs/Web/API/File_System_Access_API)
(Chrome/Edge, not Firefox) does allow a web app to read local files with a picker, which
could serve as a third option for Chromium-browser users who don't want the Electron
install. The `makeWebFetcher` abstraction can be extended with an OPFS / FileSystemHandle
implementation without touching the engine.

Source: [cleancommit.io PWA vs Electron](https://cleancommit.io/blog/pwa-vs-electron-which-architecture-wins/),
[Web Almanac PWA 2025](https://almanac.httparchive.org/en/2025/pwa).

---

## 7. Distribution & Code Signing

### Sizes

A packaged Electron app ships Chromium (~60 MB compressed) plus Node runtime (~15 MB),
so expect a **~100–150 MB installer** and **~200 MB installed**. For a game engine this
is normal (ScummVM desktop is comparable). The engine code + assets drive the remainder.

### Code signing realities (2026)

**macOS:**
- Apple Developer Program: $99/year.
- Must code-sign AND notarize for distribution outside the Mac App Store.
- Without notarization, Gatekeeper blocks the app (users see "cannot be opened").
- Notarization is automated via `electron-notarize` + GitHub Actions.

**Windows:**
- Since June 2023, Microsoft requires an **EV (Extended Validation) certificate** for
  immediate SmartScreen trust. Standard OV certificates now behave as "unsigned" for
  SmartScreen purposes.
- EV certificates are hardware-bound (HSM/dongle); cannot be exported to a file.
- Cloud-signing services (DigiCert KeyLocker, SSL.com eSigner) allow CI integration
  without a physical dongle. Cost: ~$350–500/year for the certificate + signing service.
- Without EV signing, Windows will show a SmartScreen warning ("Unknown publisher — do
  you want to run this?"). For an open-source project targeting modders, this is
  **acceptable at launch** — the user dismisses it once.

**Linux:**
- No signing requirement. AppImage / deb / rpm distribute unsigned.

**Practical recommendation for this project:** Skip EV signing for the initial release
(target audience is Gold Box modders who expect to click through warnings). Add macOS
notarization first (the $99/year Apple cert) because Gatekeeper hard-blocks, not warns.

Source: [electron-builder code signing](https://www.electron.build/docs/features/code-signing/),
[Microsoft EV cert Q&A](https://learn.microsoft.com/en-us/answers/questions/2157250/how-to-get-code-signing-certificate-for-electron-a),
[BigBinary macOS notarization](https://www.bigbinary.com/blog/code-sign-notorize-mac-desktop-app).

---

## 8. Prior Art: Retro Emulators Shipping Both Browser and Desktop

**EmulatorJS** (linuxserver fork): Ships as a self-hosted web app; no dedicated Electron
wrapper, but the architecture is a browser-first JS core with optional server-side asset
management. The renderer is entirely in-browser.
Source: [github.com/linuxserver/emulatorjs](https://github.com/linuxserver/emulatorjs).

**ScummVM-web** (Emscripten port): Required significant refactoring — C++ blocking
`sleep()` calls had to be converted to async callbacks. The web version is a separate
fork, not the same codebase. This is a cautionary tale: starting web-first avoids this
divergence. Gold Box is starting TS-first, so this pitfall is avoided.
Source: [emscripten-scummvm discussion](https://groups.google.com/g/emscripten-discuss/c/ubv-vHIgYsc/m/6dx7egC2AAAJ).

**drewwiens-spikes/hybrid-web-and-desktop-angular-electron-app**: The closest prior art
for the pattern we want. Separates code into `client/` (Angular web), `desktop/`
(Electron), `common/` (shared types). The preload script attaches a bridge to the window
object; the web app detects it at runtime and uses it when present. Matches the
`isElectron()` + `FetchBytes` dispatch pattern above.
Source: [github.com/drewwiens-spikes/hybrid-web-and-desktop-angular-electron-app](https://github.com/drewwiens-spikes/hybrid-web-and-desktop-angular-electron-app).

**Quasar Framework**: A Vue 3 meta-framework that targets web, Electron, Capacitor, and
browser extensions from one codebase. Not a fit for this project (opinionated UI
framework), but confirms the viability of the pattern at scale.
Source: [starterpick.com 2026 boilerplates](https://starterpick.com/guides/best-electron-boilerplates-2026).

---

## 9. Migration Path from Current `web-engine/`

The current `web-engine/` is already structured correctly:
- `FetchBytes` is injected (shell-agnostic seam exists).
- Loaders are pure Uint8Array functions (no DOM, no Node).
- Vite + TypeScript already in place.

Concrete migration steps (each is a standalone PR):

1. **Rename/move** `web-engine/` → `packages/engine/`; add `pnpm-workspace.yaml`.
2. **Extract web shell** from `web-engine/src/ui/` into `apps/web/`; wire Vite config.
3. **Add `apps/electron/`** using `npx create-electron-vite@latest`; point renderer at
   `apps/web` build output.
4. **Implement preload bridge** and `electronAssetSource.ts` as sketched in section 3.
5. **Add game-folder picker UI** (first-run flow in the web shell, Electron-only).
6. **Add `vite-plugin-pwa`** to `apps/web` for service-worker offline on the web.
7. **Configure electron-builder** in `apps/electron/package.json` for NSIS + DMG targets.
8. **GitHub Actions CI**: `build:all` matrix (Windows, macOS, Linux), publish to Releases.

---

## 10. Open Questions / Unknowns

| # | Question | Impact | How to resolve |
|---|---|---|---|
| 1 | **How does the Electron renderer load the Vite bundle?** In dev, `loadURL('http://localhost:5173')`; in prod, `loadFile(path.join(__dirname, '../../web/dist/index.html'))`. The prod path depends on how electron-builder packages `apps/web/dist/`. Needs config validation on first build. | Medium | Spike: build the monorepo once and inspect the packaged asar. |
| 2 | **SharedArrayBuffer / COOP/COEP in Electron.** WebXR/WASM features that need `crossOriginIsolated` require `Cross-Origin-Opener-Policy` and `Cross-Origin-Embedder-Policy` headers, which don't apply to `file://` protocol. Electron can set them via `session.webRequest` or a custom protocol handler. | Low now, may rise if WASM used for decoders | Test with a custom protocol (`goldbox://`) in main process. |
| 3 | **Transferring large Uint8Array over IPC.** Electron IPC serializes via structured clone, which copies the buffer. For very large game files this may be slow. Solution: use `SharedArrayBuffer` or `MessagePort` for zero-copy transfer — but requires careful setup. | Low for current asset sizes (~1–5 MB), UNKNOWN for future map/audio streaming. | Benchmark with a 10 MB asset; add `transfer` option if needed. |
| 4 | **File System Access API as a third shell** (Chrome/Edge only, no install). A `fileSystemAccessSource.ts` using `FileSystemFileHandle` would give Chromium browser users local file access without Electron. Firefox does not support it (as of 2026). | Medium — could replace Electron for desktop-class Chromium users | Prototype `fileSystemAccessSource.ts`; flag-guard behind feature detect. |
| 5 | **Windows EV signing cost / timeline.** Without it, SmartScreen warns on every download. For a modding-community tool this is tolerable initially, but reduces adoption for non-technical users. | Medium-term distribution risk | Defer to post-alpha; budget $350–500/year for cloud-signing service. |
| 6 | **Auto-update for a user-supplied-asset app.** electron-updater updates the engine code; it cannot touch the user's game files (those are their own). The update story is clean — only the `asar` (app code) is swapped. Confirm the update channel (GitHub Releases is free and sufficient). | Low risk | Use `GH_TOKEN` + electron-builder `publish: github`. |
| 7 | **Tauri as alternative to Electron.** Tauri 2.0 (Rust core, system WebView) ships ~10 MB installers vs ~150 MB for Electron. The trade-off: no bundled Chromium means rendering inconsistencies across OS WebView versions (Edge on Windows, WebKit on macOS). For a pixel-art canvas renderer this is lower risk than for a rich-DOM app, but UNKNOWN without testing. | Medium — could halve install size | Evaluate post-alpha if install size becomes a user complaint. |

---

## 11. Implications for This Engine

- **Do not break the `FetchBytes` seam.** Every loader and `loadGame` must remain
  injection-based. Any future ECL VM, GEO map loader, or audio player should follow the
  same pattern: accept a `FetchBytes` (or a typed variant) rather than calling `fetch`
  or `fs.readFile` directly.
- **The web shell is the primary development target** (faster iteration, no rebuild cycle
  for the Electron wrapper). The Electron shell is thin by design.
- **No game data in the repo or the built app.** The first-run UX is: user picks their
  game folder; the engine reads the manifest; `FetchBytes` resolves from that root.
  This mirrors ScummVM's "add game" flow and keeps the project legally clean.
- **Monorepo pays off immediately** because `packages/engine` is already independently
  testable with Vitest (pure functions, no shell). The Electron shell adds no testing
  burden to the engine.
- **electron-vite's bytecode compilation** (source protection for main process code) is
  worth enabling before any public release — it makes the ECL VM logic harder to extract
  and re-use against the license.
