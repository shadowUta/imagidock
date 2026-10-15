import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createServer } from 'node:net';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { pipeName, sendRpc } from '../dist/ipc.js';

const root = resolve('..', '.local', 'tests', 'mcp-bridge-' + process.pid);
await mkdir(root, { recursive: true });
const pipe = await pipeName(root);
const calls = [];
const service = createServer(socket => {
  let bytes = Buffer.alloc(0);
  socket.on('data', chunk => {
    bytes = Buffer.concat([bytes, chunk]);
    let index;
    while ((index = bytes.indexOf(10)) >= 0) {
      const request = JSON.parse(bytes.subarray(0, index).toString('utf8'));
      bytes = bytes.subarray(index + 1);
      calls.push(request);
      let result;
      if (request.method === 'library.info') result = { library_id: 'test-library', revision: 1, counts: {} };
      else if (request.method === 'asset.list') result = { items: [{ id: 'asset-1', name: '中文材质.png', revision: 3 }], total: 1, revision: 1 };
      else if (request.method === 'asset.import') result = { job_id: 'job-1' };
      else if (request.method === 'job.get') result = { id: 'job-1', state: 'completed', progress: 1 };
      else if (request.method === 'asset.update') {
        socket.write(JSON.stringify({ id: request.id, error: { code: 'conflict', message: 'Asset changed' } }) + '\n');
        continue;
      } else result = {};
      const line = Buffer.from(JSON.stringify({ id: request.id, result }) + '\n', 'utf8');
      // Exercise UTF-8 framing across arbitrary byte boundaries.
      socket.write(line.subarray(0, 7));
      socket.write(line.subarray(7));
    }
  });
});
await new Promise(resolve => service.listen(pipe, resolve));
await writeFile(resolve(root, 'config.json'), JSON.stringify({ libraries: [{ path: root }] }));

test('official SDK stdio client discovers schemas and drives library pipe', async () => {
  const client = new Client({ name: 'imagidock-test', version: '1.0.0' });
  const transport = new StdioClientTransport({ command: process.execPath,
    args: [resolve('dist/index.js'), '--config', resolve(root, 'config.json')], stderr: 'pipe' });
  try {
    await client.connect(transport);
    const { tools } = await client.listTools();
    assert.equal(tools.length, 35);
    assert.ok(tools.find(tool => tool.name === 'relation_set').inputSchema.required.includes('revision'));
    assert.ok(tools.filter(tool => tool.name !== 'library_list').every(tool => tool.inputSchema.required.includes('library_id')));
    const listed = await client.callTool({ name: 'library_list', arguments: {} });
    assert.equal(listed.structuredContent.libraries[0].library_id, 'test-library');
    const assets = await client.callTool({ name: 'asset_list', arguments: { library_id: 'test-library', tags: ['蓝', '绿'] } });
    assert.equal(assets.structuredContent.items[0].name, '中文材质.png');
    const started = await client.callTool({ name: 'asset_import', arguments: { library_id: 'test-library', paths: [resolve(root, 'image.png')] } });
    assert.equal(started.structuredContent.job_id, 'job-1');
    const job = await client.callTool({ name: 'job_get', arguments: { library_id: 'test-library', job_id: 'job-1' } });
    assert.equal(job.structuredContent.state, 'completed');
    const conflict = await client.callTool({ name: 'asset_update', arguments: { library_id: 'test-library', asset_id: 'asset-1', revision: 2, values: { name: 'new' } } });
    assert.equal(conflict.isError, true);
    assert.equal(JSON.parse(conflict.content[0].text).code, 'conflict');
    const unknown = await client.callTool({ name: 'asset_list', arguments: { library_id: 'not-configured' } });
    assert.equal(unknown.isError, true);
    assert.equal(calls.filter(call => call.method === 'asset.update').length, 1, 'writes are not replayed');
    const invalid = await client.callTool({ name: 'relation_set', arguments: { library_id: 'test-library', asset_id: 'asset-1', items: [] } });
    assert.equal(invalid.isError, true, 'revision is mandatory');
  } finally {
    await client.close();
  }
});

test('IPC rejects wrong response ID', async () => {
  const wrongPipe = pipe + '-wrong';
  const wrong = createServer(socket => socket.once('data', () => socket.end('{"id":"wrong","result":{}}\n')));
  await new Promise(resolve => wrong.listen(wrongPipe, resolve));
  try { await assert.rejects(sendRpc(wrongPipe, 'library.info', {}), { code: 'id_mismatch' }); }
  finally { await new Promise(resolve => wrong.close(resolve)); }
});

test.after(async () => { await new Promise(resolve => service.close(resolve)); });
