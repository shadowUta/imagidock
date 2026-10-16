#pragma once
#include <QColor>
#include <QString>

struct ThemeColors {
    QString id,name;
    QColor top,bottom,panel,card,edge,text,muted,accent,glow;
    bool light=false;
};
QString normalizeTheme(const QString &theme);
ThemeColors themeColors(const QString &theme={});
