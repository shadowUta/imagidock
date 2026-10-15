// Runs against a real compiled C++ service, in a new workspace-local test library.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { spawn } from 'node:child_process';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';

const executable = process.argv[2];
if (!executable) throw new Error('Usage: node test/live-service.mjs <service.exe>');
const root = resolve('..', '.local', 'tests', 'mcp-live-' + process.pid);
await mkdir(root, { recursive: true });
// Original 1x1 PNG fixture, no third-party artwork.
const image = resolve(root, '中文.png');
await writeFile(image, Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aM1sAAAAASUVORK5CYII=', 'base64'));
await mkdir(resolve(root, 'library'), { recursive: true });
await writeFile(resolve(root, 'config.json'), JSON.stringify({ libraries: [{ path: resolve(root, 'library') }] }));
const service = spawn(resolve(executable), ['--library', resolve(root, 'library')], { windowsHide: true, stdio: 'ignore' });
await new Promise((accept, reject) => { service.once('spawn', accept); service.once('error', reject); });
await new Promise(resolve => setTimeout(resolve, 400));
const client = new Client({ name: 'imagidock-live-test', version: '1.0.0' });
const transport = new StdioClientTransport({ command: process.execPath,
  args: [resolve('dist/index.js'), '--config', resolve(root, 'config.json')], stderr: 'inherit' });
const call = async (name, args) => {
  const result = await client.callTool({ name, arguments: args });
  if (result.isError) throw new Error(JSON.stringify(result.content));
  return result.structuredContent ?? JSON.parse(result.content[0].text);
};
try {
  await client.connect(transport);
  const listed = await call('library_list', {});
  assert.equal(listed.errors.length, 0, JSON.stringify(listed.errors));
  const library_id = listed.libraries[0].library_id;
  const started = await call('asset_import', { library_id, paths: [image], backup: false });
  assert.ok(started.job_id);
  let job;
  for (let i = 0; i < 100; ++i) {
    job = await call('job_get', { library_id, job_id: started.job_id });
    if (['completed', 'failed', 'cancelled'].includes(job.state)) break;
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  assert.equal(job.state, 'completed', JSON.stringify(job));
  const assets = await call('asset_list', { library_id });
  assert.equal(assets.total, 1);
  const asset = await call('asset_get', { library_id, asset_id: assets.items[0].id });
  const updated = await call('asset_update', { library_id, asset_id: asset.id, revision: asset.revision,
    values: { tags: ['MCP验证', '测试'], favorite: true } });
  assert.ok(updated.tags.includes('MCP验证'));
  const conflict = await client.callTool({ name: 'asset_update', arguments: { library_id, asset_id: asset.id,
    revision: asset.revision, values: { name: 'stale-write' } } });
  assert.equal(conflict.isError, true);
  const queried = await call('asset_list', { library_id, tags: ['无匹配', 'MCP验证'], status: 'favorite' });
  assert.equal(queried.total, 1);
  console.log(JSON.stringify({ ok: true, library_id, job_id: started.job_id, assertions: 8, root }, null, 2));
} finally {
  await client.close();
  service.kill();
  if (service.exitCode === null) await new Promise(resolve => service.once('exit', resolve));
}
