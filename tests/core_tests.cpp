#include "core/Library.h"
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <sqlite3.h>

using imagidock::Library;
class CoreTests final:public QObject {
 Q_OBJECT
 static QJsonObject call(Library&lib,const QString&method,QJsonObject params={}){return lib.handle({{"id","test"},{"method",method},{"params",params}});}
 static QJsonObject result(Library&lib,const QString&m,QJsonObject p={}){auto r=call(lib,m,p);if(r.contains("error"))qWarning()<<m<<r;return r["result"].toObject();}
 static QJsonObject wait(Library&lib,QString id,int timeout=30000){QElapsedTimer t;t.start();while(t.elapsed()<timeout){auto j=result(lib,"job.get",{{"job_id",id}});QString state=j["state"].toString();if(state!="queued"&&state!="running")return j;QThread::msleep(10);}return {{"state","timeout"}};}
 static QJsonObject run(Library&lib,QString method,QJsonObject params){auto j=wait(lib,result(lib,method,params)["job_id"].toString());if(j["state"]!="completed")qWarning()<<method<<j;return j;}
 static void write(const QString&p,QByteArray bytes){QDir().mkpath(QFileInfo(p).absolutePath());QFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())qFatal("fixture write failed");}
private slots:
 void libraryLifecycle(){
  QString project=QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath("..");QDir().mkpath(project+"/data/tests");QTemporaryDir temp(project+"/data/tests/core-XXXXXX");QVERIFY(temp.isValid());QString root=temp.path();QString fixtures=root+"/素材";QDir().mkpath(fixtures);
  QImage pic(48,32,QImage::Format_RGBA8888);pic.fill(QColor(70,130,210));QVERIFY(pic.save(fixtures+"/颜色.png"));pic.fill(QColor(128,128,255));QVERIFY(pic.save(fixtures+"/法线.png"));
  write(fixtures+"/mesh.bin",QByteArray(128,'b'));write(fixtures+"/模型.gltf",R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":128}],"images":[{"uri":"颜色.png"}]})");write(fixtures+"/ignore.txt","unsupported");
  QString python=project+"/.tools/adapters-env/Scripts/python.exe";Library lib(root+"/库",project+"/helpers",python);
  auto info=result(lib,"library.info");QVERIFY(!info["library_id"].toString().isEmpty());QCOMPARE(info["counts"].toObject()["all"].toInt(),0);
  auto imported=run(lib,"asset.import",{{"paths",QJsonArray{fixtures}},{"backup",true}});QCOMPARE(imported["state"].toString(),"completed");QCOMPARE(imported["result"].toObject()["imported"].toInt(),3);
  auto list=result(lib,"asset.list");QCOMPARE(list["total"].toInt(),3);QString image,normal,model;for(auto v:list["items"].toArray()){auto a=v.toObject();QCOMPARE(a["backup_state"].toString(),"ready");if(a["name"]=="颜色.png")image=a["id"].toString();if(a["name"]=="法线.png")normal=a["id"].toString();if(a["type"]=="model")model=a["id"].toString();}
  QVERIFY(!image.isEmpty()&&!model.isEmpty());QCOMPARE(result(lib,"directory.list")["items"].toArray().size(),1);
  auto old=result(lib,"asset.get",{{"asset_id",image}});auto edited=result(lib,"asset.update",{{"asset_id",image},{"revision",old["revision"]},{"values",QJsonObject{{"tags",QJsonArray{"角色","蓝色"}},{"favorite",true}}}});QVERIFY(edited["revision"].toInt()>old["revision"].toInt());
  auto conflict=call(lib,"asset.update",{{"asset_id",image},{"revision",old["revision"]},{"values",QJsonObject{{"name","bad"}}}});QCOMPARE(conflict["error"].toObject()["code"].toString(),"revision_conflict");
  auto normalA=result(lib,"asset.get",{{"asset_id",normal}});result(lib,"asset.update",{{"asset_id",normal},{"revision",normalA["revision"]},{"values",QJsonObject{{"tags",QJsonArray{"法线"}}}}});
  QCOMPARE(result(lib,"asset.list",{{"tags",QJsonArray{"角色","法线"}}})["total"].toInt(),2);QCOMPARE(result(lib,"asset.list",{{"tags",QJsonArray{"角色","法线"}},{"types",QJsonArray{"model"}}})["total"].toInt(),0);
  auto mdl=result(lib,"asset.get",{{"asset_id",model}});QJsonArray rel{QJsonObject{{"target_id",image},{"role","base_color"},{"slot",4}},QJsonObject{{"target_id",normal},{"role","normal"},{"slot",4},{"normal_y","directx"}}};mdl=result(lib,"relation.set",{{"asset_id",model},{"revision",mdl["revision"]},{"items",rel}});QCOMPARE(mdl["bindings"].toArray().size(),2);QCOMPARE(mdl["bindings"].toArray()[1].toObject()["normal_y"].toString(),"directx");
  auto duplicate=rel;duplicate.append(rel[0]);auto failed=call(lib,"relation.set",{{"asset_id",model},{"revision",mdl["revision"]},{"items",duplicate}});QCOMPARE(failed["error"].toObject()["code"].toString(),"duplicate_binding");QCOMPARE(result(lib,"asset.get",{{"asset_id",model}})["relations"].toArray().size(),2);
  QCOMPARE(call(lib,"role.delete",{{"role_id","normal"}})["error"].toObject()["code"].toString(),"builtin_role");auto custom=result(lib,"role.create",{{"name","设计参考"}});QVERIFY(!custom["id"].toString().isEmpty());QVERIFY(result(lib,"role.delete",{{"role_id",custom["id"]}})["deleted"].toBool());
  mdl=result(lib,"preview.set",{{"asset_id",model},{"revision",mdl["revision"]},{"items",QJsonArray{QJsonObject{{"target_id",image}},QJsonObject{{"path",fixtures+"/法线.png"},{"label","外部图"}}}},{"cover_id",image}});QCOMPARE(mdl["previews"].toArray().size(),2);QVERIFY(!mdl["previews"].toArray()[0].toObject()["target_id"].toString().isEmpty());QVERIFY(mdl["previews"].toArray()[1].toObject()["target_id"].toString().isEmpty());QCOMPARE(result(lib,"asset.list")["total"].toInt(),3);
  QCOMPARE(run(lib,"backup.refresh",{{"asset_ids",QJsonArray{model}}})["state"].toString(),"completed");
  result(lib,"asset.delete",{{"asset_ids",QJsonArray{image}}});QVERIFY(QFileInfo::exists(fixtures+"/颜色.png"));QVERIFY(result(lib,"asset.get",{{"asset_id",model}})["relations"].toArray()[0].toObject()["missing"].toBool());QCOMPARE(result(lib,"asset.list")["total"].toInt(),2);
  result(lib,"asset.placeholder",{{"asset_id",image}});auto placeholder=result(lib,"asset.get",{{"asset_id",image}});QVERIFY(placeholder["placeholder"].toBool());QVERIFY(placeholder["missing"].toBool());result(lib,"asset.restore",{{"asset_ids",QJsonArray{image}}});
  auto dirs=result(lib,"directory.list")["items"].toArray();auto child=result(lib,"directory.create",{{"name","子目录"},{"parent_id",dirs[0].toObject()["id"]}});auto cycle=call(lib,"directory.update",{{"directory_id",dirs[0].toObject()["id"]},{"parent_id",child["id"]}});QCOMPARE(cycle["error"].toObject()["code"].toString(),"directory_cycle");
  auto exportJob=run(lib,"export.assets",{{"asset_ids",QJsonArray{model}},{"destination",root+"/取用"}});QCOMPARE(exportJob["state"].toString(),"completed");QString exportPath=exportJob["result"].toObject()["paths"].toArray()[0].toString();QVERIFY(QFileInfo::exists(exportPath));QVERIFY(QFileInfo::exists(QFileInfo(exportPath).absolutePath()+"/mesh.bin"));
  QFile original(fixtures+"/模型.gltf");QVERIFY(original.open(QIODevice::ReadOnly));QByteArray originalHash=QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256);original.close();QVERIFY(QFile::remove(fixtures+"/模型.gltf"));QVERIFY(QFile::remove(fixtures+"/mesh.bin"));
  auto scan=run(lib,"issue.scan",{});QCOMPARE(scan["state"].toString(),"completed");QCOMPARE(result(lib,"asset.list",{{"status","missing"}})["total"].toInt(),1);auto resolved=result(lib,"asset.resolve",{{"asset_id",model}});QVERIFY(resolved.contains("job_id"));QCOMPARE(wait(lib,resolved["job_id"].toString())["state"].toString(),"completed");resolved=result(lib,"asset.resolve",{{"asset_id",model}});QVERIFY(resolved["exists"].toBool());QVERIFY(resolved["from_backup"].toBool());QFile restored(resolved["path"].toString());QVERIFY(restored.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(restored.readAll(),QCryptographicHash::Sha256),originalHash);QVERIFY(QFileInfo::exists(QFileInfo(resolved["path"].toString()).absolutePath()+"/mesh.bin"));restored.close();
  if(QFileInfo::exists(python)){
   auto package=run(lib,"library.export",{{"destination",root+"/bundle.imagidock"}});QCOMPARE(package["state"].toString(),"completed");QVERIFY(QFileInfo::exists(root+"/bundle.imagidock"));auto reimport=run(lib,"library.import",{{"path",root+"/bundle.imagidock"},{"destination",root+"/独立库"}});QCOMPARE(reimport["state"].toString(),"completed");Library independent(root+"/独立库",project+"/helpers",python);QVERIFY(result(independent,"library.info")["library_id"]!=info["library_id"]);QCOMPARE(result(independent,"asset.list")["total"].toInt(),3);auto importedModel=result(independent,"asset.get",{{"asset_id",model}});QVERIFY(importedModel["previews"].toArray()[1].toObject()["path"].toString().startsWith(root+"/独立库/"));
  }
  QVERIFY(!Library::pipeName(root+"/库").isEmpty());QCOMPARE(Library::pipeName(root+"/库"),Library::pipeName(root+"/库/."));
 }
 void interruptedRecovery(){
  QString project=QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath("..");QDir().mkpath(project+"/data/tests");QTemporaryDir temp(project+"/data/tests/recovery-XXXXXX");QString root=temp.path();{Library lib(root,project+"/helpers","");}
  sqlite3* db=nullptr;QByteArray p=(root+"/library.sqlite3").toUtf8();QCOMPARE(sqlite3_open(p.constData(),&db),SQLITE_OK);QCOMPARE(sqlite3_exec(db,"INSERT INTO jobs(id,kind,args,state,created,updated) VALUES('interrupted','issue.scan','{}','running','0','0')",nullptr,nullptr,nullptr),SQLITE_OK);sqlite3_close(db);
  Library reopened(root,project+"/helpers","");QCOMPARE(result(reopened,"job.get",{{"job_id","interrupted"}})["state"].toString(),"interrupted");auto retry=result(reopened,"job.retry",{{"job_id","interrupted"}});QVERIFY(retry["id"].toString()!="interrupted");QCOMPARE(wait(reopened,retry["id"].toString())["state"].toString(),"completed");
 }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
