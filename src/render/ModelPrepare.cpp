#include "ModelPrepare.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QImage>
#include <QBuffer>
#include <QUrl>
#include <QMap>
#include <QtEndian>
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <algorithm>
#include <cmath>

namespace {
quint32 word(const QByteArray& b,qsizetype p){return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+p));}
void appendWord(QByteArray& b,quint32 x){x=qToLittleEndian(x);b.append(reinterpret_cast<const char*>(&x),4);}
QByteArray readFile(const QString& p){QFile f(p);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
QByteArray dataUri(const QString& s){int comma=s.indexOf(',');return comma<0?QByteArray():QByteArray::fromBase64(s.mid(comma+1).toLatin1());}
QImage textureImage(const QJsonObject& doc,int texture,const QByteArray& bin,const QString& base){
    auto textures=doc["textures"].toArray(),images=doc["images"].toArray();
    if(texture<0||texture>=textures.size())return {};
    int i=textures[texture].toObject()["source"].toInt(-1);if(i<0||i>=images.size())return {};
    auto image=images[i].toObject();auto uri=image["uri"].toString();
    if(!uri.isEmpty())return QImage::fromData(uri.startsWith("data:")?dataUri(uri):readFile(QDir(base).filePath(QUrl::fromPercentEncoding(uri.toUtf8()))));
    auto views=doc["bufferViews"].toArray();int vi=image["bufferView"].toInt(-1);if(vi<0||vi>=views.size())return {};
    auto v=views[vi].toObject();auto buffers=doc["buffers"].toArray();int bi=v["buffer"].toInt();QByteArray bytes=bin;
    if(bi<buffers.size()){auto u=buffers[bi].toObject()["uri"].toString();if(!u.isEmpty())bytes=u.startsWith("data:")?dataUri(u):readFile(QDir(base).filePath(QUrl::fromPercentEncoding(u.toUtf8())));}
    auto offset=qint64(v["byteOffset"].toDouble()),length=qint64(v["byteLength"].toDouble());
    if(offset<0||length<0||offset>bytes.size()||length>bytes.size()-offset)return {};
    return QImage::fromData(bytes.mid(offset,length));
}
int addImage(QJsonObject& doc,const QImage& image){
    QByteArray bytes;QBuffer out(&bytes);out.open(QIODevice::WriteOnly);image.save(&out,"PNG");
    auto images=doc["images"].toArray(),textures=doc["textures"].toArray();int i=images.size(),t=textures.size();
    images.append(QJsonObject{{"uri",QString("data:image/png;base64,")+QString::fromLatin1(bytes.toBase64())}});
    textures.append(QJsonObject{{"source",i}});doc["images"]=images;doc["textures"]=textures;return t;
}
QJsonObject ref(int i){return QJsonObject{{"index",i}};}
}
PreparedModel prepareModel(const QString& path,const QJsonArray& bindings){
    PreparedModel result;result.resourcePath=path;QFileInfo fi(path);
    if(!fi.exists()){result.error=QStringLiteral("模型来源不存在");return result;}
    if(fi.size()>1024ll*1024*1024){result.error=QStringLiteral("模型超过单次预览 1 GiB 限制");return result;}
    QByteArray bytes;QJsonArray dependencies;QMap<QString,QString> textureSources;
    const QString ext=fi.suffix().toLower();
    if(ext=="glb"||ext=="gltf")bytes=readFile(path);
    else{
        Assimp::Importer importer;
        // Native Windows paths pass through a UTF-8-aware IO adapter in newer Assimp.
        const aiScene* scene=importer.ReadFile(path.toUtf8().constData(),aiProcess_Triangulate|aiProcess_GenSmoothNormals|aiProcess_CalcTangentSpace|aiProcess_JoinIdenticalVertices|aiProcess_ValidateDataStructure);
        if(!scene){result.error=QStringLiteral("模型导入失败：")+QString::fromUtf8(importer.GetErrorString());return result;}
        for(unsigned m=0;m<scene->mNumMaterials;++m)for(int type=1;type<=AI_TEXTURE_TYPE_MAX;++type){
            auto textureType=static_cast<aiTextureType>(type);
            for(unsigned t=0;t<scene->mMaterials[m]->GetTextureCount(textureType);++t){
                aiString uri;if(scene->mMaterials[m]->GetTexture(textureType,t,&uri)!=AI_SUCCESS)continue;
                QString name=QString::fromUtf8(uri.C_Str());if(name.isEmpty()||name.startsWith('*')||scene->GetEmbeddedTexture(uri.C_Str()))continue;
                QString source=QDir(fi.absolutePath()).absoluteFilePath(name);
                if(!QFileInfo::exists(source)){
                    QString filename=QFileInfo(QDir::fromNativeSeparators(name)).fileName();
                    for(auto candidate:{fi.dir().absoluteFilePath(filename),fi.dir().absoluteFilePath(fi.completeBaseName()+".fbm/"+filename)})if(QFileInfo::exists(candidate)){source=candidate;break;}
                }
                textureSources[name]=source;if(!dependencies.contains(source))dependencies.append(source);
            }
        }
        Assimp::Exporter exporter;const auto* blob=exporter.ExportToBlob(scene,"glb2");
        if(!blob){result.error=QStringLiteral("模型转换失败：")+QString::fromUtf8(exporter.GetErrorString());return result;}
        bytes=QByteArray(static_cast<const char*>(blob->data),qsizetype(blob->size));
    }
    QByteArray json,bin;
    const bool glb=bytes.size()>=12&&word(bytes,0)==0x46546c67;
    if(glb){
        if(word(bytes,4)!=2||word(bytes,8)>quint32(bytes.size())){result.error="Invalid GLB header";return result;}
        for(qsizetype pos=12;pos+8<=bytes.size();){quint32 len=word(bytes,pos),type=word(bytes,pos+4);pos+=8;if(len>quint64(bytes.size()-pos)){result.error="Truncated GLB";return result;}if(type==0x4e4f534a)json=bytes.mid(pos,len);else if(type==0x004e4942)bin=bytes.mid(pos,len);pos+=len;}
    }else json=bytes;
    QJsonParseError err;QJsonDocument parsed=QJsonDocument::fromJson(json,&err);
    if(!parsed.isObject()){result.error="Invalid glTF JSON: "+err.errorString();return result;}
    auto doc=parsed.object();auto materials=doc["materials"].toArray();QJsonArray materialSlots,bindingIssues,validBindings;
    for(int i=0;i<materials.size();++i)materialSlots.append(QJsonObject{{"slot",i+1},{"name",materials[i].toObject()["name"].toString(QStringLiteral("材质 %1").arg(i+1))}});
    for(auto value:bindings){
        auto binding=value.toObject();int index=binding["slot"].toInt(1)-1;
        QString expected=binding["material_name"].toString();
        QString actual=index>=0&&index<materialSlots.size()?materialSlots[index].toObject()["name"].toString():QString();
        if(index<0||index>=materialSlots.size()||(!expected.isEmpty()&&expected!=actual)){
            bindingIssues.append(QJsonObject{{"slot",index+1},{"expected",expected},{"actual",actual},{"role",binding["role"]}});
        }else validBindings.append(binding);
    }
    for(int slot=0;slot<materials.size();++slot){
        QJsonObject material=materials[slot].toObject(),pbr=material["pbrMetallicRoughness"].toObject();QMap<QString,QImage> maps;
        for(auto item:validBindings){auto b=item.toObject();if(b["slot"].toInt(1)!=slot+1)continue;QString role=b["role"].toString();QImage image(b["path"].toString());if(image.isNull())continue;image=image.convertToFormat(QImage::Format_RGBA8888);
            if(role=="normal"&&b["normal_y"].toString()=="directx")for(int y=0;y<image.height();++y){uchar* row=image.scanLine(y);for(int x=0;x<image.width();++x)row[4*x+1]=255-row[4*x+1];}
            maps[role]=image;
        }
        if(maps.isEmpty())continue;
        if(maps.contains("base_color")){pbr["baseColorTexture"]=ref(addImage(doc,maps["base_color"]));pbr["baseColorFactor"]=QJsonArray{1,1,1,1};}
        if(maps.contains("normal"))material["normalTexture"]=ref(addImage(doc,maps["normal"]));
        if(maps.contains("emissive")){material["emissiveTexture"]=ref(addImage(doc,maps["emissive"]));material["emissiveFactor"]=QJsonArray{1,1,1};}
        if(maps.contains("opacity")){
            auto color=maps.value("base_color",textureImage(doc,pbr["baseColorTexture"].toObject()["index"].toInt(-1),bin,fi.absolutePath()));
            if(color.isNull()){color=QImage(maps["opacity"].size(),QImage::Format_RGBA8888);color.fill(Qt::white);}color=color.convertToFormat(QImage::Format_RGBA8888);auto opacity=maps["opacity"].scaled(color.size()).convertToFormat(QImage::Format_RGBA8888);
            for(int y=0;y<color.height();++y){auto* dst=color.scanLine(y);auto* src=opacity.constScanLine(y);for(int x=0;x<color.width();++x)dst[x*4+3]=src[x*4];}
            pbr["baseColorTexture"]=ref(addImage(doc,color));material["alphaMode"]="BLEND";
        }
        if(maps.contains("orm")||maps.contains("ao")||maps.contains("roughness")||maps.contains("metallic")){
            QImage orm=maps.value("orm",textureImage(doc,pbr["metallicRoughnessTexture"].toObject()["index"].toInt(-1),bin,fi.absolutePath()));
            if(orm.isNull()){QSize s(1,1);for(auto it=maps.begin();it!=maps.end();++it)if(it.value().width()*it.value().height()>s.width()*s.height())s=it.value().size();orm=QImage(s,QImage::Format_RGBA8888);orm.fill(QColor(255,255,255));}
            orm=orm.convertToFormat(QImage::Format_RGBA8888);
            const QStringList roles{"ao","roughness","metallic"};
            for(int c=0;c<3;++c)if(maps.contains(roles[c])){auto channel=maps[roles[c]].scaled(orm.size()).convertToFormat(QImage::Format_RGBA8888);for(int y=0;y<orm.height();++y){auto* dst=orm.scanLine(y);auto* src=channel.constScanLine(y);for(int x=0;x<orm.width();++x)dst[4*x+c]=src[4*x];}}
            int index=addImage(doc,orm);pbr["metallicRoughnessTexture"]=ref(index);
            if(maps.contains("roughness")||maps.contains("orm"))pbr["roughnessFactor"]=1;
            if(maps.contains("metallic")||maps.contains("orm"))pbr["metallicFactor"]=1;
            if(maps.contains("ao")||maps.contains("orm"))material["occlusionTexture"]=ref(index);
        }
        material["pbrMetallicRoughness"]=pbr;materials[slot]=material;
    }
    doc["materials"]=materials;
    // Embed relative buffers/images, so Unicode paths and sidecar resources don't depend on C file APIs.
    for(const auto key:{"buffers","images"}){auto entries=doc[key].toArray();for(int i=0;i<entries.size();++i){auto v=entries[i].toObject();QString uri=v["uri"].toString();if(uri.isEmpty()||uri.startsWith("data:"))continue;QString decoded=QUrl::fromPercentEncoding(uri.toUtf8());QString source=textureSources.value(decoded,QDir(fi.absolutePath()).filePath(decoded));QByteArray data=readFile(source);if(data.isEmpty()){result.error=QStringLiteral("缺少模型依赖：")+source;return result;}if(!dependencies.contains(source))dependencies.append(source);QString mime="application/octet-stream";if(QString(key)=="images"){QString suffix=QFileInfo(source).suffix().toLower();mime=suffix=="jpg"||suffix=="jpeg"?"image/jpeg":suffix=="webp"?"image/webp":suffix=="ktx2"?"image/ktx2":"image/png";}v["uri"]="data:"+mime+";base64,"+QString::fromLatin1(data.toBase64());entries[i]=v;}doc[key]=entries;}
    json=QJsonDocument(doc).toJson(QJsonDocument::Compact);
    if(glb){while(json.size()%4)json.append(' ');while(bin.size()%4)bin.append('\0');QByteArray packed;appendWord(packed,0x46546c67);appendWord(packed,2);appendWord(packed,quint32(12+8+json.size()+(bin.isEmpty()?0:8+bin.size())));appendWord(packed,quint32(json.size()));appendWord(packed,0x4e4f534a);packed+=json;if(!bin.isEmpty()){appendWord(packed,quint32(bin.size()));appendWord(packed,0x004e4942);packed+=bin;}result.bytes=packed;}
    else result.bytes=json;
    result.metadata={{"materials",materialSlots},{"dependencies",dependencies},{"binding_issues",bindingIssues},{"material_count",materials.size()},{"mesh_count",doc["meshes"].toArray().size()},{"animation_count",doc["animations"].toArray().size()},{"format",ext},{"renderer","Filament 1.78.0 / OpenGL"}};
    return result;
}
