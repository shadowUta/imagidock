# Preview adapters, MCP and portable runtimes

The C++ library service calls `helpers/preview.py`. This supervisor starts a
fresh decoding worker for each preview and monitors the worker's entire process
tree. Default limits are 120 seconds, 1 GiB resident memory, 2 GiB input file size
and 64 million image pixels. Every output is staged inside the destination cache
and atomically replaced after success. Failed decoding preserves the last good
preview. Source artwork is opened read-only.

PSD and PSB use `PSDImage.topil()` with the saved-composite flag and ICC
conversion; the adapter never invokes layer reconstruction. If the saved
composite is absent/too large, an embedded thumbnail can be used. Otherwise the
asset remains indexed with a repairable preview error. SAI 1 uses the native
libsai helper and reports its embedded preview dimensions. The MIT SAI2 reader
extracts saved composites; unsupported version tags or failures can fall back
to a bounded JSSF thumbnail. These modes are reported separately to the UI.

`imagidock-media` uses Qt Multimedia for streaming PCM envelope generation and
the first decoded video frame. It runs with Qt's offscreen platform and a
110-second deadline. At most two hours of audio are sampled; the result reports
truncation. Model snapshots use the Filament renderer. Blender and Aseprite are
optional user-installed tools and are never copied into the release. Their
preferences/temp directories point into each disposable preview cache. Blender
is started with factory settings and automatic scripts disabled; compositor
and sequencer outputs are disabled for previews, and the source blend is not
saved. Blender 4 and 5 Eevee engine names are supported.

## Reproduce checks

Run from the repository root, using a local Python 3.11 environment whose
packages match `helpers/requirements-lock.txt`. Set `PIP_CACHE_DIR`, `TEMP`,
`TMP` and `PYTHONPYCACHEPREFIX` to directories under the project. Install locked
wheels with `pip install --require-hashes -r helpers/requirements-lock.txt`.

Build native targets, add the selected Qt `bin` directory to this process's
PATH, and set `QT_PLUGIN_PATH` to its `plugins` directory. Then run:

```text
.tools/adapters-env/Scripts/python.exe tests/adapters_test.py
npm --prefix mcp ci --ignore-scripts --cache .cache/npm
npm --prefix mcp test
```

The Python suite covers full-image containment, alpha/ICC, PSD/PSB saved
composites, missing composites, SAI 1 encrypted thumbnail extraction, SAI2 saved
composites and alpha, JSSF fallback, corrupt inputs, output preservation,
waveforms, video first frames, limits and unchanged input hashes. Original
fixtures are generated under `.local/tests`; no private artwork is committed.

`tests/adapters_archive_smoke.py <archive> --blender <exe> --aseprite <exe>`
selects one small PNG, WAV, GLB, Blend and Aseprite source for a read-only smoke
check. Detailed paths, hashes and previews stay in `.local/tests` and are
excluded from publication. The first Blender 5 check exposed an engine rename;
the adapter now selects the supported Eevee engine and the real archived scene
renders correctly.

MCP integration checks use the official SDK client against the stdio bridge.
The bridge verifies an explicitly configured library's identity on every call;
updates use current revisions, and accepted writes are never automatically
replayed. `mcp/test/live-service.mjs` performs a real C++ service import, job
poll, query, tag/favorite edit and conflict check. For repeated development,
copy the service to a private test-run directory before this test so linking
the build output is not blocked. The test stops its own service when complete.

## Portable packaging

`scripts/package-adapters.py <directory-inside-project>` stages the official
Python 3.11.9 embeddable runtime under `python/`, official Node 24.19.0 under
`runtime/node/`, the pinned SAI2 reader, Python packages, compiled MCP bridge and
the local Skill. Runtime archives are taken from `.tools/portable-downloads`
and Python wheels from `.cache/wheels-adapters`; packaging performs no network
access. The committed lock records official URLs and SHA-256 values. Python's
downloaded Sigstore bundle digest and Node's downloaded SHASUMS are checked
against the lock; this is a digest consistency check, not a full Sigstore
certificate/log verification.

The native executables and Qt DLLs are deployed by the main package script.
The package includes source/license locations, installed wheel license files,
MCP dependency licenses and a machine-readable adapter manifest. The separate
GPL imageio-ffmpeg executable is excluded; media previews use the application's
Qt backend. The build validates imports using the packaged Python and starts
the packaged Node without relying on a global Python/Node installation.
