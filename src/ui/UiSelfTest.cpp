#include "UiSelfTest.h"
#include "ApplicationCapture.h"
#include "GlassDialog.h"
#include "PreviewPane.h"
#include <QLayout>
#include <QComboBox>
#include <QLabel>
#include <QMenuBar>
#include <QSplitter>
#include <QToolBar>
#include "Theme.h"
#include <QKeyEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QToolButton>
#include <QSet>
#include "../render/ModelView.h"
#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPainter>
#include <QScreen>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QSaveFile>
#include <QTimer>
#include <memory>

namespace {
struct State : public QObject {
    QPointer<MainWindow> window,second,detachedWindow;RpcClient *rpc;QString report,screenshot,modelId,imageId,normalId;QPointer<QWidget> dialogOrigin;QJsonArray assertions,captureWarnings,nativePresentations,windowStatesReport;QRect initialGeometry;bool done=false;int attempts=0;
    State(MainWindow *w,RpcClient *r,const QString &p,const QString &s):QObject(qApp),window(w),rpc(r),report(p),screenshot(s){}
    bool check(const QString &name,bool passed){assertions.append(QJsonObject{{"name",name},{"passed",passed}});if(!passed)finish(false,name);return passed;}
    void finish(bool ok,const QString &message={}){if(done)return;done=true;if(window&&!screenshot.isEmpty()){QDir().mkpath(QFileInfo(screenshot).absolutePath());const auto capture=captureApplicationWidget(window);if(capture.error.isEmpty())capture.image.save(screenshot);else captureWarnings.append(capture.error);}if(!report.isEmpty()){QDir().mkpath(QFileInfo(report).absolutePath());QSaveFile out(report);if(out.open(QIODevice::WriteOnly)){out.write(QJsonDocument(QJsonObject{{"ok",ok},{"message",message},{"assertions",assertions},{"capture_warnings",captureWarnings},{"native_presentations",nativePresentations},{"window_states",windowStatesReport},{"library",rpc->libraryPath()},{"device_pixel_ratio",window?window->devicePixelRatioF():0.},{"screen_available",window?QJsonArray{window->screen()->availableGeometry().width(),window->screen()->availableGeometry().height()}:QJsonArray{}}}).toJson());out.commit();}}if(second)second->close();rpc->call("service.shutdown",{{"cancel_jobs",true}},this,[ok](const QJsonObject &,const QString &){qApp->exit(ok?0:2);});QTimer::singleShot(3500,qApp,[ok]{qApp->exit(ok?0:2);});}
    void call(const QString &method,const QJsonObject &params,std::function<void(const QJsonObject&)> next){rpc->call(method,params,this,[this,method,next](const QJsonObject &r,const QString &err){if(done)return;if(!err.isEmpty()){finish(false,method+": "+err);return;}next(r);});}
    void awaitJob(const QString &id,std::function<void()> next){if(done)return;if(++attempts>300){finish(false,"job timeout");return;}call("job.get",{{"job_id",id}},[this,id,next](const QJsonObject &job){const auto state=job["state"].toString();if(state=="completed"){attempts=0;next();}else if(state=="failed"||state=="cancelled"||state=="interrupted")finish(false,job["error"].toString());else QTimer::singleShot(150,this,[this,id,next]{awaitJob(id,next);});});}
    QString fixtures(){const QString directory=QFileInfo(rpc->libraryPath()).absolutePath()+"/ui-fixtures";QDir().mkpath(directory);QImage color(320,240,QImage::Format_RGBA8888);color.fill(QColor("#67c8b6"));{QPainter p(&color);p.setPen(Qt::NoPen);p.setBrush(QColor("#ffe0a6"));p.drawEllipse(80,30,160,160);p.setPen(QColor("#102927"));p.drawText(color.rect(),Qt::AlignCenter,"ImagiDock UI");}color.save(directory+"/UI color.png");QImage normal(64,64,QImage::Format_RGBA8888);normal.fill(QColor(128,128,255));normal.save(directory+"/UI normal.png");
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);for(int i=0;i<4;++i){const float x=float(i%2)*1.3f-.65f,z=float(i/2)*1.3f-.65f;for(float v:{x-.5f,0.f,z-.5f,x+.5f,0.f,z-.5f,x,0.f,z+.5f})stream<<v;}for(int i=0;i<4;++i)for(quint16 v:{quint16(0),quint16(1),quint16(2)})stream<<v;
        QJsonArray views,accessors,primitives,materials;for(int i=0;i<4;++i){views.append(QJsonObject{{"buffer",0},{"byteOffset",i*36},{"byteLength",36},{"target",34962}});views.append(QJsonObject{{"buffer",0},{"byteOffset",144+i*6},{"byteLength",6},{"target",34963}});const double x=(i%2)*1.3-.65,z=(i/2)*1.3-.65;accessors.append(QJsonObject{{"bufferView",i*2},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",QJsonArray{x-.5,0.,z-.5}},{"max",QJsonArray{x+.5,0.,z+.5}}});accessors.append(QJsonObject{{"bufferView",i*2+1},{"componentType",5123},{"count",3},{"type","SCALAR"}});primitives.append(QJsonObject{{"attributes",QJsonObject{{"POSITION",i*2}}},{"indices",i*2+1},{"material",i}});materials.append(QJsonObject{{"name",QString("UI slot %1").arg(i+1)},{"doubleSided",true},{"pbrMetallicRoughness",QJsonObject{{"baseColorFactor",QJsonArray{.2+i*.2,.8-i*.15,.6,1.}},{"roughnessFactor",.65},{"metallicFactor",0.}}}});}
        QJsonObject gltf{{"asset",QJsonObject{{"version","2.0"},{"generator","ImagiDock UI self test"}}},{"buffers",QJsonArray{QJsonObject{{"uri","data:application/octet-stream;base64,"+QString::fromLatin1(bytes.toBase64())},{"byteLength",bytes.size()}}}},{"bufferViews",views},{"accessors",accessors},{"materials",materials},{"meshes",QJsonArray{QJsonObject{{"primitives",primitives}}}},{"nodes",QJsonArray{QJsonObject{{"mesh",0}}}},{"scenes",QJsonArray{QJsonObject{{"nodes",QJsonArray{0}}}}},{"scene",0}};QSaveFile file(directory+"/UI four slots.gltf");if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(gltf).toJson());file.commit();}return directory;
    }
    void start(){initialGeometry=window->geometry();recordWindowState("initial");foreground(window);if(!check("frameless_window_flag",window->windowFlags().testFlag(Qt::FramelessWindowHint)))return;
#ifdef Q_OS_WIN
const auto handle=reinterpret_cast<HWND>(window->winId());RECT rectangle{};GetWindowRect(handle,&rectangle);const auto region=CreateRectRgn(0,0,0,0);const bool rounded=GetWindowRgn(handle,region)!=ERROR&&!PtInRegion(region,0,0);DeleteObject(region);if(!check("native_window_has_round_region",rounded))return;const auto edge=SendMessage(handle,WM_NCHITTEST,0,MAKELPARAM(rectangle.left+2,(rectangle.top+rectangle.bottom)/2));const auto brand=window->findChild<QLabel*>("windowBrand");const auto brandLocal=brand->mapTo(window,brand->rect().center());POINT brandPoint{qRound(brandLocal.x()*window->devicePixelRatioF()),qRound(brandLocal.y()*window->devicePixelRatioF())};ClientToScreen(handle,&brandPoint);const auto caption=SendMessage(handle,WM_NCHITTEST,0,MAKELPARAM(brandPoint.x,brandPoint.y));if(!check("native_resize_and_caption_hit_regions",edge==HTLEFT&&caption==HTCAPTION))return;
#endif
if(!chromeHitRegions())return;
call("library.info",{},[this](const QJsonObject &r){if(!check("dedicated_empty_library",r["counts"].toObject()["all"].toInt()==0))return;const QString path=fixtures();call("asset.import",{{"paths",QJsonArray{path}},{"backup",false}},[this](const QJsonObject &r){awaitJob(r["job_id"].toString(),[this]{loaded();});});});}
    bool chromeHitRegions(){
        auto selector=window->findChild<QComboBox*>("headerTheme");
        if(!check("header_exposes_four_existing_themes",selector&&selector->count()==4&&selector->itemData(0)=="ivory"&&selector->itemData(1)=="graphite"&&selector->itemData(2)=="ocean"&&selector->itemData(3)=="mint"))return false;
#ifdef Q_OS_WIN
        const auto handle=reinterpret_cast<HWND>(window->winId());
        auto hit=[this,handle](QWidget *widget,QPoint point){auto local=widget->mapTo(window,point);POINT native{qRound(local.x()*window->devicePixelRatioF()),qRound(local.y()*window->devicePixelRatioF())};ClientToScreen(handle,&native);return SendMessage(handle,WM_NCHITTEST,0,MAKELPARAM(native.x,native.y));};
        if(!check("theme_selector_keeps_client_clicks",hit(selector,selector->rect().center())==HTCLIENT))return false;
        auto menu=window->findChild<QMenuBar*>();
        if(!check("application_menu_keeps_client_clicks",menu&&!menu->actions().isEmpty()&&hit(menu,menu->actionGeometry(menu->actions().first()).center())==HTCLIENT))return false;
        auto tabs=window->findChild<AssetTabs*>();
        if(!check("real_tab_keeps_reorder_and_detach_input",hit(tabs->bar,tabs->bar->tabRect(0).center())==HTCLIENT))return false;
        auto plus=qobject_cast<QWidget*>(tabs->cornerWidget(Qt::TopRightCorner));
        if(!check("new_tab_button_keeps_client_clicks",plus&&hit(plus,plus->rect().center())==HTCLIENT))return false;
        QToolButton *newTab=nullptr;for(auto button:tabs->findChildren<QToolButton*>())if(button->accessibleName()==QString::fromUtf8("新建页签"))newTab=button;const auto tabCenter=tabs->bar->mapToGlobal(tabs->bar->tabRect(0).adjusted(0,0,0,-8).center());if(!check("new_tab_action_aligns_with_tab_row",newTab&&qAbs(newTab->mapToGlobal(newTab->rect().center()).y()-tabCenter.y())<=1))return false;
        QPoint blank=window->mapFromGlobal(tabs->bar->mapToGlobal(tabs->bar->tabRect(tabs->count()-1).topRight()+QPoint(40,16)));
        if(!check("empty_tab_strip_drags_window",hit(window,blank)==HTCAPTION))return false;
#endif
        return true;
    }
    bool alignedPanels(int width){
        auto tab=window->currentTab();auto panels=tab->findChild<QSplitter*>("libraryPanels");
        if(!panels||panels->count()!=3)return check(QString("three_panel_bounds_aligned_%1").arg(width),false);
        const auto first=panels->widget(0)->geometry();bool aligned=true;
        for(int i=1;i<3;++i){const auto next=panels->widget(i)->geometry();aligned&=next.top()==first.top()&&next.bottom()==first.bottom()&&next.width()>0;}
        if(!check(QString("three_panel_bounds_aligned_%1").arg(width),aligned))return false;
        const int left=panels->widget(0)->mapTo(window,QPoint()).x(),right=panels->widget(2)->mapTo(window,panels->widget(2)->rect().topRight()).x();if(!check(QString("workspace_gutters_balanced_%1").arg(width),left==16&&right==window->width()-17))return false;
        auto search=tab->findChild<QLineEdit*>("assetSearch");auto toolbar=tab->findChild<QToolBar*>("libraryTools");
        const auto center=search->mapTo(toolbar,search->rect().center());bool controlsAligned=true;
        for(auto combo:toolbar->findChildren<QComboBox*>())if(combo->isVisible())controlsAligned&=qAbs(combo->mapTo(toolbar,combo->rect().center()).y()-center.y())<=1&&qAbs(combo->height()-search->height())<=2;
        return check(QString("filter_controls_share_height_and_baseline_%1").arg(width),controlsAligned);
    }
    void loaded(){call("asset.list",{{"limit",100}},[this](const QJsonObject &r){if(!check("import_three_ui_fixtures",r["total"].toInt()==3))return;for(const auto &v:r["items"].toArray()){const auto a=v.toObject();if(a["type"]=="model")modelId=a["id"].toString();else if(a["name"]=="UI color.png")imageId=a["id"].toString();else normalId=a["id"].toString();}if(!check("stable_ids_present",!modelId.isEmpty()&&!imageId.isEmpty()&&!normalId.isEmpty()))return;call("asset.get",{{"asset_id",modelId}},[this](const QJsonObject &a){QJsonArray bindings;for(int slot=1;slot<=4;++slot)bindings.append(QJsonObject{{"target_id",imageId},{"label",QString("base_color_%1").arg(slot)},{"role","base_color"},{"slot",slot}});call("relation.set",{{"asset_id",modelId},{"revision",a["revision"]},{"items",bindings}},[this](const QJsonObject &model){if(!check("four_material_slots_saved",model["relations"].toArray().size()==4))return;call("asset.get",{{"asset_id",imageId}},[this](const QJsonObject &a){call("asset.update",{{"asset_id",imageId},{"revision",a["revision"]},{"values",QJsonObject{{"tags",QJsonArray{"ui-test","color"}},{"favorite",true}}}},[this](const QJsonObject &){filter();});});});});});}
    void filter(){call("asset.list",{{"types",QJsonArray{"image","video"}},{"tags",QJsonArray{"ui-test","does-not-exist"}},{"status","favorite"}},[this](const QJsonObject &r){if(!check("filter_or_within_and_across",r["total"].toInt()==1&&r["items"].toArray()[0].toObject()["id"]==imageId))return;auto tab=window->currentTab();tab->openAsset(modelId);QTimer::singleShot(1500,this,[this]{windowStates();});});}
    void foreground(QWidget *widget){
        foregroundApplicationTestWindow(widget);
    }
    bool nativePresentation(ModelView *model,const QString &frameName,const QString &presentName,const QString &fileName){
        const auto capture=captureApplicationWidget(model,true);
        if(!capture.error.isEmpty()){finish(false,capture.error);return false;}
        const auto screen=capture.image.toImage();const QString path=QFileInfo(report).absolutePath()+"/"+fileName;
        if(!check(frameName,model&&model->isVisible()&&model->saveSnapshot(path+".png")))return false;
        const QImage reference(path+".png");double difference=0,foregroundDifference=0;int samples=0,foregroundPixels=0;
        // Qt screen capture rounds fractional device pixels up, while Filament's viewport uses integer dimensions.
        const bool aligned=!reference.isNull()&&qAbs(reference.width()-screen.width())<=1&&qAbs(reference.height()-screen.height())<=1;
        if(aligned){
            const auto background=reference.pixelColor(2,2);
            for(int y=8;y<qMin(reference.height(),screen.height())-8;y+=4)for(int x=8;x<qMin(reference.width(),screen.width())-8;x+=4){
                const auto a=reference.pixelColor(x,y),b=screen.pixelColor(x,y);
                const int error=qAbs(a.red()-b.red())+qAbs(a.green()-b.green())+qAbs(a.blue()-b.blue());difference+=error;samples+=3;
                if(qAbs(a.red()-background.red())+qAbs(a.green()-background.green())+qAbs(a.blue()-background.blue())>45){++foregroundPixels;foregroundDifference+=error;}
            }
        }
        screen.save(path+"-dwm.png");
        QJsonArray nativeClient;
#ifdef Q_OS_WIN
        RECT client{};GetClientRect(reinterpret_cast<HWND>(model->winId()),&client);nativeClient=QJsonArray{int(client.right-client.left),int(client.bottom-client.top)};
#endif
        nativePresentations.append(QJsonObject{{"name",presentName},{"native_client_size",nativeClient},{"foreground_pixels",foregroundPixels},{"mean_rgb_error",samples?difference/samples:-1.},{"foreground_mean_rgb_error",foregroundPixels?foregroundDifference/(foregroundPixels*3.):-1.},{"width",reference.width()},{"height",reference.height()},{"capture_width",screen.width()},{"capture_height",screen.height()},{"pixel_bounds_aligned",aligned}});
        return check(presentName,samples>0&&foregroundPixels>100&&difference/samples<10.&&foregroundDifference/(foregroundPixels*3.)<10.);
    }
    void recordWindowState(const QString &name){auto rect=[](const QRect &r){return QJsonArray{r.x(),r.y(),r.width(),r.height()};};windowStatesReport.append(QJsonObject{{"name",name},{"state",int(window->windowState())},{"flags",double(quint64(window->windowFlags()))},{"geometry",rect(window->geometry())},{"normal_geometry",rect(window->normalGeometry())}});}
    void clickMaximize(){for(auto button:window->findChildren<QToolButton*>())if(button->accessibleName()==QString::fromUtf8("最大化窗口")||button->accessibleName()==QString::fromUtf8("还原窗口")){button->click();return;}}
    void captionDoubleClick(){
#ifdef Q_OS_WIN
        const auto handle=reinterpret_cast<HWND>(window->winId());RECT bounds{};GetWindowRect(handle,&bounds);SendMessage(handle,WM_NCLBUTTONDBLCLK,HTCAPTION,MAKELPARAM(bounds.left+qRound(700*window->devicePixelRatioF()),bounds.top+qRound(28*window->devicePixelRatioF())));
#endif
    }
    void windowStates(){
        recordWindowState("before_titlebar_maximize");clickMaximize();
        QTimer::singleShot(400,this,[this]{
            recordWindowState("titlebar_maximized");if(!check("titlebar_button_maximizes",window->isMaximized()))return;
            clickMaximize();QTimer::singleShot(400,this,[this]{
                recordWindowState("titlebar_restored");if(!check("titlebar_button_restores_original_geometry",!window->isMaximized()&&!window->isMinimized()&&window->geometry()==initialGeometry))return;
                captionDoubleClick();QTimer::singleShot(400,this,[this]{
                    recordWindowState("caption_doubleclick_maximized");if(!check("titlebar_doubleclick_maximizes",window->isMaximized()))return;
                    captionDoubleClick();QTimer::singleShot(400,this,[this]{
                        recordWindowState("caption_doubleclick_restored");if(!check("titlebar_doubleclick_restores_original_geometry",!window->isMaximized()&&window->geometry()==initialGeometry))return;
                        renderWindowStates();
                    });
                });
            });
        });
    }
    void renderWindowStates(){
        window->showMaximized();foreground(window);recordWindowState("render_maximized");
        QTimer::singleShot(900,this,[this]{
            const auto screen=window->screen()->availableGeometry();
            if(!check("maximize_uses_screen_work_area",window->isMaximized()&&qAbs(window->width()-screen.width())<=2&&qAbs(window->height()-screen.height())<=2))return;
            if(!nativePresentation(window->currentTab()->findChild<ModelView*>(),"maximized_model_framebuffer","model_presents_on_native_window","ui-maximized-model"))return;
            window->showMinimized();
            QTimer::singleShot(300,this,[this]{
                if(!check("custom_window_minimize",window->isMinimized()))return;
                window->showNormal();recordWindowState("normal_before_foreground");foreground(window);
                QTimer::singleShot(900,this,[this]{
                    recordWindowState("normal_after_foreground");if(!check("minimize_restore_returns_normal_geometry",!window->isMinimized()&&!window->isMaximized()&&window->geometry()==initialGeometry))return;
                    if(!nativePresentation(window->currentTab()->findChild<ModelView*>(),"restored_model_framebuffer","restored_model_presents_on_native_window","ui-restored-model"))return;
                    reparent();
                });
            });
        });
    }
    void reparent(){
        auto tabs=window->findChild<AssetTabs*>();const auto before=qApp->topLevelWidgets();
        QMetaObject::invokeMethod(tabs->bar,"detached",Qt::DirectConnection,Q_ARG(int,tabs->currentIndex()),Q_ARG(QPoint,window->pos()+QPoint(50,50)));
        for(auto widget:qApp->topLevelWidgets())if(!before.contains(widget))if(auto created=qobject_cast<MainWindow*>(widget))detachedWindow=created;
        if(!check("tear_out_creates_native_window",detachedWindow&&!detachedWindow->currentTab()->session()["history"].toArray().isEmpty()))return;
        foreground(detachedWindow);
        QTimer::singleShot(1500,this,[this]{
            if(!nativePresentation(detachedWindow->currentTab()->findChild<ModelView*>(),"detached_model_framebuffer_readback","detached_model_presents_on_native_window","ui-detached-model"))return;
            detachedWindow->moveCurrentTabTo(window);detachedWindow->close();foreground(window);
            QTimer::singleShot(1500,this,[this]{
                if(!nativePresentation(window->currentTab()->findChild<ModelView*>(),"reattached_model_framebuffer_readback","reattached_model_presents_on_native_window","ui-reattached-model"))return;
                if(!check("reparent_preserves_model_asset",window->currentTab()->currentAssetId()==modelId))return;
                navigation();
            });
        });
    }
    void waitForThumbnail(){if(done)return;auto grid=window->currentTab()->findChild<AssetGrid*>();bool found=false;if(grid&&grid->model())for(int row=0;row<grid->model()->rowCount();++row){auto index=grid->model()->index(row,0);if(index.data(AssetModel::AssetRole).toJsonObject()["id"]==imageId)found=!index.data(Qt::DecorationRole).value<QPixmap>().isNull();}if(found){if(!check("thumbnail_decoded_and_available_to_delegate",true))return;foreground(window);QTimer::singleShot(250,this,[this]{keyboard();});return;}if(++attempts>80){finish(false,"GUI thumbnail did not become ready");return;}QTimer::singleShot(250,this,[this]{waitForThumbnail();});}
    void keyboard(){auto tab=window->currentTab();tab->focusSearch();auto search=QApplication::focusWidget();QKeyEvent next(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(search,&next);if(!check("search_tab_order_reaches_status",QApplication::focusWidget()&&QApplication::focusWidget()->accessibleName()==QString::fromUtf8("资产状态筛选")))return;auto grid=tab->findChild<AssetGrid*>();const auto first=grid->model()->index(0,0);if(!check("grid_accessible_name_and_description",first.data(Qt::AccessibleTextRole)==first.data(Qt::DisplayRole)&&!first.data(Qt::AccessibleDescriptionRole).toString().isEmpty()))return;grid->setCurrentIndex(first);grid->setFocus(Qt::OtherFocusReason);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(grid,&enter);QTimer::singleShot(350,this,[this]{if(!check("enter_activates_asset_details",window->currentTab()->session()["detail"].toBool()&&window->currentTab()->session()["history"].toArray().size()==1))return;call("asset.get",{{"asset_id",imageId}},[this](const QJsonObject &a){call("asset.update",{{"asset_id",imageId},{"revision",a["revision"]},{"values",QJsonObject{{"name",QString::fromUtf8("角色设计稿_含长文件名与透明贴图_用于验证窗口缩放和键盘读取完整信息_完整版本.png")}}}},[this](const QJsonObject &){window->currentTab()->openAsset(imageId);QTimer::singleShot(350,this,[this]{layoutSizes(0);});});});});}
    void layoutSizes(int at){const auto room=(window->screen()->availableGeometry().size()-QSize(32,32)).expandedTo(window->minimumSize());const auto largest=QSize(1440,940).boundedTo(room);auto middle=QSize(1280,800).boundedTo(room);if(middle==largest)middle=QSize((960+largest.width())/2,(640+largest.height())/2);QList<QSize> sizes;for(auto size:{QSize(960,640),middle,largest})if(!sizes.contains(size))sizes.append(size);if(at>=sizes.size()){window->resize(initialGeometry.size());window->currentTab()->home();themeSwitch(0);return;}window->resize(sizes[at]);QTimer::singleShot(350,this,[this,at,sizes]{const auto requested=sizes[at];if(!alignedPanels(requested.width()))return;if(!check(QString("window_size_%1x%2").arg(requested.width()).arg(requested.height()),window->size()==requested))return;auto title=window->currentTab()->findChild<QLabel*>("pageTitle");for(auto candidate:window->currentTab()->findChildren<QLabel*>("pageTitle"))if(candidate->isVisible())title=candidate;if(!check(QString("long_title_elides_without_clipping_%1").arg(requested.width()),title&&!title->wordWrap()&&!title->toolTip().isEmpty()&&title->accessibleName()==title->toolTip()&&title->fontMetrics().horizontalAdvance(title->text())<=title->contentsRect().width()))return;auto detail=window->currentTab()->findChild<QScrollArea*>("detailScroll");if(!check(QString("details_no_horizontal_clipping_%1").arg(requested.width()),detail&&detail->horizontalScrollBar()->maximum()==0))return;auto canvas=window->currentTab()->findChild<ImageCanvas*>();const auto imageRect=canvas->mapFromScene(canvas->sceneRect()).boundingRect();const auto visibleCanvas=canvas->viewport()->rect().adjusted(-3,-3,3,3);auto host=window->currentTab()->findChild<QWidget*>("detailHost");const auto canvasTop=canvas->viewport()->mapToGlobal(QPoint(0,0)),detailTop=host->mapToGlobal(QPoint(0,0));if(!check(QString("fitted_image_fully_visible_%1").arg(requested.width()),!imageRect.isEmpty()&&visibleCanvas.contains(imageRect)&&canvasTop.y()>=detailTop.y()&&canvasTop.y()+canvas->viewport()->height()<=detailTop.y()+host->height()))return;auto manage=window->currentTab()->findChild<QPushButton*>("manageRelations");detail->ensureWidgetVisible(manage,0,0);const auto manageCenter=manage->mapToGlobal(manage->rect().center()),organizationTop=detail->viewport()->mapToGlobal(QPoint(0,0));if(!check(QString("organization_controls_scroll_reachable_%1").arg(requested.width()),manageCenter.y()>=organizationTop.y()&&manageCenter.y()<organizationTop.y()+detail->viewport()->height()))return;const auto capture=captureApplicationWidget(window);if(capture.error.isEmpty())capture.image.save(QFileInfo(report).absolutePath()+QString("/ui-layout-%1x%2.png").arg(requested.width()).arg(requested.height()));else captureWarnings.append(QJsonObject{{"size",QString("%1x%2").arg(requested.width()).arg(requested.height())},{"error",capture.error}});layoutSizes(at+1);});}
    void themeSwitch(int index){
        const QStringList ids{"ivory","graphite","ocean","mint"};
        if(index>=ids.size()){dialog();return;}
        auto selector=window->findChild<QComboBox*>("headerTheme");selector->setCurrentIndex(selector->findData(ids[index]));
        QMetaObject::invokeMethod(selector,"activated",Qt::DirectConnection,Q_ARG(int,selector->currentIndex()));
        QTimer::singleShot(350,this,[this,index,ids]{call("library.info",{},[this,index,ids](const QJsonObject &info){
            if(!check("header_theme_persists_"+ids[index],info["settings"].toObject()["theme"]==ids[index]&&themeColors().id==ids[index]&&window->findChild<QComboBox*>("headerTheme")->currentData()==ids[index]))return;
            const auto capture=captureApplicationWidget(window);if(capture.error.isEmpty())capture.image.save(QFileInfo(report).absolutePath()+"/theme-"+ids[index]+".png");else captureWarnings.append(capture.error);
            themeSwitch(index+1);
        });});
    }
    void dialog(){window->currentTab()->focusSearch();dialogOrigin=QApplication::focusWidget();window->showSettings();for(auto candidate:window->findChildren<GlassDialog*>())if(candidate->isVisible())foreground(candidate);QTimer::singleShot(400,this,[this]{GlassDialog *settings=nullptr;for(auto candidate:window->findChildren<GlassDialog*>())if(candidate->isVisible())settings=candidate;if(!check("glass_dialog_frameless",settings&&settings->windowFlags().testFlag(Qt::FramelessWindowHint)))return;if(!check("glass_dialog_content_below_titlebar",settings->layout()&&settings->layout()->contentsMargins().top()>=44))return;if(!check("dialog_focus_starts_in_content",QApplication::focusWidget()&&settings->isAncestorOf(QApplication::focusWidget())&&QApplication::focusWidget()->objectName()!="windowClose"))return;const auto capture=captureApplicationWidget(settings);if(capture.error.isEmpty())capture.image.save(QFileInfo(report).absolutePath()+"/ui-settings-dialog.png");else captureWarnings.append(capture.error);QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(settings,&escape);if(!check("esc_closes_glass_dialog",!settings->isVisible()))return;QTimer::singleShot(100,this,[this]{if(!check("dialog_returns_focus_to_invoker",dialogOrigin&&QApplication::focusWidget()==dialogOrigin))return;finish(true);});});}
    void navigation(){auto tab=window->currentTab();tab->openAsset(imageId);if(!check("association_history_push",tab->session()["history"].toArray()==QJsonArray{modelId,imageId}))return;tab->back();if(!check("association_back_returns_model",tab->session()["history"].toArray()==QJsonArray{modelId}))return;tab->home();if(!check("home_clears_history",tab->session()["history"].toArray().isEmpty()&&!tab->session()["detail"].toBool()))return;QJsonObject state{{"filter",QJsonObject{{"query","UI"},{"types",QJsonArray{"image","video"}},{"tags",QJsonArray{"ui-test","color"}},{"status","favorite"},{"sort","name"}}},{"history",QJsonArray{modelId,imageId}},{"detail",true},{"asset_id",imageId}};auto duplicate=window->addTab(state);if(!check("duplicate_tab_keeps_independent_filters",duplicate->session()["filter"]==state["filter"]&&tab->session()["history"].toArray().isEmpty()))return;second=new MainWindow(rpc,new ThumbnailStore(rpc,this));second->show();second->currentTab()->openAsset(imageId);call("asset.get",{{"asset_id",imageId}},[this](const QJsonObject &asset){call("asset.update",{{"asset_id",imageId},{"revision",asset["revision"]},{"values",QJsonObject{{"name","UI synchronized.png"}}}},[this](const QJsonObject &){rpc->refresh();QTimer::singleShot(2300,this,[this]{if(!window||!second){finish(false,"window closed during test");return;}if(!check("two_windows_share_asset_identity",window->currentTab()->currentAssetId()==imageId&&second->currentTab()->currentAssetId()==imageId))return;auto hasName=[](MainWindow *w){for(auto edit:w->findChildren<QLineEdit*>())if(edit->text()=="UI synchronized.png")return true;return false;};if(!check("cross_window_update_visible",hasName(window)&&hasName(second)))return;window->currentTab()->home();window->currentTab()->restoreSession({{"filter",QJsonObject{}},{"history",QJsonArray{}},{"detail",false}});attempts=0;waitForThumbnail();});});});}
};
}
void runUiSelfTest(MainWindow *window,RpcClient *rpc,const QString &reportPath,const QString &screenshotPath){auto state=new State(window,rpc,reportPath,screenshotPath);QTimer::singleShot(0,state,[state]{state->start();});QTimer::singleShot(90000,state,[state]{if(!state->done)state->finish(false,"self-test timeout");});}
