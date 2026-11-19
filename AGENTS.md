# Working on Bitcoin Roots GUI

This repository is the standalone C++20 Roots desktop GUI. Its root
`CMakeLists.txt` is the build entry point. It talks to existing Bitcoin Roots or
compatible Core nodes over authenticated RPC; it does not contain or require a
Bitcoin node source tree, Qt, or Core internal libraries.

Read `README.md`, `docs/build.md`, `docs/test-harness.md`, and the current
`docs/capability-ledger.json` before changing behavior. `docs/provenance.md`
identifies the imported source snapshot and inherited qualifications. Historical
planning text is not evidence that a feature or platform works today.

## Architecture and invariants

- `domain/`: exact amounts, typed identities, approval/workflow invariants.
- `application/` and `app/`: workspace state, use cases, recovery and orchestration.
- `services/`: scoped asynchronous node observations and explicit operations.
- `adapters/`: RPC/JSON, transaction/public-data protocols, local SQLite storage.
- `ui/`: ImGui/ImKit components, themes and wallet/transaction workspace.
- `platform/`: native host, input, accessibility, camera and desktop integration.
- `third_party/`: pinned offline sources, verification/preparation code and notices.
- `tests/`, `cmake/`, `packaging/`: executable contracts, build wiring and delivery.

Keep domain and application contracts independent of the native UI and node
implementation. Preserve exact integer money and fee handling. Treat missing,
stale or contradictory node facts as unknown, not as authorization. Keep review,
approval, signing, finalization, policy checking and broadcast separate; never
turn observation into an implicit financial action. Do not add seed/private-key
custody to this GUI or access a daemon wallet database directly.

Credential contents, wallet material and protected workspace data must not enter
logs, screenshots, checked-in fixtures or telemetry. Keep local metadata and
GUI reservations separate from daemon state. Respect cancellation, lock epochs,
identity/revision binding and worker settlement. Do not add consensus/policy
enforcement, Core/Qt links, sibling checkout dependencies or configure-time
downloads.

## Build and verify

Use CMake >=3.22, GCC >=12.1 or Clang >=17, Python >=3.10 and Ninja. Build outside
the source repository and use `-j 2`; keep build trees, caches and evidence out
of Git. Use separate directories for incompatible toolchains/configurations.

```sh
cmake -S . -B ../roots-gui-dev -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DROOTS_GUI_BUILD_TESTS=ON
cmake --build ../roots-gui-dev --target roots-gui roots_gui_model_tests -j 2
python3 -m unittest discover -s tests/project
```

The model target runs its tests. Run a focused reproducer while editing, then
the full applicable suite for the changed boundary. For broader deterministic
coverage with an explicitly configured Roots daemon binary, build
`roots_gui_applicable_tests`; it executes the configured model,
RPC, CPU interaction and boundary cases once. Check its actual selection and
report excluded native/daemon/platform cases. Native/visual/accessibility and
regtest checks need the prerequisites in the test guide. Missing capability or
an empty/all-skipped selection is not a passing qualification. Never relink a
binary while its tests are running.

For Python lint, install `requirements-dev.txt` into a virtual environment
outside the repository and run `ruff check .`. Preserve pinned third-party
sources and their licenses; avoid cosmetic changes inside embedded QR code or
archives. A dependency update needs explicit provenance, hash/notice updates
and relevant compatibility verification. Register new C/C++ sources in their
own CMake targets instead of using global flags or broad globs.

## Scope and collaboration

Preserve existing user changes and attribution. Inspect the current source and
Git diff before editing; do not infer current contracts from an old report.
Fix root causes rather than converting failures to passes. Keep routine CI
bounded; broader assurance remains separately invokable. Tests must work both
in ordinary developer shells and in an optional managed scratch environment.

Node tests must use explicitly supplied binaries, disposable regtest datadirs,
isolated credentials and owned loopback fixtures. Never use personal wallets,
live funds, the user's existing node, or external peers as test inputs. Linux
X11/OpenGL is the supported path; macOS, Windows, Wayland and physical hardware
need independent qualification before support claims.

Sign new commits with the maintainer's OpenPGP key
`5EADD53F2CD1F0B7AEEE920D25FC5C29CD528E32` (Denis Roio, jaromil@dyne.org), using
repository-local signing configuration and explicit `-S`. Verify each signature.
If that key is unavailable, leave the commit pending; do not disable signing,
substitute an unsigned commit, rewrite published history or invent attribution.
Commit signing does not authorize release signing. System changes, dependency
installation, publishing, merges, releases and public-ref changes require
their own user authorization. Keep generated evidence and coordination notes
outside the repository and report exactly what was built, executed and omitted.
