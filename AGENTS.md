# Automation & Collaboration Guide

This document defines how human maintainers and automation agents collaborate around the OBSWS-ESP32 project. Update it whenever workflows or tooling change.

## Roles

- **Maintainers**: Curate roadmap, review contributions, publish releases, and ensure documentation stays in sync (EN/JA parity).
- **Contributors**: Submit features, bug fixes, or docs that align with the roadmap and coding standards.
- **Automation Agents**: CI pipelines, formatting hooks, code-quality bots, and release scripts that assist humans while remaining opt-in and transparent.

## Workflow Expectations

1. **Issue Lifecycle**
   - Tag new issues with scope (`protocol`, `examples`, `infrastructure`, etc.) and priority.
   - Capture the OBS WebSocket request/event IDs impacted.
   - Link hardware context (ESP32 module, peripherals) when relevant.
2. **Planning**
   - Maintain a public project board grouped by OBS feature domains.
   - Keep parity between English and Japanese documentation for every milestone update.
3. **Pull Requests**
   - Describe the OBS interactions covered (request/event IDs, expected responses).
   - Include serial log snippets or unit test output for critical changes.
   - Ping maintainers only after automated checks pass.

## Coding Standards

- Follow C++17, avoiding exceptions on the hot path (prefer error codes or `std::optional`).
- Keep code comments in English.
- Adhere to a consistent formatting profile (clang-format style TBD) enforced via automation.
- Ensure JSON payload handling is memory-aware (no dynamic allocations on the main loop when avoidable).
- Focus on the Arduino toolchain; contributions targeting native ESP-IDF support should be proposed separately before implementation.

## Build & Verification

- **Tooling**: Require Arduino CLI ≥ 1.3.1 with the ESP32 core (`arduino-cli core install esp32:esp32`). Keep `arduino-cli` on the system `PATH` for CI and local scripts.
- **Config files**: Use the repository-level `arduino-cli.yaml` for global settings and the sketch-local `sketch.yaml` profiles to avoid passing `--fqbn` manually.
- **Quick compile (human check)**:
   ```powershell
   # Run from the repository workspace
   arduino-cli compile --config-file arduino-cli.yaml examples\ObsWsEsp32Connect
   ```
- **JSON build report (CI or tooling)**:
   ```powershell
   # Run from the repository workspace
   powershell -Command "arduino-cli --json compile examples\ObsWsEsp32Connect | Tee-Object -FilePath build\compile-report.json"
   ```
   The command blocks until the build completes (~30s on Windows) and emits a machine-readable report capturing memory usage, dependency metadata, and the build cache path.
- Ensure the target folder exists before redirecting (e.g., `if not exist build mkdir build`).
- **CI hint**: For multiple sketches, iterate over `examples/*` and run `arduino-cli compile --config-file arduino-cli.yaml --format json` per sketch, archiving the resulting reports as artifacts.

## Testing Strategy (TBD)

| Layer | Goal | Tooling |
| --- | --- | --- |
| Unit | Validate protocol helpers and JSON serializers | GoogleTest or Unity (decision pending) |
| Integration | Exercise OBS WebSocket handshake & commands | Dockerised OBS reference server |
| Hardware-in-the-loop | Confirm behavior on real ESP32 boards | GitHub Actions self-hosted runners or manual checklists |

> Decision pending: choose primary test framework for Arduino-friendly builds. Track this in the roadmap.

## Documentation Rules

- Always update `README.md` and `README.ja.md` together.
- Provide sample sketches with generous logging for diagnostics.
- Record breaking changes in a future `CHANGELOG.md` once releases begin.

## Automation Backlog

- [ ] Set up CI for formatting, linting, and unit tests.
- [ ] Add spell-check and translation parity checks for documentation.
- [ ] Configure release tags to auto-generate binaries/examples bundles.

## Release Checklist (draft)

1. Confirm compatibility with targeted OBS Studio and WebSocket versions.
2. Run full automated and manual test suites.
3. Update documentation (EN/JA), examples, and version metadata.
4. Publish release notes summarising supported OBS features and known gaps.
5. Tag release, submit/update the Arduino Library Manager index entry, and archive firmware bundles or PlatformIO packages.

## Communication Etiquette

- Prefer concise, actionable comments; summarise scope and impact.
- Provide translations or bilingual summaries for user-facing announcements when possible.
- Document any undocumented hacks or workarounds directly in issues/PR descriptions.

## Escalation Path

- Open a high-priority issue for blocking regressions or security concerns.
- If immediate human attention is required, reference the maintainer rotation documented in the repository wiki (TBD).

## Next Decisions to Capture

- Choose the canonical code formatter and static analysis toolchain.
- Define OBS WebSocket feature coverage tiers (core vs. optional).
- Select the unit test framework and CI provider matrix (GitHub Actions, Azure Pipelines, etc.).
