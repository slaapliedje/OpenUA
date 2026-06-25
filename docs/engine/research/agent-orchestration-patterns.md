# Agent Orchestration Patterns — Research Findings

> Research compiled for the Greybox autonomous multi-agent build protocol.
> Target consumer: `docs/engine/ORCHESTRATION.md`.
> Sources fetched: June 2026.

---

## 1. Patterns Table

| Pattern | Core mechanic | When to use | Our application in Greybox |
|---|---|---|---|
| **Prompt chaining** | Sequential LLM calls; each step's output feeds the next; "gates" verify progress | Fixed, predictable subtask order; well-scoped pipelines | Format decode → asset convert → HLIB repack pipeline; each stage verifiable before proceeding |
| **Routing** | Classifier decides which specialist sub-pipeline handles a given input | Input types diverge significantly; one-size prompt hurts quality | Route incoming asset by type: DAX block → dax_decode, DAA block → daa_decode, HLIB TILE → hlib_decode |
| **Parallelization — sectioning** | Independent subtasks run concurrently; results aggregated | Subtasks don't share state; latency matters; throughput > serial | Fan out CoK DAX extraction, DoK DAX extraction, and DQK HLIB decode in parallel at build start |
| **Parallelization — voting** | Same task N times; majority or best-of-N selected | Nondeterministic outputs; need confidence in correctness | ECL opcode re-mapping: 3 independent translation attempts → compare; flag disagreements for human review |
| **Orchestrator-workers** | Orchestrator LLM decomposes dynamically; delegates to worker agents; synthesizes | Complex, open-ended tasks where subtask scope is input-dependent | Build-runner orchestrator decomposes "rebuild CoK on DQK engine" into per-phase worker tasks at runtime |
| **Evaluator-optimizer** | Generator produces output; separate evaluator critiques; generator revises in loop | Clear evaluation criteria exist; improvement is demonstrable over iterations | Palette-conversion loop: generator maps Amiga 32-color → VGA 256-color; evaluator checks pixel fidelity vs reference render; iterate until delta < threshold |
| **Hierarchical teams** | Supervisors coordinate nested sub-supervisors and workers | Scale beyond a single orchestrator's context capacity | Phase-level orchestrators (Art Phase, ECL Phase, Build Phase) each manage their own worker pool; top-level supervisor tracks phase completion |
| **ReAct (Reason-Act-Observe)** | Interleaved Thought → Action → Observation loops grounded in tool results | Any agent needing reliable step-by-step reasoning with real tool feedback | Standard inner loop for all worker agents: think about next step, run tool (decode/compare/patch), observe actual output |
| **Shared scratchpad** | All agents read/write to common message history; rule-based router | Small collaborating agents where cross-visibility matters more than isolation | Useful for art-validation subteam (decoder + comparator + reporter share the same working state) |
| **Supervisor-delegated (isolated scratchpad)** | Supervisor routes to agents with private contexts; only final output propagates | Larger teams; avoids context bloat from every agent's reasoning leaking upward | Preferred for major phases — workers keep private contexts; only structured result objects go to orchestrator |

---

## 2. Reliability & Anti-Drift

### 2.1 The core failure modes

| Failure mode | Cause | Mitigation |
|---|---|---|
| **Context rot** | Long conversation history fills window; old, irrelevant tokens crowd out current state | Save plan to external memory before context hits ~70% capacity; spawn fresh subagent with clean context + structured handoff |
| **Goal drift** | Agent loses track of original objective over many steps | Anchor every subagent prompt with an explicit goal statement; re-state constraints at the start of every resumed session |
| **Premature convergence** | Agent declares success before fully verifying outcome | Separate "did I finish?" from "is it correct?" — use a distinct evaluator role; require tool-verified evidence, not self-assertion |
| **Compounding errors** | Early mistake propagates through chain with no checkpoint | Insert programmatic gate checks between pipeline stages; fail loudly rather than silently forward bad data |
| **Tool hallucination** | Agent invents tool calls or parameters that don't exist | Provide exhaustive tool docs with examples and edge cases; apply poka-yoke constraints (e.g., require absolute file paths — see Anthropic ACI best practices) |
| **Emergent behavior from small changes** | A tweak to the orchestrator unpredictably alters subagent behavior | Treat orchestrator prompts as API contracts; version them; regression-test on a fixed evaluation set after any change |

### 2.2 Concrete anti-drift practices

**Checkpoint gates.** After every pipeline stage, run a programmatic assertion before handing off to the next stage. For our build: after DAX decode, verify PNG output size and palette entry count before feeding into the HLIB repacker. Gate failures abort the stage — never silently forward.

**Explicit effort budgets.** Embed rules like "simple format inspection: 1 agent, ≤10 tool calls; full ECL opcode port: ≤5 subagents, ≤50 tool calls each." The Anthropic multi-agent research system embeds these directly in orchestrator prompts; they prevent runaway exploration.
Source: https://www.anthropic.com/engineering/built-multi-agent-research-system

**External memory before window exhaustion.** When accumulated context approaches the limit, the orchestrator serializes its plan and completed-work summary to a markdown file (or structured JSON) before spawning the next subagent with that file as its only context. Subagents write outputs to the filesystem, not back through conversation history, to prevent information loss.
Source: https://www.anthropic.com/engineering/built-multi-agent-research-system

**End-state evaluation, not step-validation.** For multi-turn work, evaluate whether the agent reached the correct final state (e.g., "does the rebuilt CoK binary boot in DOSBox?"), not whether it followed the exact expected path. Intermediate paths vary; only outcomes are contractual.
Source: https://www.anthropic.com/engineering/built-multi-agent-research-system

**Human-in-the-loop gates for irreversible actions.** Pause and request explicit approval before any write to the DOS game folders (which are treated as read-only source), before patching `DQK.EXE`, or before overwriting a repacked asset. Autonomy within a phase; human approval at phase boundaries.
Source: https://huyenchip.com/2025/01/07/agents.html (Chip Huyen "Agents")

**Voting for high-stakes decisions.** For ECL opcode remapping (the highest-risk phase), run 3 independent translation passes and compare. Flag disagreements for human review rather than picking the majority silently.
Source: https://www.anthropic.com/news/building-effective-agents

**ReAct inner loop for all workers.** Every worker agent uses the Thought → Action → Observation cycle — never acts without first stating its reasoning. This grounds outputs in actual tool results rather than hallucinated assumptions.
Source: https://lilianweng.github.io/posts/2023-06-23-agent/

**Reflexion for stuck agents.** If a worker fails 2 consecutive tool calls on the same subtask, it writes a short self-reflection ("why did this fail, what will I try differently") before retrying. Prevents infinite retry loops on a broken strategy.
Source: https://lilianweng.github.io/posts/2023-06-23-agent/

**Observability tracing.** Log orchestrator decision points and subagent spawns (not full conversation contents — just the decision graph) so post-hoc debugging can reconstruct why an agent took an unexpected path.
Source: https://www.anthropic.com/engineering/built-multi-agent-research-system

---

## 3. Cost & Token Efficiency

### 3.1 Key cost facts (from Anthropic's multi-agent research data)

- Agents use ~4× more tokens than single-turn chat.
- Multi-agent systems use ~15× more tokens than chat.
- Token usage alone explains 80% of performance variance in browsing evaluations.
- 3–5 parallel subagents instead of serial reduces wall-clock research time by up to 90%.
- A stronger model (Claude Sonnet 4 → Opus 4 upgrade) delivered larger gains than doubling the token budget on older models.

Source: https://www.anthropic.com/engineering/built-multi-agent-research-system

### 3.2 Efficiency practices

**Fan out, don't serialize.** Where subtasks are independent (CoK art extract, DoK art extract, DQK HLIB parse), run them in parallel. Serial execution is the easiest way to waste wall time with no quality benefit.

**Isolate context per worker.** Workers with private scratchpads cost less than shared-scratchpad designs because they don't accumulate every other agent's reasoning trace. Use isolated scratchpad (supervisor-delegated) for all major phases; reserve shared scratchpad only for small, tightly coupled collaborations.

**Route by task difficulty.** Use a small/fast model for classification, file-format inspection, and structured data extraction. Reserve the large model (orchestrator) for decomposition, synthesis, and evaluation. This "cascade" pattern cuts cost without sacrificing quality on the hard steps.
Source: https://eugeneyan.com/writing/llm-patterns/

**Cache deterministic decode outputs.** DAX and DAA decode is deterministic given the same input file. Cache PNG renders and verified palette files; agents should read cached results rather than re-running decodes. (Cache invalidation: on source file change or tool version bump.)
Source: https://eugeneyan.com/writing/llm-patterns/

**Ablate tools ruthlessly.** Each additional tool in an agent's inventory increases hallucination risk (the agent may invoke a tool it doesn't actually need). Audit the tool set per agent type; remove any tool that doesn't appear in successful traces.
Source: https://huyenchip.com/2025/01/07/agents.html

**Start with small eval sets.** Test orchestration changes on ~20 representative tasks before full runs. Early in a project, a fix can move success rate from 30% → 80% — small samples are sufficient to detect large improvements cheaply.
Source: https://www.anthropic.com/engineering/built-multi-agent-research-system

**LLM-as-judge for quality, not cost.** Use a structured rubric (factual accuracy, format correctness, completeness, tool efficiency scored 0–1) rather than human review for each intermediate output. Most consistent when evaluation criteria are unambiguous (e.g., "does the palette have exactly 256 entries?").
Source: https://www.anthropic.com/engineering/built-multi-agent-research-system

---

## 4. Task Decomposition for Long-Horizon Coding Builds

### 4.1 Hierarchical decomposition model

```
Level 0 (top supervisor):   "Rebuild CoK and DoK on DQK engine with best-of art"
  Level 1 (phase orchestrators):
    Art Phase orchestrator  → workers: [DAX decoder, DAA decoder, palette mapper, HLIB art packer]
    Data Phase orchestrator → workers: [GEO map porter, MON/ITEM porter, ECL translator]
    Build Phase orchestrator→ workers: [HLIB repacker, linker, DOSBox tester]
  Level 2 (workers):        individual tool-use agents, 1 capability each
```

Plan at multiple granularities: high-level roadmap first, then decompose into subtasks only as far as currently necessary. Avoid over-specifying future phases whose exact structure will depend on what earlier phases discover.
Source: https://huyenchip.com/2025/01/07/agents.html

### 4.2 Plan-validate-execute-reflect cycle (per worker)

```
1. Generate plan     → write explicit subtask list + acceptance criteria
2. Validate plan     → check: are all tools in inventory? step count ≤ budget?
                              is every step reversible or gated?
3. Execute           → ReAct inner loop
4. Reflect           → did output meet acceptance criteria?
                       if no: write self-reflection, retry (max 2 retries) or escalate
```

Source: https://huyenchip.com/2025/01/07/agents.html; https://lilianweng.github.io/posts/2023-06-23-agent/

### 4.3 Structured handoff contract

Between every phase boundary, the outgoing orchestrator writes a structured handoff file:

```json
{
  "phase": "Art Phase",
  "status": "complete",
  "outputs": [
    { "asset": "BIGPIC1_114.png", "palette": "cok_amiga_32.json", "verified": true }
  ],
  "open_issues": ["SPRIT* inner pixel encoding unsolved — needs manual investigation"],
  "next_phase_inputs": ["cok_amiga_art/", "cok_amiga_palettes/"],
  "context_summary": "DAX and DAA decoded and cross-verified. Amiga art matches DOS reference at block 114."
}
```

The incoming phase orchestrator reads only this file, not the full conversation history of the previous phase. This is the primary mechanism against context rot in multi-phase builds.

---

## 5. When Multi-Agent Helps vs. Hurts

### Helps when:
- Subtasks are genuinely independent (fan-out possible without shared state)
- Task scope is unknown in advance (orchestrator-workers)
- Evaluation is separable from generation (evaluator-optimizer)
- Context would overflow a single long conversation
- Different subtasks benefit from different tool sets / system prompts

### Hurts when (and what to do instead):
- All agents need to share context constantly → use a single agent with a shared scratchpad
- Many inter-agent dependencies exist (each step needs output of many others) → use prompt chaining (sequential pipeline) instead
- Task is simple and well-understood → single LLM call with good tools; no orchestration overhead needed
- Coordination cost exceeds parallel speedup → serialize and gate instead

Source: https://www.anthropic.com/news/building-effective-agents; https://www.anthropic.com/engineering/built-multi-agent-research-system

---

## 6. Tool / ACI Design Rules

These apply to every tool exposed to any agent in the system. Poor tool design is the primary cause of agents "taking completely wrong paths."

1. **Exhaustive descriptions.** Every tool doc: what it does, when to use it, what it returns, edge cases, one worked example. Invest ACI effort ≥ HCI effort.
2. **Poka-yoke arguments.** Prevent common mistakes through parameter design: require absolute file paths (not relative); require explicit format strings (not inferred); reject ambiguous inputs at the tool boundary, not silently.
3. **Natural text formats.** Prefer formats that appear on the web (JSON, plain text, structured markdown) over custom binary encodings in tool I/O. This leverages training distribution.
4. **Tool ablation.** After a build phase, remove tools from the inventory that never appeared in successful agent traces. Fewer tools → less hallucination risk.
5. **Test tools in isolation.** Use the workbench to step through tool behavior before wiring into an agent pipeline. Unexpected tool behavior is easier to fix before it becomes an emergent agent failure.

Source: https://www.anthropic.com/news/building-effective-agents

---

## 7. Source Index

| Source | URL | Type |
|---|---|---|
| Anthropic "Building Effective Agents" | https://www.anthropic.com/news/building-effective-agents | Primary — Anthropic official |
| Anthropic "How We Built Our Multi-Agent Research System" | https://www.anthropic.com/engineering/built-multi-agent-research-system | Primary — Anthropic engineering blog |
| Lilian Weng "LLM Powered Autonomous Agents" | https://lilianweng.github.io/posts/2023-06-23-agent/ | Authoritative survey |
| Chip Huyen "Agents" | https://huyenchip.com/2025/01/07/agents.html | Practitioner deep-dive |
| Eugene Yan "LLM Patterns" | https://eugeneyan.com/writing/llm-patterns/ | Engineering patterns catalogue |
| LangChain "LangGraph Multi-Agent Workflows" | https://www.langchain.com/blog/langgraph-multi-agent-workflows | Framework-level orchestration |

---

*Uncertain / needs validation:*
- Token multipliers (4× agent vs chat; 15× multi-agent vs chat) are from Anthropic's research system; may not generalize to all coding tasks — treat as order-of-magnitude estimates.
- The 90% time reduction from parallel subagents is specific to breadth-first research workloads; coding tasks have fewer parallelizable elements (Anthropic note).
- Reflexion retry patterns are from academic literature (2023); production behavior may differ with newer models.
