#pragma once
#include "RpcClient.h"
#include <QGraphicsView>
#include <QJsonArray>
#include <QWidget>
#include <memory>
class QGraphicsPixmapItem;
class QMediaPlayer;
class QAudioOutput;
class QLabel;
class QSlider;
class QStackedWidget;
class QPushButton;
class ModelView;
class QAction;

class ImageCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget *parent=nullptr);
    void setImage(const QImage &image);
    void fit(); void actualSize();
protected:
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void drawBackground(QPainter *,const QRectF &) override;
private: QGraphicsPixmapItem *m_item=nullptr; bool m_fit=true;
};

class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(RpcClient *rpc,QWidget *parent=nullptr);
    void setAsset(const QJsonObject &asset);
    void setImagePath(const QString &path,const QString &caption={});
    void stop();
signals: void snapshotSaved(const QString &path); void metadataReady(const QJsonObject &metadata);
protected: void hideEvent(QHideEvent *) override;
private:
    RpcClient *m_rpc; QJsonObject m_asset; quint64 m_generation=0;
    QStackedWidget *m_stack; ImageCanvas *m_image; ModelView *m_model;
    QMediaPlayer *m_player; QAudioOutput *m_audio;
    QLabel *m_status,*m_time; QSlider *m_seek; QPushButton *m_play; QWidget *m_controls;
    void resolve(quint64 generation);
    void load(const QString &path,quint64 generation);
    void image(const QString &path,quint64 generation,const QString &caption);
    void renderBindings(const QString &path,quint64 generation);
    void resolveBinding(const QString &modelPath,QJsonObject binding,quint64 generation,std::shared_ptr<QJsonArray> bindings,std::shared_ptr<int> remaining,int attempts=0);
    void resolveTexturePreview(const QString &modelPath,QJsonObject binding,quint64 generation,std::shared_ptr<QJsonArray> bindings,std::shared_ptr<int> remaining,int attempts=0);
    void waitBinding(const QString &jobId,const QString &modelPath,QJsonObject binding,quint64 generation,std::shared_ptr<QJsonArray> bindings,std::shared_ptr<int> remaining,int attempts);
    void consumeBinding(const QString &modelPath,QJsonObject binding,const QString &resolved,const QString &error,quint64 generation,std::shared_ptr<QJsonArray> bindings,std::shared_ptr<int> remaining);
    QStringList m_bindingWarnings;
    QList<QAction*> m_imageActions,m_modelActions;
};
