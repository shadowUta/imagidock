#pragma once
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <memory>
#include "ModelPrepare.h"
class RenderScene {
public:
    RenderScene(void* window,int width,int height);
    ~RenderScene();
    bool load(const PreparedModel& prepared);
    void resize(int width,int height);
    void orbit(float dx,float dy);
    void pan(float dx,float dy);
    void zoom(float delta);
    void resetCamera();
    void animate(float time);
    void render();
    QImage snapshot();
    QString error() const;
    QJsonObject metadata() const;
private:
    struct Impl;std::unique_ptr<Impl> d;
};
