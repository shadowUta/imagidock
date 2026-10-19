#include "UiSelfTest.h"
#include "ApplicationCapture.h"
#include "GlassDialog.h"
#include "PreviewPane.h"
#include "UiIcons.h"
#include "UiMetrics.h"
#include <QAbstractButton>
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
    QPointer<MainWindow> window,second,detachedWindow;RpcClient *rpc;QString report,screenshot,modelId,imageId,normalId;QPointer<QWidget> dialogOrigin;QJsonArray assertions,captureWarnings,nativePresentations,windowStatesReport,layoutDiagnostics;QRect initialGeometry;QIcon folderIcon=uiIcon("folder");bool desktopValidation,done=false;int attempts=0;
    State(MainWindow *w,RpcClient *r,const QString &p,const QString &s,bool desktop):QObject(qApp),window(w),rpc(r),report(p),screenshot(s),desktopValidation(desktop){}
    ApplicationCapture captureWidget(QWidget *widget){
        if(desktopValidation)return captureApplicationWidget(widget);
        if(!widget)return {{},QStringLiteral("functional_capture_failed: target widget is missing")};
        // QWidget::grab renders this widget's Qt raster content; it is not a desktop or GPU presentation capture.
        const auto raster=widget->grab();
        if(raster.isNull())return {{},QStringLiteral("functional_capture_failed: Qt raster is empty")};
        return {raster,{}};
    }
    bool check(const QString &name,bool passed){assertions.append(QJsonObject{{"name",name},{"passed",passed}});if(!passed)finish(false,name);return passed;}
    void finish(bool ok,const QString &message={}){if(done)return;done=true;if(window&&!screenshot.isEmpty()){QDir().mkpath(QFileInfo(screenshot).absolutePath());const auto capture=captureWidget(window);if(capture.error.isEmpty())capture.image.save(screenshot);else captureWarnings.append(capture.error);}if(!report.isEmpty()){QDir().mkpath(QFileInfo(report).absolutePath());QSaveFile out(report);if(out.open(QIODevice::WriteOnly)){out.write(QJsonDocument(QJsonObject{{"ok",ok},{"message",message},{"desktop_validation",desktopValidation},{"scope",desktopValidation?"strict: functional checks, framebuffer readback and guarded desktop presentation comparison":"functional-only: Qt raster layout/input and framebuffer readback; no desktop presentation evidence"},{"assertions",assertions},{"capture_warnings",captureWarnings},{"native_presentations",nativePresentations},{"window_states",windowStatesReport},{"layout_diagnostics",layoutDiagnostics},{"library",rpc->libraryPath()},{"device_pixel_ratio",window?window->devicePixelRatioF():0.},{"screen_available",window?QJsonArray{window->screen()->availableGeometry().width(),window->screen()->availableGeometry().height()}:QJsonArray{}}}).toJson());out.commit();}}if(second)second->close();rpc->call("service.shutdown",{{"cancel_jobs",true}},this,[ok](const QJsonObject &,const QString &){qApp->exit(ok?0:2);});QTimer::singleShot(3500,qApp,[ok]{qApp->exit(ok?0:2);});}
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
        QToolButton *newTab=nullptr;for(auto button:tabs->findChildren<QToolButton*>())if(button->accessibleName()==QString::fromUtf8("新建页签"))newTab=button;const auto tabCenter=tabs->bar->mapToGlobal(tabs->bar->tabRect(0).adjusted(0,0,0,-UiMetrics::TabBottomInset).center());if(!check("new_tab_action_aligns_with_tab_row",newTab&&qAbs(newTab->mapToGlobal(newTab->rect().center()).y()-tabCenter.y())<=1))return false;
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
        auto content=tab->findChild<QWidget*>("contentPanel");auto location=tab->findChild<QWidget*>("locationBar");
        auto toolbar=tab->findChild<QWidget*>("libraryTools");auto pages=tab->findChild<QWidget*>("assetPages");
        bool ordered=content&&location&&toolbar&&pages&&panels->widget(1)==content&&content->isAncestorOf(location)&&content->isAncestorOf(toolbar)&&content->isAncestorOf(pages);
        if(ordered){
            const QRect locationBounds(location->mapTo(content,QPoint()),location->size()),filterBounds(toolbar->mapTo(content,QPoint()),toolbar->size()),pageBounds(pages->mapTo(content,QPoint()),pages->size());
            ordered=locationBounds.height()>0&&filterBounds.height()>0&&pageBounds.height()>0&&content->rect().contains(locationBounds)&&content->rect().contains(filterBounds)&&content->rect().contains(pageBounds)&&locationBounds.bottom()<filterBounds.top()&&filterBounds.bottom()<pageBounds.top();
        }
        if(!check(QString("content_toolbar_rows_ordered_%1").arg(width),ordered))return false;
        auto header=window->findChild<QWidget*>("windowHeader");
        auto menu=header?header->findChild<QMenuBar*>():nullptr;auto selector=header?header->findChild<QComboBox*>("headerTheme"):nullptr;
        QList<QWidget*> headerControls;if(menu)headerControls.append(menu);if(selector)headerControls.append(selector);
        int windowButtons=0,closeButtons=0;
        if(header)for(auto button:header->findChildren<QToolButton*>()){
            if(button->objectName()=="windowButton"){headerControls.append(button);++windowButtons;}
            else if(button->objectName()=="windowClose"){headerControls.append(button);++closeButtons;}
        }
        bool contained=header&&menu&&selector&&windowButtons==2&&closeButtons==1;QList<QRect> headerBounds;
        if(contained)for(auto control:headerControls){
            const QRect bounds(control->mapTo(header,QPoint()),control->size());
            contained&=control->isVisible()&&bounds.width()>0&&bounds.height()>0&&header->rect().contains(bounds);
            for(const auto &other:headerBounds)contained&=!bounds.intersects(other);
            headerBounds.append(bounds);
        }
        if(!check(QString("header_controls_contained_without_overlap_%1").arg(width),contained))return false;
        auto search=tab->findChild<QLineEdit*>("assetSearch");
        if(!search||!toolbar)return check(QString("filter_controls_share_height_and_baseline_%1").arg(width),false);
        const auto center=search->mapTo(toolbar,search->rect().center());bool controlsAligned=true;
        for(auto combo:toolbar->findChildren<QComboBox*>())if(combo->isVisible())controlsAligned&=qAbs(combo->mapTo(toolbar,combo->rect().center()).y()-center.y())<=1&&qAbs(combo->height()-search->height())<=2;
        if(!check(QString("filter_controls_share_height_and_baseline_%1").arg(width),controlsAligned))return false;
        QComboBox *status=nullptr,*sort=nullptr;
        for(auto combo:toolbar->findChildren<QComboBox*>()){
            if(combo->accessibleName()==QString::fromUtf8("资产状态筛选"))status=combo;
            else if(combo->accessibleName()==QString::fromUtf8("资产排序"))sort=combo;
        }
        const auto originalSizes=panels->sizes();
        panels->setSizes({panels->widget(0)->maximumWidth(),1,panels->widget(2)->maximumWidth()});QApplication::processEvents();
        bool reachable=status&&sort;QList<QRect> filterBounds;
        if(reachable)for(auto control:QList<QWidget*>{search,status,sort}){
            if(!control->isVisible()||!toolbar->isAncestorOf(control)){reachable=false;continue;}
            const QRect bounds(control->mapTo(toolbar,QPoint()),control->size());
            reachable&=bounds.width()>0&&bounds.height()>0&&toolbar->rect().contains(bounds);
            for(const auto &other:filterBounds)reachable&=!bounds.intersects(other);
            filterBounds.append(bounds);
        }
        QJsonArray filterDiagnostics;
        for(auto control:QList<QWidget*>{search,status,sort})if(control){
            const auto position=control->mapTo(toolbar,QPoint());
            filterDiagnostics.append(QJsonObject{{"name",control->accessibleName()},{"visible",control->isVisible()},{"bounds",QJsonArray{position.x(),position.y(),control->width(),control->height()}}});
        }
        layoutDiagnostics.append(QJsonObject{{"window_width",width},{"content_width",content->width()},{"toolbar_width",toolbar->width()},{"toolbar_height",toolbar->height()},{"controls",filterDiagnostics}});
        panels->setSizes(originalSizes);QApplication::processEvents();
        return check(QString("filter_controls_reachable_at_sidebar_limits_%1").arg(width),reachable);
    }
    void loaded(){call("asset.list",{{"limit",100}},[this](const QJsonObject &r){if(!check("import_three_ui_fixtures",r["total"].toInt()==3))return;for(const auto &v:r["items"].toArray()){const auto a=v.toObject();if(a["type"]=="model")modelId=a["id"].toString();else if(a["name"]=="UI color.png")imageId=a["id"].toString();else normalId=a["id"].toString();}if(!check("stable_ids_present",!modelId.isEmpty()&&!imageId.isEmpty()&&!normalId.isEmpty()))return;call("asset.get",{{"asset_id",modelId}},[this](const QJsonObject &a){QJsonArray bindings;for(int slot=1;slot<=4;++slot)bindings.append(QJsonObject{{"target_id",imageId},{"label",QString("base_color_%1").arg(slot)},{"role","base_color"},{"slot",slot}});call("relation.set",{{"asset_id",modelId},{"revision",a["revision"]},{"items",bindings}},[this](const QJsonObject &model){if(!check("four_material_slots_saved",model["relations"].toArray().size()==4))return;call("asset.get",{{"asset_id",imageId}},[this](const QJsonObject &a){call("asset.update",{{"asset_id",imageId},{"revision",a["revision"]},{"values",QJsonObject{{"tags",QJsonArray{"ui-test","color"}},{"favorite",true}}}},[this](const QJsonObject &){filter();});});});});});}
    void filter(){call("asset.list",{{"types",QJsonArray{"image","video"}},{"tags",QJsonArray{"ui-test","does-not-exist"}},{"status","favorite"}},[this](const QJsonObject &r){if(!check("filter_or_within_and_across",r["total"].toInt()==1&&r["items"].toArray()[0].toObject()["id"]==imageId))return;auto tab=window->currentTab();tab->openAsset(modelId);QTimer::singleShot(1500,this,[this]{windowStates();});});}
    void foreground(QWidget *widget){
        foregroundApplicationTestWindow(widget);
    }
    bool nativePresentation(ModelView *model,const QString &frameName,const QString &presentName,const QString &fileName){
        if(!desktopValidation){
            const QString path=QFileInfo(report).absolutePath()+"/"+fileName+"-framebuffer.png";
            if(!check(frameName,model&&model->isVisible()&&model->saveSnapshot(path)))return false;
            nativePresentations.append(QJsonObject{{"name",presentName},{"skipped",true},{"reason","functional-only mode: framebuffer readback is checked; desktop presentation is not captured or validated"},{"framebuffer_file",QFileInfo(path).fileName()}});
            return true;
        }
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
        QJsonArray nativeClient,widgetAncestors;
        for(auto ancestor=static_cast<QWidget*>(model);ancestor;ancestor=ancestor->parentWidget()){
            const auto geometry=ancestor->geometry();
            widgetAncestors.append(QJsonObject{
                {"class_name",QString::fromLatin1(ancestor->metaObject()->className())},
                {"object_name",ancestor->objectName()},
                {"internal_win_id",qint64(ancestor->internalWinId())},
                {"geometry",QJsonArray{geometry.x(),geometry.y(),geometry.width(),geometry.height()}},
                {"visible",ancestor->isVisible()},
                {"native_window",ancestor->testAttribute(Qt::WA_NativeWindow)},
                {"dont_create_native_ancestors",ancestor->testAttribute(Qt::WA_DontCreateNativeAncestors)},
                {"styled_background",ancestor->testAttribute(Qt::WA_StyledBackground)},
                {"paint_on_screen",ancestor->testAttribute(Qt::WA_PaintOnScreen)},
                {"no_system_background",ancestor->testAttribute(Qt::WA_NoSystemBackground)},
                {"opaque_paint_event",ancestor->testAttribute(Qt::WA_OpaquePaintEvent)},
                {"translucent_background",ancestor->testAttribute(Qt::WA_TranslucentBackground)},
                {"auto_fill_background",ancestor->autoFillBackground()}
            });
        }
        QJsonObject nativeWindow;
        QJsonArray nativeParentChain;
#ifdef Q_OS_WIN
        const auto modelHandle=reinterpret_cast<HWND>(model->winId());
        RECT client{};GetClientRect(modelHandle,&client);nativeClient=QJsonArray{int(client.right-client.left),int(client.bottom-client.top)};
        auto nativeDetails=[](HWND handle){
            RECT bounds{},clientBounds{};POINT clientOrigin{};
            const bool haveWindow=GetWindowRect(handle,&bounds),haveClient=GetClientRect(handle,&clientBounds),haveOrigin=ClientToScreen(handle,&clientOrigin);
            wchar_t className[256]{};GetClassNameW(handle,className,256);
            const auto classStyle=GetClassLongPtrW(handle,GCL_STYLE);
            return QJsonObject{
                {"hwnd",qint64(reinterpret_cast<quintptr>(handle))},
                {"parent_hwnd",qint64(reinterpret_cast<quintptr>(GetParent(handle)))},
                {"class_name",QString::fromWCharArray(className)},
                {"class_style",qint64(classStyle)},
                {"cs_own_dc",bool(classStyle&CS_OWNDC)},
                {"window_style",qint64(GetWindowLongPtrW(handle,GWL_STYLE))},
                {"window_ex_style",qint64(GetWindowLongPtrW(handle,GWL_EXSTYLE))},
                {"window_rect_valid",haveWindow},
                {"window_rect",QJsonArray{int(bounds.left),int(bounds.top),int(bounds.right-bounds.left),int(bounds.bottom-bounds.top)}},
                {"client_rect_valid",haveClient},
                {"client_rect",QJsonArray{int(clientBounds.left),int(clientBounds.top),int(clientBounds.right-clientBounds.left),int(clientBounds.bottom-clientBounds.top)}},
                {"client_screen_origin_valid",haveOrigin},
                {"client_screen_origin",QJsonArray{int(clientOrigin.x),int(clientOrigin.y)}},
                {"visible",bool(IsWindowVisible(handle))}
            };
        };
        nativeWindow=nativeDetails(modelHandle);
        if(const auto dc=GetDC(modelHandle)){
            const int pixelFormat=GetPixelFormat(dc);PIXELFORMATDESCRIPTOR descriptor{};
            const bool described=pixelFormat>0&&DescribePixelFormat(dc,pixelFormat,sizeof(descriptor),&descriptor)!=0;
            nativeWindow["hdc"]=qint64(reinterpret_cast<quintptr>(dc));
            nativeWindow["pixel_format"]=pixelFormat;
            nativeWindow["pixel_format_described"]=described;
            nativeWindow["pixel_format_flags"]=qint64(descriptor.dwFlags);
            nativeWindow["pixel_format_double_buffer"]=bool(descriptor.dwFlags&PFD_DOUBLEBUFFER);
            nativeWindow["pixel_format_draw_to_window"]=bool(descriptor.dwFlags&PFD_DRAW_TO_WINDOW);
            nativeWindow["pixel_format_support_opengl"]=bool(descriptor.dwFlags&PFD_SUPPORT_OPENGL);
            nativeWindow["pixel_format_color_bits"]=int(descriptor.cColorBits);
            nativeWindow["pixel_format_alpha_bits"]=int(descriptor.cAlphaBits);
            ReleaseDC(modelHandle,dc);
        }
        for(HWND handle=modelHandle;handle&&nativeParentChain.size()<32;handle=GetParent(handle))nativeParentChain.append(nativeDetails(handle));
#endif
        nativePresentations.append(QJsonObject{{"name",presentName},{"native_client_size",nativeClient},{"foreground_pixels",foregroundPixels},{"mean_rgb_error",samples?difference/samples:-1.},{"foreground_mean_rgb_error",foregroundPixels?foregroundDifference/(foregroundPixels*3.):-1.},{"width",reference.width()},{"height",reference.height()},{"capture_width",screen.width()},{"capture_height",screen.height()},{"pixel_bounds_aligned",aligned},{"model_diagnostics",model->diagnostics()},{"widget_ancestors",widgetAncestors},{"native_window",nativeWindow},{"native_parent_chain",nativeParentChain}});
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
    void waitForThumbnail(){if(done)return;auto grid=window->currentTab()->findChild<AssetGrid*>();bool found=false;if(grid&&grid->model())for(int row=0;row<grid->model()->rowCount();++row){auto index=grid->model()->index(row,0);if(index.data(AssetModel::AssetRole).toJsonObject()["id"]==imageId)found=!index.data(Qt::DecorationRole).value<QPixmap>().isNull();}if(found){if(!check("thumbnail_decoded_and_available_to_delegate",true))return;foreground(window);QTimer::singleShot(250,this,[this]{quickNavigation();});return;}if(++attempts>80){finish(false,"GUI thumbnail did not become ready");return;}QTimer::singleShot(250,this,[this]{waitForThumbnail();});}
    void quickNavigation(){
        auto tab=window->currentTab();auto favorites=tab->findChild<QAbstractButton*>("navFavorites");auto all=tab->findChild<QAbstractButton*>("navAll");
        if(favorites){tab->openAsset(imageId);favorites->click();favorites->click();}
        auto state=tab->session();
        if(!check("favorites_navigation_filters_and_returns_home",favorites&&favorites->isChecked()&&state["filter"].toObject()["status"]=="favorite"&&!state["detail"].toBool()&&state["history"].toArray().isEmpty()))return;
        if(all){tab->openAsset(imageId);all->click();all->click();}
        state=tab->session();
        if(!check("all_assets_navigation_filters_and_returns_home",all&&all->isChecked()&&state["filter"].toObject()["status"]=="all"&&!state["detail"].toBool()&&state["history"].toArray().isEmpty()))return;
        attempts=0;waitForNavigationRows();
    }
    void waitForNavigationRows(){
        if(done)return;auto grid=window->currentTab()->findChild<AssetGrid*>();
        if(grid&&grid->model()&&grid->model()->rowCount()>0){QTimer::singleShot(100,this,[this]{keyboard();});return;}
        if(++attempts>80){finish(false,"navigation asset rows did not become ready");return;}
        QTimer::singleShot(100,this,[this]{waitForNavigationRows();});
    }
    void keyboard(){auto tab=window->currentTab();tab->focusSearch();auto search=QApplication::focusWidget();QKeyEvent next(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(search,&next);if(!check("search_tab_order_reaches_status",QApplication::focusWidget()&&QApplication::focusWidget()->accessibleName()==QString::fromUtf8("资产状态筛选")))return;auto grid=tab->findChild<AssetGrid*>();const auto first=grid->model()->index(0,0);if(!check("grid_accessible_name_and_description",first.data(Qt::AccessibleTextRole)==first.data(Qt::DisplayRole)&&!first.data(Qt::AccessibleDescriptionRole).toString().isEmpty()))return;grid->setCurrentIndex(first);grid->setFocus(Qt::OtherFocusReason);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(grid,&enter);QTimer::singleShot(350,this,[this]{if(!check("enter_activates_asset_details",window->currentTab()->session()["detail"].toBool()&&window->currentTab()->session()["history"].toArray().size()==1))return;call("asset.get",{{"asset_id",imageId}},[this](const QJsonObject &a){call("asset.update",{{"asset_id",imageId},{"revision",a["revision"]},{"values",QJsonObject{{"name",QString::fromUtf8("角色设计稿_含长文件名与透明贴图_用于验证窗口缩放和键盘读取完整信息_完整版本.png")}}}},[this](const QJsonObject &){window->currentTab()->openAsset(imageId);QTimer::singleShot(350,this,[this]{layoutSizes(0);});});});});}
    void layoutSizes(int at){const auto room=(window->screen()->availableGeometry().size()-QSize(32,32)).expandedTo(window->minimumSize());const auto largest=QSize(1440,940).boundedTo(room);auto middle=QSize(1280,800).boundedTo(room);if(middle==largest)middle=QSize((960+largest.width())/2,(640+largest.height())/2);QList<QSize> sizes;for(auto size:{QSize(960,640),middle,largest})if(!sizes.contains(size))sizes.append(size);if(at>=sizes.size()){window->resize(initialGeometry.size());window->currentTab()->home();themeSwitch(0);return;}window->resize(sizes[at]);QTimer::singleShot(350,this,[this,at,sizes]{const auto requested=sizes[at];if(!alignedPanels(requested.width()))return;if(!check(QString("window_size_%1x%2").arg(requested.width()).arg(requested.height()),window->size()==requested))return;if(!inspectorThumbnailFits(requested.width()))return;auto title=window->currentTab()->findChild<QLabel*>("pageTitle");for(auto candidate:window->currentTab()->findChildren<QLabel*>("pageTitle"))if(candidate->isVisible())title=candidate;if(!check(QString("long_title_elides_without_clipping_%1").arg(requested.width()),title&&!title->wordWrap()&&!title->toolTip().isEmpty()&&title->accessibleName()==title->toolTip()&&title->fontMetrics().horizontalAdvance(title->text())<=title->contentsRect().width()))return;auto detail=window->currentTab()->findChild<QScrollArea*>("detailScroll");if(!check(QString("details_no_horizontal_clipping_%1").arg(requested.width()),detail&&detail->horizontalScrollBar()->maximum()==0))return;auto canvas=window->currentTab()->findChild<ImageCanvas*>();const auto imageRect=canvas->mapFromScene(canvas->sceneRect()).boundingRect();const auto visibleCanvas=canvas->viewport()->rect().adjusted(-3,-3,3,3);auto host=window->currentTab()->findChild<QWidget*>("detailHost");const auto canvasTop=canvas->viewport()->mapToGlobal(QPoint(0,0)),detailTop=host->mapToGlobal(QPoint(0,0));if(!check(QString("fitted_image_fully_visible_%1").arg(requested.width()),!imageRect.isEmpty()&&visibleCanvas.contains(imageRect)&&canvasTop.y()>=detailTop.y()&&canvasTop.y()+canvas->viewport()->height()<=detailTop.y()+host->height()))return;auto manage=window->currentTab()->findChild<QPushButton*>("manageRelations");detail->ensureWidgetVisible(manage,0,0);const auto manageCenter=manage->mapToGlobal(manage->rect().center()),organizationTop=detail->viewport()->mapToGlobal(QPoint(0,0));if(!check(QString("organization_controls_scroll_reachable_%1").arg(requested.width()),manageCenter.y()>=organizationTop.y()&&manageCenter.y()<organizationTop.y()+detail->viewport()->height()))return;const auto capture=captureWidget(window);if(capture.error.isEmpty())capture.image.save(QFileInfo(report).absolutePath()+QString("/ui-layout-%1x%2.png").arg(requested.width()).arg(requested.height()));else captureWarnings.append(QJsonObject{{"size",QString("%1x%2").arg(requested.width()).arg(requested.height())},{"error",capture.error}});layoutSizes(at+1);});}
    bool inspectorThumbnailFits(int width){
        auto thumbnail=window->currentTab()->findChild<QLabel*>("inspectorThumbnail");bool fits=thumbnail&&!thumbnail->pixmap().isNull();
        if(fits){
            const auto shown=thumbnail->pixmap().deviceIndependentSize();const auto room=thumbnail->contentsRect().size();
            // The generated color fixture is 320 x 240; allow only integer scaling rounding.
            fits=shown.width()>0&&shown.height()>0&&shown.width()<=room.width()+1&&shown.height()<=room.height()+1&&qAbs(shown.width()-shown.height()*320./240.)<=1.5;
        }
        return check(QString("inspector_thumbnail_fits_%1").arg(width),fits);
    }
    void themeSwitch(int index){
        const QStringList ids{"ivory","graphite","ocean","mint"};
        if(index>=ids.size()){dialog();return;}
        auto selector=window->findChild<QComboBox*>("headerTheme");selector->setCurrentIndex(selector->findData(ids[index]));
        QMetaObject::invokeMethod(selector,"activated",Qt::DirectConnection,Q_ARG(int,selector->currentIndex()));
        QTimer::singleShot(350,this,[this,index,ids]{call("library.info",{},[this,index,ids](const QJsonObject &info){
            if(!check("header_theme_persists_"+ids[index],info["settings"].toObject()["theme"]==ids[index]&&themeColors().id==ids[index]&&window->findChild<QComboBox*>("headerTheme")->currentData()==ids[index]))return;
            QImage iconImage(64,64,QImage::Format_ARGB32_Premultiplied);iconImage.fill(Qt::transparent);
            {QPainter painter(&iconImage);folderIcon.paint(&painter,iconImage.rect(),Qt::AlignCenter,QIcon::Normal,QIcon::Off);}
            const QColor expected=themeColors().text;int coloredPixels=0,opaquePixels=0;bool matches=true;
            for(int y=0;y<iconImage.height();++y)for(int x=0;x<iconImage.width();++x){
                const QRgb pixel=iconImage.pixel(x,y);const int alpha=qAlpha(pixel);if(!alpha)continue;++coloredPixels;if(alpha>=250)++opaquePixels;
                // Compare premultiplied RGB so antialiased edge pixels retain the same theme hue.
                matches&=qAbs(qRed(pixel)-qRound(expected.red()*alpha/255.))<=1&&qAbs(qGreen(pixel)-qRound(expected.green()*alpha/255.))<=1&&qAbs(qBlue(pixel)-qRound(expected.blue()*alpha/255.))<=1;
            }
            if(!check("line_icon_recolors_to_theme_text_"+ids[index],matches&&coloredPixels>16&&opaquePixels>16))return;
            const auto capture=captureWidget(window);if(capture.error.isEmpty())capture.image.save(QFileInfo(report).absolutePath()+"/theme-"+ids[index]+".png");else captureWarnings.append(capture.error);
            themeSwitch(index+1);
        });});
    }
    void dialog(){window->currentTab()->focusSearch();dialogOrigin=QApplication::focusWidget();window->showSettings();for(auto candidate:window->findChildren<GlassDialog*>())if(candidate->isVisible())foreground(candidate);QTimer::singleShot(400,this,[this]{GlassDialog *settings=nullptr;for(auto candidate:window->findChildren<GlassDialog*>())if(candidate->isVisible())settings=candidate;if(!check("glass_dialog_frameless",settings&&settings->windowFlags().testFlag(Qt::FramelessWindowHint)))return;if(!check("glass_dialog_content_below_titlebar",settings->layout()&&settings->layout()->contentsMargins().top()>=44))return;if(!check("dialog_focus_starts_in_content",QApplication::focusWidget()&&settings->isAncestorOf(QApplication::focusWidget())&&QApplication::focusWidget()->objectName()!="windowClose"))return;const auto capture=captureWidget(settings);if(capture.error.isEmpty())capture.image.save(QFileInfo(report).absolutePath()+"/ui-settings-dialog.png");else captureWarnings.append(capture.error);QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(settings,&escape);if(!check("esc_closes_glass_dialog",!settings->isVisible()))return;QTimer::singleShot(100,this,[this]{if(!check("dialog_returns_focus_to_invoker",dialogOrigin&&QApplication::focusWidget()==dialogOrigin))return;finish(true);});});}
    void navigation(){auto tab=window->currentTab();tab->openAsset(imageId);if(!check("association_history_push",tab->session()["history"].toArray()==QJsonArray{modelId,imageId}))return;tab->back();if(!check("association_back_returns_model",tab->session()["history"].toArray()==QJsonArray{modelId}))return;tab->home();if(!check("home_clears_history",tab->session()["history"].toArray().isEmpty()&&!tab->session()["detail"].toBool()))return;QJsonObject state{{"filter",QJsonObject{{"query","UI"},{"types",QJsonArray{"image","video"}},{"tags",QJsonArray{"ui-test","color"}},{"status","favorite"},{"sort","name"}}},{"history",QJsonArray{modelId,imageId}},{"detail",true},{"asset_id",imageId}};auto duplicate=window->addTab(state);if(!check("duplicate_tab_keeps_independent_filters",duplicate->session()["filter"]==state["filter"]&&tab->session()["history"].toArray().isEmpty()))return;second=new MainWindow(rpc,new ThumbnailStore(rpc,this));second->show();second->currentTab()->openAsset(imageId);call("asset.get",{{"asset_id",imageId}},[this](const QJsonObject &asset){call("asset.update",{{"asset_id",imageId},{"revision",asset["revision"]},{"values",QJsonObject{{"name","UI synchronized.png"}}}},[this](const QJsonObject &){rpc->refresh();QTimer::singleShot(2300,this,[this]{if(!window||!second){finish(false,"window closed during test");return;}if(!check("two_windows_share_asset_identity",window->currentTab()->currentAssetId()==imageId&&second->currentTab()->currentAssetId()==imageId))return;auto hasName=[](MainWindow *w){for(auto edit:w->findChildren<QLineEdit*>())if(edit->text()=="UI synchronized.png")return true;return false;};if(!check("cross_window_update_visible",hasName(window)&&hasName(second)))return;window->currentTab()->home();window->currentTab()->restoreSession({{"filter",QJsonObject{}},{"history",QJsonArray{}},{"detail",false}});attempts=0;waitForThumbnail();});});});}
};
}
void runUiSelfTest(MainWindow *window,RpcClient *rpc,const QString &reportPath,const QString &screenshotPath,bool desktopValidation){auto state=new State(window,rpc,reportPath,screenshotPath,desktopValidation);QTimer::singleShot(0,state,[state]{state->start();});QTimer::singleShot(90000,state,[state]{if(!state->done)state->finish(false,"self-test timeout");});}
