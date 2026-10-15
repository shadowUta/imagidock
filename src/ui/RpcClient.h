#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QJsonObject>
#include <QPointer>
#include <QHash>
#include <QTimer>
#include <functional>

class RpcClient : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonObject &, const QString &)>;
    explicit RpcClient(const QString &library, QObject *parent = nullptr);
    void call(const QString &method, QJsonObject params, QObject *context, Reply reply);
    QString libraryPath() const { return m_library; }
    QString libraryId() const { return m_libraryId; }
    void refresh();
signals:
    void ready(const QJsonObject &info);
    void changed();
    void connectionChanged(bool connected, const QString &message);
private:
    struct Pending { QByteArray line; QPointer<QObject> context; Reply reply; qint64 started; bool sent = false; };
    QString m_library, m_pipe, m_libraryId;
    QLocalSocket m_socket;
    QHash<QString, Pending> m_pending;
    QByteArray m_buffer;
    QTimer m_retry, m_poll, m_timeout;
    qint64 m_revision = -1;
    bool m_launched = false, m_polling = false;
    void connectService();
    void read();
    void flush();
};
