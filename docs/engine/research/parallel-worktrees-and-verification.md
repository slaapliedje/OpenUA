# Parallel Worktrees and Verification Discipline for Autonomous Multi-Agent Builds

*Synthesized from authoritative sources: git-scm.com, code.claude.com, jestjs.io, playwright.dev, martinfowler.com. Date: 2026-06-21.*

---

## Background and Scope

This document establishes the orchestration policy for the **Greybox autonomous build** — multiple AI coding agents working in parallel to implement, verify, and merge work on the Gold Box reverse-engineering and rebuild project. It covers two interlocking concerns:

1. **Parallel work isolation** — using git worktrees to let agents work concurrently without stomping on each other's files.
2. **Verification discipline** — the exact gates an agent must pass before any task can be marked "done," enforced by hooks so the agent cannot self-certify without machine proof.

---

## Part 1 — Parallel Work Policy

### How git worktrees provide isolation

A git worktree is a separate checked-out working directory linked to the same repository history. Each worktree has its own `HEAD`, index, and per-worktree refs, but shares all commits, objects, and remotes with the main checkout. This means two agents can each write to their own tree simultaneously; neither can interfere with the other's in-progress files.

Source: [git-scm.com/docs/git-worktree](https://git-scm.com/docs/git-worktree)

Claude Code's `--worktree` flag automates this: `claude --worktree <name>` creates `.claude/worktrees/<name>/` on a fresh branch named `worktree-<name>`, branched from `origin/HEAD` by default. Each parallel session gets its own checkout with no shared working files.

Source: [code.claude.com/docs/en/worktrees](https://code.claude.com/docs/en/worktrees)

### Parallel work policy rules

**Rule 1 — One task, one worktree, one branch.**
Every agent task gets its own worktree and feature branch. Two agents must never share a worktree. This is the single most important rule; violating it guarantees merge conflicts.

**Rule 2 — Partition by ownership, not by time.**
The orchestrator assigns each agent a disjoint set of files or modules before spawning. If two agents must touch the same file, that file becomes a dependency: one agent finishes and merges first; the second branches from the merged result. Do not parallelize tasks with overlapping file ownership.

Source: [code.claude.com/docs/en/agent-teams — "Avoid file conflicts"](https://code.claude.com/docs/en/agent-teams)

**Rule 3 — Branch from `origin/HEAD` (the remote default), not from local HEAD.**
The default `worktree.baseRef = "fresh"` in Claude Code ensures each new worktree starts from a clean, agreed-upon baseline. Override to `"head"` only when subagents explicitly need in-progress commits from the parent session.

Source: [code.claude.com/docs/en/worktrees — "Choose the base branch"](https://code.claude.com/docs/en/worktrees)

**Rule 4 — Copy environment files via `.worktreeinclude`, never commit them.**
Files like `.env` are gitignored and not present in fresh worktrees. List them in a `.worktreeinclude` file at the project root so Claude Code copies them automatically. Never commit secrets.

Source: [code.claude.com/docs/en/worktrees — "Copy gitignored files"](https://code.claude.com/docs/en/worktrees)

**Rule 5 — Keep tasks small enough for same-day completion.**
Per Martin Fowler's CI principle, no branch should sit unintegrated for more than a day. Long-lived branches accumulate drift. Break large tasks into units that produce a clear, testable deliverable (one module, one test file, one decoder). Target 5–6 tasks per agent, each independently verifiable.

Source: [martinfowler.com/articles/continuousIntegration](https://martinfowler.com/articles/continuousIntegration.html)

**Rule 6 — Merge-back strategy: sequential, smallest-diff first.**
After an agent passes all verification gates (see Part 2), the orchestrator merges its branch into main via a pull request or direct fast-forward. Merge order matters: merge the smallest, least-conflicting changes first to keep the integration surface small. Agents waiting to merge must rebase onto the freshly integrated main before their own merge.

**Rule 7 — Lock worktrees during active runs; prune on completion.**
Claude Code automatically runs `git worktree lock` while an agent is active, preventing concurrent cleanup from removing a live worktree. After an agent's session ends cleanly (no uncommitted changes, no unpushed commits), the worktree is removed automatically. For non-interactive (`-p`) runs, clean up manually with `git worktree remove`.

Source: [code.claude.com/docs/en/worktrees — "Clean up worktrees"](https://code.claude.com/docs/en/worktrees)

### Practical pitfalls and mitigations

| Pitfall | Mitigation |
|---------|------------|
| **Disk cost** — each worktree is a full checkout | Use sparse checkouts for large repos; delete worktrees immediately after merge; budget ~2× repo size per active agent |
| **"Same branch already checked out" error** | Each worktree must use a unique branch name; enforce naming convention `wt/<task-id>/<slug>` |
| **Submodules** | git worktree does not support submodule moves; avoid submodules in agent-managed directories or handle them in post-merge hooks |
| **Stale worktree metadata** | Run `git worktree prune` in the orchestrator after each build cycle |
| **Config sharing** | All worktrees share the repo-level `git config` by default; use `extensions.worktreeConfig = true` for per-worktree config if needed |
| **Agent overlap on shared infra files** | Designate a single "infra agent" that owns `CLAUDE.md`, build scripts, and package files; all other agents treat these as read-only |

---

## Part 2 — Verification Gates (Hard Definition of Done)

An agent **cannot** mark a task done, merge its branch, or report success without every gate below passing. These gates are enforced by `TaskCompleted` hooks that exit with code 2 (blocking completion) if any gate fails. The agent receives feedback and must fix and retry.

Source for hook mechanism: [code.claude.com/docs/en/agent-teams — "Enforce quality gates with hooks"](https://code.claude.com/docs/en/agent-teams)

### Mandatory verification gates checklist

**Gate 1 — Type check (zero errors)**
Run the project's static type checker (e.g., `pyright`, `mypy`, `tsc --noEmit`) with zero allowed errors. No suppressions added to pass. Warnings are permitted but must not increase versus the base branch.

**Gate 2 — Lint (zero new violations)**
Run the linter (`ruff`, `flake8`, `eslint`, etc.) in CI-equivalent mode (no autofix). Zero new violations versus the base branch. The agent may apply autofixes but must re-lint after to confirm clean.

**Gate 3 — Unit / integration tests (all pass, no skips added)**
Run the full test suite. All pre-existing tests must pass. The agent must not skip, xfail, or delete tests to make the suite pass. New code must have new tests covering the introduced logic.

**Gate 4 — Golden / snapshot tests (binary-identical output)**
For any tool that produces binary or structured output (decoders, repackers, palette converters), a golden-file test must exist: a known-good input → known-good output pair committed to the repo. The gate runs the tool against the input and byte-compares against the golden output. Failure means a regression in the encoded/decoded artifact, even if Python-level tests pass.

Best practice: mock all non-deterministic values (timestamps, random IDs) before snapshotting; commit `.snap` / golden files in version control; review golden-file diffs as carefully as source diffs.

Source: [jestjs.io/docs/snapshot-testing](https://jestjs.io/docs/snapshot-testing)

**Gate 5 — Deterministic seed enforcement**
Any test that uses randomness must seed the PRNG with a fixed, committed seed. The CI run must produce byte-identical results across at least two sequential runs (`pytest --count=2` or equivalent). A flaky test is a failing test.

**Gate 6 — No regression in render outputs (image diff)**
For graphical decoders (DAX, DAA, HLIB), the pipeline runs the decoder on a reference asset and compares the PNG output against the committed reference render using a pixel-hash comparison. Any pixel change is a regression and blocks merge. Updates to reference renders require explicit human review and a commit message explaining the intentional change.

*Note: pixel-diff tooling is project-specific; this gate is marked uncertain until a diff tool is selected (candidates: `pixelmatch`, `Pillow`-based script in `tools/`).*

**Gate 7 — Headless E2E smoke test (if applicable)**
For any browser-facing component (future web engine work), run Playwright in headless mode with `workers: 1` for CI stability. Configure retries (max 2) and trace-on-failure. Fail the gate if any test fails after retries. Upload the HTML report as a build artifact.

To reduce flakiness: use Docker for a fixed browser environment; never cache browser binaries across OS updates; set explicit timeout values rather than relying on defaults.

Source: [playwright.dev/docs/ci](https://playwright.dev/docs/ci)

**Gate 8 — Build completes without warnings promoted to errors**
The project's build command (e.g., `python -m build`, `make`, `npm run build`) must exit 0 with no new warnings compared to the base branch. Warnings on new code are treated as errors.

**Gate 9 — No uncommitted changes remain**
After all tool runs, `git status` must report a clean tree. Any auto-generated artifact that is not gitignored must be committed as part of the task, not left as a dirty side-effect.

Source: CI principle — "the build verifies that no significant bugs are in the product." [martinfowler.com/articles/continuousIntegration](https://martinfowler.com/articles/continuousIntegration.html)

### Hook wiring summary

```json
{
  "hooks": {
    "TaskCompleted": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash tools/ci_gate.sh"
          }
        ]
      }
    ]
  }
}
```

`tools/ci_gate.sh` runs gates 1–9 in sequence, exits 2 with a human-readable failure message on any failure, exits 0 only when all gates pass. The `TaskCompleted` hook receives exit code 2 and returns feedback to the agent, which must fix and re-invoke.

---

## Part 3 — Orchestration Summary

### Recommended parallel-work topology for Greybox

```
Orchestrator (lead session, main worktree)
  ├── Agent A  → wt/a/<slug>  [owns: tools/dax_*.py, renders/dax/]
  ├── Agent B  → wt/b/<slug>  [owns: tools/daa_*.py, renders/daa/]
  ├── Agent C  → wt/c/<slug>  [owns: tools/hlib_*.py, renders/hlib/]
  └── Agent D  → wt/d/<slug>  [owns: docs/*, tests/]
```

Each agent runs independently. The orchestrator merges in order of completion, with each subsequent agent rebasing onto the freshest main before its own merge. The orchestrator uses `TeammateIdle` hooks to detect completion and trigger the merge pipeline, which re-runs all gates on the merge commit before pushing.

### Team size guidance

Start with 3–5 agents. Token costs scale linearly per agent context window. Beyond 5 agents, coordination overhead and merge serialization negate the parallelism benefit. For this project's current scale (3 format decoders + docs), 3–4 agents is the practical ceiling.

Source: [code.claude.com/docs/en/agent-teams — "Choose an appropriate team size"](https://code.claude.com/docs/en/agent-teams)

---

## Sources

- [git-scm.com/docs/git-worktree](https://git-scm.com/docs/git-worktree) — official git worktree reference: commands, limitations, disk layout, lock/prune behavior
- [code.claude.com/docs/en/worktrees](https://code.claude.com/docs/en/worktrees) — Claude Code `--worktree` flag, `.worktreeinclude`, subagent isolation, cleanup lifecycle
- [code.claude.com/docs/en/agents](https://code.claude.com/docs/en/agents) — comparison of subagents vs. agent teams vs. dynamic workflows
- [code.claude.com/docs/en/agent-teams](https://code.claude.com/docs/en/agent-teams) — `TaskCompleted`/`TeammateIdle` hooks, file-conflict avoidance, team size guidance, quality gate hooks
- [jestjs.io/docs/snapshot-testing](https://jestjs.io/docs/snapshot-testing) — golden/snapshot test best practices, deterministic seeds, property matchers, CI behavior (no auto-generate)
- [playwright.dev/docs/ci](https://playwright.dev/docs/ci) — headless E2E in CI, `workers: 1` for stability, retries, trace-on-failure, browser environment determinism
- [martinfowler.com/articles/continuousIntegration](https://martinfowler.com/articles/continuousIntegration.html) — CI principles: short-lived branches, fix broken build immediately, definition of done in CI context
