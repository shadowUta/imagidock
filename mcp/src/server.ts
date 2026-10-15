import { McpServer } from '@modelcontextprotocol/server';
import { z } from 'zod';
import { LibraryBridge, ServiceError } from './ipc.js';

const text = z.string().min(1).max(8192);
const id = z.string().min(1).max(128);
const revision = z.number().int().nonnegative();
const ids = z.array(id).min(1).max(10_000);
const role = z.enum(['base_color', 'normal', 'roughness', 'metallic', 'ao', 'emissive', 'opacity', 'orm']);
const optionalRole = z.union([role, z.literal('')]).optional();
const asset = { asset_id: id };
const changing = { asset_id: id, revision };
const exportSelection = { asset_ids: ids.optional(), directory_id: id.optional(), destination: text };

interface ToolSpec {
  name: string; method: string; description: string;
  shape: z.ZodRawShape; read?: boolean;
}

export const specs: ToolSpec[] = [
  { name: 'library_info', method: 'library.info', description: 'Get library identity, settings, counters and revision.', shape: {}, read: true },
  { name: 'library_settings', method: 'library.settings', description: 'Update local application settings. Does not change artwork.', shape: { values: z.object({
    theme: z.enum(['ivory', 'graphite', 'ocean', 'mint', 'dark', 'light']).optional(), card_size: z.number().int().min(96).max(320).optional(),
    thumb_memory_mb: z.number().int().min(32).max(4096).optional(), default_backup: z.boolean().optional(),
    cache_disk_mb: z.number().int().min(128).max(1048576).optional(),
    job_concurrency: z.number().int().min(1).max(8).optional(), blender_path: z.string().optional(), aseprite_path: z.string().optional()
  }).strict() } },
  { name: 'asset_list', method: 'asset.list', description: 'Search assets. OR within tags/types, AND across query/directory/type/tag/status. Directory includes descendants.', shape: {
    query: z.string().max(2048).optional(), types: z.array(z.enum(['image', 'model', 'audio', 'video', 'source'])).optional(),
    tags: z.array(text).max(100).optional(), directory_id: id.optional(), status: z.enum(['all', 'missing', 'backed_up', 'stale', 'favorite', 'deleted']).optional(),
    offset: z.number().int().min(0).optional(), limit: z.number().int().min(1).max(1000).optional(), sort: z.enum(['name', 'modified', 'imported']).optional()
  }, read: true },
  { name: 'asset_get', method: 'asset.get', description: 'Read asset including current revision, ordered relations, previews, material bindings, dependencies and issues.', shape: asset, read: true },
  { name: 'asset_update', method: 'asset.update', description: 'Update metadata using the current revision. Virtual moves never move source files. Full tags array replaces previous tags.', shape: { ...changing, values: z.object({ name: text.optional(), directory_id: id.nullable().optional(), tags: z.array(text).max(100).optional(), favorite: z.boolean().optional() }).strict() } },
  { name: 'asset_import', method: 'asset.import', description: 'Import files or folders recursively in the background. Index only by default. Returns job_id; poll job_get.', shape: { paths: z.array(text).min(1).max(10_000), backup: z.boolean().optional(), directory_id: id.optional() } },
  { name: 'asset_delete', method: 'asset.delete', description: 'Move assets to the library recycle bin; external source files are never deleted.', shape: { asset_ids: ids } },
  { name: 'asset_restore', method: 'asset.restore', description: 'Restore library records from the recycle bin, retaining IDs and links.', shape: { asset_ids: ids } },
  { name: 'asset_placeholder', method: 'asset.placeholder', description: 'Restore a missing library placeholder under the same asset ID. Does not create a file on disk.', shape: asset },
  { name: 'asset_relink', method: 'asset.relink', description: 'Relink a missing source to an existing file, preserving asset ID and relations.', shape: { ...asset, path: text } },
  { name: 'asset_resolve', method: 'asset.resolve', description: 'Resolve an existing source or materialize a backup. If job_id is returned, poll completion then retry resolution.', shape: asset },
  { name: 'directory_list', method: 'directory.list', description: 'List virtual library folders and their stable IDs.', shape: {}, read: true },
  { name: 'directory_create', method: 'directory.create', description: 'Create a virtual folder; no source folders are moved or created.', shape: { name: text, parent_id: id.optional() } },
  { name: 'directory_update', method: 'directory.update', description: 'Rename or move a virtual folder.', shape: { directory_id: id, name: text.optional(), parent_id: id.nullable().optional() } },
  { name: 'tag_list', method: 'tag.list', description: 'List normal asset tags. These differ from material roles.', shape: {}, read: true },
  { name: 'relation_set', method: 'relation.set', description: 'Replace the complete ordered relation set using the source revision. Empty role is a reference; explicit rendering roles drive material slots (1-based). One role per slot.', shape: {
    ...changing, items: z.array(z.object({ id: id.optional(), target_id: id, label: z.string().max(256).optional(), role: optionalRole,
      slot: z.number().int().min(1).max(1024).optional(), normal_y: z.enum(['opengl', 'directx']).optional(), material_name: z.string().max(1024).optional() }).strict()).max(10_000)
  } },
  { name: 'role_list', method: 'role.list', description: 'List immutable built-in material roles and user-defined relation labels.', shape: {}, read: true },
  { name: 'role_create', method: 'role.create', description: 'Create a custom relation label, optionally mapped to a rendering role.', shape: { name: text, render_role: role.optional() } },
  { name: 'role_delete', method: 'role.delete', description: 'Delete a custom role. Built-in roles cannot be deleted.', shape: { role_id: id } },
  { name: 'role_reorder', method: 'role.reorder', description: 'Reorder custom relation labels. Supply every custom role ID exactly once; built-in roles retain their fixed order and must be omitted.', shape: { role_ids: z.array(id).max(10_000).refine(values => new Set(values).size === values.length, 'Role IDs must be unique') } },
  { name: 'preview_set', method: 'preview.set', description: 'Replace the ordered preview set with revision protection. target_id links a library image; path copies a compressed external image without indexing it. Preserve existing items by id. cover_id is auto, preview id or new target id.', shape: {
    ...changing, items: z.array(z.object({ id: id.optional(), target_id: id.optional(), path: text.optional(), label: z.string().max(256).optional() }).strict()
      .refine(item => !(item.target_id && item.path) && Boolean(item.id || item.target_id || item.path), 'Use an existing id, a library target_id, or external path; target_id and path are mutually exclusive')).max(1000), cover_id: id.optional()
  } },
  { name: 'preview_resolve', method: 'preview.resolve', description: 'Resolve a detail preview at up to 2048 pixels. source_only decodes the original asset or backup and ignores its selected cover, useful for material maps. Returns a cache path or background job_id.', shape: { ...asset, size: z.union([z.literal(512), z.literal(1024), z.literal(2048)]).optional(), preview_id: id.optional(), source_only: z.boolean().optional() } },
  { name: 'thumbnail_request', method: 'thumbnail.request', description: 'Request a cached card thumbnail; decoding is asynchronous and returns job_id when needed.', shape: { ...asset, size: z.union([z.literal(256), z.literal(512)]).optional(), priority: z.number().int().min(-100).max(100).optional() } },
  { name: 'backup_start', method: 'backup.start', description: 'Enable/disable backup intent and start background backups. Disabling keeps the last valid backup.', shape: { asset_ids: ids, enabled: z.boolean().optional() } },
  { name: 'backup_refresh', method: 'backup.refresh', description: 'Refresh asset backups and required dependencies. Previous good backups survive failure.', shape: { asset_ids: ids } },
  { name: 'export_assets', method: 'export.assets', description: 'Extract selected assets or a virtual directory with model dependencies. Existing destination files are preserved by renaming. Returns job_id.', shape: exportSelection },
  { name: 'library_export', method: 'library.export', description: 'Export a versioned independent library package with database snapshot, backed-up objects and preview images. complete_backup first backs up indexed sources.', shape: { destination: text, complete_backup: z.boolean().optional() } },
  { name: 'library_import', method: 'library.import', description: 'Import a package as a new independent library at destination. Does not merge into the active library.', shape: { path: text, destination: text } },
  { name: 'issue_list', method: 'issue.list', description: 'List missing sources/dependencies, decode failures, broken material slots and backup issues.', shape: {}, read: true },
  { name: 'issue_scan', method: 'issue.scan', description: 'Scan source links and backup state in the background.', shape: {} },
  { name: 'issue_relink_prefix', method: 'issue.relink_prefix', description: 'Batch-relink source paths after a folder move, preserving asset IDs. Specify concrete old and new directory prefixes.', shape: { old_prefix: text, new_prefix: text } },
  { name: 'job_list', method: 'job.list', description: 'List background tasks and their persisted states.', shape: {}, read: true },
  { name: 'job_get', method: 'job.get', description: 'Read background task progress/result/error. Completion is state=completed; merely returning a job_id is not success.', shape: { job_id: id }, read: true },
  { name: 'job_cancel', method: 'job.cancel', description: 'Cancel unfinished work; preserve items already completed.', shape: { job_id: id } },
  { name: 'job_retry', method: 'job.retry', description: 'Retry a failed, cancelled or interrupted task after inspecting its error.', shape: { job_id: id } }
];

function response(value: unknown) {
  return { content: [{ type: 'text' as const, text: JSON.stringify(value, null, 2) }], structuredContent: value as Record<string, unknown> };
}

export function buildServer(bridge: LibraryBridge): McpServer {
  const server = new McpServer({ name: 'imagidock-assets', version: '1.0.0' }, {
    instructions: 'Manage explicitly configured local ImagiDock libraries. Call library_list first; use stable IDs and read the current asset revision before updates. Relations/previews are ordered full replacements. Long operations return job_id, not completion. Source artwork is never modified by metadata changes or library deletion.'
  });
  server.registerTool('library_list', { description: 'Discover configured local libraries and their actual stable IDs. Does not enumerate arbitrary folders.', inputSchema: z.object({}), annotations: { readOnlyHint: true, openWorldHint: false } }, async () => response(await bridge.list()));
  for (const spec of specs) {
    server.registerTool(spec.name, {
      description: spec.description, inputSchema: z.object({ library_id: id, ...spec.shape }).strict(),
      annotations: { readOnlyHint: Boolean(spec.read), destructiveHint: spec.name === 'asset_delete' || spec.name === 'role_delete', openWorldHint: false }
    }, async (input: Record<string, unknown>) => {
      const { library_id, ...params } = input;
      try { return response(await bridge.call(String(library_id), spec.method, params)); }
      catch (error) {
        return { isError: true, content: [{ type: 'text' as const, text: JSON.stringify({ code: error instanceof ServiceError ? error.code : 'service_error', message: error instanceof Error ? error.message : String(error) }) }] };
      }
    });
  }
  return server;
}
