#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
struct PreparedModel {
    QByteArray bytes;
    QString resourcePath;
    QString error;
    QJsonObject metadata;
};
PreparedModel prepareModel(const QString& path,const QJsonArray& bindings);
