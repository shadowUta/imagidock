# ImagiDock IPC v1

UTF-8 newline-delimited JSON over QLocalSocket/QLocalServer (current user only). Pipe name is `imagidock-` plus the first 24 lowercase hex characters of SHA256 of the UTF-8 canonical absolute library directory with slash separators, lowercase on Windows. Service: `imagidock-service.exe --library <absolute folder>`. Qt UI may launch it if absent; Node uses `\\.\pipe\<pipe-name>`. Every connection may send multiple requests. Maximum line 8 MiB.

Request: `{ "id":"uuid", "method":"asset.list", "params":{} }`. Response: `{ "id":"uuid", "result":{...} }` or `{ "id":"uuid", "error":{"code":"...","message":"..."} }`. IDs echoed; no unsolicited responses. UI polls active jobs and library revision; no nested blocking event loops for ordinary UI operations.

Methods and parameters (optional values in parentheses):

- `library.info {}` -> `{library_id, root, revision, counts:{all,image,model,audio,video,source,missing,deleted}, settings:{...}}`
- `library.settings {values:{...}}` -> updated settings. Defaults: dark theme, card_size 176, thumb_memory_mb 256, default_backup false, blender_path empty, aseprite_path empty.
- `asset.list {query?,types?:[],tags?:[],directory_id?,status?:all|missing|backed_up|stale|favorite|deleted,offset?:0,limit?:200,sort?:name|modified|imported}` -> `{items:[Asset],total,revision}`. OR within tags/types; AND across fields. Directory includes descendants. Default excludes deleted.
- `asset.get {asset_id}` -> Asset extended with `relations`, `previews`, `bindings`, `dependencies`, `issues`.
- `asset.update {asset_id,revision,values:{name?,directory_id?,tags?:[],favorite?}}` -> Asset.
- `asset.delete {asset_ids:[]}` / `asset.restore {asset_ids:[]}` / `asset.placeholder {asset_id}` -> `{updated}`; no external file deletion.
- `asset.relink {asset_id,path}` -> Asset (same ID).
- `asset.resolve {asset_id}` -> `{path,from_backup,exists}`; materializes backup asynchronously when needed by returning `{job_id}`; poll then retry.
- `asset.import {paths:[],backup?:false,directory_id?}` -> `{job_id}`; all supported source formats accepted even when preview unavailable.
- `directory.list {}` -> `{items:[{id,parent_id,name,path}]}`.
- `directory.create {name,parent_id?}` / `directory.update {directory_id,name?,parent_id?}` -> directory.
- `tag.list {}` -> `{items:[string]}`.
- `relation.set {asset_id,revision,items:[{id?,target_id,label?,role?,slot?:1,normal_y?:opengl|directx}]}` -> updated Asset; this is the ordered full set. Empty role = general reference. role in base_color,normal,roughness,metallic,ao,emissive,opacity,orm for rendering. Slot is 1-based. Role+slot unique. Source asset revision required.
- `role.list {}` -> `{items:[{id,name,builtin}],slots:[1,2,3]}`; `role.create {name,render_role?}` and `role.delete {role_id}`; system roles cannot be deleted/renamed.
- `role.reorder {role_ids:[]}` -> `{updated}`. Requires each current custom role exactly once in the desired order; builtin roles cannot be included or reordered. New custom roles append after the saved order.
- `preview.set {asset_id,revision,items:[{id?,target_id?,path?,label?}],cover_id?}` -> Asset. target_id means navigable library image; path means external image copied/downscaled into library, not independently indexed. Existing id preserves preview. cover_id can be `auto`, a preview id, or target_id for newly added library image.
- `backup.start {asset_ids:[],enabled?:true}` / `backup.refresh {asset_ids:[]}` -> `{job_id}`. `enabled:false` only disables future intent, does not delete the last good backup.
- `export.assets {asset_ids?:[],directory_id?,destination}` -> `{job_id}`. Preserve dependencies; never silently overwrite.
- `library.export {destination,complete_backup?:false}` -> `{job_id}`; `library.import {path,destination}` -> `{job_id}` (new independent library).
- `issue.list {}` -> `{items:[{id,asset_id,kind,message,path}]}`; `issue.scan {}` -> `{job_id}`; `issue.relink_prefix {old_prefix,new_prefix}` -> `{job_id}`.
- `job.list {}` -> `{items:[Job]}`; `job.get {job_id}` -> Job; `job.cancel {job_id}` / `job.retry {job_id}` -> Job.
- `thumbnail.request {asset_id,size?:256|512,priority?:0}` -> `{path?,job_id?,state}`. UI displays placeholder until ready. Renderer/helpers populate caches. Preview tasks must not block the service event loop.
- `preview.resolve {asset_id,size?:2048,preview_id?,source_only?:false}` -> `{path?,job_id?,state,preview_kind?}`. Returns a separate full-preview cache; size is clamped to 512–4096. Optional preview_id selects one existing preview instead of the cover. `source_only:true` instead decodes the asset's original source/backup, ignoring its cover and preview_id; it never replaces the asset's preview_path, thumbnail_path or preview_kind. Use this for PSD/SAI material textures. Image decoding is asynchronous and isolated.
- `service.shutdown {cancel_jobs?:true}` -> `{accepted:true}`. Explicitly cancels outstanding work and exits the service after acknowledging. Only use on an explicit final-window Quit; closing into the tray keeps the service running. Cancelled/interrupted jobs remain retryable.

Asset baseline fields: `id,name,type,extension,source_path,directory_id,virtual_path,size,mtime,revision,tags:[],favorite,deleted,placeholder,missing,backup_state:none|ready|stale|failed,thumbnail_path,preview_path,preview_kind,metadata:{}}`. `type`: image/model/audio/video/source. Job: `id,kind,state:queued|running|completed|failed|cancelled|interrupted,progress:0..1,message,result:{},error?`. All numbers fitting int64 may use decimal strings where byte size demands it. All paths from service are absolute.

Helper process contract: `python helpers/preview.py --input <path> --output <png path> --size 512 --kind image|model|audio|video|source --settings <JSON file>` writes one JSON result to stdout with `ok,path,preview_kind,width,height,canvas_width?,canvas_height?,metadata?,error?`. Output always explicitly under library cache. It may execute configured Blender/Aseprite and configured `model_renderer` helper only with an argument list, no shell. Root builds `imagidock-render.exe --input <model> --output <png> --size 512` for Filament thumbnails. `preview.py` must not modify input files. Core writes settings JSON containing tool paths and runtime paths. Full preview can use `--size 2048`.

Core CMake target `imagidock_core` with Qt Core/Network/Gui/Concurrent plus sqlite3 and zstd; service executable main in src/core/service_main.cpp. UI entry `int main` in src/ui/main.cpp and other files in src/ui. Root provides renderer header `src/render/ModelView.h`: class ModelView : public QWidget { Q_OBJECT; public explicit ModelView(QWidget* parent=nullptr); void loadModel(const QString& path,const QJsonArray& bindings={}); void setTextureBindings(const QJsonArray& bindings); void resetCamera(); void setAnimationPlaying(bool); void setAnimationPosition(float seconds); bool saveSnapshot(const QString& path); QString lastError() const; signals: void modelLoaded(const QJsonObject& metadata); void errorOccurred(const QString& message); }. Bindings contain role,slot,path,normal_y. Main build enables renderer; no fake 3D preview.
