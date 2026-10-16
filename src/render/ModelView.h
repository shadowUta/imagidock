#pragma once
#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <memory>
#include "ModelPrepare.h"
class RenderScene;
class ModelView : public QWidget {
    Q_OBJECT
public:
    explicit ModelView(QWidget* parent=nullptr);
    ~ModelView() override;
    void loadModel(const QString& path,const QJsonArray& bindings={});
    void setTextureBindings(const QJsonArray& bindings);
    void resetCamera();
    void setAnimationPlaying(bool playing);
    void setAnimationPosition(float seconds);
    bool saveSnapshot(const QString& path);
    QString lastError() const;
    QJsonObject diagnostics() const;
    QPaintEngine* paintEngine() const override { return nullptr; }
signals:
    void modelLoaded(const QJsonObject& metadata);
    void errorOccurred(const QString& message);
protected:
    bool event(QEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void paintEvent(QPaintEvent*) override {}
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    void tick();
    void createScene();
    std::unique_ptr<RenderScene> scene_;
    QTimer timer_;
    QElapsedTimer clock_;
    QString path_,error_;
    QJsonArray bindings_;
    QFutureWatcher<PreparedModel>* loading_=nullptr;
    PreparedModel pending_;
    QPoint lastMouse_;
    bool playing_=false;
    float animationTime_=0;
    quint64 generation_=0;
    WId nativeSurface_=0;
    quint64 tickCount_=0, renderCount_=0, showCount_=0, hideCount_=0;
};
