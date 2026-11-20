'use strict';

const {execFileSync} = require('node:child_process');
const stableTag = /^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
const commitSha = /^[a-f0-9]{40}$/;

function validateVersion(version) {
  if (!stableTag.test(version)) throw new Error(`Invalid stable release tag: ${version}`);
  return version;
}

async function optional(request) {
  try { return (await request()).data; }
  catch (error) {
    if (error.status === 404) return null;
    throw error;
  }
}

async function assertCandidate({github, context}) {
  if (context.ref !== 'refs/heads/main' || !commitSha.test(context.sha)) {
    throw new Error('Release must run on an exact main commit');
  }
  const {data: branch} = await github.rest.repos.getBranch({...context.repo, branch: 'main'});
  if (branch.commit.sha !== context.sha) throw new Error('Candidate is no longer the main tip; dispatch again');
  const {data} = await github.rest.actions.listWorkflowRuns({
    ...context.repo, workflow_id: 'ci.yml', branch: 'main', event: 'push',
    head_sha: context.sha, per_page: 100,
  });
  const runs = data.workflow_runs.filter(run => run.head_sha === context.sha && run.event === 'push');
  runs.sort((a, b) => b.run_number - a.run_number);
  const run = runs[0];
  if (!run || run.status !== 'completed' || run.conclusion !== 'success') {
    throw new Error('Latest Standalone GUI push CI for this commit must complete successfully');
  }
  return run.html_url;
}

function git(args) {
  return execFileSync('git', args, {encoding: 'utf8'}).trim();
}

async function prepare({github, context, core, readGit = git}) {
  const ci = await assertCandidate({github, context});
  // Git sorts numerically; ignore prereleases, malformed tags and tags off this history.
  const base = readGit(['tag', '--merged', context.sha, '--sort=-version:refname'])
    .split('\n').find(tag => stableTag.test(tag)) || '';
  const baseSha = base ? readGit(['rev-parse', `refs/tags/${base}^{commit}`]) : '';
  core.setOutput('base', base);
  core.setOutput('base_sha', baseSha);
  core.info(`Candidate ${context.sha}; CI ${ci}; base ${base || '(initial v0.1.0)'}`);
}

function finalize({context, core, base, baseSha, next, publish}) {
  const version = validateVersion(base ? next : 'v0.1.0');
  const ready = !base || version !== base || baseSha === context.sha;
  core.setOutput('version', version);
  core.setOutput('ready', String(ready));
  return core.summary.addHeading(publish ? 'Release requested' : 'Release preview')
    .addTable([
      [{data: 'Candidate', header: true}, context.sha],
      [{data: 'Version', header: true}, version],
      [{data: 'Action', header: true}, ready
        ? (publish ? 'Publish or reconcile existing release' : 'Preview only; no tag or release created')
        : 'No version bump; no release'],
    ]).write();
}

async function tagCommit({github, context, version}) {
  const ref = await optional(() => github.rest.git.getRef({...context.repo, ref: `tags/${version}`}));
  if (!ref) return null;
  let object = ref.object;
  for (let depth = 0; depth < 8 && object.type === 'tag'; depth++) {
    const {data} = await github.rest.git.getTag({...context.repo, tag_sha: object.sha});
    object = data.object;
  }
  if (object.type !== 'commit') throw new Error('Release tag does not resolve to a commit');
  return object.sha;
}

async function publish({github, context, core, version}) {
  validateVersion(version);
  await assertCandidate({github, context});
  const input = {github, context, version};
  const assertTag = sha => {
    if (sha && sha !== context.sha) throw new Error('Existing release tag points to a different commit');
  };
  let sha = await tagCommit(input);
  assertTag(sha);
  const release = await optional(() => github.rest.repos.getReleaseByTag({...context.repo, tag: version}));
  if (release) {
    if (!sha || release.draft || release.prerelease) throw new Error('Existing release requires manual inspection');
    core.info(`Release already exists: ${release.html_url}`);
    return;
  }
  if (!sha) {
    try {
      await github.rest.git.createRef({...context.repo, ref: `refs/tags/${version}`, sha: context.sha});
    } catch (error) {
      if (![409, 422].includes(error.status)) throw error;
      // A competing creator is acceptable only if the exact ref now matches.
      sha = await tagCommit(input);
      if (!sha) throw error;
      assertTag(sha);
    }
  }
  sha = await tagCommit(input);
  if (sha !== context.sha) throw new Error('Release tag changed before publication');
  const {data} = await github.rest.repos.createRelease({
    ...context.repo, tag_name: version, target_commitish: context.sha, name: version,
    generate_release_notes: true, draft: false, prerelease: false, make_latest: 'true',
  });
  core.info(`Published source release: ${data.html_url}`);
}

module.exports = {prepare, finalize, publish, assertCandidate, tagCommit, validateVersion};
