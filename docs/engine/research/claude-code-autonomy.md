# Claude Code Autonomy Features — Greybox Reference

> Research compiled from official Anthropic docs at https://code.claude.com/docs/
> (docs.anthropic.com/en/docs/claude-code/* now redirects there as of 2026-06-21)
> Web search was unavailable; all findings are from direct doc fetches.

---

## Features Table

| Feature | What It Does | How We Use It for Greybox |
|---|---|---|
| **`CLAUDE.md`** | Markdown loaded into every session's context. Hierarchical: managed policy → user (`~/.claude/CLAUDE.md`) → project (`CLAUDE.md` / `.claude/CLAUDE.md`) → local (`CLAUDE.local.md`). Subdirectory CLAUDE.md files load on demand when Claude opens files in that dir. Max ~200 lines per file for reliable adherence. | Already have one at repo root. Keep it factual: format specs, tool paths, roadmap state. Move procedures into Skills. Use `CLAUDE.local.md` (gitignored) for machine-local paths (Amiga ADF location, WSL mounts). |
| **`.claude/rules/*.md`** | Path-scoped rules loaded only when Claude touches matching files. Frontmatter: `paths: ["tools/**/*.py"]`. Avoids burning context on rules irrelevant to the current file. | Scope Python-tool rules to `tools/**/*.py`; scope DAX/HLIB decoder rules to `tools/dax_decode.py`, `tools/hlib_decode.py` etc. |
| **Auto Memory** | Claude writes its own notes (`~/.claude/projects/<repo>/memory/MEMORY.md` + topic files). First 200 lines / 25 KB loaded each session. Topic files read on-demand. Shared across all worktrees of the same repo. Toggle: `autoMemoryEnabled` in settings or `/memory`. Requires v2.1.59+. | Enable for the main Greybox session. Claude accumulates build-command discoveries, decoder quirks, palette insights without manual effort. |
| **Skills (custom slash commands)** | `SKILL.md` files in `.claude/skills/<name>/` or `~/.claude/skills/<name>/`. Loaded on demand, not at startup — zero context cost until invoked. Support dynamic context injection (`` !`cmd` `` runs and inlines shell output), `$ARGUMENTS`, path-scoped activation, `context: fork` (runs in subagent), `disable-model-invocation: true` (manual-only). | Create `/resume-build`, `/verify-render`, `/decode-check` skills. See Recommended Setup below. |
| **Subagents** | `.claude/agents/<name>.md` with YAML frontmatter. Own context window, custom system prompt, restricted tools, per-subagent model (`haiku`/`sonnet`/`opus`/`fable`/full ID), `isolation: worktree` for git-isolated runs, `memory: project` for persistent cross-session learning, `background: true` to always run async. Built-in: `Explore` (Haiku, read-only), `Plan` (read-only), `general-purpose`. | Define a lightweight `format-researcher` subagent (Haiku, read-only, focused on hackdocs and format specs) to keep costly format-archaeology out of the main context. Define a `render-verifier` subagent to run Python render scripts and report diffs. |
| **Headless / Print mode** | `claude -p "prompt"` — runs query non-interactively, exits. `--output-format json` or `stream-json`. `--max-turns N`, `--max-budget-usd X`. `--dangerously-skip-permissions` for CI. `--bare` skips all skill/hook/MCP discovery for fast scripted calls. `--continue` / `--resume <name>` resumes last session. | Use `claude -p --continue "pick up where we left off on HLIB reader"` to resume a named build session from a script or after a machine wake. |
| **Background agents** | `claude --bg "task"` starts a session and returns immediately, printing a session ID. `claude attach <id>` to rejoin. `claude logs <id>` to stream output. `claude agents --json` lists running sessions. A supervisor daemon keeps them alive. | Kick off a long decode run (`claude --bg "decode all HLIB TLB files and render tiles to renders/"`) and work on other tasks. Check status with `claude logs <id>`. |
| **Hooks** | Shell commands / HTTP endpoints / MCP tools / prompt / agent — fired at lifecycle events. Configured in `.claude/settings.json` → `hooks` key. Key events: `SessionStart`, `PreToolUse`, `PostToolUse`, `Stop`, `SubagentStart/Stop`, `FileChanged`. Can block (exit code 2), inject context (`additionalContext`), or update tool output. `async: true` for fire-and-forget. | See Recommended Setup: PostToolUse auto-lint on `.py` writes; Stop hook to write a checkpoint summary; SessionStart hook to inject current render inventory. |
| **Plan mode** | `--permission-mode plan` — Claude reads and plans but cannot write files or run commands. The built-in `Plan` subagent (read-only) does codebase research for the plan. Switch out of plan mode with `Shift+Tab`. | Use at the start of a complex phase (e.g., ECL opcode mapping) to get a full plan before any files are touched. Review, then flip to auto mode. |
| **Auto mode** | `--permission-mode auto` — built-in classifier decides what to allow without prompting. `claude auto-mode defaults > rules.json` prints the classifier rules. | Use for unattended build runs where all operations are low-risk (reads + Python tool runs). Combine with hooks to block dangerous calls. |
| **`bypassPermissions` mode** | Skips all permission prompts. `--dangerously-skip-permissions`. Use only in locked-down CI or with a `PreToolUse` hook as the safety net. | Not recommended for interactive work, but useful for a tightly scoped headless script that only runs `python tools/*.py`. |
| **Worktrees** | `claude -w <name>` creates `.claude/worktrees/<name>` as an isolated git worktree. Subagents can also get `isolation: worktree`. | Use for risky experiments (e.g., patching DQK.EXE) without touching the main working tree. |
| **Routines / Scheduled tasks** | `/schedule` in CLI or Desktop app creates recurring cloud agents (Anthropic-managed infra) or local scheduled tasks. `/loop` repeats a prompt within a session. | Low priority for Greybox now, but useful for nightly "run all decoders and update renders/" summaries once decoders are stable. |
| **`-c` / `--resume`** | `claude -c` resumes most recent session in the current dir. `claude -r "name"` resumes by name. `claude -n "greybox-hlib"` names a session for later resumption. | Name each major build phase session. Resume by name the next day: `claude -r greybox-hlib`. |
| **`--fallback-model`** | Auto-falls back to secondary model if primary is overloaded. | `claude --fallback-model sonnet,haiku` for long unattended runs. |
| **`--effort`** | Sets thinking depth: `low`/`medium`/`high`/`xhigh`/`max`. Per-subagent and per-skill overridable. | Use `--effort high` for ECL opcode analysis; `--effort low` on format-researcher subagent (Haiku, fast lookups). |

---

## Recommended Autonomous Setup for Greybox

### 1. CLAUDE.md — Keep It Lean and Factual

The existing `CLAUDE.md` is already well-structured. Additions:

- Add a "Current phase" section (update manually each session): which roadmap step is active, what the last verified output was.
- Keep under 200 lines. Move multi-step procedures to Skills.
- Add `CLAUDE.local.md` (gitignored) for:
  ```
  Amiga ADF path: D:\Games\Emulator\Amiga\...
  WSL mount: /mnt/d/...
  Python env: C:\Users\Caldor\...
  ```

### 2. Path-Scoped Rules in `.claude/rules/`

```
.claude/rules/
  python-tools.md      # paths: ["tools/**/*.py"] — style, argparse, no silent failures
  format-specs.md      # paths: ["docs/**/*.md"] — don't overwrite verified findings
  renders.md           # paths: ["renders/**"] — read-only; renders are tool outputs
```

### 3. Skills

Create these in `.claude/skills/`:

**`/resume-build`** — injects current git status, last render list, open questions from auto memory. `disable-model-invocation: true` (manual trigger). Dynamic context:
```yaml
---
description: Resume the current Greybox build phase with full context
disable-model-invocation: true
---
## Git status
!`git status --short`

## Recent renders
!`dir /b renders\ 2>nul | tail -20`

## Open questions from CLAUDE.md
<!-- paste or @-import the open questions section -->
Review the above and continue where we left off.
```

**`/verify-render`** — runs `python tools/build_gallery.py`, opens `progress.html` check, reports any missing renders. `context: fork` (isolated subagent run).

**`/decode-check`** — runs all three decoders (`dax_decode.py`, `daa_decode.py`, `hlib_decode.py`) on their reference inputs and verifies output checksums against known-good renders.

**`/phase-summary`** — summarizes what was accomplished this session and what to do next; writes to auto memory.

### 4. Hooks in `.claude/settings.json`

```json
{
  "hooks": {
    "PostToolUse": [
      {
        "matcher": "Write|Edit",
        "hooks": [
          {
            "type": "command",
            "command": "python -m py_compile ${tool_input.file_path}",
            "if": "Write(tools/*.py)|Edit(tools/*.py)",
            "timeout": 15
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "${CLAUDE_PROJECT_DIR}/.claude/hooks/checkpoint.ps1",
            "shell": "powershell",
            "async": true,
            "timeout": 30
          }
        ]
      }
    ],
    "SessionStart": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "${CLAUDE_PROJECT_DIR}/.claude/hooks/inject-context.ps1",
            "shell": "powershell",
            "timeout": 15
          }
        ]
      }
    ]
  }
}
```

`checkpoint.ps1` — writes a one-line JSON `{timestamp, session_id, last_tool}` to `.claude/checkpoints.jsonl` so we have an audit trail.

`inject-context.ps1` — outputs JSON with `hookSpecificOutput.additionalContext` containing the current render count and last-modified decoder.

### 5. Subagents in `.claude/agents/`

**`format-researcher.md`**:
```yaml
---
name: format-researcher
description: Look up Gold Box file format details in hackdocs_extracted/ and docs/. Use proactively when asked about DAX/DAA/HLIB/ECL formats. Read-only, fast.
model: haiku
tools: Read, Grep, Glob
effort: low
---
You are a Gold Box file-format reference agent. Search hackdocs_extracted/ for TXT docs and docs/ for verified findings. Return precise byte-level details. Never write files.
```

**`render-verifier.md`**:
```yaml
---
name: render-verifier
description: Run Python decoder scripts and verify render outputs. Use after decoder changes.
model: sonnet
tools: Bash, Read, Glob
disallowedTools: Write, Edit
memory: project
effort: medium
---
You run the three Gold Box decoders (dax_decode.py, daa_decode.py, hlib_decode.py) with standard test inputs, compare outputs to known-good renders, and report pass/fail with any diff details.
```

### 6. Session Naming for Resumption

Start each major phase with a named session:
```powershell
claude -n "greybox-hlib-reader" --permission-mode auto --effort high
```

Resume next day:
```powershell
claude -r "greybox-hlib-reader"
```

For unattended long runs:
```powershell
claude --bg -n "greybox-hlib-decode-all" "Decode all TLB/GLB files, render tiles, update progress.html"
# Check later:
claude logs greybox-hlib-decode-all
```

---

## Key Limits and Gotchas

- **CLAUDE.md is context, not enforcement.** If you need something to *always* happen (e.g., run lint before commit), use a `PreToolUse` hook — hooks execute unconditionally.
- **Auto memory cap**: only first 200 lines / 25 KB of `MEMORY.md` load at startup. Topic files are on-demand. Keep `MEMORY.md` as an index; push details to topic files.
- **Subagents load at session start**: if you add a `.claude/agents/*.md` file mid-session, restart to pick it up (unless using `/agents` UI, which is immediate).
- **Skills are live-reloaded**: editing `SKILL.md` takes effect within the current session without restart.
- **`--bare` mode** skips skills/hooks/MCP for fastest scripted calls — don't use it when you need hooks to fire.
- **`isolation: worktree` subagents** branch from the default branch (not HEAD) — good for clean experiments, unexpected if you want current HEAD state.
- **`background: true` subagents** always run async and return immediately — the parent continues without waiting for them.
- **Context window after `/compact`**: project-root CLAUDE.md is re-injected after compaction; nested CLAUDE.md files are not (they reload next time Claude touches a file in that subdir). If a rule must survive compaction, put it in the root CLAUDE.md.
- **`PostToolUse` hooks cannot block** — use `PreToolUse` for gating. `PostToolUse` is for validation and side effects.

---

## Sources

- https://code.claude.com/docs/en/overview — overview, feature list
- https://code.claude.com/docs/en/hooks — full hooks reference (events, types, config syntax)
- https://code.claude.com/docs/en/memory — CLAUDE.md, auto memory, rules, scoping
- https://code.claude.com/docs/en/sub-agents — subagent config, frontmatter fields, model selection
- https://code.claude.com/docs/en/skills — skills / custom commands, frontmatter, dynamic injection
- https://code.claude.com/docs/en/cli-reference — all CLI flags including -p, --bg, --resume, --bare, --effort, --permission-mode
