# Desktop releases

Run the **Release** workflow from GitHub Actions on `main` after the
**Standalone GUI**, **Native Windows GUI**, and **Apple Silicon GUI** push
workflows succeed. Leave **publish** unchecked to preview the version and the
three selected build runs in the job summary. Check it to publish the release.

```sh
gh workflow run release.yml --ref main -f publish=false
# After reviewing the preview:
gh workflow run release.yml --ref main -f publish=true
```

GitHub generates source archives for the tag. The workflow also attaches
these packages and a `SHA256SUMS` file that covers all three.

| Download | Contents |
| --- | --- |
| `bitcoin-roots-gui-linux-musl-x86_64.tar.gz` | Static SDL3/X11 build with fonts and dependency notices. Extract and run `bin/roots-gui`. |
| `bitcoin-roots-gui-windows-x86_64.zip` | Native x64 package with runtime DLLs, fonts, and notices. Extract and run `bin/roots-gui.exe`. |
| `bitcoin-roots-gui-macos-arm64.zip` | Ad hoc signed `Bitcoin Roots.app` for Apple Silicon on macOS 15 or later, with dependency notices. |

Each run binds version calculation, packages, and tagging to its exact
`main` commit. Package selection uses the latest successful result and
artifact IDs from each platform's latest push run for that commit.
Publication checks these IDs and the branch tip again after uploading.
If a package expired, rerun its build workflow for the same commit. If
`main` advances, dispatch again after the new commit's CI succeeds.

The workflow stages assets on a draft and publishes it after GitHub reports
the expected size and SHA-256 for each upload. If an upload is interrupted,
a rerun can resume the workflow's draft when its commit, selected artifacts,
and previously uploaded bytes still match. Conflicting tags, drafts, and
assets stop for manual inspection. A matching published release is a no-op.
Releases run only through an explicit publish dispatch.

The first release is `v0.1.0`. Subsequent releases use
[`ietf-tools/semver-action`](https://github.com/ietf-tools/semver-action), pinned
to v1.12.0's immutable commit, starting from the highest stable
`vMAJOR.MINOR.PATCH` tag reachable from the candidate. Tags outside that history
and prerelease tags are ignored. Use conventional commit messages:

- `feat:` or `feature:` increments the minor version.
- `fix:`, `bugfix:`, `perf:`, `refactor:`, `test:` or `tests:` increments patch.
- A conventional breaking change (`feat!:` or a `BREAKING CHANGE:` footer)
  increments major, including before 1.0.
- Docs/chore-only changes without a breaking change create no new release.

The Linux release build uses the pinned
[Dyne Musl toolchain source](https://github.com/dyne/musl/tree/v2.7.0)
and the X11 and SDL3 build instructions in the [build guide](build.md#static-sdl3-build).
The workflow creates lightweight tags. Maintainer commit signing remains
required. The preview job has read-only access, and only the explicit
publication job can write release refs and assets.

Release policy tests need Node.js 24 (application builds do not).

```sh
node --test tests/release/release.test.cjs
```
