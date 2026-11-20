'use strict';

const {execFileSync} = require('node:child_process');
const {createHash} = require('node:crypto');
const {lstatSync, readFileSync} = require('node:fs');
const {join} = require('node:path');
const {isDeepStrictEqual} = require('node:util');
const stableTag = /^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
const commitSha = /^[a-f0-9]{40}$/;
const platforms = {
  linux: {workflow: 'ci.yml', label: 'Linux Musl x86-64',
    name: 'bitcoin-roots-gui-linux-musl-x86_64', extension: '.tar.gz', type: 'application/gzip'},
  windows: {workflow: 'windows.yml', label: 'Windows x64',
    name: 'bitcoin-roots-gui-windows-x86_64', extension: '.zip', type: 'application/zip'},
  macos: {workflow: 'macos.yml', label: 'macOS ARM64',
    name: 'bitcoin-roots-gui-macos-arm64', extension: '.zip', type: 'application/zip'},
};

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
  const builds = await Promise.all(Object.entries(platforms).map(async ([platform, spec]) => {
    const {data} = await github.rest.actions.listWorkflowRuns({
      ...context.repo, workflow_id: spec.workflow, branch: 'main', event: 'push',
      head_sha: context.sha, per_page: 100,
    });
    const runs = data.workflow_runs.filter(run =>
      run.head_sha === context.sha && run.head_branch === 'main' && run.event === 'push');
    runs.sort((a, b) => b.run_number - a.run_number);
    const run = runs[0];
    if (!run || run.status !== 'completed' || run.conclusion !== 'success') {
      throw new Error(`Latest ${spec.label} push CI for this commit must complete successfully`);
    }
    const {data: packages} = await github.rest.actions.listWorkflowRunArtifacts({
      ...context.repo, run_id: run.id, name: spec.name, per_page: 100,
    });
    const matching = packages.artifacts.filter(artifact => artifact.name === spec.name);
    const [artifact] = matching;
    if (matching.length !== 1 || artifact.expired || artifact.size_in_bytes <= 0) {
      throw new Error(`Missing, expired, or ambiguous ${spec.label} package in run ${run.id}`);
    }
    return [platform, {run: run.id, artifact: artifact.id, url: run.html_url}];
  }));
  return Object.fromEntries(builds);
}

function git(args) {
  return execFileSync('git', args, {encoding: 'utf8'}).trim();
}

async function prepare({github, context, core, readGit = git}) {
  const builds = await assertCandidate({github, context});
  // Git sorts numerically; ignore prereleases, malformed tags and tags off this history.
  const base = readGit(['tag', '--merged', context.sha, '--sort=-version:refname'])
    .split('\n').find(tag => stableTag.test(tag)) || '';
  const baseSha = base ? readGit(['rev-parse', `refs/tags/${base}^{commit}`]) : '';
  core.setOutput('base', base);
  core.setOutput('base_sha', baseSha);
  core.setOutput('builds', JSON.stringify(builds));
  core.info(`Candidate ${context.sha}; base ${base || '(initial v0.1.0)'}`);
}

function finalize({context, core, base, baseSha, next, publish, builds}) {
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
      ...Object.entries(platforms).map(([platform, spec]) => [
        {data: spec.label, header: true},
        `[Run ${builds[platform].run}](${builds[platform].url}), artifact ${builds[platform].artifact}`,
      ]),
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

function releaseFiles(assetRoot) {
  const files = new Map();
  const add = (name, type, data) => {
    const digest = createHash('sha256').update(data).digest('hex');
    files.set(name, {type, data, digest});
  };
  for (const [platform, spec] of Object.entries(platforms)) {
    const name = spec.name + spec.extension;
    const path = join(assetRoot, platform, name);
    const stat = lstatSync(path, {throwIfNoEntry: false});
    if (!stat?.isFile() || !stat.size) throw new Error(`Missing or empty release file: ${name}`);
    add(name, spec.type, readFileSync(path));
  }
  const sums = Array.from(files, ([name, file]) => `${file.digest}  ${name}\n`).join('');
  add('SHA256SUMS', 'text/plain', Buffer.from(sums));
  return files;
}

function verifiedAssets(assets, files) {
  const verified = new Set();
  for (const asset of assets) {
    const file = files.get(asset.name);
    if (!file || verified.has(asset.name) || asset.state !== 'uploaded'
        || asset.size !== file.data.length || asset.digest !== `sha256:${file.digest}`) {
      throw new Error(`Draft asset requires manual inspection: ${asset.name}`);
    }
    verified.add(asset.name);
  }
  return verified;
}

async function publish({github, context, core, version, builds, assetRoot}) {
  validateVersion(version);
  const checkBuilds = async () => {
    if (!isDeepStrictEqual(await assertCandidate({github, context}), builds)) {
      throw new Error('The selected build artifacts have changed; dispatch again');
    }
  };
  await checkBuilds();
  const input = {github, context, version};
  const assertTag = sha => {
    if (sha && sha !== context.sha) throw new Error('Existing release tag points to a different commit');
  };
  const existingSha = await tagCommit(input);
  assertTag(existingSha);
  const matches = (await github.paginate(github.rest.repos.listReleases, {
    ...context.repo, per_page: 100,
  })).filter(release => release.tag_name === version);
  if (matches.length > 1) throw new Error('Multiple releases for this tag require manual inspection');
  let [release] = matches;
  const marker = `<!-- roots-gui-assets ${context.sha} ${Object.values(builds).map(build => build.artifact).join(' ')} -->`;
  if (release) {
    if (release.prerelease) throw new Error('Existing release requires manual inspection');
    if (!release.draft) {
      if (await tagCommit(input) !== context.sha) throw new Error('Existing release requires manual inspection');
      core.info(`Release already exists: ${release.html_url}`);
      return;
    }
    if (!release.body?.includes(marker) || (!existingSha && release.target_commitish !== context.sha)) {
      throw new Error('Existing draft release requires manual inspection');
    }
  }

  const files = releaseFiles(assetRoot);
  if (!release) {
    const sources = Object.entries(platforms).map(([platform, spec]) =>
      `- [${spec.label} build](${builds[platform].url})`).join('\n');
    ({data: release} = await github.rest.repos.createRelease({
      ...context.repo, tag_name: version, target_commitish: context.sha, name: version,
      body: `Built from \`${context.sha}\`. Verify the archives with \`SHA256SUMS\`.

${sources}

${marker}`,
      generate_release_notes: true, draft: true, prerelease: false,
    }));
  }
  const releaseArgs = {...context.repo, release_id: release.id};
  const listAssets = () => github.paginate(github.rest.repos.listReleaseAssets, {
    ...releaseArgs, per_page: 100,
  });
  const uploaded = verifiedAssets(await listAssets(), files);
  for (const [name, file] of files) {
    if (uploaded.has(name)) continue;
    await github.rest.repos.uploadReleaseAsset({
      ...releaseArgs, name, data: file.data,
      headers: {'content-type': file.type, 'content-length': file.data.length},
    });
  }
  if (verifiedAssets(await listAssets(), files).size !== files.size) {
    throw new Error('The draft release is missing one or more verified assets');
  }

  await checkBuilds();
  let sha = await tagCommit(input);
  assertTag(sha);
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
  if (await tagCommit(input) !== context.sha) throw new Error('Release tag changed before publication');
  const {data} = await github.rest.repos.updateRelease({...releaseArgs, draft: false, make_latest: 'true'});
  core.info(`Published desktop release: ${data.html_url}`);
}

module.exports = {prepare, finalize, publish, assertCandidate, tagCommit, validateVersion};
