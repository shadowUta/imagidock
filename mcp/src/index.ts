import { serveStdio } from '@modelcontextprotocol/server/stdio';
import { loadConfig, LibraryBridge } from './ipc.js';
import { buildServer } from './server.js';

const index = process.argv.indexOf('--config');
const configPath = index >= 0 ? process.argv[index + 1] : process.env.IMAGIDOCK_MCP_CONFIG;
if (!configPath) {
  process.stderr.write('Usage: node dist/index.js --config <local-config.json>\n');
  process.exitCode = 1;
} else {
  try {
    const bridge = new LibraryBridge(await loadConfig(configPath));
    serveStdio(() => buildServer(bridge));
  } catch (error) {
    process.stderr.write(String(error) + '\n');
    process.exitCode = 1;
  }
}
