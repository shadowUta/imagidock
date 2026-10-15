import { createHash, randomUUID } from 'node:crypto';
import { realpath, readFile } from 'node:fs/promises';
import { resolve, isAbsolute, dirname } from 'node:path';
import { createConnection } from 'node:net';
import { spawn } from 'node:child_process';

export interface LibraryConfig { path: string; }
export interface BridgeConfig { libraries: LibraryConfig[]; service_path?: string; timeout_ms?: number; }
export interface LibraryInfo { library_id: string; [key: string]: unknown; }
export class ServiceError extends Error {
  constructor(public readonly code: string, message: string) { super(message); }
}

export async function loadConfig(path: string): Promise<BridgeConfig> {
  const absolute = resolve(path);
  const config = JSON.parse(await readFile(absolute, 'utf8')) as BridgeConfig;
  if (!Array.isArray(config.libraries) || !config.libraries.length || config.libraries.length > 32)
    throw new Error('config requires 1–32 explicitly configured libraries');
  config.libraries = config.libraries.map(item => {
    if (!item || typeof item.path !== 'string' || !item.path.length) throw new Error('invalid library path');
    return { path: isAbsolute(item.path) ? item.path : resolve(dirname(absolute), item.path) };
  });
  if (config.service_path && !isAbsolute(config.service_path)) config.service_path = resolve(dirname(absolute), config.service_path);
  return config;
}

export async function pipeName(library: string): Promise<string> {
  let canonical = (await realpath(library)).replace(/\\/g, '/');
  if (process.platform === 'win32') canonical = canonical.toLowerCase();
  canonical = canonical.replace(/\/$/, '');
  const name = 'imagidock-' + createHash('sha256').update(canonical, 'utf8').digest('hex').slice(0, 24);
  return process.platform === 'win32' ? '\\\\.\\pipe\\' + name : '/tmp/' + name;
}

export async function sendRpc(pipe: string, method: string, params: Record<string, unknown>, timeout = 30_000): Promise<Record<string, unknown>> {
  const id = randomUUID();
  const request = Buffer.from(JSON.stringify({ id, method, params }) + '\n', 'utf8');
  if (request.length > 8 * 1024 * 1024) throw new ServiceError('message_too_large', 'Request exceeds the 8 MiB IPC limit');
  return new Promise((accept, reject) => {
    const socket = createConnection(pipe);
    let received = Buffer.alloc(0);
    let done = false;
    const finish = (error?: Error, result?: Record<string, unknown>) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      socket.destroy();
      if (error) reject(error); else accept(result!);
    };
    const timer = setTimeout(() => finish(new ServiceError('timeout', 'Library service timed out; inspect job status before repeating a write')), timeout);
    socket.on('connect', () => socket.write(request));
    socket.on('error', error => finish(error));
    socket.on('end', () => finish(new ServiceError('disconnected', 'Library service disconnected before replying')));
    socket.on('data', chunk => {
      received = Buffer.concat([received, chunk]);
      if (received.length > 8 * 1024 * 1024) return finish(new ServiceError('message_too_large', 'Service response exceeds 8 MiB'));
      const end = received.indexOf(10);
      if (end < 0) return;
      try {
        const response = JSON.parse(received.subarray(0, end).toString('utf8'));
        if (response.id !== id) throw new ServiceError('id_mismatch', 'Response ID did not match the request');
        if (response.error) throw new ServiceError(String(response.error.code), String(response.error.message));
        if (!response.result || typeof response.result !== 'object') throw new ServiceError('invalid_response', 'Missing response result');
        finish(undefined, response.result);
      } catch (error) { finish(error instanceof Error ? error : new Error(String(error))); }
    });
  });
}

export class LibraryBridge {
  private known = new Map<string, { path: string; pipe: string }>();
  private starting = new Map<string, Promise<void>>();
  constructor(private readonly config: BridgeConfig) {}

  private async connectInfo(path: string, pipe: string): Promise<LibraryInfo> {
    try { return await sendRpc(pipe, 'library.info', {}, 3000) as LibraryInfo; }
    catch (error) {
      const code = (error as NodeJS.ErrnoException).code;
      if (!this.config.service_path || !['ENOENT', 'ECONNREFUSED'].includes(code ?? '')) throw error;
      let pending = this.starting.get(pipe);
      if (!pending) {
        pending = new Promise<void>((accept, reject) => {
          const child = spawn(this.config.service_path!, ['--library', path], {
            detached: true, stdio: 'ignore', windowsHide: true, cwd: dirname(this.config.service_path!)
          });
          child.once('error', reject);
          child.once('spawn', () => { child.unref(); accept(); });
        });
        this.starting.set(pipe, pending);
      }
      await pending;
      // Only connection discovery is retried. No mutation is replayed.
      for (let attempt = 0; attempt < 30; ++attempt) {
        await new Promise(resolve => setTimeout(resolve, 100));
        try { return await sendRpc(pipe, 'library.info', {}, 3000) as LibraryInfo; } catch (next) {
          if (attempt === 29) throw next;
        }
      }
      throw error;
    }
  }

  async list(): Promise<{ libraries: LibraryInfo[]; errors: { path: string; error: string }[] }> {
    const libraries: LibraryInfo[] = [], errors: { path: string; error: string }[] = [];
    for (const item of this.config.libraries) {
      try {
        const pipe = await pipeName(item.path);
        const info = await this.connectInfo(item.path, pipe);
        if (!info.library_id || typeof info.library_id !== 'string') throw new Error('Service did not provide a library ID');
        this.known.set(info.library_id, { path: item.path, pipe });
        libraries.push(info);
      } catch (error) { errors.push({ path: item.path, error: String(error) }); }
    }
    return { libraries, errors };
  }

  async call(library_id: string, method: string, params: Record<string, unknown>): Promise<Record<string, unknown>> {
    if (!this.known.has(library_id)) await this.list();
    const library = this.known.get(library_id);
    if (!library) throw new ServiceError('unknown_library', 'Library ID is not among the configured local libraries; call library_list');
    // Check identity on every call, including after service restarts or directory replacement.
    const info = await this.connectInfo(library.path, library.pipe);
    if (info.library_id !== library_id) {
      this.known.delete(library_id);
      throw new ServiceError('library_changed', 'The library identity changed; select it again with library_list');
    }
    return sendRpc(library.pipe, method, { ...params, library_id }, this.config.timeout_ms ?? 30_000);
  }
}
