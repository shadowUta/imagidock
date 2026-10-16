#include "Theme.h"
#include <QApplication>
#include <QVariant>

QString normalizeTheme(const QString &value){const auto id=value.trimmed().toLower();if(id=="light"||id=="warm ivory"||id=="warm_ivory"||id=="ivory")return "ivory";if(id=="ocean"||id=="mint")return id;return "graphite";}
ThemeColors themeColors(const QString &value){const QString id=normalizeTheme(value.isEmpty()&&qApp?qApp->property("imagidockTheme").toString():value);
    if(id=="ivory")return {id,QString::fromUtf8("暖白 · Warm Ivory"),QColor("#e9e3dc"),QColor("#ddd8d1"),QColor(255,253,249,115),QColor(255,255,253,145),QColor(255,255,255,210),QColor("#514b45"),QColor("#8c8277"),QColor("#b79576"),QColor("#fffaf1"),true};
    if(id=="ocean")return {id,QString::fromUtf8("深海蓝 · Ocean"),QColor("#182b46"),QColor("#10213a"),QColor(116,155,207,22),QColor(143,181,233,30),QColor(165,199,242,45),QColor("#e2eeff"),QColor("#91aacb"),QColor("#7bb5ff"),QColor("#3f7cbf"),false};
    if(id=="mint")return {id,QString::fromUtf8("冰青 · Mint"),QColor("#e2f0ef"),QColor("#cbdfe1"),QColor(250,255,255,112),QColor(255,255,255,130),QColor(255,255,255,215),QColor("#355c60"),QColor("#719597"),QColor("#69aca9"),QColor("#efffff"),true};
    return {id,QString::fromUtf8("石墨黑 · Graphite"),QColor("#24262b"),QColor("#171b22"),QColor(216,225,240,10),QColor(225,232,247,17),QColor(225,235,255,29),QColor("#e5e8ee"),QColor("#999faa"),QColor("#97acd1"),QColor("#67768f"),false};
}
