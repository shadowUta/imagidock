#include "Library.h"
#include <sqlite3.h>
#include <zstd.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <QUrl>
#include <QSet>
#include <QRunnable>
#include <QThread>
#include <QVariant>
#include <QtEndian>
#include <functional>
#include <stdexcept>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace imagidock {
namespace {
struct Failure : std::runtime_error { QString code; Failure(QString c,QString m):std::runtime_error(m.toStdString()),code(c){} };
[[noreturn]] void fail(const QString &c,const QString &m){throw Failure(c,m);}
QString uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
bool validId(const QString&id){return QRegularExpression("^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$").match(id).hasMatch();}
QString now(){return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);}
QByteArray json(const QJsonValue &v){return v.isArray()?QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact):QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);}
QJsonObject object(const QString &s){return QJsonDocument::fromJson(s.toUtf8()).object();}
QJsonArray array(const QString &s){return QJsonDocument::fromJson(s.toUtf8()).array();}
QString text(const QJsonObject&p,const char*k){return p.value(QLatin1String(k)).toString();}
QString absolute(const QString&p){auto f=QFileInfo(p);return QDir::fromNativeSeparators(f.exists()?f.canonicalFilePath():f.absoluteFilePath());}
QString pathKey(QString p){p=absolute(p);
#ifdef Q_OS_WIN
p=p.toLower();
#endif
return p;}
bool within(const QString &p,const QString&r){auto a=pathKey(p),b=pathKey(r);return a==b || a.startsWith(b+'/');}
QString safeName(QString n){n=n.trimmed();if(n.isEmpty()||n=="."||n==".."||n.contains(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]")))fail("invalid_name","名称包含不允许的字符");return n;}
QString uniquePath(const QString &path){if(!QFileInfo::exists(path))return path;QFileInfo f(path);for(int i=2;i<100000;++i){QString p=f.absolutePath()+"/"+f.completeBaseName()+QString(" (%1)").arg(i)+(f.suffix().isEmpty()?"":"."+f.suffix());if(!QFileInfo::exists(p))return p;}fail("conflict","同名文件过多");}
void check(const std::shared_ptr<std::atomic_bool>&c){if(c&&c->load())fail("cancelled","操作已取消");}
struct Db {
 sqlite3 *h=nullptr;
 explicit Db(const QString&p){auto b=p.toUtf8();if(sqlite3_open_v2(b.constData(),&h,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){QString e=h?QString::fromUtf8(sqlite3_errmsg(h)):"open failed";if(h)sqlite3_close(h);h=nullptr;fail("database",e);}sqlite3_busy_timeout(h,15000);exec("PRAGMA foreign_keys=ON");}
 ~Db(){if(h)sqlite3_close(h);}
 void exec(const char*s){char*e=nullptr;if(sqlite3_exec(h,s,nullptr,nullptr,&e)!=SQLITE_OK){QString m=QString::fromUtf8(e?e:sqlite3_errmsg(h));sqlite3_free(e);fail("database",m);}}
};
struct Stmt {
 Db &db;sqlite3_stmt*s=nullptr;
 Stmt(Db&d,const QString&q):db(d){auto b=q.toUtf8();if(sqlite3_prepare_v2(d.h,b.constData(),-1,&s,nullptr)!=SQLITE_OK)fail("database",QString::fromUtf8(sqlite3_errmsg(d.h)));}
 ~Stmt(){sqlite3_finalize(s);}
 void bind(int n,const QString&v){auto b=v.toUtf8();sqlite3_bind_text(s,n,b.constData(),int(b.size()),SQLITE_TRANSIENT);}
 void bind(int n,qint64 v){sqlite3_bind_int64(s,n,v);}
 void bind(int n,const QByteArray&v){sqlite3_bind_text(s,n,v.constData(),int(v.size()),SQLITE_TRANSIENT);}
 bool step(){int r=sqlite3_step(s);if(r==SQLITE_ROW)return true;if(r!=SQLITE_DONE)fail("database",QString::fromUtf8(sqlite3_errmsg(db.h)));return false;}
 QString str(int n){const auto*p=sqlite3_column_text(s,n);return p?QString::fromUtf8(reinterpret_cast<const char*>(p)):QString();}
 qint64 num(int n){return sqlite3_column_int64(s,n);}
};
struct Tx {Db&d;bool done=false;explicit Tx(Db&v):d(v){d.exec("BEGIN IMMEDIATE");}~Tx(){if(!done)sqlite3_exec(d.h,"ROLLBACK",nullptr,nullptr,nullptr);}void commit(){d.exec("COMMIT");done=true;}};
QString meta(Db&d,const QString&key){Stmt s(d,"SELECT value FROM meta WHERE key=?");s.bind(1,key);return s.step()?s.str(0):QString();}
void setMeta(Db&d,const QString&key,const QString&val){Stmt s(d,"INSERT INTO meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");s.bind(1,key);s.bind(2,val);s.step();}
void bump(Db&d){d.exec("UPDATE meta SET value=CAST(value AS INTEGER)+1 WHERE key='revision'");}
void touch(Db&d,const QString&id){Stmt s(d,"UPDATE assets SET revision=revision+1 WHERE id=?");s.bind(1,id);s.step();bump(d);}
QJsonObject settings(Db&d){return object(meta(d,"settings"));}
QString resolvedRoot(Db&d,const QString&id,const QString&root){Stmt s(d,"SELECT backup_manifest FROM assets WHERE id=?");s.bind(1,id);if(!s.step())fail("not_found","资产不存在");QString generation=QString::fromLatin1(QCryptographicHash::hash(s.str(0).toUtf8(),QCryptographicHash::Sha256).toHex().left(16));QString safeId=validId(id)?id:QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256).toHex());return root+"/cache/resolved/"+safeId+"/"+generation;}
QString category(const QString&e){
 static const QSet<QString> images={"png","jpg","jpeg","webp","bmp","gif","tiff","tif","tga","psd","psb","sai","sai2","exr","hdr"};
 static const QSet<QString> models={"glb","gltf","obj","fbx"},audio={"wav","mp3","flac","ogg","m4a","aac"},video={"mp4","webm","mov","mkv","avi"},source={"blend","aseprite","ase"};
 if(images.contains(e))return "image";if(models.contains(e))return "model";if(audio.contains(e))return "audio";if(video.contains(e))return "video";if(source.contains(e))return "source";return {};
}
QString directoryPath(Db&d,QString id){QStringList parts;QSet<QString>seen;while(!id.isEmpty()){if(seen.contains(id))fail("directory_cycle","目录循环");seen.insert(id);Stmt s(d,"SELECT name,parent_id FROM directories WHERE id=?");s.bind(1,id);if(!s.step())break;parts.prepend(s.str(0));id=s.str(1);}return parts.join('/');}
QString ensureDirectory(Db&d,const QString&name,const QString&parent){
 Stmt f(d,"SELECT id FROM directories WHERE name=? AND parent_id=?");f.bind(1,name);f.bind(2,parent);if(f.step())return f.str(0);
 QString id=uuid();Stmt s(d,"INSERT INTO directories VALUES(?,?,?)");s.bind(1,id);s.bind(2,parent);s.bind(3,safeName(name));s.step();return id;
}
QJsonObject directoryObject(Db&d,const QString&id){Stmt s(d,"SELECT parent_id,name FROM directories WHERE id=?");s.bind(1,id);if(!s.step())fail("not_found","目录不存在");return {{"id",id},{"parent_id",s.str(0)},{"name",s.str(1)},{"path",directoryPath(d,id)}};}
QStringList roleIds(){return {"base_color","normal","roughness","metallic","ao","emissive","opacity","orm"};}
void issue(Db&d,const QString&id,const QString&kind,const QString&message,const QString&path={}){Stmt s(d,"INSERT INTO issues(id,asset_id,kind,message,path) VALUES(?,?,?,?,?) ON CONFLICT(asset_id,kind,path) DO UPDATE SET message=excluded.message");s.bind(1,uuid());s.bind(2,id);s.bind(3,kind);s.bind(4,message);s.bind(5,path);s.step();}
void clearIssue(Db&d,const QString&id,const QString&kind){Stmt s(d,"DELETE FROM issues WHERE asset_id=? AND kind=?");s.bind(1,id);s.bind(2,kind);s.step();}
QJsonArray issueList(Db&d,const QString&id={}){Stmt s(d,id.isEmpty()?"SELECT id,asset_id,kind,message,path FROM issues ORDER BY rowid DESC":"SELECT id,asset_id,kind,message,path FROM issues WHERE asset_id=? ORDER BY rowid DESC");if(!id.isEmpty())s.bind(1,id);QJsonArray a;while(s.step())a.append(QJsonObject{{"id",s.str(0)},{"asset_id",s.str(1)},{"kind",s.str(2)},{"message",s.str(3)},{"path",s.str(4)}});return a;}
QJsonObject asset(Db&d,const QString&id,bool extended=false){
 Stmt s(d,"SELECT id,name,type,extension,source_path,directory_id,size,mtime,revision,favorite,deleted,placeholder,backup_state,thumbnail_path,preview_path,preview_kind,metadata,cover_id,backup_manifest,backup_enabled FROM assets WHERE id=?");s.bind(1,id);if(!s.step())fail("not_found","资产不存在");
 QString path=s.str(4),dir=s.str(5);QFileInfo f(path);bool missing=path.isEmpty()||!f.isFile()||s.num(10)||s.num(11);QString bs=s.str(12);auto manifest=object(s.str(18));
 if(bs=="ready"&&!missing && (manifest.value("mtime").toVariant().toLongLong()!=f.lastModified().toMSecsSinceEpoch()||manifest.value("size").toVariant().toLongLong()!=f.size()))bs="stale";
 QJsonArray tags;Stmt t(d,"SELECT tag FROM asset_tags WHERE asset_id=? ORDER BY tag COLLATE NOCASE");t.bind(1,id);while(t.step())tags.append(t.str(0));
 QString vp=directoryPath(d,dir);if(!vp.isEmpty())vp+='/';vp+=s.str(1);
 QJsonObject a{{"id",id},{"name",s.str(1)},{"type",s.str(2)},{"extension",s.str(3)},{"source_path",path},{"directory_id",dir},{"virtual_path",vp},{"size",s.num(6)},{"mtime",s.num(7)},{"revision",s.num(8)},{"favorite",bool(s.num(9))},{"deleted",bool(s.num(10))},{"placeholder",bool(s.num(11))},{"missing",missing},{"backup_state",bs},{"thumbnail_path",s.str(13)},{"preview_path",s.str(14)},{"preview_kind",s.str(15)},{"metadata",object(s.str(16))},{"cover_id",s.str(17)},{"backup_enabled",bool(s.num(19))},{"tags",tags}};
 if(!extended)return a;
 QJsonArray relations,bindings;Stmt r(d,"SELECT id,target_id,label,role,slot,normal_y,material_name FROM relations WHERE asset_id=? ORDER BY position");r.bind(1,id);
 while(r.step()){QJsonObject v{{"id",r.str(0)},{"target_id",r.str(1)},{"label",r.str(2)},{"role",r.str(3)},{"slot",r.num(4)},{"normal_y",r.str(5)},{"material_name",r.str(6)}};try{auto target=asset(d,r.str(1));v["target"]=target;v["missing"]=target["missing"];QString tp=target["source_path"].toString();if(!QFileInfo::exists(tp)){Stmt b(d,"SELECT backup_manifest FROM assets WHERE id=?");b.bind(1,r.str(1));if(b.step()){auto bm=object(b.str(0));QString libraryRoot=QFileInfo(QString::fromUtf8(sqlite3_db_filename(d.h,"main"))).absolutePath();tp=resolvedRoot(d,r.str(1),libraryRoot)+"/"+bm.value("entry").toString();}}v["path"]=tp;}catch(...){v["missing"]=true;}relations.append(v);if(!r.str(3).isEmpty())bindings.append(v);}
 QJsonArray previews;Stmt pr(d,"SELECT id,target_id,path,label FROM previews WHERE asset_id=? ORDER BY position");pr.bind(1,id);while(pr.step()){QJsonObject v{{"id",pr.str(0)},{"target_id",pr.str(1)},{"path",pr.str(2)},{"label",pr.str(3)}};if(!pr.str(1).isEmpty()){try{auto t=asset(d,pr.str(1));v["target"]=t;v["missing"]=t["missing"];v["path"]=t["preview_path"].toString().isEmpty()?t["source_path"]:t["preview_path"];}catch(...){v["missing"]=true;}}previews.append(v);}
 a["relations"]=relations;a["bindings"]=bindings;a["previews"]=previews;a["dependencies"]=manifest.value("files").toArray();a["issues"]=issueList(d,id);return a;
}
void requireRevision(Db&d,const QJsonObject&p){auto a=asset(d,text(p,"asset_id"));if(!p.contains("revision")||a["revision"].toVariant().toLongLong()!=p["revision"].toVariant().toLongLong())fail("revision_conflict","资产已在其他窗口更新，请刷新后重试");}
QJsonObject job(Db&d,const QString&id){Stmt s(d,"SELECT kind,state,progress,message,result,error FROM jobs WHERE id=?");s.bind(1,id);if(!s.step())fail("not_found","任务不存在");return {{"id",id},{"kind",s.str(0)},{"state",s.str(1)},{"progress",sqlite3_column_double(s.s,2)},{"message",s.str(3)},{"result",object(s.str(4))},{"error",s.str(5)}};}
void jobState(Db&d,const QString&id,const QString&state,double progress,const QString&message,const QJsonObject&result={},const QString&error={}){Stmt s(d,"UPDATE jobs SET state=?,progress=?,message=?,result=?,error=?,updated=? WHERE id=?");s.bind(1,state);sqlite3_bind_double(s.s,2,progress);s.bind(3,message);s.bind(4,json(result));s.bind(5,error);s.bind(6,now());s.bind(7,id);s.step();}
QJsonObject process(const QString &program,const QStringList&args,const std::shared_ptr<std::atomic_bool>&cancel,int timeout=120000){QProcess p;p.setProgram(program);p.setArguments(args);auto environment=QProcessEnvironment::systemEnvironment();environment.insert("PYTHONUTF8","1");environment.insert("PYTHONIOENCODING","utf-8");p.setProcessEnvironment(environment);p.setProcessChannelMode(QProcess::SeparateChannels);p.start();if(!p.waitForStarted(5000))fail("helper_unavailable",p.errorString());
#ifdef Q_OS_WIN
 HANDLE processJob=CreateJobObjectW(nullptr,nullptr);auto closeJob=qScopeGuard([&]{if(processJob)CloseHandle(processJob);});
 if(!processJob){p.kill();p.waitForFinished(5000);fail("helper_isolation","无法建立解码进程组");}
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;limits.ProcessMemoryLimit=SIZE_T(1536)*1024*1024;
 HANDLE child=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE,FALSE,DWORD(p.processId()));bool assigned=SetInformationJobObject(processJob,JobObjectExtendedLimitInformation,&limits,sizeof(limits))&&child&&AssignProcessToJobObject(processJob,child);if(child)CloseHandle(child);if(!assigned){p.kill();p.waitForFinished(5000);fail("helper_isolation","无法限制解码进程资源");}
#endif
 QElapsedTimer timer;timer.start();QByteArray out,err;while(!p.waitForFinished(100)){out+=p.readAllStandardOutput();err+=p.readAllStandardError();if((cancel&&cancel->load())||timer.elapsed()>timeout||out.size()>8*1024*1024||err.size()>1024*1024){
#ifdef Q_OS_WIN
 TerminateJobObject(processJob,1);
#endif
 p.kill();p.waitForFinished(5000);check(cancel);fail("helper_limit","预览进程超过时间或输出限制");}}out+=p.readAllStandardOutput();err+=p.readAllStandardError();if(p.exitStatus()!=QProcess::NormalExit||p.exitCode()!=0)fail("helper_failed",QString::fromUtf8(err.isEmpty()?out:err).left(4000));QJsonObject result;for(const auto&line:out.split('\n')){auto doc=QJsonDocument::fromJson(line);if(doc.isObject())result=doc.object();}if(!result.value("ok").toBool())fail("decode_failed",result.value("error").toString("预览器未返回有效结果"));return result;}
void writeJson(const QString&p,const QJsonObject&o){QSaveFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(json(o))<0||!f.commit())fail("io",f.errorString());}
QStringList dependencies(const QString&path){
 QStringList found{absolute(path)};QSet<QString>seen{pathKey(path)};auto add=[&](const QString&p){QString a=absolute(p);if(!seen.contains(pathKey(a))){seen.insert(pathKey(a));found.append(a);}};
 QFileInfo f(path);QString ext=f.suffix().toLower();QDir dir=f.dir();
 if(ext=="gltf"||ext=="glb"){QFile in(path);if(!in.open(QIODevice::ReadOnly))fail("io",in.errorString());QByteArray bytes;if(ext=="gltf"){if(in.size()>64*1024*1024)fail("model_limit","glTF JSON超过依赖扫描安全上限");bytes=in.readAll();}else{auto header=in.read(12);if(header.size()!=12||header.left(4)!="glTF")fail("invalid_model","GLB头无效");while(!in.atEnd()){auto chunk=in.read(8);if(chunk.size()!=8)fail("invalid_model","GLB块头截断");quint32 len=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(chunk.constData())),type=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(chunk.constData()+4));if(qint64(len)>in.size()-in.pos())fail("invalid_model","GLB块越界");if(type==0x4e4f534a){if(len>64*1024*1024)fail("model_limit","GLB JSON超过依赖扫描安全上限");bytes=in.read(len);break;}in.seek(in.pos()+len);}}QJsonParseError error;auto parsed=QJsonDocument::fromJson(bytes,&error);if(error.error!=QJsonParseError::NoError||!parsed.isObject())fail("invalid_model","无法读取模型依赖清单");auto doc=parsed.object();for(const char*k:{"buffers","images"})for(auto v:doc.value(k).toArray()){QString uri=v.toObject().value("uri").toString();if(!uri.isEmpty()&&!uri.startsWith("data:")){QUrl u(uri);if(u.scheme().isEmpty())add(dir.absoluteFilePath(QUrl::fromPercentEncoding(uri.toUtf8())));else fail("remote_dependency","模型包含无法离线备份的远程依赖: "+uri);}}}
 if(ext=="obj"){QFile in(path);if(in.open(QIODevice::ReadOnly|QIODevice::Text)){while(!in.atEnd()){QString line=QString::fromUtf8(in.readLine()).trimmed();if(line.startsWith("mtllib ")){QString names=line.mid(7).trimmed();QString mt=dir.absoluteFilePath(names);if(QFileInfo::exists(mt))add(mt);else for(const auto&n:names.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts))add(dir.absoluteFilePath(n));}}}}
 const auto originals=found;for(const auto&mt:originals)if(QFileInfo(mt).suffix().compare("mtl",Qt::CaseInsensitive)==0){QFile in(mt);if(in.open(QIODevice::ReadOnly|QIODevice::Text))while(!in.atEnd()){auto line=QString::fromUtf8(in.readLine()).trimmed();auto parts=QProcess::splitCommand(line);if(parts.size()>1&&(parts[0].startsWith("map_")||parts[0]=="bump"||parts[0]=="disp"||parts[0]=="norm")){QString filename=line.mid(parts[0].size()).trimmed();for(int start=1;start<parts.size();++start){auto candidate=parts.mid(start).join(' ');if(QFileInfo::exists(QFileInfo(mt).dir().absoluteFilePath(candidate))){filename=candidate;break;}}if(filename.startsWith('-'))filename=parts.last();add(QFileInfo(mt).dir().absoluteFilePath(filename));}}}
 return found;
}
void decompressObject(const QString&,const QString&,const QString&,qint64,const std::shared_ptr<std::atomic_bool>&);
QString compressObject(const QString&source,const QString&root,const std::shared_ptr<std::atomic_bool>&cancel){
 QFile in(source);if(!in.open(QIODevice::ReadOnly))fail("source_missing",in.errorString()+": "+source);QDir().mkpath(root+"/objects");const QString tmp=root+"/objects/."+uuid()+".partial";QFile out(tmp);if(!out.open(QIODevice::WriteOnly))fail("io",out.errorString());
 auto cleanup=qScopeGuard([&]{out.close();QFile::remove(tmp);});auto *stream=ZSTD_createCStream();if(!stream)fail("memory","无法创建压缩流");auto free=qScopeGuard([&]{ZSTD_freeCStream(stream);});ZSTD_initCStream(stream,3);QCryptographicHash hash(QCryptographicHash::Sha256);QByteArray output(int(ZSTD_CStreamOutSize()),Qt::Uninitialized);
 while(!in.atEnd()){check(cancel);QByteArray bytes=in.read(1024*1024);if(bytes.isEmpty()&&in.error()!=QFile::NoError)fail("io",in.errorString());hash.addData(bytes);ZSTD_inBuffer input{bytes.constData(),size_t(bytes.size()),0};while(input.pos<input.size){ZSTD_outBuffer ob{output.data(),size_t(output.size()),0};size_t r=ZSTD_compressStream2(stream,&ob,&input,ZSTD_e_continue);if(ZSTD_isError(r))fail("compression",ZSTD_getErrorName(r));if(out.write(output.constData(),qint64(ob.pos))!=qint64(ob.pos))fail("io",out.errorString());}}
 ZSTD_inBuffer empty{nullptr,0,0};size_t remain;do{check(cancel);ZSTD_outBuffer ob{output.data(),size_t(output.size()),0};remain=ZSTD_compressStream2(stream,&ob,&empty,ZSTD_e_end);if(ZSTD_isError(remain))fail("compression",ZSTD_getErrorName(remain));if(out.write(output.constData(),qint64(ob.pos))!=qint64(ob.pos))fail("io",out.errorString());}while(remain);
 if(!out.flush())fail("io",out.errorString());out.close();QString digest=QString::fromLatin1(hash.result().toHex()),dest=root+"/objects/"+digest+".zst";decompressObject(tmp,{},digest,in.size(),cancel);if(QFileInfo::exists(dest))decompressObject(dest,{},digest,in.size(),cancel);else if(!QFile::rename(tmp,dest)&&!QFileInfo::exists(dest))fail("io","无法提交备份对象");return digest;
}
void decompressObject(const QString&objectPath,const QString&destination,const QString&expected,qint64 expectedSize,const std::shared_ptr<std::atomic_bool>&cancel){
 if(!QRegularExpression("^[a-f0-9]{64}$").match(expected).hasMatch()||expectedSize<0)fail("backup_corrupt","备份对象标识或长度无效");
 QFile in(objectPath);if(!in.open(QIODevice::ReadOnly))fail("backup_corrupt","备份对象丢失: "+expected);std::unique_ptr<QSaveFile>out;if(!destination.isEmpty()){QDir().mkpath(QFileInfo(destination).absolutePath());out=std::make_unique<QSaveFile>(destination);if(!out->open(QIODevice::WriteOnly))fail("io",out->errorString());}auto*s=ZSTD_createDStream();if(!s)fail("memory","解压内存不足");auto free=qScopeGuard([&]{ZSTD_freeDStream(s);});ZSTD_initDStream(s);QByteArray buffer(int(ZSTD_DStreamOutSize()),Qt::Uninitialized);QCryptographicHash hash(QCryptographicHash::Sha256);size_t remain=1;qint64 total=0;
 while(!in.atEnd()){check(cancel);QByteArray bytes=in.read(1024*1024);if(bytes.isEmpty()&&in.error()!=QFile::NoError)fail("io",in.errorString());ZSTD_inBuffer ib{bytes.constData(),size_t(bytes.size()),0};while(ib.pos<ib.size){ZSTD_outBuffer ob{buffer.data(),size_t(buffer.size()),0};remain=ZSTD_decompressStream(s,&ob,&ib);if(ZSTD_isError(remain))fail("backup_corrupt",ZSTD_getErrorName(remain));total+=qint64(ob.pos);if(total>expectedSize)fail("backup_corrupt","备份解压长度超过清单");hash.addData(QByteArrayView(buffer.constData(),qsizetype(ob.pos)));if(out&&out->write(buffer.constData(),qint64(ob.pos))!=qint64(ob.pos))fail("io",out->errorString());}}
 if(remain||total!=expectedSize||QString::fromLatin1(hash.result().toHex())!=expected)fail("backup_corrupt","备份校验失败");if(out&&!out->commit())fail("io",out->errorString());
}
QString commonDirectory(const QStringList&paths){QString dir=QFileInfo(paths.first()).absolutePath();for(const auto&p:paths){while(!within(p,dir)){QString parent=QFileInfo(dir).absolutePath();if(parent==dir)fail("dependency_path","依赖位于不同磁盘，无法保留相对路径");dir=parent;}}return dir;}
QJsonObject backupAsset(Db&d,const QString&id,const QString&root,const std::shared_ptr<std::atomic_bool>&cancel){
 auto a=asset(d,id);QString src=a["source_path"].toString();if(!QFileInfo(src).isFile())fail("source_missing","源文件不可用: "+src);auto paths=dependencies(src);QString base=commonDirectory(paths);QJsonArray files;
 for(const auto&p:paths){check(cancel);QFileInfo before(p);if(!before.isFile()){issue(d,id,"dependency_missing","依赖文件不存在",p);fail("dependency_missing",p);}qint64 size=before.size(),mtime=before.lastModified().toMSecsSinceEpoch();QString hash=compressObject(p,root,cancel);QFileInfo after(p);if(size!=after.size()||mtime!=after.lastModified().toMSecsSinceEpoch())fail("source_changed","备份期间源文件变化，请重试");files.append(QJsonObject{{"path",QDir(base).relativeFilePath(p)},{"source",p},{"hash",hash},{"size",size},{"mtime",mtime}});}
 Stmt r(d,"SELECT target_id,role,slot,normal_y FROM relations WHERE asset_id=? AND role<>''");r.bind(1,id);while(r.step()){auto t=asset(d,r.str(0));QString p=t["source_path"].toString();if(!QFileInfo(p).isFile())fail("dependency_missing","绑定贴图丢失: "+p);auto f=QFileInfo(p);qint64 size=f.size(),mtime=f.lastModified().toMSecsSinceEpoch();files.append(QJsonObject{{"path","bindings/"+r.str(0)+"/"+f.fileName()},{"source",p},{"hash",compressObject(p,root,cancel)},{"size",size},{"mtime",mtime},{"target_id",r.str(0)},{"role",r.str(1)},{"slot",r.num(2)},{"normal_y",r.str(3)}});}
 for(auto v:files){auto f=v.toObject();QFileInfo current(f["source"].toString());if(current.size()!=f["size"].toVariant().toLongLong()||current.lastModified().toMSecsSinceEpoch()!=f["mtime"].toVariant().toLongLong())fail("source_changed","备份期间来源或贴图发生变化，请重试");}
 QJsonObject manifest{{"version",1},{"entry",QDir(base).relativeFilePath(src)},{"mtime",files.first().toObject()["mtime"]},{"size",files.first().toObject()["size"]},{"files",files}};
 Tx tx(d);if(asset(d,id)["revision"]!=a["revision"])fail("revision_conflict","备份期间资产配置改变，请重试");Stmt u(d,"UPDATE assets SET backup_manifest=?,backup_state='ready',backup_enabled=1 WHERE id=?");u.bind(1,json(manifest));u.bind(2,id);u.step();clearIssue(d,id,"backup_failed");clearIssue(d,id,"backup_stale");clearIssue(d,id,"dependency_missing");touch(d,id);tx.commit();return manifest;
}
QString restoreBundle(Db&d,const QString&id,const QString&root,const QString&destination,const std::shared_ptr<std::atomic_bool>&cancel){Stmt s(d,"SELECT backup_manifest FROM assets WHERE id=?");s.bind(1,id);if(!s.step())fail("not_found","资产不存在");auto m=object(s.str(0));if(m["files"].toArray().isEmpty())fail("no_backup","没有有效备份");QString entry=QDir(destination).absoluteFilePath(m["entry"].toString());if(!within(entry,destination)||m["entry"].toString().isEmpty())fail("unsafe_path","备份入口路径越界");for(auto v:m["files"].toArray()){auto f=v.toObject();QString relative=f["path"].toString(),target=QDir(destination).absoluteFilePath(relative);if(!within(target,destination)||relative.isEmpty()||QDir::isAbsolutePath(relative))fail("unsafe_path","备份清单路径越界");decompressObject(root+"/objects/"+f["hash"].toString()+".zst",target,f["hash"].toString(),f["size"].toVariant().toLongLong(),cancel);}if(within(destination,root+"/cache/resolved"))writeJson(destination+"/.complete.json",{{"entry",entry},{"completed",now()}});return entry;}
void copyFile(const QString&src,const QString&dst,const std::shared_ptr<std::atomic_bool>&cancel){QFile in(src);if(!in.open(QIODevice::ReadOnly))fail("io",in.errorString());QDir().mkpath(QFileInfo(dst).absolutePath());QSaveFile out(dst);if(!out.open(QIODevice::WriteOnly))fail("io",out.errorString());while(!in.atEnd()){check(cancel);auto b=in.read(1024*1024);if(b.isEmpty()&&in.error()!=QFile::NoError)fail("io",in.errorString());if(out.write(b)!=b.size())fail("io",out.errorString());}if(!out.commit())fail("io",out.errorString());}
}

Library::Library(QString root,QString helpers,QString python):root_(absolute(root)),database_(root_+"/library.sqlite3"),helpers_(absolute(helpers)),python_(python){
 QDir().mkpath(root_);for(auto p:{"objects","previews","cache","cache/thumbs","cache/resolved","staging"})QDir().mkpath(root_+'/'+p);
 Db d(database_);d.exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
 d.exec(R"SQL(
 CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
 CREATE TABLE IF NOT EXISTS directories(id TEXT PRIMARY KEY,parent_id TEXT NOT NULL DEFAULT '',name TEXT NOT NULL,UNIQUE(parent_id,name));
 CREATE TABLE IF NOT EXISTS assets(id TEXT PRIMARY KEY,name TEXT NOT NULL,type TEXT NOT NULL,extension TEXT NOT NULL,source_path TEXT NOT NULL,source_key TEXT NOT NULL UNIQUE,directory_id TEXT NOT NULL DEFAULT '',size INTEGER NOT NULL DEFAULT 0,mtime INTEGER NOT NULL DEFAULT 0,revision INTEGER NOT NULL DEFAULT 1,favorite INTEGER NOT NULL DEFAULT 0,deleted INTEGER NOT NULL DEFAULT 0,placeholder INTEGER NOT NULL DEFAULT 0,backup_state TEXT NOT NULL DEFAULT 'none',backup_enabled INTEGER NOT NULL DEFAULT 0,backup_manifest TEXT NOT NULL DEFAULT '{}',thumbnail_path TEXT NOT NULL DEFAULT '',preview_path TEXT NOT NULL DEFAULT '',preview_kind TEXT NOT NULL DEFAULT '',metadata TEXT NOT NULL DEFAULT '{}',cover_id TEXT NOT NULL DEFAULT 'auto',imported TEXT NOT NULL);
 CREATE INDEX IF NOT EXISTS assets_directory ON assets(directory_id,deleted,name COLLATE NOCASE);
 CREATE INDEX IF NOT EXISTS assets_type ON assets(type,deleted,name COLLATE NOCASE);
 CREATE INDEX IF NOT EXISTS assets_name ON assets(deleted,name COLLATE NOCASE);
 CREATE TABLE IF NOT EXISTS asset_tags(asset_id TEXT NOT NULL REFERENCES assets(id),tag TEXT NOT NULL,PRIMARY KEY(asset_id,tag));
 CREATE INDEX IF NOT EXISTS tags_search ON asset_tags(tag,asset_id);
 CREATE TABLE IF NOT EXISTS relations(id TEXT PRIMARY KEY,asset_id TEXT NOT NULL REFERENCES assets(id),target_id TEXT NOT NULL REFERENCES assets(id),label TEXT NOT NULL DEFAULT '',role TEXT NOT NULL DEFAULT '',slot INTEGER NOT NULL DEFAULT 1,normal_y TEXT NOT NULL DEFAULT 'opengl',position INTEGER NOT NULL);
 CREATE TABLE IF NOT EXISTS previews(id TEXT PRIMARY KEY,asset_id TEXT NOT NULL REFERENCES assets(id),target_id TEXT NOT NULL DEFAULT '',path TEXT NOT NULL DEFAULT '',label TEXT NOT NULL DEFAULT '',position INTEGER NOT NULL);
 CREATE TABLE IF NOT EXISTS roles(id TEXT PRIMARY KEY,name TEXT NOT NULL UNIQUE,render_role TEXT NOT NULL DEFAULT '',builtin INTEGER NOT NULL DEFAULT 0);
 CREATE TABLE IF NOT EXISTS issues(id TEXT PRIMARY KEY,asset_id TEXT NOT NULL,kind TEXT NOT NULL,message TEXT NOT NULL,path TEXT NOT NULL DEFAULT '',UNIQUE(asset_id,kind,path));
 CREATE TABLE IF NOT EXISTS jobs(id TEXT PRIMARY KEY,kind TEXT NOT NULL,args TEXT NOT NULL,state TEXT NOT NULL,progress REAL NOT NULL DEFAULT 0,message TEXT NOT NULL DEFAULT '',result TEXT NOT NULL DEFAULT '{}',error TEXT NOT NULL DEFAULT '',created TEXT NOT NULL,updated TEXT NOT NULL);
 )SQL");
 {Stmt columns(d,"PRAGMA table_info(relations)");bool hasName=false;while(columns.step())if(columns.str(1)=="material_name")hasName=true;if(!hasName)d.exec("ALTER TABLE relations ADD COLUMN material_name TEXT NOT NULL DEFAULT ''");}
 if(meta(d,"schema_version").isEmpty()){Tx t(d);setMeta(d,"schema_version","1");setMeta(d,"library_id",uuid());setMeta(d,"revision","1");QJsonObject config{{"theme","dark"},{"card_size",176},{"thumb_memory_mb",256},{"cache_disk_mb",4096},{"default_backup",false},{"blender_path",""},{"aseprite_path",""},{"model_renderer",""},{"job_concurrency",2}};setMeta(d,"settings",QString::fromUtf8(json(config)));QStringList names={"固有色","法线","粗糙度","金属度","AO","自发光","透明度","ORM"};auto ids=roleIds();for(int i=0;i<ids.size();++i){Stmt s(d,"INSERT INTO roles VALUES(?,?,?,1)");s.bind(1,ids[i]);s.bind(2,names[i]);s.bind(3,ids[i]);s.step();}t.commit();}
 if(meta(d,"schema_version")!="1")fail("unsupported_version","库版本不受此程序支持");
 d.exec("UPDATE jobs SET state='interrupted',message='上次运行中断，可重试' WHERE state IN ('running','queued')");
 workers_.setMaxThreadCount(qBound(1,settings(d).value("job_concurrency").toInt(2),8));
 previews_.setMaxThreadCount(2);
}
Library::~Library(){{QMutexLocker lock(&jobsMutex_);for(auto c:cancellations_)c->store(true);}workers_.waitForDone();previews_.waitForDone();}
QString Library::pipeName(const QString&root){return "imagidock-"+QString::fromLatin1(QCryptographicHash::hash(pathKey(root).toUtf8(),QCryptographicHash::Sha256).toHex().left(24));}
QJsonObject Library::handle(const QJsonObject&r){QJsonObject response{{"id",r["id"]}};try{response["result"]=dispatch(text(r,"method"),r["params"].toObject());}catch(const Failure&e){response["error"]=QJsonObject{{"code",e.code},{"message",QString::fromUtf8(e.what())}};}catch(const std::exception&e){response["error"]=QJsonObject{{"code","internal"},{"message",QString::fromUtf8(e.what())}};}return response;}

QJsonObject Library::dispatch(const QString&m,const QJsonObject&p){
 Db d(database_);
 if(m=="service.shutdown"){QMutexLocker lock(&jobsMutex_);if(!p.contains("cancel_jobs")||p["cancel_jobs"].toBool())for(auto c:cancellations_)c->store(true);return {{"accepted",true}};}
 if(m=="library.info"){
  QJsonObject counts;Stmt c(d,"SELECT type,COUNT(*) FROM assets WHERE deleted=0 GROUP BY type");qint64 total=0;while(c.step()){counts[c.str(0)]=c.num(1);total+=c.num(1);}counts["all"]=total;Stmt x(d,"SELECT COUNT(*) FROM assets WHERE deleted=1");x.step();counts["deleted"]=x.num(0);Stmt missing(d,"SELECT COUNT(DISTINCT asset_id) FROM issues WHERE kind='source_missing'");missing.step();counts["missing"]=missing.num(0);return {{"library_id",meta(d,"library_id")},{"root",root_},{"revision",meta(d,"revision").toLongLong()},{"counts",counts},{"settings",settings(d)}};
 }
 if(m=="library.settings"){
  Tx tx(d);auto values=settings(d);auto changes=p["values"].toObject();for(auto it=changes.begin();it!=changes.end();++it)values[it.key()]=it.value();values["job_concurrency"]=qBound(1,values["job_concurrency"].toInt(2),8);values["card_size"]=qBound(96,values["card_size"].toInt(176),512);values["thumb_memory_mb"]=qBound(32,values["thumb_memory_mb"].toInt(256),4096);setMeta(d,"settings",QString::fromUtf8(json(values)));bump(d);tx.commit();workers_.setMaxThreadCount(values["job_concurrency"].toInt());return {{"settings",values}};
 }
 if(m=="asset.get")return asset(d,text(p,"asset_id"),true);
 if(m=="asset.list"){
  QString where="a.deleted="+QString(text(p,"status")=="deleted"?"1":"0");QStringList args;
  if(!text(p,"query").isEmpty()){where+=" AND a.name LIKE ? ESCAPE '\\'";QString q=text(p,"query");q.replace("\\","\\\\").replace("%","\\%").replace("_","\\_");args<<'%'+q+'%';}
  auto clauses=[&](const QJsonArray&v,const QString&field){if(v.isEmpty())return;QStringList marks;for(auto i:v){marks<<"?";args<<i.toString();}where+=" AND "+field+" IN ("+marks.join(',')+")";};
  clauses(p["types"].toArray(),"a.type");
  if(!p["tags"].toArray().isEmpty()){QStringList marks;for(auto v:p["tags"].toArray()){marks<<"?";args<<v.toString();}where+=" AND a.id IN(SELECT t.asset_id FROM asset_tags t WHERE t.tag IN ("+marks.join(',')+"))";}
  QString dir=text(p,"directory_id");if(!dir.isEmpty()){where+=" AND a.directory_id IN (WITH RECURSIVE tree(id) AS(SELECT ? UNION ALL SELECT d.id FROM directories d JOIN tree ON d.parent_id=tree.id) SELECT id FROM tree)";args<<dir;}
  auto status=text(p,"status");if(status=="favorite")where+=" AND a.favorite=1";if(status=="backed_up")where+=" AND a.backup_state IN ('ready','stale')";if(status=="stale")where+=" AND (a.backup_state='stale' OR EXISTS(SELECT 1 FROM issues i WHERE i.asset_id=a.id AND i.kind='backup_stale'))";if(status=="missing")where+=" AND (a.placeholder=1 OR EXISTS(SELECT 1 FROM issues i WHERE i.asset_id=a.id AND i.kind='source_missing'))";
  QString order="a.name COLLATE NOCASE,a.id";if(text(p,"sort")=="modified")order="a.mtime DESC,a.id";if(text(p,"sort")=="imported")order="a.imported DESC,a.id";
  Stmt count(d,"SELECT COUNT(*) FROM assets a WHERE "+where);for(int i=0;i<args.size();++i)count.bind(i+1,args[i]);count.step();auto total=count.num(0);
  Stmt rows(d,"SELECT a.id FROM assets a WHERE "+where+" ORDER BY "+order+" LIMIT ? OFFSET ?");for(int i=0;i<args.size();++i)rows.bind(i+1,args[i]);rows.bind(args.size()+1,qBound(1,p["limit"].toInt(200),1000));rows.bind(args.size()+2,qMax(0,p["offset"].toInt()));QJsonArray a;while(rows.step())a.append(asset(d,rows.str(0)));return {{"items",a},{"total",total},{"revision",meta(d,"revision").toLongLong()}};
 }
 if(m=="asset.update"){
  Tx tx(d);requireRevision(d,p);QString id=text(p,"asset_id");auto v=p["values"].toObject();
  for(auto key:{"name","directory_id","favorite"})if(v.contains(key)){if(QString(key)=="directory_id"&&!v[key].toString().isEmpty())directoryObject(d,v[key].toString());Stmt s(d,"UPDATE assets SET "+QString(key)+"=? WHERE id=?");if(QString(key)=="favorite")s.bind(1,qint64(v[key].toBool()));else s.bind(1,QString(key)=="name"?safeName(v[key].toString()):v[key].toString());s.bind(2,id);s.step();}
  if(v.contains("tags")){Stmt del(d,"DELETE FROM asset_tags WHERE asset_id=?");del.bind(1,id);del.step();for(auto t:v["tags"].toArray()){QString tag=t.toString().trimmed();if(tag.isEmpty())continue;if(tag.size()>128)fail("invalid_tag","标签过长");Stmt s(d,"INSERT OR IGNORE INTO asset_tags VALUES(?,?)");s.bind(1,id);s.bind(2,tag);s.step();}}
  touch(d,id);tx.commit();return asset(d,id,true);
 }
 if(m=="asset.delete"||m=="asset.restore"||m=="asset.placeholder"){
  Tx tx(d);QJsonArray ids=p["asset_ids"].toArray();if(m=="asset.placeholder")ids=QJsonArray{text(p,"asset_id")};int n=0;for(auto v:ids){QString id=v.toString();asset(d,id);Stmt s(d,"UPDATE assets SET deleted=?,placeholder=? WHERE id=?");s.bind(1,qint64(m=="asset.delete"));s.bind(2,qint64(m=="asset.placeholder"));s.bind(3,id);s.step();touch(d,id);++n;}tx.commit();return {{"updated",n}};
 }
 if(m=="asset.relink"){
  QString path=absolute(text(p,"path"));QFileInfo f(path);if(!f.isFile()||category(f.suffix().toLower()).isEmpty())fail("invalid_source","需要可支持的现有素材文件");Tx tx(d);QString id=text(p,"asset_id");asset(d,id);Stmt s(d,"UPDATE assets SET source_path=?,source_key=?,size=?,mtime=?,type=?,extension=?,placeholder=0,deleted=0,thumbnail_path='',preview_path='',backup_state=CASE WHEN backup_state='none' THEN 'none' ELSE 'stale' END WHERE id=?");s.bind(1,path);s.bind(2,pathKey(path));s.bind(3,f.size());s.bind(4,f.lastModified().toMSecsSinceEpoch());s.bind(5,category(f.suffix().toLower()));s.bind(6,f.suffix().toLower());s.bind(7,id);s.step();clearIssue(d,id,"source_missing");touch(d,id);tx.commit();return asset(d,id,true);
 }
 if(m=="directory.list"){Stmt s(d,"SELECT id FROM directories ORDER BY name COLLATE NOCASE");QJsonArray a;while(s.step())a.append(directoryObject(d,s.str(0)));return {{"items",a}};}
 if(m=="directory.create"||m=="directory.update"){
  Tx tx(d);QString parent=text(p,"parent_id");if(!parent.isEmpty())directoryObject(d,parent);QString id;
  if(m=="directory.create")id=ensureDirectory(d,safeName(text(p,"name")),parent);
  else{id=text(p,"directory_id");auto existing=directoryObject(d,id);if(!p.contains("parent_id"))parent=existing["parent_id"].toString();QString cur=parent;while(!cur.isEmpty()){if(cur==id)fail("directory_cycle","目录不能移动到自身子目录");cur=directoryObject(d,cur)["parent_id"].toString();}Stmt s(d,"UPDATE directories SET name=?,parent_id=? WHERE id=?");s.bind(1,p.contains("name")?safeName(text(p,"name")):existing["name"].toString());s.bind(2,parent);s.bind(3,id);s.step();}bump(d);tx.commit();return directoryObject(d,id);
 }
 if(m=="tag.list"){Stmt s(d,"SELECT DISTINCT tag FROM asset_tags ORDER BY tag COLLATE NOCASE");QJsonArray a;while(s.step())a.append(s.str(0));return {{"items",a}};}
 if(m=="role.list"){Stmt s(d,"SELECT id,name,builtin,render_role FROM roles ORDER BY builtin DESC,rowid");QJsonArray a;while(s.step())a.append(QJsonObject{{"id",s.str(0)},{"name",s.str(1)},{"builtin",bool(s.num(2))},{"render_role",s.str(3)}});return {{"items",a},{"slots",QJsonArray{1,2,3}}};}
 if(m=="role.create"){QString role=text(p,"render_role");if(!role.isEmpty()&&!roleIds().contains(role))fail("invalid_role","未知渲染角色");QString id=uuid();Tx tx(d);Stmt s(d,"INSERT INTO roles VALUES(?,?,?,0)");s.bind(1,id);s.bind(2,safeName(text(p,"name")));s.bind(3,role);s.step();bump(d);tx.commit();return {{"id",id},{"name",text(p,"name")},{"render_role",role},{"builtin",false}};}
 if(m=="role.delete"){Tx tx(d);Stmt s(d,"SELECT builtin FROM roles WHERE id=?");s.bind(1,text(p,"role_id"));if(!s.step())fail("not_found","角色不存在");if(s.num(0))fail("builtin_role","固定角色不可删除");Stmt del(d,"DELETE FROM roles WHERE id=?");del.bind(1,text(p,"role_id"));del.step();bump(d);tx.commit();return {{"deleted",true}};}
 if(m=="relation.set"){
  Tx tx(d);requireRevision(d,p);QString id=text(p,"asset_id");auto materials=asset(d,id)["metadata"].toObject()["materials"].toArray();Stmt del(d,"DELETE FROM relations WHERE asset_id=?");del.bind(1,id);del.step();QSet<QString>unique;int order=0;
  for(auto v:p["items"].toArray()){auto item=v.toObject();QString target=text(item,"target_id"),role=text(item,"role");auto ta=asset(d,target);if(target==id)fail("self_relation","不可关联自身");int slot=item["slot"].toInt(1);if(slot<1||slot>4096)fail("invalid_slot","材质槽必须在 1 到 4096 之间");if(!role.isEmpty()&&!roleIds().contains(role)){Stmt custom(d,"SELECT render_role FROM roles WHERE id=?");custom.bind(1,role);if(!custom.step())fail("invalid_role","未知角色");role=custom.str(0);}if(!role.isEmpty()){if(ta["type"].toString()!="image")fail("invalid_texture","材质绑定必须关联图片");QString key=role+QString::number(slot);if(unique.contains(key))fail("duplicate_binding","同一材质槽不能重复绑定同角色");unique.insert(key);}
   QString normal=item["normal_y"].toString("opengl"),materialName=text(item,"material_name");if(materialName.isEmpty())for(auto mat:materials)if(mat.toObject()["slot"].toInt()==slot)materialName=mat.toObject()["name"].toString();if(normal!="opengl"&&normal!="directx")fail("invalid_normal","法线方向无效");Stmt s(d,"INSERT INTO relations(id,asset_id,target_id,label,role,slot,normal_y,position,material_name) VALUES(?,?,?,?,?,?,?,?,?)");s.bind(1,text(item,"id").isEmpty()?uuid():text(item,"id"));s.bind(2,id);s.bind(3,target);s.bind(4,text(item,"label"));s.bind(5,role);s.bind(6,slot);s.bind(7,normal);s.bind(8,order++);s.bind(9,materialName);s.step();
  }Stmt u(d,"UPDATE assets SET thumbnail_path='',preview_path='',backup_state=CASE WHEN backup_state='ready' THEN 'stale' ELSE backup_state END WHERE id=?");u.bind(1,id);u.step();touch(d,id);tx.commit();return asset(d,id,true);
 }
 if(m=="preview.set"){
  QString id=text(p,"asset_id");auto old=asset(d,id,true);QHash<QString,QJsonObject>previous;for(auto v:old["previews"].toArray())previous.insert(v.toObject()["id"].toString(),v.toObject());QJsonArray prepared;QStringList created;auto rollback=qScopeGuard([&]{for(auto f:created)QFile::remove(f);});
  for(auto v:p["items"].toArray()){auto item=v.toObject();QString pid=text(item,"id");if(pid.isEmpty())pid=uuid();if(!validId(pid))fail("invalid_id","预览标识必须为 UUID");QString target=text(item,"target_id"),path=text(item,"path");if(!target.isEmpty()){auto t=asset(d,target);if(t["type"].toString()!="image")fail("invalid_preview","库内预览必须是图片");path.clear();}else if(previous.contains(pid)){path=previous[pid]["path"].toString();target=previous[pid]["target_id"].toString();}else{if(QFileInfo::exists(root_+"/previews/"+pid+".png"))fail("duplicate_preview","预览标识已存在");QImageReader reader(path);reader.setAutoTransform(true);if(!reader.canRead())fail("invalid_preview","此预览图片不可解码");auto size=reader.size();if(size.isValid()&&(size.width()>2048||size.height()>2048))reader.setScaledSize(size.scaled(2048,2048,Qt::KeepAspectRatio));auto image=reader.read();if(image.isNull())fail("invalid_preview",reader.errorString());if(image.width()>2048||image.height()>2048)image=image.scaled(2048,2048,Qt::KeepAspectRatio,Qt::SmoothTransformation);path=root_+"/previews/"+pid+".png";QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||!image.save(&file,"PNG")||!file.commit())fail("io","无法保存预览图片");created<<path;}prepared.append(QJsonObject{{"id",pid},{"target_id",target},{"path",path},{"label",text(item,"label")}});}
  Tx tx(d);requireRevision(d,p);Stmt del(d,"DELETE FROM previews WHERE asset_id=?");del.bind(1,id);del.step();int i=0;QString cover=p["cover_id"].toString(old["cover_id"].toString("auto"));bool coverFound=cover=="auto";
  for(auto v:prepared){auto x=v.toObject();if(cover==x["target_id"].toString()&&!cover.isEmpty())cover=x["id"].toString();if(cover==x["id"].toString())coverFound=true;Stmt s(d,"INSERT INTO previews VALUES(?,?,?,?,?,?)");s.bind(1,x["id"].toString());s.bind(2,id);s.bind(3,x["target_id"].toString());s.bind(4,x["path"].toString());s.bind(5,x["label"].toString());s.bind(6,i++);s.step();}if(!coverFound)cover="auto";Stmt u(d,"UPDATE assets SET cover_id=?,thumbnail_path='',preview_path='' WHERE id=?");u.bind(1,cover);u.bind(2,id);u.step();touch(d,id);tx.commit();rollback.dismiss();return asset(d,id,true);
 }
 if(m=="issue.list")return {{"items",issueList(d)}};
 if(m=="job.list"){Stmt s(d,"SELECT id FROM jobs ORDER BY created DESC LIMIT 200");QJsonArray a;while(s.step())a.append(job(d,s.str(0)));return {{"items",a}};}
 if(m=="job.get")return job(d,text(p,"job_id"));
 if(m=="job.cancel"){QString id=text(p,"job_id");QMutexLocker lock(&jobsMutex_);if(cancellations_.contains(id))cancellations_[id]->store(true);return job(d,id);}
 if(m=="job.retry"){QString id=text(p,"job_id");auto j=job(d,id);if(j["state"]=="running"||j["state"]=="queued")fail("job_running","任务仍在执行");QString kind;QJsonObject args;{Stmt s(d,"SELECT kind,args FROM jobs WHERE id=?");s.bind(1,id);s.step();kind=s.str(0);args=object(s.str(1));}QString newId=startJob(kind,args);return job(d,newId);}
 if(m=="asset.resolve"){
  auto a=asset(d,text(p,"asset_id"));QString path=a["source_path"].toString();if(!a["deleted"].toBool()&&!a["placeholder"].toBool()&&QFileInfo(path).isFile())return {{"path",path},{"exists",true},{"from_backup",false}};
  Stmt s(d,"SELECT backup_manifest FROM assets WHERE id=?");s.bind(1,text(p,"asset_id"));s.step();auto manifest=object(s.str(0));if(manifest["files"].toArray().isEmpty())return {{"path",path},{"exists",false},{"from_backup",false}};
  QString cache=resolvedRoot(d,text(p,"asset_id"),root_),resolved=cache+"/"+manifest["entry"].toString();if(QFileInfo(resolved).isFile()&&QFileInfo(cache+"/.complete.json").isFile())return {{"path",resolved},{"exists",true},{"from_backup",true}};return {{"job_id",startJob("resolve",p)}};
 }
 if(m=="thumbnail.request"||m=="preview.resolve"){
  QString id=text(p,"asset_id");auto a=asset(d,id);QString previewId=text(p,"preview_id");if(!previewId.isEmpty()){Stmt pr(d,"SELECT 1 FROM previews WHERE id=? AND asset_id=?");pr.bind(1,previewId);pr.bind(2,id);if(!pr.step())fail("not_found","预览图不属于该资产");}int size=m=="preview.resolve"?qBound(512,p["size"].toInt(2048),4096):(p["size"].toInt(256)==512?512:256);
  QString identity=id+"-"+QString::number(a["revision"].toVariant().toLongLong())+"-"+QString::number(size)+"-"+previewId;QString key=QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(),QCryptographicHash::Sha256).toHex());QString path=root_+"/cache/thumbs/"+key+".png";if(QFileInfo(path).isFile())return {{"path",path},{"state","ready"},{"preview_kind",a["preview_kind"]}};
  {QMutexLocker lock(&jobsMutex_);if(thumbnailJobs_.contains(key)){QString jid=thumbnailJobs_.value(key);auto state=job(d,jid)["state"].toString();if(state=="queued"||state=="running")return {{"job_id",jid},{"state",state}};thumbnailJobs_.remove(key);}}
  QJsonObject args{{"asset_id",id},{"size",size},{"key",key},{"path",path},{"full",m=="preview.resolve"},{"preview_id",previewId},{"priority",m=="preview.resolve"?10:p["priority"].toInt(0)}};QString jid=startJob("thumbnail",args);return {{"job_id",jid},{"state","queued"}};
 }
 if(m=="asset.import"||m=="backup.start"||m=="backup.refresh"||m=="export.assets"||m=="library.export"||m=="library.import"||m=="issue.scan"||m=="issue.relink_prefix")return {{"job_id",startJob(m,p)}};
 fail("unknown_method","未知方法: "+m);
}

QString Library::startJob(const QString&kind,const QJsonObject&args){
 QString id=uuid();Db d(database_);Stmt s(d,"INSERT INTO jobs(id,kind,args,state,created,updated) VALUES(?,?,?,'queued',?,?)");s.bind(1,id);s.bind(2,kind);s.bind(3,json(args));s.bind(4,now());s.bind(5,now());s.step();auto cancel=std::make_shared<std::atomic_bool>(false);{QMutexLocker lock(&jobsMutex_);cancellations_[id]=cancel;if(kind=="thumbnail")thumbnailJobs_[args["key"].toString()]=id;}
 auto runnable=QRunnable::create([this,id,kind,args,cancel]{runJob(id,kind,args,cancel);});if(kind=="thumbnail")previews_.start(runnable,args["priority"].toInt());else workers_.start(runnable);return id;
}

void Library::runJob(const QString&id,const QString&kind,const QJsonObject&p,const std::shared_ptr<std::atomic_bool>&cancel){
 auto done=qScopeGuard([&]{QMutexLocker lock(&jobsMutex_);cancellations_.remove(id);if(kind=="thumbnail"&&thumbnailJobs_.value(p["key"].toString())==id)thumbnailJobs_.remove(p["key"].toString());});
 try{
  Db d(database_);check(cancel);jobState(d,id,"running",0,"正在处理");QJsonObject result;
  auto progress=[&](double value,const QString&message){check(cancel);jobState(d,id,"running",value,message);};
  if(kind=="asset.import"){
   QString parent=text(p,"directory_id");if(!parent.isEmpty())directoryObject(d,parent);QJsonArray imported,errors;struct Candidate{QString path;QStringList folders;};QList<Candidate>files;QSet<QString>seen;
   for(auto pv:p["paths"].toArray()){check(cancel);QString path=absolute(pv.toString());QFileInfo entry(path);if(within(path,root_)){errors.append(QJsonObject{{"path",path},{"error","不能导入库自身数据"}});continue;}
    if(entry.isDir()){QDirIterator it(path,QDir::Files|QDir::NoDotAndDotDot|QDir::Hidden,QDirIterator::Subdirectories);while(it.hasNext()){check(cancel);it.next();QFileInfo f=it.fileInfo();if(f.isSymLink()||within(f.absoluteFilePath(),root_))continue;if(category(f.suffix().toLower()).isEmpty())continue;QString key=pathKey(f.absoluteFilePath());if(seen.contains(key))continue;seen.insert(key);QString relative=QDir(path).relativeFilePath(f.absolutePath());QStringList folders{entry.fileName()};if(relative!=".")folders+=relative.split('/');files.append({f.absoluteFilePath(),folders});}}
    else if(entry.isFile()&&!category(entry.suffix().toLower()).isEmpty()){if(!seen.contains(pathKey(path))){seen.insert(pathKey(path));files.append({path,{}});}}
    else errors.append(QJsonObject{{"path",path},{"error","文件不存在或格式不受支持"}});
   }
   for(int i=0;i<files.size();++i){check(cancel);const auto&candidate=files[i];try{
    QFileInfo f(candidate.path);QString aid,dir=parent;{Tx tx(d);for(const auto&folder:candidate.folders)dir=ensureDirectory(d,folder,dir);Stmt find(d,"SELECT id,mtime,size FROM assets WHERE source_key=?");find.bind(1,pathKey(candidate.path));bool existing=find.step();if(existing){aid=find.str(0);bool changed=find.num(1)!=f.lastModified().toMSecsSinceEpoch()||find.num(2)!=f.size();Stmt update(d,"UPDATE assets SET size=?,mtime=?,deleted=0,placeholder=0,revision=revision+1,backup_state=CASE WHEN ? AND backup_state='ready' THEN 'stale' ELSE backup_state END,thumbnail_path=CASE WHEN ? THEN '' ELSE thumbnail_path END,preview_path=CASE WHEN ? THEN '' ELSE preview_path END WHERE id=?");update.bind(1,f.size());update.bind(2,f.lastModified().toMSecsSinceEpoch());update.bind(3,qint64(changed));update.bind(4,qint64(changed));update.bind(5,qint64(changed));update.bind(6,aid);update.step();}else{aid=uuid();Stmt insert(d,"INSERT INTO assets(id,name,type,extension,source_path,source_key,directory_id,size,mtime,backup_enabled,imported) VALUES(?,?,?,?,?,?,?,?,?,?,?)");insert.bind(1,aid);insert.bind(2,f.fileName());insert.bind(3,category(f.suffix().toLower()));insert.bind(4,f.suffix().toLower());insert.bind(5,absolute(candidate.path));insert.bind(6,pathKey(candidate.path));insert.bind(7,dir);insert.bind(8,f.size());insert.bind(9,f.lastModified().toMSecsSinceEpoch());insert.bind(10,qint64(p["backup"].toBool()));insert.bind(11,now());insert.step();}clearIssue(d,aid,"source_missing");bump(d);tx.commit();}
    if(p["backup"].toBool())try{backupAsset(d,aid,root_,cancel);}catch(const Failure&e){if(e.code=="cancelled")throw;issue(d,aid,"backup_failed",QString::fromUtf8(e.what()),candidate.path);errors.append(QJsonObject{{"path",candidate.path},{"asset_id",aid},{"error",QString::fromUtf8(e.what())}});}imported.append(aid);
   }catch(const Failure&e){if(e.code=="cancelled")throw;errors.append(QJsonObject{{"path",candidate.path},{"error",QString::fromUtf8(e.what())}});}if(i%20==0||i+1==files.size())progress(double(i+1)/qMax(1,files.size()),QString("已处理 %1 / %2").arg(i+1).arg(files.size()));}
   result={{"asset_ids",imported},{"imported",imported.size()},{"errors",errors}};
  }
  else if(kind=="backup.start"||kind=="backup.refresh"){
   auto ids=p["asset_ids"].toArray();QJsonArray errors;int completed=0;for(auto v:ids){check(cancel);QString aid=v.toString();try{if(p.contains("enabled")&&!p["enabled"].toBool()){Tx tx(d);Stmt u(d,"UPDATE assets SET backup_enabled=0 WHERE id=?");u.bind(1,aid);u.step();touch(d,aid);tx.commit();}else backupAsset(d,aid,root_,cancel);++completed;}catch(const Failure&e){if(e.code=="cancelled")throw;issue(d,aid,"backup_failed",QString::fromUtf8(e.what()));errors.append(QJsonObject{{"asset_id",aid},{"error",QString::fromUtf8(e.what())}});}progress(double(completed+errors.size())/qMax(1,ids.size()),"正在备份");}result={{"completed",completed},{"errors",errors}};if(!ids.isEmpty()&&completed==0&&!errors.isEmpty())fail("backup_failed",QString::fromUtf8(json(errors)).left(4000));
  }
  else if(kind=="resolve"){
   QString aid=text(p,"asset_id"),dest=resolvedRoot(d,aid,root_);QString path=restoreBundle(d,aid,root_,dest,cancel);result={{"path",path},{"from_backup",true},{"exists",true}};
  }
  else if(kind=="thumbnail"){
   QString aid=text(p,"asset_id");auto a=asset(d,aid,true);QString input=a["source_path"].toString();QString mediaKind=a["type"].toString();QString cover=p["preview_id"].toString().isEmpty()?a["cover_id"].toString():p["preview_id"].toString();
   if(cover!="auto")for(auto v:a["previews"].toArray()){auto preview=v.toObject();if(preview["id"].toString()==cover){input=preview["path"].toString();mediaKind="image";if(preview["missing"].toBool())fail("preview_missing","封面关联资产丢失");break;}}
   if(!QFileInfo(input).isFile())input=restoreBundle(d,aid,root_,resolvedRoot(d,aid,root_),cancel);
   QString output=text(p,"path"),configPath=root_+"/cache/preview-settings-"+id+".json",bindingsPath=root_+"/cache/preview-bindings-"+id+".json";auto config=settings(d);QJsonArray bindings=a["bindings"].toArray();for(int i=0;i<bindings.size();++i){auto b=bindings[i].toObject();QString path=b["path"].toString();if(!QFileInfo(path).isFile()){try{path=restoreBundle(d,b["target_id"].toString(),root_,resolvedRoot(d,b["target_id"].toString(),root_),cancel);}catch(const Failure&e){if(e.code=="cancelled")throw;issue(d,aid,"dependency_missing",QString::fromUtf8(e.what()),path);}}b["path"]=path;bindings[i]=b;}config["bindings"]=bindings;config["bindings_path"]=bindingsPath;config["library_root"]=root_;config["cache_root"]=root_+"/cache";{QSaveFile file(bindingsPath);if(!file.open(QIODevice::WriteOnly)||file.write(json(bindings))<0||!file.commit())fail("io","无法写入材质绑定快照");}writeJson(configPath,config);auto removeConfig=qScopeGuard([&]{QFile::remove(configPath);QFile::remove(bindingsPath);});
   QJsonObject decoded;try{decoded=process(python_,{helpers_+"/preview.py","--input",input,"--output",output,"--size",QString::number(p["size"].toInt()),"--kind",mediaKind,"--settings",configPath},cancel,180000);}catch(const Failure&e){if(e.code!="cancelled")issue(d,aid,"preview_failed",QString::fromUtf8(e.what()),a["source_path"].toString());throw;}
   if(!QFileInfo(output).isFile())fail("preview_failed","预览器未生成输出图片");Tx tx(d);if(asset(d,aid)["revision"]!=a["revision"])fail("revision_conflict","预览生成期间资产改变，请重新请求");auto md=a["metadata"].toObject();auto extra=decoded["metadata"].toObject();for(auto it=extra.begin();it!=extra.end();++it)md[it.key()]=it.value();for(auto key:{"width","height","canvas_width","canvas_height"})if(decoded.contains(key))md[key]=decoded[key];if(p["preview_id"].toString().isEmpty()){Stmt u(d,p["full"].toBool()?"UPDATE assets SET preview_path=?,preview_kind=?,metadata=? WHERE id=? AND revision=?":"UPDATE assets SET thumbnail_path=?,preview_kind=?,metadata=? WHERE id=? AND revision=?");u.bind(1,output);u.bind(2,decoded["preview_kind"].toString());u.bind(3,json(md));u.bind(4,aid);u.bind(5,a["revision"].toVariant().toLongLong());u.step();}if(mediaKind=="model"){clearIssue(d,aid,"material_slot_mismatch");for(auto value:extra["binding_issues"].toArray()){auto bindingIssue=value.toObject();issue(d,aid,"material_slot_mismatch",QString::fromUtf8(json(bindingIssue)),"slot:"+QString::number(bindingIssue["slot"].toInt()));}for(auto value:extra["materials"].toArray()){auto material=value.toObject();Stmt s(d,"UPDATE relations SET material_name=? WHERE asset_id=? AND slot=? AND material_name=''");s.bind(1,material["name"].toString());s.bind(2,aid);s.bind(3,material["slot"].toInt());s.step();}}clearIssue(d,aid,"preview_failed");bump(d);tx.commit();result={{"path",output},{"state","ready"},{"preview_kind",decoded["preview_kind"]},{"metadata",md}};
  }
  else if(kind=="export.assets"){
   QString dest=absolute(text(p,"destination"));if(text(p,"destination").isEmpty())fail("invalid_destination","请选择取用目录");if(within(dest,root_))fail("invalid_destination","取用目录不能在库数据内部");QDir().mkpath(dest);QJsonArray ids=p["asset_ids"].toArray();if(ids.isEmpty()){QString sql="SELECT id FROM assets WHERE deleted=0";QString dir=text(p,"directory_id");if(!dir.isEmpty())sql+=" AND directory_id IN (WITH RECURSIVE tree(id) AS(SELECT ? UNION ALL SELECT d.id FROM directories d JOIN tree ON d.parent_id=tree.id) SELECT id FROM tree)";Stmt s(d,sql);if(!dir.isEmpty())s.bind(1,dir);while(s.step())ids.append(s.str(0));}
   QJsonArray paths,errors;int n=0;for(auto v:ids){check(cancel);auto aid=v.toString();try{auto a=asset(d,aid,true);QString source=a["source_path"].toString(),virtualDir=directoryPath(d,a["directory_id"].toString());QString base=dest+(virtualDir.isEmpty()?"":"/"+virtualDir);QDir().mkpath(base);QString exported;
    if(QFileInfo(source).isFile()){auto deps=dependencies(source);auto bindings=a["bindings"].toArray();if(deps.size()==1&&bindings.isEmpty()){exported=uniquePath(base+'/'+a["name"].toString());copyFile(source,exported,cancel);}else{QString bundle=uniquePath(base+'/'+QFileInfo(a["name"].toString()).completeBaseName());QDir().mkpath(bundle);QString common=commonDirectory(deps);for(const auto&dep:deps)copyFile(dep,bundle+'/'+QDir(common).relativeFilePath(dep),cancel);QJsonArray exportedBindings;for(auto bv:bindings){auto binding=bv.toObject();QString bp=binding["path"].toString();if(!QFileInfo(bp).isFile())bp=restoreBundle(d,binding["target_id"].toString(),root_,resolvedRoot(d,binding["target_id"].toString(),root_),cancel);QString relative="bindings/"+binding["target_id"].toString()+"/"+QFileInfo(bp).fileName();copyFile(bp,bundle+'/'+relative,cancel);exportedBindings.append(QJsonObject{{"path",relative},{"role",binding["role"]},{"slot",binding["slot"]},{"normal_y",binding["normal_y"]}});}if(!exportedBindings.isEmpty())writeJson(bundle+"/imagidock-bindings.json",{{"bindings",exportedBindings}});exported=bundle+'/'+QDir(common).relativeFilePath(source);}}
    else {Stmt s(d,"SELECT backup_manifest FROM assets WHERE id=?");s.bind(1,aid);s.step();auto manifest=object(s.str(0));auto files=manifest["files"].toArray();if(files.size()==1){auto file=files[0].toObject();exported=uniquePath(base+'/'+a["name"].toString());decompressObject(root_+"/objects/"+file["hash"].toString()+".zst",exported,file["hash"].toString(),file["size"].toVariant().toLongLong(),cancel);}else{QString bundle=uniquePath(base+'/'+QFileInfo(a["name"].toString()).completeBaseName());exported=restoreBundle(d,aid,root_,bundle,cancel);}}paths.append(exported);
   }catch(const Failure&e){if(e.code=="cancelled")throw;errors.append(QJsonObject{{"asset_id",aid},{"error",QString::fromUtf8(e.what())}});}progress(double(++n)/qMax(1,ids.size()),"正在取用文件");}result={{"paths",paths},{"errors",errors}};if(paths.isEmpty()&&!errors.isEmpty())fail("export_failed",QString::fromUtf8(json(errors)).left(4000));
  }
  else if(kind=="issue.scan"||kind=="issue.relink_prefix"){
   QStringList ids;Stmt all(d,"SELECT id FROM assets WHERE deleted=0");while(all.step())ids<<all.str(0);int n=0;QJsonArray errors;
   for(auto aid:ids){check(cancel);auto a=asset(d,aid);QString src=a["source_path"].toString();if(kind=="issue.relink_prefix"){QString old=text(p,"old_prefix"),replacement=text(p,"new_prefix");if(old.isEmpty()||replacement.isEmpty())fail("invalid_prefix","路径前缀不能为空");if(within(src,old)){QString target=QDir(replacement).absoluteFilePath(QDir(old).relativeFilePath(src));if(QFileInfo(target).isFile()){auto response=handle({{"id",uuid()},{"method","asset.relink"},{"params",QJsonObject{{"asset_id",aid},{"path",target}}}});if(response.contains("error"))errors.append(response["error"]);else a=response["result"].toObject();}}src=a["source_path"].toString();}
    if(!QFileInfo(src).isFile())issue(d,aid,"source_missing","来源文件已丢失",src);else{clearIssue(d,aid,"source_missing");clearIssue(d,aid,"dependency_missing");for(const auto&dep:dependencies(src))if(!QFileInfo(dep).isFile())issue(d,aid,"dependency_missing","模型依赖文件已丢失",dep);if(a["backup_state"].toString()=="stale"){issue(d,aid,"backup_stale","来源文件已变更，备份需要更新",src);Stmt u(d,"UPDATE assets SET backup_state='stale' WHERE id=?");u.bind(1,aid);u.step();}QFileInfo current(src);if(current.size()!=a["size"].toVariant().toLongLong()||current.lastModified().toMSecsSinceEpoch()!=a["mtime"].toVariant().toLongLong()){Stmt u(d,"UPDATE assets SET size=?,mtime=?,thumbnail_path='',preview_path='',revision=revision+1 WHERE id=?");u.bind(1,current.size());u.bind(2,current.lastModified().toMSecsSinceEpoch());u.bind(3,aid);u.step();}}
    Stmt bm(d,"SELECT backup_manifest FROM assets WHERE id=?");bm.bind(1,aid);bm.step();auto manifest=object(bm.str(0));bool corrupt=false;for(auto v:manifest["files"].toArray()){auto f=v.toObject();QString obj=root_+"/objects/"+f["hash"].toString()+".zst";try{decompressObject(obj,{},f["hash"].toString(),f["size"].toVariant().toLongLong(),cancel);}catch(const Failure&e){if(e.code=="cancelled")throw;issue(d,aid,"backup_corrupt",QString::fromUtf8(e.what()),obj);corrupt=true;}}if(!corrupt)clearIssue(d,aid,"backup_corrupt");if(n%20==0||n+1==ids.size())progress(double(n+1)/qMax(1,ids.size()),"正在检查来源和备份");++n;
   }bump(d);result={{"checked",n},{"issues",issueList(d)},{"errors",errors}};
  }
  else if(kind=="library.export"){
   QString destination=absolute(text(p,"destination"));if(text(p,"destination").isEmpty())fail("invalid_destination","导出路径不能为空");if(QFileInfo(destination).isDir())destination+="/ImagiDock-library.imagidock";destination=uniquePath(destination);if(within(destination,root_))fail("invalid_destination","导出包不能位于库内部");
   if(p["complete_backup"].toBool()){QStringList ids;Stmt s(d,"SELECT id FROM assets WHERE deleted=0");while(s.step())ids<<s.str(0);for(int i=0;i<ids.size();++i){backupAsset(d,ids[i],root_,cancel);progress(double(i+1)/qMax(1,ids.size())*0.7,"补齐备份");}}
   QString stage=root_+"/staging/"+id;QDir().mkpath(stage);auto cleanup=qScopeGuard([&]{QDir(stage).removeRecursively();});{
    Db snapshot(stage+"/library.sqlite3");auto*b=sqlite3_backup_init(snapshot.h,"main",d.h,"main");if(!b)fail("database","无法创建数据库快照");auto finish=qScopeGuard([&]{sqlite3_backup_finish(b);});int code;do{check(cancel);code=sqlite3_backup_step(b,128);if(code==SQLITE_BUSY||code==SQLITE_LOCKED)QThread::msleep(20);}while(code==SQLITE_OK||code==SQLITE_BUSY||code==SQLITE_LOCKED);if(code!=SQLITE_DONE)fail("database","数据库快照失败");}
   // Only copy immutable objects referenced by the consistent snapshot.
   {Db snapshot(stage+"/library.sqlite3");QSet<QString>objects;Stmt s(snapshot,"SELECT backup_manifest FROM assets");while(s.step())for(auto v:object(s.str(0))["files"].toArray())objects.insert(v.toObject()["hash"].toString());for(const auto&hash:objects)copyFile(root_+"/objects/"+hash+".zst",stage+"/objects/"+hash+".zst",cancel);Stmt previews(snapshot,"SELECT DISTINCT path FROM previews WHERE path<>''");while(previews.step()){QString path=previews.str(0);if(!within(path,root_+"/previews"))fail("unsafe_path","预览路径不属于库");copyFile(path,stage+"/previews/"+QFileInfo(path).fileName(),cancel);}snapshot.exec("UPDATE assets SET thumbnail_path='',preview_path=''; DELETE FROM jobs;");setMeta(snapshot,"export_source_root",root_);}
   writeJson(stage+"/package.json",{{"format","imagidock-library"},{"version",1},{"library_id",meta(d,"library_id")},{"created",now()}});progress(0.8,"正在打包库");process(python_,{helpers_+"/archive_io.py","pack","--source",stage,"--output",destination},cancel,3600000);result={{"path",destination}};
  }
  else if(kind=="library.import"){
   QString destination=absolute(text(p,"destination"));if(text(p,"destination").isEmpty()||QFileInfo::exists(destination))fail("invalid_destination","新库目录必须尚不存在");if(within(destination,root_))fail("invalid_destination","新库不能嵌套在当前库内");QString stage=root_+"/staging/"+id;auto cleanup=qScopeGuard([&]{QDir(stage).removeRecursively();});process(python_,{helpers_+"/archive_io.py","unpack","--source",absolute(text(p,"path")),"--output",stage},cancel,3600000);
   QFile package(stage+"/package.json");if(!package.open(QIODevice::ReadOnly))fail("invalid_package","缺少包清单");auto pkg=QJsonDocument::fromJson(package.readAll()).object();if(pkg["format"]!="imagidock-library"||pkg["version"].toInt()!=1)fail("unsupported_version","不支持的库包版本");{
    Db imported(stage+"/library.sqlite3");if(meta(imported,"schema_version")!="1")fail("unsupported_version","不支持的数据库版本");Stmt integrity(imported,"PRAGMA integrity_check");if(!integrity.step()||integrity.str(0)!="ok")fail("invalid_package","数据库完整性校验失败");QString old=meta(imported,"export_source_root");Stmt previews(imported,"SELECT id,path FROM previews WHERE path<>''");QList<QPair<QString,QString>> changes;while(previews.step())changes.append({previews.str(0),destination+"/previews/"+QFileInfo(previews.str(1)).fileName()});for(const auto&change:changes){Stmt u(imported,"UPDATE previews SET path=? WHERE id=?");u.bind(1,change.second);u.bind(2,change.first);u.step();}setMeta(imported,"library_id",uuid());setMeta(imported,"export_source_root","");imported.exec("PRAGMA wal_checkpoint(TRUNCATE)");}
   // Stage lives under current library; copy to a sibling temporary target for cross-volume safety.
   QString publishing=destination+".importing-"+id;QDir().mkpath(publishing);auto removePartial=qScopeGuard([&]{QDir(publishing).removeRecursively();});QDirIterator files(stage,QDir::Files|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);while(files.hasNext()){check(cancel);QString src=files.next();copyFile(src,publishing+"/"+QDir(stage).relativeFilePath(src),cancel);}if(!QDir().rename(publishing,destination))fail("io","无法发布导入的库目录");removePartial.dismiss();result={{"root",destination},{"pipe",pipeName(destination)}};
  }
  else fail("unknown_job","未知任务类型");
  check(cancel);jobState(d,id,"completed",1,"完成",result);
 }catch(const Failure&e){try{Db d(database_);jobState(d,id,e.code=="cancelled"?"cancelled":"failed",0,e.code=="cancelled"?"已取消":"失败",{},QString::fromUtf8(e.what()));}catch(...){} }
 catch(const std::exception&e){try{Db d(database_);jobState(d,id,"failed",0,"失败",{},QString::fromUtf8(e.what()));}catch(...){} }
}
}
