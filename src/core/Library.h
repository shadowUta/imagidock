#pragma once
#include <QJsonObject>
#include <QHash>
#include <QMutex>
#include <QThreadPool>
#include <QString>
#include <atomic>
#include <memory>

namespace imagidock {
// Thread-safe service facade. Call handle() off the UI/IPC event thread.
class Library final {
public:
    Library(QString root, QString helpers, QString python);
    ~Library();
    QJsonObject handle(const QJsonObject &request);
    QString root() const { return root_; }
    static QString pipeName(const QString &root);
private:
    QString root_, database_, helpers_, python_;
    QThreadPool workers_;
    QThreadPool previews_;
    QMutex jobsMutex_;
    QHash<QString, std::shared_ptr<std::atomic_bool>> cancellations_;
    QHash<QString, QString> thumbnailJobs_;
    std::atomic<qint64> nextCacheSweep_{0};
    QJsonObject dispatch(const QString &, const QJsonObject &);
    QString startJob(const QString &, const QJsonObject &);
    void runJob(const QString &, const QString &, const QJsonObject &, const std::shared_ptr<std::atomic_bool> &);
};
}
