# Project Guidance

## Working Principles

- Understand the required outcome, existing code, constraints, and acceptance
  criteria before changing anything. Apply first-principles reasoning.
- Choose the smallest complete solution. Compare additions with removal, reuse, or a
  simpler alternative; preserve correctness, useful evidence, and project rules.
- Follow settled requirements. Clarify material uncertainties before
  implementation and decide local mechanics autonomously. Report scope
  expansion and preserve unrelated behavior and user changes. Treat tentative
  designs as proposals.

## Project Scope

osvault is a lightweight modern C++ library for protected storage through
Windows Credential Manager, macOS Keychain, and Linux Secret Service. Expose
small data through a direct binary key-value interface while making native
capabilities and limits explicit.

- Preserve arbitrary binary values, including embedded zero bytes. Keep
  grouping stable and scope deletion and cleanup to the owned records.
- Manage native resources with RAII. Offer filesystem-style throwing overloads
  and overloads that report operation failures through `std::error_code`.
  Keep both paths consistent, report unsupported capabilities explicitly, and
  preserve platform differences that affect behavior.
- Use native backends by default. Add `hrantzsch/keychain` only for a concrete
  need, as an independent optional target rather than a core dependency.
- Keep the library focused on storage access. Data formats, additional
  encryption, authorization decisions, and time checks belong to callers.
- Prefer direct interfaces, standard-library naming, and minimal dependencies.
  Favor header-only integration where practical; introduce abstractions only
  for established needs.

## Engineering

Use `cpp-project-engineering` and its task-relevant references when available;
read the applicable rules before editing and check compliance before delivery.
Use `first-principles`, `planning-clarification`, `modern-cpp`, and `modern-cmake`
as needed. Repository-specific requirements take precedence. If a skill is
unavailable, state the limitation and follow repository rules and configuration.

- Follow declared standards, dependencies, formatting, and lint configuration.
  Change them only when required by the task.
- Keep ownership, lifetimes, supported inputs, and failure behavior explicit.
  Establish guarantees at their owning boundaries and rely on them downstream.
  Update affected interfaces, callers, documentation, tests, and build wiring
  together.
- Write comments, documentation, and user-visible text in concise English.
  Explain non-obvious decisions and preserve useful context.

## Build and Delivery

- Inspect the current platform, available toolchains, and CMake presets before
  selecting a configuration. Prefer applicable user presets; otherwise use
  project presets. Ask the user if the selection remains unclear after
  inspection. Use matching configure/build presets and test entry points.
- Use a suitable Debug configuration for normal iteration and initialize the
  required compiler environment. On Windows, use an x64 Visual Studio
  developer environment.
- Let presets own the toolchain, configuration, and output directories. Keep
  machine-specific settings in untracked user presets and reuse the build tree.
  Preserve standard CMake configuration defaults; explain task-required
  overrides before applying them. Keep temporary artifacts in the selected
  output area.
- Run required checks and focused validation for changed behavior, meaningful
  boundaries, and failures. Include consumer checks for public-header or package
  changes. Reuse valid evidence; repeat or broaden checks only for changes,
  failures, or unresolved questions. Do not weaken checks or change
  configurations merely to obtain a pass.
- Review the diff for necessity, rule compliance, and affected behavior. Report
  changes, actual validation, and remaining gaps concisely. Commit or push only
  when requested.
- Format commit messages as `subject: message`, with no space before the colon
  and one space after it. Keep the message neutral, factual, and clear.
