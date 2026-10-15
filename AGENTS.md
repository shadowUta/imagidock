# ImagiDock development boundaries

All writes, dependencies, caches, fixtures, logs and build outputs must remain under this directory. Parent game and archive files are read-only. Do not run the game or touch saves. Do not publish archive artwork, local libraries, absolute personal paths or credentials. Source is publicly visible but no open-source license is granted to first-party code. Preserve third-party notices.

Application: C++20, Qt 6 Widgets/Multimedia, Filament, SQLite, Zstandard. Build with CMake/MSVC Release. UI and MCP call the same JSON RPC library service over a current-user local named pipe. Long operations return job IDs. See docs/IPC.md for the agreed contract. Use bounded async work, stable IDs, optimistic revisions, and never overwrite source artwork.

Concurrent ownership: core agent owns src/core and core tests; UI agent owns src/ui; adapters agent owns helpers, mcp and project skills; root owns build scripts, renderer, packaging and integration. Coordinate contract changes before editing another owner's files.
