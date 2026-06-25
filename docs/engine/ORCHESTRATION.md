# Full-Auto Mode — Autonomous Orchestration Protocol

> This is the **self-instruction** the user asked for: how I (the lead Claude
> session) drive this project autonomously with subagents. A future session
> re-enters full-auto by reading this file + `VISION.md` + `ROADMAP.md` + the
> project memory, then picking up the next open task. Trigger phrase to resume:
> **"resume goldbox full-auto."**

## Standing authorizations (granted by the user)

- Operate autonomously; **don't ask permission for routine steps** — keep going.
- Spawn subagents freely (**Sonnet for research, Opus for planning/architecture**),
  use WebSearch/WebFetch, WSL, install software, and use Codex as a subagent.
- Deploy the web build to `https://dionysus.dk/goldbox/` (see the deploy memory).
- Token cost is not a constraint; favor thoroughness on research/design/review.

## Roles (which agent does what)

| Role | Model | Tools | Job |
| ---- | ----- | ----- | --- |
| **Researcher** | Sonnet | read + WebSearch/WebFetch + Write | Gather grounded facts (formats, tools, prior art); write a findings doc; cite sources; flag uncertainty. Never edits engine code. |
| **Planner / Architect** | Opus | read + Write | Turn research into architecture decisions, interface contracts, milestone/task breakdowns. Produces ADRs + roadmap updates. |
| **Implementer** | Opus (hard) / Sonnet (mechanical) | full | Build a scoped feature/module with tests. Worktree isolation when several edit in parallel. |
| **Verifier** | Opus | read + run tests | Adversarially check a finding/implementation: run `npm test`, e2e, headless live checks; try to refute correctness. |

Custom agent definitions live in `.claude/agents/` (`goldbox-researcher`,
`goldbox-planner`). For ad-hoc spawns, use `general-purpose` with a `model` override.

## The loop

```
DECOMPOSE → RESEARCH (parallel Sonnet) → INTEGRATE → PLAN (Opus)
   → SLICE into milestones/tasks → IMPLEMENT (subagents) → VERIFY (adversarial)
   → INTEGRATE → update ROADMAP/memory → repeat
```

- **Parallelize independent work**: fan out research/implementation; one distinct
  output file per agent to avoid write races. Use worktree isolation for parallel
  code edits.
- **Verify before claiming done**: format work needs golden-file parity; UI/engine
  work needs `npm test` + `npm run e2e` + a headless render check against the build.
  Never report "works" without a machine check.
- **Integrate visibly**: every wave updates `ROADMAP.md` (what's done / next) and the
  project memory so state survives context compaction.

## When to pause for the user (don't auto-proceed)

1. **Outward / irreversible**: a *public* release of the engine, anything touching
   copyrighted-asset distribution, force-pushes, deleting non-self-created data.
2. **Major scope/legal forks**: ruleset licensing, redistributing third-party engine
   source, changing the distribution model.
3. **Genuine product decisions** the user owns (e.g. which game ships first publicly).

Everything else (research, internal architecture, building & testing modules,
deploying to the existing private `/goldbox/` demo) proceeds **without asking**.

## Quality gates (definition of done per slice)

- Code typechecks (`tsc --noEmit`) and tests pass (`vitest`).
- Browser path verified headless (puppeteer-core + Edge) with zero console errors /
  4xx asset fetches; live deploys re-verified against the real URL.
- Format/decoder work cross-checked against a golden oracle (Python decoders / known
  renders) — byte- or pixel-parity, not eyeballing.
- No copyrighted game assets committed; engine runs on user-supplied data.

## Resume checklist (start of a full-auto session)

1. Read `VISION.md`, this file, `ROADMAP.md`, `research/*`, and project memory.
2. Pick the highest-priority open task from `ROADMAP.md`.
3. Choose role/model, spawn, verify, integrate, update roadmap + memory.
