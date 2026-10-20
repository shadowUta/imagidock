#pragma once
#include "RpcClient.h"
#include <QAbstractListModel>
#include <QCache>
#include <QJsonArray>
#include <QListView>
#include <QPixmap>
#include <QSet>
#include <QStyledItemDelegate>
#include <QElapsedTimer>

class ThumbnailStore : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailStore(RpcClient *rpc, QObject *parent=nullptr);
    QPixmap get(const QJsonObject &asset, int size=256);
    void clear();
    void setMemoryBudget(int megabytes) { m_cache.setMaxCost(qBound(64,megabytes,2048)*1024); }
signals: void available(const QString &id);
private:
    RpcClient *m_rpc;
    QCache<QString,QPixmap> m_cache;
    QSet<QString> m_pending;
    QHash<QString,qint64> m_retryAt;
    void load(const QString &key,const QString &id,const QString &path);
};

class AssetModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { AssetRole=Qt::UserRole+1 };
    AssetModel(RpcClient *rpc, ThumbnailStore *thumbs, QObject *parent=nullptr);
    int rowCount(const QModelIndex &parent={}) const override;
    QVariant data(const QModelIndex &index,int role) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent={}) override;
    void setFilter(QJsonObject filter);
    void refreshRange(int first,int count);
    QJsonObject asset(int row) const;
    int total() const { return m_total; }
signals: void loaded(int count,int total); void error(const QString &message);
private:
    RpcClient *m_rpc; ThumbnailStore *m_thumbs; QJsonObject m_filter;
    QList<QJsonObject> m_assets; int m_total=0; bool m_loading=false,m_refreshing=false; quint64 m_generation=0;
};

class AssetDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit AssetDelegate(QObject *parent=nullptr):QStyledItemDelegate(parent){}
    void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override { return {m_size,m_size+64}; }
    void setCardSize(int size){m_size=size;}
private: int m_size=176;
};

class AssetGrid : public QListView {
    Q_OBJECT
public:
    explicit AssetGrid(QWidget *parent=nullptr);
    void setCardSize(int size);
    void beginMeasurement();
    QJsonObject timingReport() const;
signals: void filesDropped(const QStringList &paths);
protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool viewportEvent(QEvent *) override;
private:
    void scheduleGridLayout();
    void updateGridLayout();
    AssetDelegate *m_delegate;
    int m_preferredCardSize=176,m_columns=1;
    bool m_layoutQueued=false,m_measure=false;
    QElapsedTimer m_frameClock;
    QVector<double> m_paintSamples,m_frameSamples;
};
