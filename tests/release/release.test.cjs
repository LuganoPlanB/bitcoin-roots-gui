'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const {createHash} = require('node:crypto');
const {mkdirSync, mkdtempSync, writeFileSync, rmSync} = require('node:fs');
const {tmpdir} = require('node:os');
const {join} = require('node:path');
const {prepare, finalize, publish, assertCandidate, tagCommit, validateVersion} =
  require('../../.github/scripts/release.cjs');

const sha = 'a'.repeat(40);
const oldSha = 'b'.repeat(40);
const missing = () => { throw Object.assign(new Error('Missing'), {status: 404}); };

const packages = [
  ['linux', 'ci.yml', 'bitcoin-roots-gui-linux-musl-x86_64', '.tar.gz'],
  ['windows', 'windows.yml', 'bitcoin-roots-gui-windows-x86_64', '.zip'],
  ['macos', 'macos.yml', 'bitcoin-roots-gui-macos-arm64', '.zip'],
];

function fixture(t) {
  const outputs = {};
  const writes = [];
  const state = {tag: null, release: null, assets: [], head: sha, runs: {}, artifacts: {}};
  const context = {repo: {owner: 'fixture', repo: 'gui'}, ref: 'refs/heads/main', sha,
    runId: 3000, serverUrl: 'https://github.test'};
  const core = {setOutput: (key, value) => { outputs[key] = value; }, info: () => {},
    summary: {addHeading() { return this; }, addTable() { return this; }, async write() {}}};
  const assetRoot = mkdtempSync(join(tmpdir(), 'roots-gui-release-test-'));
  t.after(() => rmSync(assetRoot, {recursive: true, force: true}));
  const builds = Object.fromEntries(packages.map(([platform, workflow, name, extension], i) => {
    const run = 1000 + i;
    const artifact = 2000 + i;
    const url = 'https://github.test/actions/runs/' + run;
    state.runs[workflow] = [{id: run, head_sha: sha, head_branch: 'main', event: 'push',
      run_number: 1, status: 'completed', conclusion: 'success', html_url: url}];
    state.artifacts[run] = [{id: artifact, name, expired: false, size_in_bytes: 100}];
    mkdirSync(join(assetRoot, platform));
    writeFileSync(join(assetRoot, platform, name + extension), platform);
    return [platform, {run, artifact, url}];
  }));
  const github = {paginate: async (request, args) => (await request(args)).data, rest: {
    actions: {
      listWorkflowRuns: async args => {
        assert.equal(args.head_sha, sha);
        assert.equal(args.branch, 'main');
        assert.equal(args.event, 'push');
        return {data: {workflow_runs: state.runs[args.workflow_id]}};
      },
      listWorkflowRunArtifacts: async ({run_id, name}) => {
        assert.equal(name, packages[run_id - 1000][2]);
        return {data: {artifacts: state.artifacts[run_id]}};
      },
    },
    repos: {
      getBranch: async () => ({data: {commit: {sha: state.head}}}),
      listReleases: async () => ({data: state.release ? [state.release] : []}),
      listReleaseAssets: async () => ({data: state.assets}),
      createRelease: async args => {
        writes.push(['draft', args]);
        state.release = {...args, id: 100, html_url: 'release'};
        return {data: state.release};
      },
      uploadReleaseAsset: async args => {
        writes.push(['asset', args]);
        state.assets.push({
          name: args.name, size: args.data.length, state: 'uploaded',
          digest: 'sha256:' + createHash('sha256').update(args.data).digest('hex'),
        });
      },
      updateRelease: async args => {
        writes.push(['publish', args]);
        Object.assign(state.release, args);
        return {data: state.release};
      },
    },
    git: {
      getRef: async () => state.tag ? {data: {object: state.tag}} : missing(),
      getTag: async () => ({data: {object: {type: 'commit', sha}}}),
      createRef: async args => {
        writes.push(['tag', args]);
        state.tag = {type: 'commit', sha: args.sha};
        return {data: {}};
      },
    },
  }};
  return {github, context, core, outputs, writes, state, assetRoot, builds, version: 'v0.1.0',
    artifacts: ['3001', '3002', '3003']};
}

test('bootstrap preview selects v0.1.0 and performs no writes', async t => {
  const f = fixture(t);
  await prepare({...f, readGit: () => ''});
  assert.equal(f.outputs.base, '');
  await finalize({...f, base: '', publish: false});
  assert.deepEqual(f.outputs, {
    base: '', base_sha: '', version: 'v0.1.0', build_version: '0.1.0', ready: 'true', builds: JSON.stringify(f.builds),
  });
  assert.deepEqual(f.writes, []);
});

test('base selection uses merged numeric tag order and ignores unstable tags', async t => {
  const f = fixture(t);
  await prepare({...f, readGit: args => {
    if (args[0] === 'tag') {
      assert.deepEqual(args, ['tag', '--merged', sha, '--sort=-version:refname']);
      return 'v99.0.0-rc.1\nv01.0.0\nv2.10.0\nv2.9.0';
    }
    assert.equal(args[1], 'refs/tags/v2.10.0^{commit}');
    return oldSha;
  }});
  assert.equal(f.outputs.base, 'v2.10.0');
  assert.equal(f.outputs.base_sha, oldSha);
});

test('unchanged version skips docs-only commits but allows recovery at tagged commit', async t => {
  const f = fixture(t);
  await finalize({...f, base: 'v0.1.0', next: 'v0.1.0', baseSha: oldSha});
  assert.equal(f.outputs.ready, 'false');
  await finalize({...f, base: 'v0.1.0', next: 'v0.1.0', baseSha: sha});
  assert.equal(f.outputs.ready, 'true');
  await finalize({...f, base: 'v0.1.0', next: 'v0.2.0', baseSha: oldSha});
  assert.equal(f.outputs.version, 'v0.2.0');
  assert.equal(f.outputs.build_version, '0.2.0');
  assert.equal(f.outputs.ready, 'true');
});

test('rejects non-main, malformed and stale candidates before writing', async t => {
  for (const context of [{ref: 'refs/heads/topic'}, {sha: 'main'}]) {
    const f = fixture(t);
    Object.assign(f.context, context);
    await assert.rejects(publish(f), /exact main commit/);
    assert.deepEqual(f.writes, []);
  }
  const f = fixture(t);
  f.github.rest.repos.getBranch = async () => ({data: {commit: {sha: oldSha}}});
  await assert.rejects(publish(f), /no longer the main tip/);
  assert.deepEqual(f.writes, []);
});

test('requires the latest successful exact main push for every platform', async t => {
  for (const [, workflow] of packages) {
    for (const status of ['queued', 'in_progress', 'completed']) {
      const f = fixture(t);
      const [previous] = f.state.runs[workflow];
      f.state.runs[workflow].push({...previous, run_number: 2, status, conclusion: 'failure'});
      await assert.rejects(publish(f), /CI.*successfully/);
      assert.deepEqual(f.writes, []);
    }
    const f = fixture(t);
    const [previous] = f.state.runs[workflow];
    f.state.runs[workflow] = [
      {...previous, head_sha: oldSha}, {...previous, head_branch: 'topic'}, {...previous, event: 'pull_request'},
    ];
    await assert.rejects(publish(f), /CI.*successfully/);
    assert.deepEqual(f.writes, []);
  }
});

test('rejects missing, expired, empty or ambiguous build artifacts before writes', async t => {
  for (const change of [
    () => [], ([a]) => [{...a, expired: true}],
    ([a]) => [{...a, size_in_bytes: 0}], ([a]) => [a, {...a, id: a.id + 50}],
  ]) {
    const f = fixture(t);
    f.state.artifacts[1002] = change(f.state.artifacts[1002]);
    await assert.rejects(publish(f), /macOS ARM64 package/);
    assert.deepEqual(f.writes, []);
  }
});

test('publishes all three exact-commit archives and checksums before exposing the release', async t => {
  const f = fixture(t);
  await publish(f);
  assert.deepEqual(f.writes.map(w => w[0]), ['draft', 'asset', 'asset', 'asset', 'asset', 'tag', 'publish']);
  const draft = f.writes[0][1];
  assert.equal(draft.target_commitish, sha);
  assert.equal(draft.generate_release_notes, true);
  assert.equal(draft.draft, true);
  assert.equal(draft.prerelease, false);
  assert.ok(draft.body.includes(`<!-- roots-gui-assets ${sha} 3001 3002 3003 -->`));
  assert.deepEqual(f.writes[5][1], {...f.context.repo, sha, ref: 'refs/tags/v0.1.0'});
  assert.equal(f.writes[6][1].draft, false);
  const names = packages.map(([, , name, extension]) => name + extension);
  assert.deepEqual(f.writes.slice(1, 5).map(([, a]) => a.name), [...names, 'SHA256SUMS']);
  const checksums = names.map((name, i) =>
    createHash('sha256').update(packages[i][0]).digest('hex') + '  ' + name + '\n').join('');
  assert.equal(f.writes[4][1].data.toString(), checksums);
  await publish(f);
  assert.equal(f.writes.length, 7, 'rerun must not mutate an existing release');
});

test('recovers an interrupted workflow draft without reuploading verified assets', async t => {
  const f = fixture(t);
  const upload = f.github.rest.repos.uploadReleaseAsset;
  f.github.rest.repos.uploadReleaseAsset = args => {
    if (f.state.assets.length === 1) throw new Error('Upload interrupted');
    return upload(args);
  };
  await assert.rejects(publish(f), /Upload interrupted/);
  assert.equal(f.state.release.draft, true);
  assert.equal(f.state.tag, null);
  f.github.rest.repos.uploadReleaseAsset = upload;
  await assert.rejects(publish({...f, artifacts: ['4001', '4002', '4003']}), /draft release requires manual inspection/);
  await publish(f);
  assert.deepEqual(f.writes.map(w => w[0]), ['draft', 'asset', 'asset', 'asset', 'asset', 'tag', 'publish']);
});

test('any missing local archive or changed selection stops new writes', async t => {
  const f = fixture(t);
  rmSync(join(f.assetRoot, 'windows', packages[1][2] + '.zip'));
  await assert.rejects(publish(f), /Missing or empty release file/);
  assert.deepEqual(f.writes, []);
  const g = fixture(t);
  g.state.artifacts[1000][0].id++;
  await assert.rejects(publish(g), /selected build artifacts have changed/);
  assert.deepEqual(g.writes, []);
});

test('keeps the complete draft private when main or artifact selection changes during upload', async t => {
  for (const advance of [f => { f.state.head = oldSha; }, f => { f.state.artifacts[1000][0].id++; }]) {
    const f = fixture(t);
    const upload = f.github.rest.repos.uploadReleaseAsset;
    f.github.rest.repos.uploadReleaseAsset = async args => {
      await upload(args);
      if (f.state.assets.length === 4) advance(f);
    };
    await assert.rejects(publish(f), /no longer the main tip|selected build artifacts have changed/);
    assert.equal(f.state.release.draft, true);
    assert.equal(f.state.tag, null);
    assert.deepEqual(f.writes.map(w => w[0]), ['draft', 'asset', 'asset', 'asset', 'asset']);
  }
});

test('refuses to overwrite a mismatching draft asset or publish incomplete uploads', async t => {
  const f = fixture(t);
  const upload = f.github.rest.repos.uploadReleaseAsset;
  f.github.rest.repos.uploadReleaseAsset = args => {
    if (f.state.assets.length === 1) throw new Error('Upload interrupted');
    return upload(args);
  };
  await assert.rejects(publish(f), /Upload interrupted/);
  f.state.assets[0].digest = 'sha256:bad';
  f.writes.length = 0;
  await assert.rejects(publish(f), /Draft asset requires manual inspection/);
  assert.deepEqual(f.writes, []);
  const g = fixture(t);
  g.github.rest.repos.uploadReleaseAsset = async () => {};
  await assert.rejects(publish(g), /missing one or more verified assets/);
  assert.deepEqual(g.writes.map(w => w[0]), ['draft']);
});

test('matching dangling annotated tag can finish a release', async t => {
  const f = fixture(t);
  f.state.tag = {type: 'tag', sha: oldSha};
  assert.equal(await tagCommit(f), sha);
  await publish(f);
  assert.deepEqual(f.writes.map(w => w[0]), ['draft', 'asset', 'asset', 'asset', 'asset', 'publish']);
});

test('conflicting tag, draft or prerelease stops without overwrites', async t => {
  const f = fixture(t);
  f.state.tag = {type: 'commit', sha: oldSha};
  await assert.rejects(publish(f), /different commit/);
  for (const release of [{draft: true}, {prerelease: true}]) {
    f.state.tag = {type: 'commit', sha};
    f.state.release = {tag_name: f.version, ...release};
    await assert.rejects(publish(f), /manual inspection/);
  }
  assert.deepEqual(f.writes, []);
});

test('tag creation race accepts only a matching newly created tag', async t => {
  for (const racedSha of [sha, oldSha, null]) {
    const f = fixture(t);
    f.github.rest.git.createRef = async () => {
      f.state.tag = racedSha ? {type: 'commit', sha: racedSha} : null;
      throw Object.assign(new Error('Already exists'), {status: 422});
    };
    if (racedSha === sha) {
      await publish(f);
      assert.deepEqual(f.writes.map(w => w[0]), ['draft', 'asset', 'asset', 'asset', 'asset', 'publish']);
    } else {
      await assert.rejects(publish(f));
      assert.deepEqual(f.writes.map(w => w[0]), ['draft', 'asset', 'asset', 'asset', 'asset']);
    }
  }
});

test('API authorization errors are not treated as missing refs', async t => {
  const f = fixture(t);
  f.github.rest.git.getRef = async () => { throw Object.assign(new Error('Forbidden'), {status: 403}); };
  await assert.rejects(publish(f), /Forbidden/);
  assert.deepEqual(f.writes, []);
});

test('annotated tag chains are bounded and must terminate at a commit', async t => {
  const f = fixture(t);
  f.state.tag = {type: 'tag', sha: oldSha};
  let count = 0;
  f.github.rest.git.getTag = async () => {
    count++;
    return {data: {object: f.state.tag}};
  };
  await assert.rejects(tagCommit(f), /does not resolve/);
  assert.equal(count, 8);
});

test('rejects malformed versions before release writes', async t => {
  for (const version of ['', '0.1.0', 'v01.0.0', 'v0.1.0-rc.1', 'v0.1.0\n']) {
    assert.throws(() => validateVersion(version));
    const f = fixture(t);
    await assert.rejects(publish({...f, version}), /Invalid stable release tag/);
    assert.deepEqual(f.writes, []);
  }
});

test('rejects missing or duplicate release package IDs before writes', async t => {
  const f = fixture(t);
  for (const artifacts of [[], ['3001', '3002', ''], ['3001', '3002', '3002']]) {
    await assert.rejects(publish({...f, artifacts}), /three distinct versioned package artifacts/);
  }
  assert.deepEqual(f.writes, []);
});
