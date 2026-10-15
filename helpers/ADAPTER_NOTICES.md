# Preview adapter dependencies

ImagiDock's own adapter code is not offered under an open-source license.
Third-party components retain their licenses. Package artifacts are local build
dependencies and are not committed with private artwork.

| Component | Pinned version | Source | License |
|---|---|---|---|
| Pillow | 12.3.0 | https://github.com/python-pillow/Pillow | HPND |
| psd-tools | 1.25.0 | https://github.com/psd-tools/psd-tools | MIT |
| psutil | 7.2.2 | https://github.com/giampaolo/psutil | BSD-3-Clause |
| NumPy | 2.4.6 | https://github.com/numpy/numpy | BSD-3-Clause and bundled notices |
| attrs | 26.1.0 | https://github.com/python-attrs/attrs | MIT |
| imageio-ffmpeg (development tests only) | 0.6.0 | https://github.com/imageio/imageio-ffmpeg | BSD-2-Clause; bundled FFmpeg has its own license; excluded from portable package |
| libsai | f07ee8878e184eeeded79a5a2ef808bc51a3a7f7 | https://github.com/Wunkolo/libsai | MIT |
| sai2-parser binary | v2.0.0 | https://github.com/mishiki/sai2-parser/releases/tag/v2.0.0 | MIT |
| MCP server/client SDK | 2.3.1 | https://github.com/modelcontextprotocol/typescript-sdk | Apache-2.0 |

SAI2 binary SHA-256 (official release API digest, checked before execution):
`c79239a6e7a7d5ed2f319034b6875c47bd0e2f8c174f826cdad861f6441af241`.
SAI2 reference source inspected at commit
`6671463ef4b5364f3db5ee9659a6015fd6c2ca0a`.

`sai2_thumbnail.py` implements the JSSF thumbnail framing documented by
libsai; its MIT permission notice is reproduced in `licenses/libsai.txt`.
SAI2 full image decoding executes the unmodified upstream binary as a bounded
child process. ImagiDock supplies an explicit cache output and does not register
upstream shell integration or execute upstream install scripts.

The portable package uses `imagidock-media.exe`, a native Qt Multimedia helper,
for waveforms and video frames. Qt's deployed FFmpeg shared libraries and their
licenses are covered by the main package's Qt notices/SBOM. The separate
imageio-ffmpeg Windows executable reports GPL v3 or later and is used only to
generate development fixtures; `package-adapters.py` explicitly excludes that
wheel and executable. If a redistributor chooses to bundle an additional
FFmpeg executable, its own notices and corresponding source obligations apply.
The adapter can use a user-configured FFmpeg path when no native helper exists.

Pip wheel SHA-256 values are locked in `requirements-lock.txt`; npm tarball
integrity values are locked in `mcp/package-lock.json`. Portable packaging must
include the installed dependency license files in addition to this index.
