# Standalone extraction and qualifications

The initial source was imported from
[`LuganoPlanB/bitcoin-roots`, commit `4d6b6fa302379f0f203d9753c7286badd3776c2c`](https://github.com/LuganoPlanB/bitcoin-roots/tree/4d6b6fa302379f0f203d9753c7286badd3776c2c/src/roots-gui),
specifically its tracked `src/roots-gui/` subtree. The original signed history
remains in that repository. This repository starts with a signed import that
preserves file-level credits, source pins, patches, license texts and notices.
The Bitcoin node sources and parent CMake hooks are not imported.

Extraction moves the GUI to the repository root, updates staging/evidence
boundaries, removes parent-build assumptions from boundary audits, and provides
a normal CMake install, project documentation and focused CI. The application
and financial workflow behavior are preserved. The diagnostic application
version string is retained from the source snapshot; it is not a new release
tag or a support claim for an entire node distribution.

Earlier source work recorded a full configured Linux registry and installed
package qualifications. Those historical results are not validation of this
new repository layout. Current standalone builds, models, project regressions
and install smoke checks are independently exercised by its CI and documented
commands. Native/visual/regtest assurance has separate prerequisites.

Linux X11/OpenGL is the supported path. Other operating systems, Wayland,
physical camera/signer/assistive hardware, and unavailable provider integrations
remain unqualified. The earlier hostile-input/fuzz/sanitizer/threat-model
campaign was explicitly unexecuted; extraction does not establish it.

Inherited lower-severity work remains in the tab-context Lock route, password
credential provider, ambiguous file-drop routing and offline-draft proof
restoration. Consult the source and current capability ledger before extending
or describing these surfaces. The old flattened-checkout build-location
problem is addressed here by deriving the repository from its actual root.

Project code is MIT licensed. Original Bitcoin Core/Bitcoin Developers and
Plan-B Foundation copyright notices remain in `COPYING`. Vendored components,
fonts and branding retain their original licenses and attribution; see
`third_party/NOTICES.md`, `packaging/embedded-notices.txt`, and the asset notices.
