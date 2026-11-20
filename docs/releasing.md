# Source releases

Run the **Release** workflow from GitHub Actions on `main`, after its
**Standalone GUI** push CI completes successfully. Leave **publish** unchecked
to preview the version in the job summary. Check it to create the tag and
GitHub Release with generated release notes. The equivalent CLI commands are:

```sh
gh workflow run release.yml --ref main -f publish=false
# After reviewing the preview:
gh workflow run release.yml --ref main -f publish=true
```

Each run binds the version calculation and tag to its exact `main` commit.
Publication rechecks that this commit is still the branch tip and that its
latest push CI succeeded. If `main` advances, run the workflow again after CI.
No release runs automatically on push, pull requests, or preview dispatches.

The first source release is `v0.1.0`. Subsequent releases use
[`ietf-tools/semver-action`](https://github.com/ietf-tools/semver-action), pinned
to v1.12.0's immutable commit, starting from the highest stable `vMAJOR.MINOR.PATCH`
tag reachable from the candidate. Tags outside that history and prerelease
tags are ignored. Use conventional commit messages:

- `feat:` or `feature:` increments the minor version.
- `fix:`, `bugfix:`, `perf:`, `refactor:`, `test:` or `tests:` increments patch.
- A conventional breaking change (`feat!:` or a `BREAKING CHANGE:` footer)
  increments major, including before 1.0.
- Docs/chore-only history without a breaking change creates no new release.

A rerun never moves an existing tag. A matching tag left by an interrupted run
can finish creating its release; a matching published release is a no-op.
A conflicting tag or existing draft/prerelease stops for manual inspection.

These are **source releases**, with GitHub's generated source archives. They do
not upload desktop binaries, change the inherited diagnostic version string,
or provide cryptographic release attestations. Tags created by the workflow
are lightweight tags. Maintainer commit signing remains required; no private
signing key or personal access token is stored in Actions. The version job has
read-only access; only the explicit publication job can write release refs.

Release policy tests need Node.js 24 (application builds do not):

```sh
node --test tests/release/release.test.cjs
```
