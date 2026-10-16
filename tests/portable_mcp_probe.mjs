// Actual packaged Node + SDK + stdio server + C++ service. No development modules.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash, randomUUID } from 'node:crypto';
import { createWriteStream } from 'node:fs';
import { copyFile, mkdir, readFile, realpath, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { createConnection } from 'node:net';
import { dirname, isAbsolute, join, relative, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const options = {};
for (let index = 2; index < process.argv.length; index += 2) {
  const key = process.argv[index];
  if (!['--package', '--work', '--logs'].includes(key) || !process.argv[index + 1]) throw new Error('Expected --package <directory> [--work <directory>] [--logs <directory>]');
  options[key] = process.argv[index + 1];
}
if (!options['--package']) throw new Error('--package is required; no default or old package fallback');
const inside = (parent, candidate) => {
  const rel = relative(parent, candidate);
  return Boolean(rel) && rel !== '..' && !rel.startsWith('..\\') && !rel.startsWith('../') && !isAbsolute(rel);
};
const packageRoot = await realpath(resolve(options['--package']));
assert.ok(inside(root, packageRoot), 'Package must be inside the workspace');
const node = join(packageRoot, 'runtime/node/node.exe');
assert.equal((await realpath(process.execPath)).toLowerCase(), (await realpath(node)).toLowerCase(), 'Launch with the selected package Node');
const runId = new Date().toISOString().replace(/[:.]/g, '-') + '-' + randomUUID().slice(0, 8);
const work = resolve(options['--work'] ?? join(root, '.local/portable-artifacts', 'mcp-' + runId));
const logs = resolve(options['--logs'] ?? join(root, 'logs/portable-artifacts', 'mcp-' + runId));
assert.ok(inside(join(root, '.local/portable-artifacts'), work), 'Work must stay inside the dedicated test output directory');
assert.ok(inside(join(root, 'logs'), logs), 'Logs must stay inside private logs');
assert.ok(!inside(packageRoot, work) && !inside(packageRoot, logs), 'Never write test state into the package');
await mkdir(work, { recursive: true });
await mkdir(logs, { recursive: true });
const env = Object.fromEntries(Object.entries(process.env).filter(([key]) => !/^(QT|QML|PYTHON|VIRTUAL_ENV|CONDA|NODE|NPM|UV_|PIP_)/i.test(key)));
for (const key of Object.keys(env)) if (/^(path|temp|tmp|tmpdir)$/i.test(key)) delete env[key];
Object.assign(env, { PATH: join(process.env.SystemRoot, 'System32'), TEMP: work, TMP: work, TMPDIR: work });

const packageRequire = createRequire(join(packageRoot, 'mcp/package.json'));
async function packagedImport(name) {
  const path = packageRequire.resolve(name);
  assert.ok(inside(packageRoot, await realpath(path)), 'SDK must resolve within the selected package');
  return { path, module: await import(pathToFileURL(path).href) };
}
const clientModule = await packagedImport('@modelcontextprotocol/client');
const stdioModule = await packagedImport('@modelcontextprotocol/client/stdio');
const Client = clientModule.module.Client ?? clientModule.module.default?.Client;
const StdioClientTransport = stdioModule.module.StdioClientTransport ?? stdioModule.module.default?.StdioClientTransport;
assert.ok(Client && StdioClientTransport, 'Packaged official SDK exposes client APIs');
const assertions = [];
const check = (name, condition, detail) => {
  assertions.push({ name, passed: Boolean(condition) });
  assert.ok(condition, detail ?? name);
};
const report = { ok: false, package: packageRoot, node: process.execPath,
  sdk_paths: [clientModule.path, stdioModule.path], assertions,
  scope: 'Packaged official SDK and Node; dedicated library; child environment has no development PATH/Qt/Python/Node hints.' };
const library = join(work, 'library');
await mkdir(library);
const pipe = '\\\\.\\pipe\\imagidock-' + createHash('sha256').update((await realpath(library)).replace(/\\/g, '/').toLowerCase()).digest('hex').slice(0, 24);
const config = join(work, 'config.json');
await writeFile(config, JSON.stringify({ libraries: [{ path: library }], timeout_ms: 15000 }));
const source = join(work, 'MCP原创图片.png');
await copyFile(join(root, '.local/tests/adapters/完整横向_中文.png'), source);
const originalHash = createHash('sha256').update(await readFile(source)).digest('hex');

// Only readiness and orderly shutdown use raw IPC. All asserted user operations
// pass through the actual packaged MCP SDK/stdio bridge.
function rpc(method, params = {}, timeout = 10000) {
  return new Promise((accept, reject) => {
    const id = randomUUID();
    const socket = createConnection(pipe);
    let data = Buffer.alloc(0), settled = false;
    const finish = (error, value) => {
      if (settled) return;
      settled = true; clearTimeout(timer); socket.destroy();
      error ? reject(error) : accept(value);
    };
    const timer = setTimeout(() => finish(new Error('Test IPC timed out')), timeout);
    socket.on('error', error => finish(error));
    socket.on('connect', () => socket.write(JSON.stringify({ id, method, params }) + '\n'));
    socket.on('end', () => finish(new Error('Service closed before replying')));
    socket.on('data', chunk => {
      data = Buffer.concat([data, chunk]);
      if (data.length > 8 * 1024 * 1024) return finish(new Error('Oversized response'));
      const end = data.indexOf(10);
      if (end < 0) return;
      try {
        const result = JSON.parse(data.subarray(0, end).toString('utf8'));
        assert.equal(result.id, id);
        if (result.error) throw new Error(JSON.stringify(result.error));
        finish(null, result.result);
      } catch (error) { finish(error); }
    });
  });
}
const delay = ms => new Promise(accept => setTimeout(accept, ms));
const serviceLog = createWriteStream(join(logs, 'service.log'));
const service = spawn(join(packageRoot, 'imagidock-service.exe'), ['--library', library],
  { cwd: work, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
service.stdout.pipe(serviceLog, { end: false });
service.stderr.pipe(serviceLog, { end: false });
let spawnError;
service.on('error', error => { spawnError = error; });
const exited = new Promise(accept => service.once('exit', (code, signal) => accept({ code, signal })));
let client, transport, failure;
const mcpLog = createWriteStream(join(logs, 'stdio-server.log'));
// Ensure a hung test cannot outlive the Python driver's timeout and leave its
// owned service or stdio server behind. These handles/PIDs are created here.
const watchdog = setTimeout(() => {
  failure ??= new Error('Portable MCP probe exceeded 90 seconds');
  if (service.exitCode === null && !spawnError) service.kill();
  if (transport?.pid) { try { process.kill(transport.pid); } catch {} }
}, 90000);
watchdog.unref();
try {
  let ready;
  const readyDeadline = Date.now() + 15000;
  while (Date.now() < readyDeadline) {
    if (spawnError) throw spawnError;
    if (service.exitCode !== null) throw new Error('Packaged library service exited before readiness');
    try { ready = await rpc('library.info', {}, 500); break; } catch { await delay(100); }
  }
  check('packaged_service_ready', Boolean(ready?.library_id));
  client = new Client({ name: 'imagidock-portable-artifacts-test', version: '1.0.0' });
  transport = new StdioClientTransport({ command: node,
    args: [join(packageRoot, 'mcp/dist/index.js'), '--config', config], cwd: work, env, stderr: 'pipe' });
  if (transport.stderr) transport.stderr.pipe(mcpLog, { end: false });
  await client.connect(transport);
  const call = async (name, args) => {
    const result = await client.callTool({ name, arguments: args });
    assert.ok(!result.isError, JSON.stringify(result.content));
    return result.structuredContent ?? JSON.parse(result.content[0].text);
  };
  const { tools } = await client.listTools();
  check('packaged_mcp_has_36_tools', tools.length === 36);
  check('all_scoped_tools_require_library_id', tools.filter(tool => tool.name !== 'library_list').every(tool => tool.inputSchema.required.includes('library_id')));
  const libraries = await call('library_list', {});
  check('dedicated_library_discovered', libraries.errors.length === 0 && libraries.libraries.length === 1);
  const library_id = libraries.libraries[0].library_id;
  check('actual_library_id_matches_service', library_id === ready.library_id);
  for (const theme of ['ivory', 'graphite', 'ocean', 'mint']) {
    await call('library_settings', { library_id, values: { theme } });
    const info = await call('library_info', { library_id });
    check('theme_' + theme + '_roundtrip', info.settings.theme === theme);
  }
  const imported = await call('asset_import', { library_id, paths: [source], backup: false });
  check('import_returns_business_job_id', typeof imported.job_id === 'string' && imported.job_id.length > 0);
  let job;
  for (let attempt = 0; attempt < 200; ++attempt) {
    job = await call('job_get', { library_id, job_id: imported.job_id });
    if (['completed', 'failed', 'cancelled', 'interrupted'].includes(job.state)) break;
    await delay(100);
  }
  check('import_job_completed', job?.state === 'completed', JSON.stringify(job));
  const listed = await call('asset_list', { library_id });
  check('imported_image_listed', listed.total === 1 && listed.items[0].type === 'image');
  const asset = await call('asset_get', { library_id, asset_id: listed.items[0].id });
  const updated = await call('asset_update', { library_id, asset_id: asset.id, revision: asset.revision,
    values: { tags: ['便携验证'], favorite: true } });
  check('metadata_write_visible', updated.favorite === true && updated.tags.includes('便携验证'));
  const stale = await client.callTool({ name: 'asset_update', arguments: { library_id, asset_id: asset.id,
    revision: asset.revision, values: { name: 'must-not-apply' } } });
  check('stale_revision_rejected', stale.isError === true);
  const final = await call('asset_get', { library_id, asset_id: asset.id });
  check('conflict_did_not_overwrite_name', final.name === asset.name);
  check('source_file_unchanged', createHash('sha256').update(await readFile(source)).digest('hex') === originalHash);
} catch (error) {
  failure = error;
} finally {
  if (client) await client.close().catch(error => { failure ??= error; });
  if (!spawnError && service.exitCode === null) {
    try { await rpc('service.shutdown', { cancel_jobs: true }); }
    catch (error) { failure ??= error; }
    const orderly = await Promise.race([exited, delay(10000).then(() => null)]);
    if (!orderly) {
      service.kill();
      await Promise.race([exited, delay(3000)]);
      failure ??= new Error('Service did not exit after shutdown');
    }
  }
  assertions.push({ name: 'owned_service_exited', passed: service.exitCode === 0 });
  if (service.exitCode !== 0) failure ??= new Error('Service exited abnormally');
  serviceLog.end(); mcpLog.end();
  clearTimeout(watchdog);
  report.ok = !failure && assertions.every(item => item.passed);
  if (failure) report.error = String(failure.stack ?? failure);
  await writeFile(join(logs, 'report.json'), JSON.stringify(report, null, 2));
}
console.log(JSON.stringify(report, null, 2));
if (failure) process.exitCode = 1;
