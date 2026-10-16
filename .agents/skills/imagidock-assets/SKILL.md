---
name: imagidock-assets
description: "Manage a local ImagiDock asset library through its MCP server: find artwork, organize virtual folders and tags, link model textures and references, manage previews, back up or extract assets, and repair missing links. Use for ImagiDock libraries, not editing artwork in a painting or 3D application."
---

# ImagiDock assets

Use the `imagidock` MCP tools to operate the same library service as the desktop
application. Start with `library_list`; select the library matching the user's
request and retain its returned `library_id`. Asset names and source paths are
display data. Stable asset IDs identify records across moves and repairs.

## Find and organize

- Query `asset_list` with pagination. Tags and types use union within their
  respective filters; directory, type, tag, search and status combine by
  intersection. A directory filter includes descendants.
- Read `asset_get` immediately before a metadata, relation or preview update;
  supply its current `revision`. On a conflict, re-read and reconcile the user's
  requested change with newer data. Do not blindly repeat the old full set.
- Virtual folder moves and names do not change source files. Library deletion
  moves records to the recycle bin and preserves source artwork.
- Import folders with `asset_import`. Default is indexing only. Enable backup
  when requested; do not treat indexing as copying or preservation.

## Appearance

For a requested appearance change, `library_settings.values.theme` accepts
`ivory` (Warm Ivory), `graphite` (Graphite), `ocean` (Ocean), or `mint` (Mint).
Legacy `light` and `dark` remain compatible aliases. Do not change a theme as
part of unrelated asset organization.

## Related assets and model materials

`relation_set` replaces the **entire ordered list**. Keep unrelated entries when
adding or removing one relation. An empty rendering role is a general reference,
such as a weapon design drawing. A texture relation uses an explicit role:
`base_color`, `normal`, `roughness`, `metallic`, `ao`, `emissive`, `opacity`, `orm`.
Use 1-based material slots, actual model slot metadata, and at most one binding
per role and slot. The default three slots are not a maximum. Preserve the
specified OpenGL/DirectX normal direction; do not infer it from a generic name.
Built-in roles cannot be renamed, deleted or reordered. To reorder custom labels,
call `role_list`, then `role_reorder` with all custom IDs exactly once and no built-in IDs.
Custom labels can optionally map to a rendering role. Preserve `material_name`
when editing bindings so changed model slots are detected. Preview settings
never rewrite the original model.

## Preview lists and source formats

`preview_set` is also an ordered full replacement. `target_id` means a navigable
library image; `path` copies a compressed external preview without creating an
asset record. Retain existing items with `id`. Select a cover with `cover_id`;
`auto` restores the source/model default.

Use `preview_resolve` for detail images and `thumbnail_request` for cards. PSD/PSB
use saved composites, and SAI1 uses its embedded preview. SAI2 saved composites
are experimental and can fall back to an embedded thumbnail. Report the returned
preview kind and dimensions honestly; suggest linking a high-resolution image
when the source has no compatible saved preview. Do not describe a thumbnail as
a fully decoded layered document.

## Background work and recovery

Imports, backup, extraction, library packages and scans return `job_id`. Poll
`job_get` until a terminal state before reporting success. Preserve the ID in
progress updates; a completed job's result may still include per-file failures.
Inspect errors before `job_retry`. If IPC times out after a write, check jobs and
current data before repeating it; the write may already have been accepted.

Use `issue_list` to distinguish missing sources, missing dependencies, preview
failures and backup problems. Relinking or restoring retains asset IDs and
repairs references. `asset_placeholder` creates only a library record, never a
disk file. A valid backup can be materialized with `asset_resolve`; when it
returns a job, wait and resolve again. Extraction preserves dependencies and
renames conflicts rather than overwriting existing files.

Library export includes existing backups and preview images. Indexed-only
sources are not automatically embedded unless `complete_backup` is requested.
Library package import creates an independent library, not a merge.

For setup, use the project-local `mcp/config.example.json` and
`mcp/codex-config.example.toml`. Configuration paths are examples, not existing
libraries. Do not edit global client settings or install anything outside the
project unless the user requests that setup.
