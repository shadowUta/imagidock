# ImagiDock MCP

Official MCP TypeScript SDK v2 stdio server. The transport uses `serveStdio`,
which negotiates modern and legacy MCP clients. It forwards operations to the
same current-user named-pipe library service as the desktop UI; it never opens
the database directly.

From this directory run `npm ci --ignore-scripts` and `npm run build`, with npm's
cache configured under the project. Copy `config.example.json` to
`config.local.json`, adjust the explicitly selected library directories and
service path, then use:

```text
node dist/index.js --config config.local.json
```

Relative paths in the JSON config are resolved against the config file. No
libraries are discovered by scanning user folders. `library_list` reads each
configured service's real library ID. All other tools require that ID. Long jobs
return immediately; follow `job_get` to completion. Conflict errors require a
fresh read and reconciliation. Writes are never automatically replayed after a
timeout.

`codex-config.example.toml` is an opt-in client setup example; no global settings
are changed by the build or application. The local skill is under
`.agents/skills/imagidock-assets` in the repository root.

`npm test` exercises the real stdio SDK, named-pipe framing, selection of an
explicit library, typed tool schemas and error propagation. With a compiled
service available, `node test/live-service.mjs <service.exe>` exercises import,
query, optimistic revision updates and background jobs through MCP.
