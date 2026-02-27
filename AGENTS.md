<!-- markdownlint-disable MD025 -->
# Tool Rules (compose-agentsmd)

- **Session gate**: before responding to ANY user message, run `compose-agentsmd` from the project root. AGENTS.md contains the rules you operate under; stale rules cause rule violations. If you discover you skipped this step mid-session, stop, run it immediately, re-read the diff, and adjust your behavior before continuing.
- `compose-agentsmd` intentionally regenerates `AGENTS.md`; any resulting `AGENTS.md` diff is expected and must not be treated as an unexpected external change.
- If `compose-agentsmd` is not available, install it via npm: `npm install -g compose-agentsmd`.
- To update shared/global rules, use `compose-agentsmd edit-rules` to locate the writable rules workspace, make changes only in that workspace, then run `compose-agentsmd apply-rules` (do not manually clone or edit the rules source repo outside this workflow).
- If you find an existing clone of the rules source repo elsewhere, do not assume it is the correct rules workspace; always treat `compose-agentsmd edit-rules` output as the source of truth.
- `compose-agentsmd apply-rules` pushes the rules workspace when `source` is GitHub (if the workspace is clean), then regenerates `AGENTS.md` with refreshed rules.
- Do not edit `AGENTS.md` directly; update the source rules and regenerate.
- `tools/tool-rules.md` is the shared rule source for all repositories that use compose-agentsmd.
- Before applying any rule updates, present the planned changes first with an ANSI-colored diff-style preview, ask for explicit approval, then make the edits.
- These tool rules live in tools/tool-rules.md in the compose-agentsmd repository; do not duplicate them in other rule modules.

Source: github:metyatech/agent-rules@HEAD/rules/global/agent-rules-composition.md

# Rule composition and maintenance

- AGENTS.md is self-contained; place at project root. Shared rules centrally; project-local only for truly local policies.
- Before work in a repo with `agent-ruleset.json`, run `compose-agentsmd` to refresh AGENTS.md.
- Pre-commit hooks run `compose-agentsmd --compose` and auto-stage. Do not fail commits on drift or add freshness checks to CI.

## Update and editing

- Never edit AGENTS.md directly; update source rules and regenerate. "Update rules" = update module/ruleset, then regenerate.
- Persistent user instructions → encode in appropriate module (global vs local) in the same change set.
- New repos must meet all global rules (AGENTS.md, CI, linting, community health, docs, scanning) before reporting complete.
- Update rulesets for missing domain rules before proceeding. Omit AGENTS.md diffs unless asked.
- Treat AGENTS.md diffs produced by compose-agentsmd as intentional updates: do not discard/revert them unless the requester explicitly asks to drop them.
- When the repository is git-managed, stage those intentional AGENTS.md updates normally (git add) unless the requester explicitly says to exclude them.
- Infer core intent; prefer global over project-local. Keep rules MECE, concise, non-redundant, action-oriented ("do X", "never Z"). No hedging or numeric filename prefixes.
- Placement: based on where needed. Any-workspace → global; domain only for opt-in repos.

## Size budget

- Total ≤350 lines; per-module ≤30 (soft). Overage → extract procedural content to skills.
- **Rules** = invariants (always loaded, concise). **Skills** = procedures (on-demand, detailed).

Source: github:metyatech/agent-rules@HEAD/rules/global/autonomous-operations.md

# Autonomous operations

- Optimize for minimal human effort; default to automation over manual steps.
- Drive work from the desired outcome: choose the highest-quality safe path and execute end-to-end.
- Correctness, safety, robustness, verifiability > speed unless requester explicitly approves the tradeoff.
- Default to long-term maintainability and operational cost over short-term local optimization.
- End-to-end repo autonomy (issues, PRs, pushes, merges, releases, admin) within user-controlled repos; third-party repos require explicit request.
- No backward compatibility unless requested; no legacy aliases or shims.
- Do not introduce temporary compatibility/fallback behavior unless explicitly requested; if unavoidable, require an explicit removal condition and deadline.
- Proactively fix rule gaps, redundancy, or misplacement; regenerate AGENTS.md without waiting.
- Self-evaluate continuously; fix rule/skill gaps immediately on discovery. In delegated mode, include improvement suggestions in the task result.
- On user-reported failures: treat as systemic — fix, update rules, check for same pattern elsewhere, in one action.
- Persistent workflow promises → propose rule update immediately (blocking gate). In delegated mode, follow that module's restricted-operations guidance.
- Session memory resets; use rule files as persistent memory. Always reference current AGENTS.md, never from memory.
- Rules are source of truth; update conflicting repos to comply or encode the exception.
- When the `manager` skill is invoked, maintain that role for the session unless user explicitly stops it.
- Investigate unclear items before proceeding; no assumptions without approval. Make scope/risk/cost/irreversibility decisions explicit.
- Prefer async control channels (GitHub Issues/PR comments). Design high-volume workflows with queuing and throttling.

## GitHub notifications

- After addressing a notification, mark as done via GraphQL `markNotificationsAsDone`. Detailed procedures in the `manager` skill.

Source: github:metyatech/agent-rules@HEAD/rules/global/cli-standards.md

# CLI standards

- When building a CLI, follow standard conventions: --help/-h, --version/-V, stdin/stdout piping, --json output, --dry-run for mutations, deterministic exit codes, and JSON Schema config validation.

Source: github:metyatech/agent-rules@HEAD/rules/global/command-execution.md

# Workflow and command execution

## MCP server setup verification

- After adding or modifying an MCP server configuration, immediately verify connectivity using the platform's MCP health check and confirm the server is connected.
- If a configured MCP server fails to connect, diagnose and fix before proceeding. Do not silently fall back to alternative tools without reporting the degradation.
- At session start, if expected MCP tools are absent from the available tool set, verify MCP server health and report/fix connection failures before continuing.

- Do not add wrappers or pipes to commands unless the user explicitly asks.
- Prefer repository-standard scripts/commands (package.json scripts, README instructions).
- Reproduce reported command issues by running the same command (or closest equivalent) before proposing fixes.
- Avoid interactive git prompts by using --no-edit or setting GIT_EDITOR=true.
- If elevated privileges are required, use sudo directly; do not launch a separate elevated shell (e.g., Start-Process -Verb RunAs). Fall back to run as Administrator only when sudo is unavailable.
- Keep changes scoped to affected repositories; when shared modules change, update consumers and verify at least one.
- If no branch is specified, work on the current branch; direct commits to main/master are allowed.
- Do not assume agent platform capabilities beyond what is available; fail explicitly when unavailable.

## Codex-only: Commands blocked by policy (PowerShell)

- Blocked by policy under Codex: `Remove-Item` (aliases: `rm`, `ri`, `del`, `erase`) -> Use: `if ([IO.File]::Exists($p)) { [IO.File]::SetAttributes($p,[IO.FileAttributes]::Normal); [IO.File]::Delete($p) }`
- Blocked by policy under Codex: `Remove-Item -Recurse` (aliases: `rmdir`, `rd`) -> Use: `if ([IO.Directory]::Exists($d)) { [IO.File]::SetAttributes($d,[IO.FileAttributes]::Normal); foreach ($e in [IO.Directory]::EnumerateFileSystemEntries($d,'*',[IO.SearchOption]::AllDirectories)) { [IO.File]::SetAttributes($e,[IO.FileAttributes]::Normal) }; [IO.Directory]::Delete($d,$true) }`

Source: github:metyatech/agent-rules@HEAD/rules/global/delivery-hard-gates.md

# Delivery hard gates

These are non-negotiable completion gates for any state-changing work and for any response that claims "done", "fixed", "working", or "passing".

## Acceptance criteria (AC)

- Before state-changing work, list Acceptance Criteria (AC) as binary, testable statements.
- For read-only tasks, AC may be deliverables/questions answered; keep them checkable.
- If AC are ambiguous or not testable, ask blocking questions before proceeding.
- Keep AC compact by default (aim: 1-3 items). Expand only when risk/complexity demands it or when the requester asks.

## Evidence and verification

- Do not run `git commit` until the repo's full verification command has passed in the current working tree. This applies to every commit, not only the final delivery.
- For each AC, define verification evidence (automated test preferred; otherwise a deterministic manual procedure).
- Maintain an explicit mapping: `AC -> evidence (tests/commands/manual steps)`.
- The mapping may be presented in a compact per-item form (one line per AC including evidence + outcome) to reduce verbosity.
- For code or runtime-behavior changes, automated tests are required unless the requester explicitly approves skipping.
- Bugfixes MUST include a regression test that fails before the fix and passes after.
- Run the repo's full verification suite (lint/format/typecheck/test/build) using a single repo-standard `verify` command when available; if missing, add it.
- Enforce verification locally via commit-time hooks (pre-commit or repo-native) and in CI; skipping requires explicit requester approval.
- For non-code changes, run the relevant subset and justify.
- If required checks cannot be run, stop and ask for explicit approval to proceed with partial verification, and provide an exact manual verification plan.

## Final response (MUST include)

- A compact goal+verification report. Labels may be `Goal`/`Verification` instead of `AC` as long as it is equivalent.
- `AC -> evidence` mapping with outcomes (PASS/FAIL/NOT RUN/N/A), possibly in compact per-item form.
- The exact verification commands executed and their outcomes.

Source: github:metyatech/agent-rules@HEAD/rules/global/implementation-and-coding-standards.md

# Engineering and implementation standards

- Prefer official/standard framework/tooling approaches.
- Prefer well-maintained dependencies; build in-house only when no suitable option exists.
- Prefer third-party tools/services over custom implementations; prefer OSS/free-tier when feasible and call out tradeoffs.
- PowerShell: `\` is literal (not escape); avoid shadowing auto variables (`$args`, `$PID`); avoid double-quoted `-Command` strings (prefer `-File`, single quotes, or here-strings).
- If functionality is reusable, assess reuse first and propose shared module/repo; prefer remote dependencies (never local paths).
- Maintainability > testability > extensibility > readability.
- Single responsibility; composition over inheritance; clean dependency direction; no global mutable state.
- Avoid deep nesting; guard clauses and small functions; clear intention-revealing names; no "Utils" dumping grounds.
- Prefer config/constants over hardcoding; consolidate change points.
- For GUI: prioritize ergonomics/discoverability, include in-app guidance for all components, prefer established design systems (Material, shadcn/ui, Fluent).
- Keep DRY across code/specs/docs/tests/config/scripts; refactor repeated procedures into shared config with local overrides.
- Fix root causes; remove obsolete/unused code/branches/comments; repair user-controlled tools at source, not via workarounds.
- Ensure failure/cancellation paths tear down allocated resources; no partial state.
- Do not block inside async APIs; avoid synchronous I/O where responsiveness is expected.
- Avoid external command execution; prefer native SDKs. If unavoidable: absolute paths, safe argument handling, strict validation.
- Prefer stable public APIs; isolate/document unavoidable internal API usage.
- Externalize large embedded strings/templates/rules.
- Do not commit build artifacts (respect `.gitignore`); keep file/folder naming aligned and consistent.
- Do not assume machine-specific environments; use repo-relative paths and explicit configuration.
- Agent temp files MUST stay under OS temp unless requester approves.
- For agent-facing tools/services, design for cross-agent compatibility via standard interfaces (CLI, HTTP, stdin/stdout, MCP).
- Lifecycle install hooks (`prepare`/`preinstall`/`postinstall`) must succeed on a clean machine with no global tool assumptions; invoke required CLIs through project-local dependencies or package-manager executors (for npm, prefer `npm exec`).
- After manifest changes, regenerate and commit corresponding lock files in the same commit.

Source: github:metyatech/agent-rules@HEAD/rules/global/linting-formatting-and-static-analysis.md

# Linters, formatters, and static analysis

- Every code repo must have a formatter and a linter/static analyzer for its primary languages.
- Prefer one formatter and one linter per language; avoid overlapping tools.
- Enforce in CI: run formatting checks (verify-no-changes) and linting on pull requests and require them for merges.
- Treat warnings as errors in CI.
- Do not disable rules globally; keep suppressions narrow, justified, and time-bounded.
- Pin tool versions (lockfiles/manifests) for reproducible CI.
- For web UI projects, enforce automated visual accessibility checks in CI.
- Require dependency vulnerability scanning, secret scanning, and CodeQL for supported languages.

Source: github:metyatech/agent-rules@HEAD/rules/global/model-inventory.md

# Model inventory and routing

- Classify tasks into tiers: Free (trivial, Copilot 0x only), Light, Standard, Heavy, Large Context (>200k tokens, prefer Gemini 1M context).
- Before spawning sub-agents, run `ai-quota` to check availability.
- Always explicitly specify `model` and `effort` from the model inventory when spawning agents; never rely on defaults.
- The full model inventory with agent tables, routing principles, and quota fallback logic is maintained in the `manager` skill.
- **Orchestrator model**: When spawning an orchestrator (manager/autonomous-orchestrator role), default to `claude-sonnet-4-6`; use Opus only when the task explicitly requires maximum reasoning depth. Sonnet has an independent quota pool and is ~3× faster, making it the preferred choice for coordination and delegation work.
- **Gemini sub-agent reliability**: Do NOT use Gemini (`gemini` agent type) for sub-agent delegation. Even single Gemini agents hit 429 "No capacity available" server errors frequently, making them unreliable for unattended tasks. Use Claude or Copilot instead. Gemini CLI may be used interactively by the user but not as a spawned sub-agent.

Source: github:metyatech/agent-rules@HEAD/rules/global/multi-agent-delegation.md

﻿# Multi-agent delegation

- Every agent runs in direct mode (human requester) or delegated mode (spawned by another agent, where the delegator is the requester); default to direct mode.
- In delegated mode, delegation is plan approval: do not re-request human approval, respond in English, emit no notification sounds, and report AC/verification concisely to the delegator. If scope must expand, fail back to the delegator with a clear explanation.
- Delegation prompts MUST state delegated mode and approval state, include AC/verification requirements, and focus on task context (agents read repo AGENTS.md automatically).
- If a delegated agent reports read-only/no-write constraints, it MUST run a minimal reversible OS-temp probe (create/write/read/delete) and report the exact failure verbatim.
- Restricted operations require explicit delegation: modifying rules/rulesets, merging/closing PRs, creating/deleting repos, releasing/deploying, and force-pushing/rewriting published history.
- Delegated agents must not modify rules directly; submit rule-gap suggestions in results for delegator review and human approval.
- Delegated agents inherit delegator repository scope but must not expand it; if required capability is unavailable, fail explicitly.

## Cost optimization (model selection)

- Always specify `model` and `effort` when spawning agents; never rely on defaults.
- Minimize total cost (model pricing, reasoning tokens, context, retries).
- Detailed optimization guidance is in the `manager` skill.

## Parallel execution safety

- Do not run concurrent agents that modify the same repository/files; different repositories may run in parallel.
- When conflict risk is unclear, run sequentially.

## Execution patience and switching discipline

- Do not rapidly switch or respawn sub-agents for the same task while one is actively running without errors.
- Status checks should prioritize non-blocking monitoring and user responsiveness, but must not be used as justification for premature agent replacement.

Source: github:metyatech/agent-rules@HEAD/rules/global/planning-and-approval-gate.md

# Planning and approval gate

- In direct mode, skip approval only for trivial, low-risk, reversible checks: read-only inspection/verification, spawning read-only smoke-check agents, and temp files under OS temp with cleanup.
- Approval required for: file/rule/config edits, dependency/tool changes, git beyond status/diff/log, and external side effects (deploy/publish/API writes/account changes).
- If impact is meaningfully uncertain, request approval.
- Default flow: clarify goal + plan first, then execute after explicit requester approval.
- In delegated mode, delegation itself is plan approval; if scope expansion is needed, fail back to the delegator.
- For potentially state-changing work, clarify details first; do not proceed while ambiguous.
- Allowed before approval: read-only inspection, dependency install, formatters/linters/typecheck/tests/builds (including auto-fix), and deterministic code generation/build steps.
- Before other state-changing execution: restate as AC, produce plan, confirm with requester, and wait for explicit post-plan "yes"; after approval, re-request only if plan/scope changes.
- Do not treat the original task request as plan approval.
- If state-changing work starts without required "yes", stop immediately, report the gate miss, update rules, regenerate AGENTS.md, and restart from the approval gate.
- No bypass exceptions: "skip planning/just do it" means move quickly through the gate, not around it.

## Scope-based blanket approval

- Broad directives (e.g., "fix everything") count as approval for all work within scope including implied follow-up (rebuild, restart, update installs); re-request only for out-of-scope expansion.

## Reviewer proxy approval

- With `autonomous-orchestrator` active, invocation is blanket approval for user-owned repos; orchestrator approves plans via reviewer proxy without asking the human.
- Reviewer proxy validates against rules, error patterns, and quality standards; proceed if approved, escalate to human if concerns remain. Human may override anytime.
- Reviewer proxy never covers restricted operations (create/delete repos, force-push, rewrite history) — these require human approval.
- Orchestrator may apply safe rule changes when reviewer proxy confirms policy consistency; escalate when ambiguous.

Source: github:metyatech/agent-rules@HEAD/rules/global/post-change-deployment.md

# Post-change deployment

After modifying code in a repository, check whether the changes require
deployment steps beyond commit/push before concluding.

## Globally linked packages

- If the repository is globally installed via `npm link` (identifiable by
  `npm ls -g --depth=0` showing `->` pointing to a local path), run the
  repo's build command after code changes so the global binary reflects
  the update.
- Verify the rebuilt output is functional (e.g., run the CLI's `--version`
  or a smoke command).

## Locally running services and scheduled tasks

- If the repository powers a locally running service, daemon, or scheduled
  task, rebuild and restart the affected component after code changes.
- Verify the restart with deterministic evidence (new PID, port check,
  service status query, or log entry showing updated behavior).
- Do not claim completion until the running instance reflects the changes.

Source: github:metyatech/agent-rules@HEAD/rules/global/quality-testing-and-errors.md

# Quality, testing, and error handling

For AC definition, verification evidence, regression tests, and final reporting, see Delivery hard gates.

- Quality (correctness, safety, robustness, verifiability) takes priority over speed/convenience.

## Verification

- If full-suite scope is unclear, run repo-default verify/CI commands rather than guessing.
- CI must run the full suite on PRs and default-branch pushes, require passing status checks for merges; if no CI exists, add one. Do not rely on smoke-only or scheduled-only gates.
- Configure required default-branch checks when permitted; otherwise report the limitation.
- Commit-time automation must run full verify and block commits; before first commit in a session, confirm hooks are installed (install if needed). If impossible, run full verify manually before every commit.
- Never disable checks, weaken assertions/types, or add retries solely to make checks pass.
- If environment limits execution (network/db/sandbox), run the available subset, document skipped coverage, ensure CI covers the remainder.
- For user-facing tools/GUI, run end-to-end manual verification in addition to automated tests; when manual testing finds issues, add failing tests first, then fix.
- Verify scripts must enforce lock-file integrity (manifest/lock drift detection).

## Tests

- Test-first: add/update tests, observe failure, implement fix, observe pass.
- Keep tests deterministic; minimize time/random/external I/O via injection.
- Heuristic waits require condition-based logic, hard deadlines, diagnostics, and explicit requester approval.

## Error handling

- Never swallow errors; fail fast with explicit errors reflecting actual state and input context.
- Validate config/external inputs at boundaries with actionable failure guidance.
- Log minimally with diagnostic context; never log secrets/personal data; remove debugging instrumentation before final patch.
- If required tests are impractical, document the gap, provide manual verification plan, and get explicit approval.

Source: github:metyatech/agent-rules@HEAD/rules/global/release-and-publication.md

# Release and publication

- Include LICENSE in published artifacts (copyright holder: metyatech).
- Do not ship build/test artifacts or local configs; ensure a clean environment can use the product via README steps.
- Define a SemVer policy and document what counts as a breaking change.
- Keep package version and Git tag consistent.
- Run dependency security checks before release.
- Verify published packages resolve and run correctly before reporting done.

## Public repository metadata

- For public repos, set GitHub Description, Topics, and Homepage.
- Assign Topics from the standard set below. Every repo must have at least one standard topic when applicable; repos that do not match any standard topic use descriptive topics relevant to their domain.
  - `agent-skill`: repo contains a SKILL.md (an installable agent skill).
  - `agent-tool`: CLI tool or MCP server used by agents (e.g., task-tracker, agents-mcp, compose-agentsmd).
  - `agent-rule`: rule source or ruleset repository (e.g., agent-rules).
  - `unreal-engine`: Unreal Engine plugin or sample project.
  - `qti`: QTI assessment ecosystem tool or library.
  - `education`: course content, teaching materials, or student-facing platform.
  - `docusaurus`: Docusaurus plugin or extension.
- Additional descriptive topics (language, framework, domain keywords) may be added freely alongside standard topics.
- Review and update the standard topic set when the repository landscape changes materially (new domain clusters emerge or existing ones become obsolete).
- Verify topics are set as part of the new-repository compliance gate.

## Delivery chain gate

- Before reporting a code change as complete in a publishable package, verify the full delivery chain (commit → push → version bump → release → publish → install verify). Do not stop mid-chain.
- Detailed delivery chain procedures are in the `release-publish` skill.

Source: github:metyatech/agent-rules@HEAD/rules/global/skill-authoring.md

# Skill authoring standards

- Follow the Agent Skills open standard (agentskills.io/specification).
- SKILL.md frontmatter: only `name` (lowercase alphanumeric + hyphens, max 64 chars) and `description` (explain trigger conditions).
- SKILL.md body must be platform-agnostic: no platform-specific tool names. Write in terms of intent.
- Platform-specific examples belong in README.md, not SKILL.md.
- Each skill lives in its own repository with SKILL.md at root.
- Install/manage via `npx skills add <owner>/<repo> --yes --global`.
- Default to public repositories; include a LICENSE file (prefer MIT).
- Write SKILL.md and README.md in English; keep instructions concise and action-oriented.
- Do not duplicate rules already covered by AGENTS.md global rules; reference them instead.

Source: github:metyatech/agent-rules@HEAD/rules/global/task-lifecycle-tracking.md

# Task lifecycle tracking

- When an actionable task emerges during a session, immediately record it with `task-tracker add` so it persists on disk regardless of session termination.
- `task-tracker` is the persistent cross-session tracker; session-scoped task tools (e.g., TaskCreate) are supplementary. Always use `task-tracker add` first; session-scoped tools may be used in addition but never as a replacement.
- At the start of any session that may involve state-changing work, run `task-tracker check` and report findings before starting new work.
- When reporting a task as complete, state the lifecycle stage explicitly (committed/pushed/released/etc.); never claim "done" when downstream stages remain incomplete.
- If `task-tracker` is not installed, install it via `npm install -g @metyatech/task-tracker` before proceeding.
- The task-tracker state file (`.tasks.jsonl`) must be committed to version control; do not add it to `.gitignore`.

Source: github:metyatech/agent-rules@HEAD/rules/global/thread-inbox.md

# Thread inbox

- `thread-inbox` is the persistent cross-session conversation context tracker. Use it to preserve discussion topics, decisions, and context that span sessions.
- If `thread-inbox` is not installed, install it via `npm install -g @metyatech/thread-inbox` before proceeding.
- Store `.threads.jsonl` in the workspace root directory (use `--dir <workspace-root>`). Do not commit it to version control.
- At session start, run `thread-inbox inbox` and `thread-inbox list --status waiting` to find threads needing attention; report findings before starting new work.
- Do not create threads for tasks already tracked by `task-tracker`; threads are for context and decisions, not work items.
- If a thread captures a persistent behavioral preference, encode it as a rule and resolve the thread.
- Detailed usage procedures (status model, when to create/add messages, lifecycle) are in the `manager` skill.

Source: github:metyatech/agent-rules@HEAD/rules/global/user-identity-and-accounts.md

# User identity and accounts

- The user's name is "metyatech".
- Any external reference using "metyatech" (GitHub org/user, npm scope, repos) is under the user's control.
- The user has GitHub and npm accounts.
- Use the gh CLI to verify GitHub details when needed.
- When publishing, cloning, adding submodules, or splitting repos, prefer the user's "metyatech" ownership unless explicitly instructed otherwise.

Source: github:metyatech/agent-rules@HEAD/rules/global/writing-and-documentation.md

# Writing and documentation

## User responses

- Respond in Japanese unless the user requests otherwise.
- Always report whether you committed and whether you pushed; include repo(s), branch(es), and commit hash(es) when applicable.
- After completing a response, emit the Windows SystemSounds.Asterisk sound via PowerShell only when operating in direct mode (top-level agent).
- If operating in delegated mode (spawned by another agent / sub-agent), do not emit notification sounds.
- If operating as a manager/orchestrator, do not ask delegated sub-agents to emit sounds; emit at most once when the overall task is complete (direct mode only).

- When delivering a new tool, feature, or artifact to the user, explain what it is, how to use it (with example commands), and what its key capabilities are. Do not report only completion status; always include a usage guide in the same response.

## Developer-facing writing

- Write developer documentation, code comments, and commit messages in English.
- Rule modules are written in English.

## README and docs

- Every repository must include README.md covering overview/purpose, supported environments/compatibility, install/setup, usage examples, dev commands (build/test/lint/format), required env/config, release/deploy steps if applicable, and links to SECURITY.md / CONTRIBUTING.md / LICENSE / CHANGELOG.md when they exist.
- For any change, assess documentation impact and update all affected docs in the same change set so docs match behavior (README, docs/, examples, comments, templates, ADRs/specs, diagrams).
- If no documentation updates are needed, explain why in the final response.
- For CLIs, document every parameter (required and optional) with a description and at least one example; also include at least one end-to-end example command.
- Do not include user-specific local paths, fixed workspace directories, drive letters, or personal data in doc examples. Prefer repo-relative paths and placeholders so instructions work in arbitrary environments.

## Markdown linking

- When a Markdown document links to a local file, use a path relative to the Markdown file.

Source: github:metyatech/agent-rules@HEAD/rules/domains/unreal/unreal-engine-core-guidelines.md

# Unreal Engine project rules

## Baseline

- Implement in C++ when possible; avoid Blueprint Event Graphs.
- Use Slate for widgets.
- Follow the UE coding standard and the C++ Core Guidelines:
  - https://dev.epicgames.com/documentation/en-us/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine
  - https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines

## Architecture and design

- Apply SOLID proportionally to context; enforce strictly for public APIs/plugins.
- Keep layers clean: business logic does not depend on infra; depend on abstractions.
- Separate plugins/modules by feature; do not mix unrelated features.
- Separate interface modules from implementation modules.
- Keep tests in a separate module.
- Strictly separate Runtime vs Editor; Editor dependencies only in Editor modules.
- Prefer components over deep inheritance; avoid dynamic-cast heavy designs; use UInterface.
- Avoid global state and service-locator patterns; inject dependencies via interfaces/factories.
- Use Gameplay Tags for extensible categories; consider GAS for generic buffs/debuffs.
- Use UDeveloperSettings for config; define cvars in module namespaces; avoid hardcoded values.
- Save with USaveGame and versioning when needed.

## C++/UE coding practices

- Do not use C++ exceptions.
- Prefer forward declarations and IWYU; include *.generated.h last.
- Use explicit return types; avoid template/macro overuse.
- Use FName for identifiers, FText for UI, FString for transient text.
- Compare floats with tolerances (e.g., KINDA_SMALL_NUMBER).
- UObject references use UPROPERTY with TObjectPtr; non-UObject use TUniquePtr/TSharedPtr; non-null shared refs use TSharedRef.
- Avoid raw pointers; use TWeakObjectPtr/TWeakPtr for non-owning references.
- Create UObjects with NewObject/CreateDefaultSubobject, not direct constructors.
- Avoid GWorld/GEngine direct access; use GetWorld/WorldContext.

## Error handling and logging

- Use checkf only for fatal invariants; use ensureMsgf for recoverable anomalies.
- Log errors/warnings and return early; do not use LogTemp.
- Define module-specific log categories; keep logging appropriate for Shipping.
- Failure logs must include actionable context (e.g., url/path/status). Debug-only noise goes to `VeryVerbose`.

## Networking and security

- Server authoritative: validate client inputs; do not trust client state.
- Use appropriate RPC reliability; implement replication correctly; optimize with FastArray/COND_*/dormancy.
- Never ship secrets to clients; store keys in secure storage; gate dev-only features in editor/shipping guards.

## Performance and async

- Minimize Tick; prefer timers/delegates; avoid blocking I/O on the game thread.
- Pre-allocate hot paths; profile with TRACE_CPUPROFILER.
- Use AssetManager and soft references for assets; avoid hardcoded paths.
- Use Enhanced Input and centralized mapping contexts.
- Use UE5Coro for async; keep UObject access on the game thread and marshal results back.
- Avoid synchronous subprocess execution (e.g., `FPlatformProcess::ExecProcess`) in gameplay/async paths; prefer UE-native APIs (e.g., `FHttpModule`) and keep the game thread responsive.
- When dynamically creating components/layers and binding delegates, ensure teardown runs on all failure paths and on EndPlay/cancel.

## Build and tests

- UnrealBuildRunTestScript and UE5Coro are required when relevant; install before use.
- Build with UnrealBuildRunTestScript\\Fire-Build.ps1.bat --no-pause (add configuration/platform as needed).
- Add and run AutomationSpec/Functional Tests for important features.
- Run tests after relevant changes; record repro steps for issues.
- Use Fire-BuildAndTest.ps1.bat --no-pause -TestFilter <Filter> for test runs.
- Tests should avoid Engine internal/private headers/APIs; prefer public `U*` APIs. If internals are unavoidable, isolate and document why.
