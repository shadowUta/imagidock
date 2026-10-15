#include "ModelPrepare.h"
#include "RenderScene.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cstdio>
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);QCommandLineParser parser;parser.addHelpOption();parser.addOption({"input","Source model","path"});parser.addOption({"output","PNG destination","path"});parser.addOption({"inspect","Inspect materials and dependencies without creating a renderer"});parser.addOption({"size","Image edge","size","512"});parser.addOption({"bindings","JSON bindings file","path"});parser.process(app);
    QJsonArray bindings;if(parser.isSet("bindings")){QFile f(parser.value("bindings"));if(f.open(QIODevice::ReadOnly))bindings=QJsonDocument::fromJson(f.readAll()).array();}
    auto prepared=prepareModel(parser.value("input"),bindings);QJsonObject result;
    if(!prepared.error.isEmpty())result={{"ok",false},{"error",prepared.error}};
    else if(parser.isSet("inspect"))result={{"ok",true},{"metadata",prepared.metadata}};
    else{int size=std::clamp(parser.value("size").toInt(),64,4096);RenderScene scene(nullptr,size,size);if(scene.load(prepared)){QImage image=scene.snapshot();QString output=parser.value("output");QDir().mkpath(QFileInfo(output).absolutePath());bool ok=!image.isNull()&&image.save(output,"PNG");result={{"ok",ok},{"path",output},{"width",size},{"height",size},{"preview_kind","model_snapshot"},{"metadata",scene.metadata()},{"error",scene.error()}};}else result={{"ok",false},{"error",scene.error()}};}
    QByteArray data=QJsonDocument(result).toJson(QJsonDocument::Compact);fwrite(data.constData(),1,data.size(),stdout);fputc('\n',stdout);return result["ok"].toBool()?0:1;
}
