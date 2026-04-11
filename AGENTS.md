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

Source: github:metyatech/agent-rules@HEAD/rules/domains/unreal/unreal-engine-core-guidelines.md

# Unreal Engine project rules

## Baseline

- Implement in C++ when possible; avoid Blueprint Event Graphs.
- Use Slate for widgets.
- Follow the UE coding standard and the C++ Core Guidelines:
  - [Epic C++ Coding Standard for Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)
  - [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)

## Architecture and design

- Apply SOLID proportionally to context; enforce strictly for public
  APIs/plugins.
- Keep layers clean: business logic does not depend on infra; depend on
  abstractions.
- Separate plugins/modules by feature; do not mix unrelated features.
- Separate interface modules from implementation modules.
- Keep tests in a separate module.
- Strictly separate Runtime vs Editor; Editor dependencies only in Editor
  modules.
- Prefer components over deep inheritance; avoid dynamic-cast heavy designs; use
  UInterface.
- Avoid global state and service-locator patterns; inject dependencies via
  interfaces/factories.
- Use Gameplay Tags for extensible categories; consider GAS for generic
  buffs/debuffs.
- Use UDeveloperSettings for config; define cvars in module namespaces; avoid
  hardcoded values.
- Save with USaveGame and versioning when needed.

## C++/UE coding practices

- Do not use C++ exceptions.
- Prefer forward declarations and IWYU; include \*.generated.h last.
- Use explicit return types; avoid template/macro overuse.
- Use FName for identifiers, FText for UI, FString for transient text.
- Compare floats with tolerances (e.g., KINDA_SMALL_NUMBER).
- UObject references use UPROPERTY with TObjectPtr; non-UObject use
  TUniquePtr/TSharedPtr; non-null shared refs use TSharedRef.
- Avoid raw pointers; use TWeakObjectPtr/TWeakPtr for non-owning references.
- Create UObjects with NewObject/CreateDefaultSubobject, not direct
  constructors.
- Avoid GWorld/GEngine direct access; use GetWorld/WorldContext.

## Error handling and logging

- Use checkf only for fatal invariants; use ensureMsgf for recoverable
  anomalies.
- Log errors/warnings and return early; do not use LogTemp.
- Define module-specific log categories; keep logging appropriate for Shipping.
- Failure logs must include actionable context (e.g., url/path/status).
  Debug-only noise goes to `VeryVerbose`.

## Networking and security

- Server authoritative: validate client inputs; do not trust client state.
- Use appropriate RPC reliability; implement replication correctly; optimize
  with FastArray/COND\_\*/dormancy.
- Never ship secrets to clients; store keys in secure storage; gate dev-only
  features in editor/shipping guards.

## Performance and async

- Minimize Tick; prefer timers/delegates; avoid blocking I/O on the game thread.
- Pre-allocate hot paths; profile with TRACE_CPUPROFILER.
- Use AssetManager and soft references for assets; avoid hardcoded paths.
- Use Enhanced Input and centralized mapping contexts.
- Use UE5Coro for async; keep UObject access on the game thread and marshal
  results back.
- Avoid synchronous subprocess execution (e.g., `FPlatformProcess::ExecProcess`)
  in gameplay/async paths; prefer UE-native APIs (e.g., `FHttpModule`) and keep
  the game thread responsive.
- When dynamically creating components/layers and binding delegates, ensure
  teardown runs on all failure paths and on EndPlay/cancel.

## Build and tests

- UnrealBuildRunTestScript and UE5Coro are required when relevant; install
  before use.
- Build with UnrealBuildRunTestScript\\Fire-Build.ps1.bat --no-pause (add
  configuration/platform as needed).
- Add and run AutomationSpec/Functional Tests for important features.
- Run tests after relevant changes; record repro steps for issues.
- Use Fire-BuildAndTest.ps1.bat --no-pause -TestFilter "filter-pattern"
  for test runs.
- Tests should avoid Engine internal/private headers/APIs; prefer public `U*`
  APIs. If internals are unavoidable, isolate and document why.
