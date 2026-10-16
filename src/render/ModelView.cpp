#include "ModelView.h"
#include "RenderScene.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QFileInfo>
#include <QEvent>
// Filament's WGL backend retains its HDC for the swap chain lifetime. Give the
// native child a private DC so Qt's backing-store painting cannot reuse it.
ModelView::ModelView(QWidget* parent):QWidget(parent,Qt::MSWindowsOwnDC){setAttribute(Qt::WA_NativeWindow);setAttribute(Qt::WA_PaintOnScreen);setAttribute(Qt::WA_NoSystemBackground);setFocusPolicy(Qt::StrongFocus);setMinimumSize(180,160);timer_.setInterval(16);connect(&timer_,&QTimer::timeout,this,&ModelView::tick);clock_.start();}
ModelView::~ModelView(){++generation_;timer_.stop();scene_.reset();}
void ModelView::createScene(){if(scene_)return;nativeSurface_=winId();scene_=std::make_unique<RenderScene>(reinterpret_cast<void*>(nativeSurface_),qRound(width()*devicePixelRatioF()),qRound(height()*devicePixelRatioF()));if(!scene_->error().isEmpty()){error_=scene_->error();emit errorOccurred(error_);}if(!pending_.bytes.isEmpty()){scene_->load(pending_);pending_={};}}
bool ModelView::event(QEvent* event){
    if(event->type()==QEvent::WinIdChange&&scene_&&effectiveWinId()!=nativeSurface_){
        timer_.stop();scene_.reset();nativeSurface_=0;
        QTimer::singleShot(0,this,[this]{if(!path_.isEmpty())loadModel(path_,bindings_);if(isVisible()){createScene();timer_.start();}});
    }
    return QWidget::event(event);
}
void ModelView::loadModel(const QString& path,const QJsonArray& bindings){path_=path;bindings_=bindings;error_.clear();const auto gen=++generation_;auto* watcher=new QFutureWatcher<PreparedModel>(this);loading_=watcher;connect(watcher,&QFutureWatcher<PreparedModel>::finished,this,[this,watcher,gen]{auto prepared=watcher->result();watcher->deleteLater();if(gen!=generation_)return;loading_=nullptr;if(!prepared.error.isEmpty()){error_=prepared.error;emit errorOccurred(error_);return;}if(isVisible())createScene();if(scene_){if(!scene_->load(prepared)){error_=scene_->error();emit errorOccurred(error_);return;}}else pending_=prepared;emit modelLoaded(prepared.metadata);});watcher->setFuture(QtConcurrent::run([path,bindings]{return prepareModel(path,bindings);}));}
void ModelView::setTextureBindings(const QJsonArray& bindings){if(!path_.isEmpty())loadModel(path_,bindings);}
void ModelView::resetCamera(){if(scene_)scene_->resetCamera();}
void ModelView::setAnimationPlaying(bool playing){playing_=playing;clock_.restart();}
void ModelView::setAnimationPosition(float seconds){animationTime_=seconds;if(scene_)scene_->animate(seconds);}
bool ModelView::saveSnapshot(const QString& path){if(!scene_)return false;return scene_->snapshot().save(path,"PNG");}
QString ModelView::lastError()const{return error_;}
QJsonObject ModelView::diagnostics()const{return {{"timer_active",timer_.isActive()},{"timer_ticks",qint64(tickCount_)},{"render_calls",qint64(renderCount_)},{"show_events",qint64(showCount_)},{"hide_events",qint64(hideCount_)},{"scene_exists",bool(scene_)},{"loading",loading_!=nullptr},{"visible",isVisible()},{"hidden",isHidden()},{"native_surface",qint64(nativeSurface_)},{"effective_win_id",qint64(effectiveWinId())}};}
void ModelView::showEvent(QShowEvent* e){++showCount_;QWidget::showEvent(e);createScene();clock_.restart();timer_.start();}
void ModelView::hideEvent(QHideEvent* e){++hideCount_;timer_.stop();QWidget::hideEvent(e);}
void ModelView::resizeEvent(QResizeEvent* e){QWidget::resizeEvent(e);if(scene_)scene_->resize(qRound(width()*devicePixelRatioF()),qRound(height()*devicePixelRatioF()));}
void ModelView::mousePressEvent(QMouseEvent* e){lastMouse_=e->pos();setFocus();}
void ModelView::mouseMoveEvent(QMouseEvent* e){if(!scene_)return;QPoint delta=e->pos()-lastMouse_;lastMouse_=e->pos();if(e->buttons()&Qt::LeftButton)scene_->orbit(delta.x(),delta.y());else if(e->buttons()&(Qt::RightButton|Qt::MiddleButton))scene_->pan(delta.x(),delta.y());}
void ModelView::wheelEvent(QWheelEvent* e){if(scene_)scene_->zoom(e->angleDelta().y());e->accept();}
void ModelView::tick(){++tickCount_;if(!scene_||!isVisible())return;++renderCount_;const qint64 ms=clock_.restart();if(playing_){animationTime_+=float(ms)/1000.f;scene_->animate(animationTime_);}scene_->render();}
