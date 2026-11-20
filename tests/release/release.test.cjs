'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const {prepare, finalize, publish, assertCandidate, tagCommit, validateVersion} =
  require('../../.github/scripts/release.cjs');

const sha = 'a'.repeat(40);
const oldSha = 'b'.repeat(40);
const missing = () => { throw Object.assign(new Error('Missing'), {status: 404}); };

function fixture() {
  const outputs = {};
  const writes = [];
  const state = {tag: null, release: null};
  const context = {repo: {owner: 'fixture', repo: 'gui'}, ref: 'refs/heads/main', sha};
  const core = {setOutput: (key, value) => { outputs[key] = value; }, info: () => {},
    summary: {addHeading() { return this; }, addTable() { return this; }, async write() {}}};
  const github = {rest: {
    actions: {listWorkflowRuns: async () => ({data: {workflow_runs: [
      {head_sha: sha, event: 'push', run_number: 1, status: 'completed', conclusion: 'success', html_url: 'ci'},
    ]}})},
    repos: {
      getBranch: async () => ({data: {commit: {sha}}}),
      getReleaseByTag: async () => state.release ? {data: state.release} : missing(),
      createRelease: async args => {
        writes.push(['release', args]);
        state.release = {html_url: 'release', draft: false, prerelease: false};
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
  return {github, context, core, outputs, writes, state, version: 'v0.1.0'};
}

test('bootstrap preview selects v0.1.0 and performs no writes', async () => {
  const f = fixture();
  await prepare({...f, readGit: () => ''});
  assert.equal(f.outputs.base, '');
  await finalize({...f, base: '', publish: false});
  assert.deepEqual(f.outputs, {base: '', base_sha: '', version: 'v0.1.0', ready: 'true'});
  assert.deepEqual(f.writes, []);
});

test('base selection uses merged numeric tag order and ignores unstable tags', async () => {
  const f = fixture();
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

test('unchanged version skips docs-only commits but allows recovery at tagged commit', async () => {
  const f = fixture();
  await finalize({...f, base: 'v0.1.0', next: 'v0.1.0', baseSha: oldSha});
  assert.equal(f.outputs.ready, 'false');
  await finalize({...f, base: 'v0.1.0', next: 'v0.1.0', baseSha: sha});
  assert.equal(f.outputs.ready, 'true');
  await finalize({...f, base: 'v0.1.0', next: 'v0.2.0', baseSha: oldSha});
  assert.equal(f.outputs.version, 'v0.2.0');
  assert.equal(f.outputs.ready, 'true');
});

test('rejects non-main, malformed and stale candidates before writing', async () => {
  for (const context of [{ref: 'refs/heads/topic'}, {sha: 'main'}]) {
    const f = fixture();
    Object.assign(f.context, context);
    await assert.rejects(publish(f), /exact main commit/);
    assert.deepEqual(f.writes, []);
  }
  const f = fixture();
  f.github.rest.repos.getBranch = async () => ({data: {commit: {sha: oldSha}}});
  await assert.rejects(publish(f), /no longer the main tip/);
  assert.deepEqual(f.writes, []);
});

test('requires latest exact push CI success, not an earlier passing run', async () => {
  for (const status of ['queued', 'in_progress', 'completed']) {
    const f = fixture();
    f.github.rest.actions.listWorkflowRuns = async args => {
      assert.equal(args.head_sha, sha);
      assert.equal(args.workflow_id, 'ci.yml');
      return {data: {workflow_runs: [
        {head_sha: sha, event: 'push', run_number: 1, status: 'completed', conclusion: 'success'},
        {head_sha: sha, event: 'push', run_number: 2, status, conclusion: 'failure'},
      ]}};
    };
    await assert.rejects(publish(f), /CI.*successfully/);
    assert.deepEqual(f.writes, []);
  }
  const f = fixture();
  f.github.rest.actions.listWorkflowRuns = async () => ({data: {workflow_runs: []}});
  await assert.rejects(assertCandidate(f), /CI.*successfully/);
});

test('publishes exact tested SHA with generated notes and no binaries', async () => {
  const f = fixture();
  await publish(f);
  assert.deepEqual(f.writes.map(w => w[0]), ['tag', 'release']);
  assert.equal(f.writes[0][1].sha, sha);
  assert.equal(f.writes[0][1].ref, 'refs/tags/v0.1.0');
  assert.equal(f.writes[1][1].target_commitish, sha);
  assert.equal(f.writes[1][1].generate_release_notes, true);
  assert.equal(f.writes[1][1].draft, false);
  assert.equal(f.writes[1][1].prerelease, false);
  await publish(f);
  assert.equal(f.writes.length, 2, 'rerun must not mutate an existing release');
});

test('matching dangling annotated tag can finish a release', async () => {
  const f = fixture();
  f.state.tag = {type: 'tag', sha: oldSha};
  assert.equal(await tagCommit(f), sha);
  await publish(f);
  assert.deepEqual(f.writes.map(w => w[0]), ['release']);
});

test('conflicting tag, draft or prerelease stops without overwrites', async () => {
  const f = fixture();
  f.state.tag = {type: 'commit', sha: oldSha};
  await assert.rejects(publish(f), /different commit/);
  for (const release of [{draft: true}, {prerelease: true}]) {
    f.state.tag = {type: 'commit', sha};
    f.state.release = release;
    await assert.rejects(publish(f), /manual inspection/);
  }
  assert.deepEqual(f.writes, []);
});

test('tag creation race accepts only a matching newly created tag', async () => {
  for (const racedSha of [sha, oldSha, null]) {
    const f = fixture();
    f.github.rest.git.createRef = async () => {
      f.state.tag = racedSha ? {type: 'commit', sha: racedSha} : null;
      throw Object.assign(new Error('Already exists'), {status: 422});
    };
    if (racedSha === sha) {
      await publish(f);
      assert.deepEqual(f.writes.map(w => w[0]), ['release']);
    } else {
      await assert.rejects(publish(f));
      assert.deepEqual(f.writes, []);
    }
  }
});

test('API authorization errors are not treated as missing refs', async () => {
  const f = fixture();
  f.github.rest.git.getRef = async () => { throw Object.assign(new Error('Forbidden'), {status: 403}); };
  await assert.rejects(publish(f), /Forbidden/);
  assert.deepEqual(f.writes, []);
});

test('annotated tag chains are bounded and must terminate at a commit', async () => {
  const f = fixture();
  f.state.tag = {type: 'tag', sha: oldSha};
  let count = 0;
  f.github.rest.git.getTag = async () => {
    count++;
    return {data: {object: f.state.tag}};
  };
  await assert.rejects(tagCommit(f), /does not resolve/);
  assert.equal(count, 8);
});

test('rejects malformed versions before release writes', async () => {
  for (const version of ['', '0.1.0', 'v01.0.0', 'v0.1.0-rc.1', 'v0.1.0\n']) {
    assert.throws(() => validateVersion(version));
    const f = fixture();
    await assert.rejects(publish({...f, version}), /Invalid stable release tag/);
    assert.deepEqual(f.writes, []);
  }
});
