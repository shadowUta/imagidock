#include "UiSelfTest.h"
#include "ApplicationCapture.h"
#include "GlassDialog.h"
#include "GlassDialogs.h"
#include "AssetDialogs.h"
#include "PreviewPane.h"
#include "UiIcons.h"
#include "InspectorSummary.h"
#include "UiMetrics.h"
#include <QAbstractButton>
#include <QLayout>
#include <QAction>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QDateTime>
#include <QCryptographicHash>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QMessageBox>
#include <QMenu>
#include <QTableWidget>
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
#include <QEventLoop>
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
    QPointer<MainWindow> window,second,detachedWindow;RpcClient *rpc;QString report,screenshot,modelId,imageId,normalId,fixtureDirectory,fixtureRootId;QJsonObject originalFilterSession;QPointer<QWidget> dialogOrigin;QJsonArray assertions,captureWarnings,nativePresentations,windowStatesReport,layoutDiagnostics;QRect initialGeometry;QIcon folderIcon=uiIcon("folder");bool desktopValidation,done=false;int attempts=0;
    State(MainWindow *w,RpcClient *r,const QString &p,const QString &s,bool desktop):QObject(qApp),window(w),rpc(r),report(p),screenshot(s),desktopValidation(desktop){}
    void recordFocusDiagnostic(const QString &name,QWidget *target=nullptr){
        if(!target)target=window;
        auto identity=[](QWidget *widget){return widget?QJsonObject{{"present",true},{"class",QString::fromLatin1(widget->metaObject()->className())},{"object_name",widget->objectName()},{"address",QString("0x%1").arg(qulonglong(reinterpret_cast<quintptr>(widget)),0,16)}}:QJsonObject{{"present",false}};};
        auto details=[&identity](QWidget *widget){auto result=identity(widget);if(widget){result["visible"]=widget->isVisible();result["enabled"]=widget->isEnabled();result["has_focus"]=widget->hasFocus();result["active_window"]=widget->isActiveWindow();result["focus_policy"]=int(widget->focusPolicy());result["internal_win_id"]=QString("0x%1").arg(qulonglong(widget->internalWinId()),0,16);result["window"]=identity(widget->window());result["focus_widget"]=identity(widget->focusWidget());}return result;};
        QJsonObject diagnostic{{"name",name},{"scope","in-process focus diagnostic; no screen capture"},{"qt_active_window",details(QApplication::activeWindow())},{"qt_active_modal_widget",details(QApplication::activeModalWidget())},{"global_focus",details(QApplication::focusWidget())},{"target",details(target)},{"target_window",details(target?target->window():nullptr)},{"test_window",details(window)},{"dialog_origin",details(dialogOrigin)},{"application_state",int(QGuiApplication::applicationState())}};
#ifdef Q_OS_WIN
        const auto nativeForeground=GetForegroundWindow();DWORD foregroundProcess=0;if(nativeForeground)GetWindowThreadProcessId(nativeForeground,&foregroundProcess);
        diagnostic["windows_foreground_hwnd"]=QString("0x%1").arg(qulonglong(reinterpret_cast<quintptr>(nativeForeground)),0,16);diagnostic["windows_foreground_pid"]=qint64(foregroundProcess);diagnostic["test_process_pid"]=qint64(GetCurrentProcessId());
#endif
        layoutDiagnostics.append(diagnostic);
    }
    ApplicationCapture captureWidget(QWidget *widget){
        if(desktopValidation)return captureApplicationWidget(widget);
        if(!widget)return {{},QStringLiteral("functional_capture_failed: target widget is missing")};
        // QWidget::grab renders this widget's Qt raster content; it is not a desktop or GPU presentation capture.
        const auto raster=widget->grab();
        if(raster.isNull())return {{},QStringLiteral("functional_capture_failed: Qt raster is empty")};
        return {raster,{}};
    }
    bool check(const QString &name,bool passed){if(!passed)recordFocusDiagnostic(name+"_failure");assertions.append(QJsonObject{{"name",name},{"passed",passed}});if(!passed)finish(false,name);return passed;}
    void finish(bool ok,const QString &message={}){
        if(done)return;done=true;
        if(window&&!screenshot.isEmpty()){QDir().mkpath(QFileInfo(screenshot).absolutePath());const auto capture=captureWidget(window);if(capture.error.isEmpty())capture.image.save(screenshot);else captureWarnings.append(capture.error);}
        int passed=0,skippedDesktop=0;for(const auto &value:assertions)passed+=value.toObject()["passed"].toBool()?1:0;for(const auto &value:nativePresentations)skippedDesktop+=value.toObject()["skipped"].toBool()?1:0;
        if(!report.isEmpty()){QDir().mkpath(QFileInfo(report).absolutePath());QSaveFile out(report);if(out.open(QIODevice::WriteOnly)){
            out.write(QJsonDocument(QJsonObject{{"ok",ok},{"message",message},{"desktop_validation",desktopValidation},{"scope",desktopValidation?"strict: functional checks, framebuffer readback and guarded desktop presentation comparison":"functional-only: Qt raster layout/input and framebuffer readback; no desktop presentation evidence"},{"assertion_count",assertions.size()},{"passed_assertion_count",passed},{"failed_assertion_count",assertions.size()-passed},{"skipped_desktop_presentation_count",skippedDesktop},{"assertions",assertions},{"capture_warnings",captureWarnings},{"native_presentations",nativePresentations},{"window_states",windowStatesReport},{"layout_diagnostics",layoutDiagnostics},{"library",rpc->libraryPath()},{"device_pixel_ratio",window?window->devicePixelRatioF():0.},{"screen_available",window?QJsonArray{window->screen()->availableGeometry().width(),window->screen()->availableGeometry().height()}:QJsonArray{}}}).toJson());out.commit();
        }}
        if(second)second->close();rpc->call("service.shutdown",{{"cancel_jobs",true}},this,[ok](const QJsonObject &,const QString &){qApp->exit(ok?0:2);});QTimer::singleShot(3500,qApp,[ok]{qApp->exit(ok?0:2);});
    }
    void call(const QString &method,const QJsonObject &params,std::function<void(const QJsonObject&)> next){rpc->call(method,params,this,[this,method,next](const QJsonObject &r,const QString &err){if(done)return;if(!err.isEmpty()){finish(false,method+": "+err);return;}next(r);});}
    void awaitJob(const QString &id,std::function<void()> next){if(done)return;if(++attempts>300){finish(false,"job timeout");return;}call("job.get",{{"job_id",id}},[this,id,next](const QJsonObject &job){const auto state=job["state"].toString();if(state=="completed"){attempts=0;next();}else if(state=="failed"||state=="cancelled"||state=="interrupted")finish(false,job["error"].toString());else QTimer::singleShot(150,this,[this,id,next]{awaitJob(id,next);});});}
    QString fixtures(){const QString directory=QFileInfo(rpc->libraryPath()).absolutePath()+"/ui-fixtures";QDir().mkpath(directory);QImage color(320,240,QImage::Format_RGBA8888);color.fill(QColor("#67c8b6"));{QPainter p(&color);p.setPen(Qt::NoPen);p.setBrush(QColor("#ffe0a6"));p.drawEllipse(80,30,160,160);p.setPen(QColor("#102927"));p.drawText(color.rect(),Qt::AlignCenter,"ImagiDock UI");}color.save(directory+"/UI color.png");QImage normal(64,64,QImage::Format_RGBA8888);normal.fill(QColor(128,128,255));normal.save(directory+"/UI normal.png");
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);for(int i=0;i<4;++i){const float x=float(i%2)*1.3f-.65f,z=float(i/2)*1.3f-.65f;for(float v:{x-.5f,0.f,z-.5f,x+.5f,0.f,z-.5f,x,0.f,z+.5f})stream<<v;}for(int i=0;i<4;++i)for(quint16 v:{quint16(0),quint16(1),quint16(2)})stream<<v;
        QJsonArray views,accessors,primitives,materials;for(int i=0;i<4;++i){views.append(QJsonObject{{"buffer",0},{"byteOffset",i*36},{"byteLength",36},{"target",34962}});views.append(QJsonObject{{"buffer",0},{"byteOffset",144+i*6},{"byteLength",6},{"target",34963}});const double x=(i%2)*1.3-.65,z=(i/2)*1.3-.65;accessors.append(QJsonObject{{"bufferView",i*2},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",QJsonArray{x-.5,0.,z-.5}},{"max",QJsonArray{x+.5,0.,z+.5}}});accessors.append(QJsonObject{{"bufferView",i*2+1},{"componentType",5123},{"count",3},{"type","SCALAR"}});primitives.append(QJsonObject{{"attributes",QJsonObject{{"POSITION",i*2}}},{"indices",i*2+1},{"material",i}});materials.append(QJsonObject{{"name",QString("UI slot %1").arg(i+1)},{"doubleSided",true},{"pbrMetallicRoughness",QJsonObject{{"baseColorFactor",QJsonArray{.2+i*.2,.8-i*.15,.6,1.}},{"roughnessFactor",.65},{"metallicFactor",0.}}}});}
        QJsonObject gltf{{"asset",QJsonObject{{"version","2.0"},{"generator","ImagiDock UI self test"}}},{"buffers",QJsonArray{QJsonObject{{"uri","data:application/octet-stream;base64,"+QString::fromLatin1(bytes.toBase64())},{"byteLength",bytes.size()}}}},{"bufferViews",views},{"accessors",accessors},{"materials",materials},{"meshes",QJsonArray{QJsonObject{{"primitives",primitives}}}},{"nodes",QJsonArray{QJsonObject{{"mesh",0}}}},{"scenes",QJsonArray{QJsonObject{{"nodes",QJsonArray{0}}}}},{"scene",0}};QSaveFile file(directory+"/UI four slots.gltf");if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(gltf).toJson());file.commit();}
        // Distinct source mtimes make the modified UI sort observable, independent of import order.
        const QStringList names{"UI four slots.gltf","UI color.png","UI normal.png"};
        for(int i=0;i<names.size();++i){QFile source(directory+"/"+names[i]);if(source.open(QIODevice::ReadWrite))source.setFileTime(QDateTime::fromMSecsSinceEpoch(1700000000000LL+i*60000,Qt::UTC),QFileDevice::FileModificationTime);}
        return directory;
    }
    void positionTestWindowInWorkArea(){
        const auto available=window->screen()->availableGeometry();const auto room=available.adjusted(16,16,-16,-16);
        const auto size=window->size().boundedTo(room.size());
        window->setGeometry(QRect(QPoint(room.left()+(room.width()-size.width())/2,room.top()+(room.height()-size.height())/2),size));
    }
    void start(){recordWindowState("startup_before_test_placement");positionTestWindowInWorkArea();initialGeometry=window->geometry();recordWindowState("initial");foreground(window);if(!check("self_test_start_window_fits_work_area",window->screen()->availableGeometry().contains(window->frameGeometry())))return;if(!check("frameless_window_flag",window->windowFlags().testFlag(Qt::FramelessWindowHint)))return;
#ifdef Q_OS_WIN
const auto handle=reinterpret_cast<HWND>(window->winId());RECT rectangle{};GetWindowRect(handle,&rectangle);const auto region=CreateRectRgn(0,0,0,0);const bool rounded=GetWindowRgn(handle,region)!=ERROR&&!PtInRegion(region,0,0);DeleteObject(region);if(!check("native_window_has_round_region",rounded))return;const auto edge=SendMessage(handle,WM_NCHITTEST,0,MAKELPARAM(rectangle.left+2,(rectangle.top+rectangle.bottom)/2));const auto brand=window->findChild<QLabel*>("windowBrand");const auto brandLocal=brand->mapTo(window,brand->rect().center());POINT brandPoint{qRound(brandLocal.x()*window->devicePixelRatioF()),qRound(brandLocal.y()*window->devicePixelRatioF())};ClientToScreen(handle,&brandPoint);const auto caption=SendMessage(handle,WM_NCHITTEST,0,MAKELPARAM(brandPoint.x,brandPoint.y));if(!check("native_resize_and_caption_hit_regions",edge==HTLEFT&&caption==HTCAPTION))return;
#endif
if(!chromeHitRegions())return;
call("library.info",{},[this](const QJsonObject &r){if(!check("dedicated_empty_library",r["counts"].toObject()["all"].toInt()==0))return;fixtureDirectory=fixtures();call("directory.create",{{"name","ui-fixtures"}},[this](const QJsonObject &directory){fixtureRootId=directory["id"].toString();if(!check("isolated_fixture_directory_created",!fixtureRootId.isEmpty()))return;importFixture(0);});});}
    void importFixture(int at){
        // Separate accepted import jobs provide a known imported DESC order without reading the DB.
        const QStringList names{"UI color.png","UI normal.png","UI four slots.gltf"};
        if(at>=names.size()){loaded();return;}
        call("asset.import",{{"paths",QJsonArray{fixtureDirectory+"/"+names[at]}},{"directory_id",fixtureRootId},{"backup",false}},[this,at](const QJsonObject &r){awaitJob(r["job_id"].toString(),[this,at]{QTimer::singleShot(250,this,[this,at]{importFixture(at+1);});});});
    }
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
    void filter(){call("asset.list",{{"types",QJsonArray{"image","video"}},{"tags",QJsonArray{"ui-test","does-not-exist"}},{"status","favorite"}},[this](const QJsonObject &r){if(!check("filter_or_within_and_across",r["total"].toInt()==1&&r["items"].toArray()[0].toObject()["id"]==imageId))return;
        call("asset.get",{{"asset_id",normalId}},[this](const QJsonObject &normal){call("asset.update",{{"asset_id",normalId},{"revision",normal["revision"]},{"values",QJsonObject{{"tags",QJsonArray{"ui-test-normal"}}}}},[this](const QJsonObject &){originalFilterSession=window->currentTab()->session();rpc->refresh();attempts=0;waitForFilterNavigation();});});
    });}
    template<class T> T *accessibleControl(const QString &name){for(auto control:window->currentTab()->findChildren<T*>())if(control->accessibleName()==name)return control;return nullptr;}
    void waitForFilterNavigation(){
        if(done)return;auto tags=accessibleControl<QListWidget>(QString::fromUtf8("标签筛选，多选取并集"));auto dirs=accessibleControl<QTreeWidget>(QString::fromUtf8("库内目录筛选"));
        bool tagReady=false,dirReady=false;
        if(tags)for(int i=0;i<tags->count();++i)tagReady|=tags->item(i)->text()=="ui-test-normal";
        if(dirs)for(QTreeWidgetItemIterator it(dirs);*it;++it)dirReady|=(*it)->data(0,Qt::UserRole).toString()==fixtureRootId;
        if(tagReady&&dirReady){attempts=0;filterStep(0);return;}
        if(++attempts>80){finish(false,"UI filter navigation did not receive fixture metadata");return;}
        QTimer::singleShot(100,this,[this]{waitForFilterNavigation();});
    }
    void expectModelRows(const QString &name,const QStringList &expected,bool ordered,std::function<void()> action,std::function<void()> next){
        auto grid=window->currentTab()->findChild<AssetGrid*>();auto model=grid?qobject_cast<AssetModel*>(grid->model()):nullptr;
        if(!check(name+"_has_live_asset_model",model!=nullptr))return;
        auto guard=new QObject(this);auto accepted=std::make_shared<bool>(false);
        connect(model,&AssetModel::loaded,guard,[this,guard,model,name,expected,ordered,next,accepted](int count,int total){
            if(done||*accepted)return;*accepted=true;QStringList actual;for(int row=0;row<model->rowCount();++row)actual<<model->asset(row)["id"].toString();
            auto wanted=expected;if(!ordered){actual.sort();wanted.sort();}
            layoutDiagnostics.append(QJsonObject{{"name",name},{"scope","actual AssetModel::loaded after UI controls"},{"expected_ids",QJsonArray::fromStringList(wanted)},{"model_ids",QJsonArray::fromStringList(actual)},{"loaded_count",count},{"total",total},{"filter",window->currentTab()->session()["filter"]}});
            guard->deleteLater();if(!check(name,count==expected.size()&&total==expected.size()&&actual==wanted))return;
            QTimer::singleShot(0,this,[this,next]{if(!done)next();});
        });
        QTimer::singleShot(6000,guard,[this,name,guard,accepted]{if(!done&&!*accepted)finish(false,name+": AssetModel::loaded timeout");guard->deleteLater();});
        action();
    }
    void filterStep(int step){
        auto tab=window->currentTab();auto types=tab->findChild<QListWidget*>("typeNavigation");auto tags=accessibleControl<QListWidget>(QString::fromUtf8("标签筛选，多选取并集"));auto dirs=accessibleControl<QTreeWidget>(QString::fromUtf8("库内目录筛选"));auto status=accessibleControl<QComboBox>(QString::fromUtf8("资产状态筛选"));auto sort=accessibleControl<QComboBox>(QString::fromUtf8("资产排序"));
        if(!check(QString("ui_filter_controls_present_step_%1").arg(step),types&&tags&&dirs&&status&&sort))return;
        auto selectType=[types](const QString &id){for(int i=0;i<types->count();++i)if(types->item(i)->data(Qt::UserRole)==id)types->item(i)->setSelected(true);};
        auto selectTag=[tags](const QString &id){for(int i=0;i<tags->count();++i)if(tags->item(i)->text()==id)tags->item(i)->setSelected(true);};
        auto advance=[this,step]{filterStep(step+1);};
        const QStringList all{imageId,modelId,normalId};
        switch(step){
        case 0:expectModelRows("ui_type_image_filters_model_rows",{imageId,normalId},false,[types,selectType]{types->clearSelection();selectType("image");},advance);break;
        case 1:expectModelRows("ui_type_multiselect_is_union",all,false,[selectType]{selectType("model");},advance);break;
        case 2:expectModelRows("ui_tag_selection_filters_model_rows",{imageId},false,[selectTag]{selectTag("ui-test");},advance);break;
        case 3:expectModelRows("ui_tag_multiselect_is_union",{imageId,normalId},false,[selectTag]{selectTag("ui-test-normal");},advance);break;
        case 4:expectModelRows("ui_directory_selection_contains_three_fixtures",all,false,[this,tags,dirs]{tags->clearSelection();for(QTreeWidgetItemIterator it(dirs);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==fixtureRootId){dirs->setCurrentItem(*it);break;}},advance);break;
        case 5:expectModelRows("ui_favorite_status_filters_model_rows",{imageId},true,[status]{status->setCurrentIndex(status->findData("favorite"));},advance);break;
        case 6:expectModelRows("ui_modified_sort_uses_source_mtime_desc",{normalId,imageId,modelId},true,[status,sort]{status->setCurrentIndex(status->findData("all"));sort->setCurrentIndex(sort->findData("modified"));},advance);break;
        case 7:expectModelRows("ui_imported_sort_uses_separate_job_import_order",{modelId,normalId,imageId},true,[sort]{sort->setCurrentIndex(sort->findData("imported"));},advance);break;
        // Library.cpp specifies SQLite ASCII NOCASE then stable id, not locale/natural sorting.
        // These distinct ASCII fixture names make the service's first comparator decisive.
        case 8:expectModelRows("ui_name_sort_matches_sqlite_nocase",{imageId,modelId,normalId},true,[sort]{sort->setCurrentIndex(sort->findData("name"));},advance);break;
        case 9:expectModelRows("ui_filter_test_restores_original_model_scope",all,false,[this,tab]{tab->restoreSession(originalFilterSession);},[this,tab]{tab->openAsset(modelId);QTimer::singleShot(1500,this,[this]{windowStates();});});break;
        }
    }
    void foreground(QWidget *widget){
        foregroundApplicationTestWindow(widget);
    }
    bool nativePresentation(ModelView *model,const QString &frameName,const QString &presentName,const QString &fileName){
        if(!desktopValidation){
            const QString path=QFileInfo(report).absolutePath()+"/"+fileName+"-framebuffer.png";
            if(!check(frameName,model&&model->isVisible()&&model->saveSnapshot(path)))return false;
            const QImage frame(path);int foregroundPixels=0;
            if(!frame.isNull()&&frame.width()>16&&frame.height()>16){const auto background=frame.pixelColor(2,2);for(int y=8;y<frame.height()-8;y+=4)for(int x=8;x<frame.width()-8;x+=4){const auto pixel=frame.pixelColor(x,y);if(qAbs(pixel.red()-background.red())+qAbs(pixel.green()-background.green())+qAbs(pixel.blue()-background.blue())>45)++foregroundPixels;}}
            if(!check(frameName+"_has_rendered_foreground",foregroundPixels>100))return false;
            nativePresentations.append(QJsonObject{{"name",presentName},{"skipped",true},{"reason","functional-only mode: framebuffer readback is checked; desktop presentation is not captured or validated"},{"framebuffer_file",QFileInfo(path).fileName()}});
            return true;
        }
        QElapsedTimer captureTraceClock;captureTraceClock.start();
        const auto capture=captureApplicationWidget(model,true);
        if(!capture.error.isEmpty()){
            recordFocusDiagnostic(presentName+"_capture_failure",model);
            QJsonArray nativeWidgets;
            for(auto widget:qApp->allWidgets())if(widget->isVisible()&&widget->internalWinId()){
                const auto origin=widget->mapToGlobal(QPoint());
                nativeWidgets.append(QJsonObject{{"class",QString::fromLatin1(widget->metaObject()->className())},{"object_name",widget->objectName()},{"native_id",QString::number(qulonglong(widget->internalWinId()))},{"top_level_id",QString::number(qulonglong(widget->window()->internalWinId()))},{"global_bounds",QJsonArray{origin.x(),origin.y(),widget->width(),widget->height()}}});
            }
            layoutDiagnostics.append(QJsonObject{{"name",presentName+"_capture_native_widgets"},{"error",capture.error},{"widgets",nativeWidgets}});
            finish(false,capture.error);return false;
        }
        const auto screen=capture.image.toImage();const QString path=QFileInfo(report).absolutePath()+"/"+fileName;
        if(qEnvironmentVariableIntValue("IMAGIDOCK_CAPTURE_TRACE")==1){
            QPointer<ModelView> target=model;
            auto recordTrace=[this,&captureTraceClock,&path,&presentName](int index,const ApplicationCapture &sample){
                const auto pixels=sample.image.toImage().convertToFormat(QImage::Format_RGB32);int minimum[3]{255,255,255},maximum[3]{0,0,0};qint64 nonblack=0;
                for(int y=0;y<pixels.height();++y){const auto line=reinterpret_cast<const QRgb*>(pixels.constScanLine(y));for(int x=0;x<pixels.width();++x){const int values[3]{qRed(line[x]),qGreen(line[x]),qBlue(line[x])};nonblack+=values[0]||values[1]||values[2];for(int component=0;component<3;++component){minimum[component]=qMin(minimum[component],values[component]);maximum[component]=qMax(maximum[component],values[component]);}}}
                const auto file=path+QString("-capture-trace-%1.png").arg(index);const bool saved=!pixels.isNull()&&pixels.save(file);
                layoutDiagnostics.append(QJsonObject{{"name",presentName+QString("_capture_trace_%1").arg(index)},{"capture_trace",true},{"scope","guarded model desktop before any saveSnapshot; diagnostic only"},{"sample_index",index},{"offset_ms",captureTraceClock.elapsed()},{"width",pixels.width()},{"height",pixels.height()},{"rgb_min",pixels.isNull()?QJsonArray{-1,-1,-1}:QJsonArray{minimum[0],minimum[1],minimum[2]}},{"rgb_max",pixels.isNull()?QJsonArray{-1,-1,-1}:QJsonArray{maximum[0],maximum[1],maximum[2]}},{"nonblack_pixels",nonblack},{"file",saved?QFileInfo(file).fileName():QString()},{"error",sample.error}});
            };
            recordTrace(0,capture);
            for(int index=1;index<=2;++index){
                if(!target){finish(false,"capture trace target disappeared before model readback");return false;}
                target->update();target->window()->update();QCoreApplication::sendPostedEvents(nullptr,QEvent::UpdateRequest);
                QEventLoop paintEvents;QTimer::singleShot(100,&paintEvents,&QEventLoop::quit);paintEvents.exec(QEventLoop::ExcludeUserInputEvents);
                if(done)return false;
                if(!target){finish(false,"capture trace target disappeared while processing paint events");return false;}
                recordTrace(index,captureApplicationWidget(target,true));
            }
        }
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
    void recordWindowState(const QString &name){auto rect=[](const QRect &r){return QJsonArray{r.x(),r.y(),r.width(),r.height()};};windowStatesReport.append(QJsonObject{{"name",name},{"state",int(window->windowState())},{"flags",double(quint64(window->windowFlags()))},{"geometry",rect(window->geometry())},{"normal_geometry",rect(window->normalGeometry())},{"frame_geometry",rect(window->frameGeometry())},{"available_geometry",rect(window->screen()->availableGeometry())},{"device_pixel_ratio",window->devicePixelRatioF()}});}
    void clickMaximize(){for(auto button:window->findChildren<QToolButton*>())if(button->accessibleName()==QString::fromUtf8("最大化窗口")||button->accessibleName()==QString::fromUtf8("还原窗口")){button->click();return;}}
    void captionDoubleClick(){
#ifdef Q_OS_WIN
        const auto handle=reinterpret_cast<HWND>(window->winId());RECT bounds{};GetWindowRect(handle,&bounds);SendMessage(handle,WM_NCLBUTTONDBLCLK,HTCAPTION,MAKELPARAM(bounds.left+qRound(700*window->devicePixelRatioF()),bounds.top+qRound(28*window->devicePixelRatioF())));
#endif
    }
    void windowStates(){
        recordWindowState("before_window_state_test_placement");positionTestWindowInWorkArea();foreground(window);
        QTimer::singleShot(250,this,[this]{
            if(!check("window_state_test_setup_fits_work_area",window->screen()->availableGeometry().contains(window->frameGeometry())))return;
            const auto buttonBaseline=window->geometry();recordWindowState("before_titlebar_maximize");clickMaximize();
            QTimer::singleShot(400,this,[this,buttonBaseline]{
                recordWindowState("titlebar_maximized");if(!check("titlebar_button_maximizes",window->isMaximized()))return;
                clickMaximize();QTimer::singleShot(400,this,[this,buttonBaseline]{
                    recordWindowState("titlebar_restored");if(!check("titlebar_button_restores_original_geometry",!window->isMaximized()&&!window->isMinimized()&&window->geometry()==buttonBaseline))return;
                    const auto captionBaseline=window->geometry();recordWindowState("before_caption_doubleclick");captionDoubleClick();
                    QTimer::singleShot(400,this,[this,captionBaseline]{
                        recordWindowState("caption_doubleclick_maximized");if(!check("titlebar_doubleclick_maximizes",window->isMaximized()))return;
                        captionDoubleClick();QTimer::singleShot(400,this,[this,captionBaseline]{
                            recordWindowState("caption_doubleclick_restored");if(!check("titlebar_doubleclick_restores_original_geometry",!window->isMaximized()&&window->geometry()==captionBaseline))return;
                            renderWindowStates();
                        });
                    });
                });
            });
        });
    }
    void renderWindowStates(){
        const auto renderBaseline=window->geometry();recordWindowState("render_before_maximize");window->showMaximized();foreground(window);recordWindowState("render_maximized");
        QTimer::singleShot(900,this,[this,renderBaseline]{
            const auto screen=window->screen()->availableGeometry();
            if(!check("maximize_uses_screen_work_area",window->isMaximized()&&qAbs(window->width()-screen.width())<=2&&qAbs(window->height()-screen.height())<=2))return;
            if(!nativePresentation(window->currentTab()->findChild<ModelView*>(),"maximized_model_framebuffer","model_presents_on_native_window","ui-maximized-model"))return;
            window->showMinimized();
            QTimer::singleShot(300,this,[this,renderBaseline]{
                if(!check("custom_window_minimize",window->isMinimized()))return;
                window->showNormal();recordWindowState("normal_before_foreground");foreground(window);
                QTimer::singleShot(900,this,[this,renderBaseline]{
                    recordWindowState("normal_after_foreground");if(!check("minimize_restore_returns_normal_geometry",!window->isMinimized()&&!window->isMaximized()&&window->geometry()==renderBaseline))return;
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
        auto summary=window->currentTab()->findChild<InspectorSummary*>();
        auto carousel=summary?summary->findChild<QWidget*>("inspectorCarousel"):nullptr;
        bool fits=summary&&carousel&&!summary->previewImageSize().isEmpty();
        if(fits){
            const auto shown=summary->previewImageRect();const auto room=carousel->contentsRect();
            // The generated color fixture is 320 x 240; allow only integer scaling rounding.
            fits=shown.width()>0&&shown.height()>0&&QRectF(room).adjusted(-1,-1,1,1).contains(shown)&&qAbs(shown.width()-shown.height()*320./240.)<=1.5;
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
    static void escapeDialog(QDialog *dialog){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(dialog,&escape);}
    bool belongsToTestWindow(const QObject *object) const {
        // QWidget::isAncestorOf deliberately stops at a child window boundary.
        // Dialog ownership follows QObject parents even though every host is a top-level window.
        for(auto owner=object;owner;owner=owner->parent())if(owner==window.data())return true;
        return false;
    }
    QDialog *ownedDialog(const QString &objectName={}){
        for(auto widget:qApp->topLevelWidgets())if(auto candidate=qobject_cast<QDialog*>(widget))if(candidate->isVisible()&&belongsToTestWindow(candidate)&&(objectName.isEmpty()||candidate->objectName()==objectName))return candidate;
        return nullptr;
    }
    bool dialogShell(QDialog *dialog,const QString &name,bool capture=true){
        layoutDiagnostics.append(QJsonObject{{"name",name},{"dialog_found",dialog!=nullptr},{"dialog_visible",dialog&&dialog->isVisible()},{"dialog_flags",dialog?double(quint64(dialog->windowFlags())):0.},{"object_name",dialog?dialog->objectName():QString()},{"scope",desktopValidation?"owned dialog desktop capture":"owned dialog Qt raster only"}});
        if(!check(name+"_frameless",dialog&&dialog->isVisible()&&dialog->windowFlags().testFlag(Qt::FramelessWindowHint)))return false;
#ifdef Q_OS_WIN
        const auto handle=reinterpret_cast<HWND>(dialog->winId());const auto region=CreateRectRgn(0,0,0,0);
        const bool rounded=GetWindowRgn(handle,region)!=ERROR&&!PtInRegion(region,0,0);DeleteObject(region);
        if(!check(name+"_native_rounded_without_caption",rounded&&(GetWindowLongPtrW(handle,GWL_STYLE)&WS_CAPTION)==0))return false;
#endif
        bool contentBelow=dialog->layout()&&dialog->layout()->contentsMargins().top()>=44;
        if(contentBelow)for(int i=0;i<dialog->layout()->count();++i){const auto item=dialog->layout()->itemAt(i);if(item->isEmpty())continue;contentBelow&=item->geometry().top()>=44&&dialog->rect().contains(item->geometry());}
        if(!check(name+"_content_below_custom_title",contentBelow))return false;
        const auto available=dialog->screen()->availableGeometry();
        if(!check(name+"_fits_screen_work_area",dialog->width()<=available.width()&&dialog->height()<=available.height()&&available.contains(dialog->frameGeometry())))return false;
        recordFocusDiagnostic(name+"_before_content_focus_check",dialog);
        auto focus=QApplication::focusWidget();
        if(!check(name+"_focus_in_content",focus&&dialog->isAncestorOf(focus)&&focus->objectName()!="windowClose"))return false;
        auto close=dialog->findChild<QToolButton*>("windowClose");
        const QRect closeBounds=close?QRect(close->mapTo(dialog,QPoint()),close->size()):QRect();
        if(!check(name+"_title_close_reachable",close&&close->isVisibleTo(dialog)&&close->isEnabled()&&dialog->rect().contains(closeBounds)&&closeBounds.top()>=0&&closeBounds.bottom()<44))return false;
        bool actionsReachable=true;int actions=0,tableActions=0;QJsonArray actionBounds;
        for(auto button:dialog->findChildren<QAbstractButton*>())if(button->isVisibleTo(dialog)&&button->objectName()!="windowClose"){
            QAbstractItemView *view=nullptr;QTableWidget *table=nullptr;
            for(auto ancestor=button->parentWidget();ancestor&&ancestor!=dialog;ancestor=ancestor->parentWidget())if(auto candidate=qobject_cast<QAbstractItemView*>(ancestor)){if(!view)view=candidate;if(auto candidateTable=qobject_cast<QTableWidget*>(candidate))table=candidateTable;}
            if(view){
                // View chrome is not an application action. Table cell widgets are real actions;
                // prove that scrolling can reveal each entire button without invoking its RPC.
                if(!table)continue;QModelIndex cell;
                for(int row=0;row<table->rowCount()&&!cell.isValid();++row)for(int column=0;column<table->columnCount();++column)if(auto widget=table->cellWidget(row,column))if(widget==button||widget->isAncestorOf(button)){cell=table->model()->index(row,column);break;}
                if(!cell.isValid())continue;
                ++actions;++tableActions;const int horizontal=table->horizontalScrollBar()->value(),vertical=table->verticalScrollBar()->value();
                table->scrollTo(cell,QAbstractItemView::PositionAtCenter);
                const QRect viewportBounds(table->viewport()->mapTo(dialog,QPoint()),table->viewport()->size()),bounds(button->mapTo(table->viewport(),QPoint()),button->size());
                const bool reachable=dialog->rect().contains(viewportBounds)&&viewportBounds.top()>=44&&table->viewport()->rect().contains(bounds);
                actionsReachable&=reachable;actionBounds.append(QJsonObject{{"text",button->text()},{"scope","table cell after scrollTo"},{"row",cell.row()},{"column",cell.column()},{"enabled",button->isEnabled()},{"button_bounds",QJsonArray{bounds.x(),bounds.y(),bounds.width(),bounds.height()}},{"viewport_bounds",QJsonArray{viewportBounds.x(),viewportBounds.y(),viewportBounds.width(),viewportBounds.height()}},{"reachable",reachable}});
                table->horizontalScrollBar()->setValue(horizontal);table->verticalScrollBar()->setValue(vertical);continue;
            }
            ++actions;const QRect bounds(button->mapTo(dialog,QPoint()),button->size());const bool reachable=dialog->rect().contains(bounds)&&bounds.top()>=44;actionsReachable&=reachable;
            actionBounds.append(QJsonObject{{"text",button->text()},{"scope","dialog body"},{"button_bounds",QJsonArray{bounds.x(),bounds.y(),bounds.width(),bounds.height()}},{"reachable",reachable}});
        }
        layoutDiagnostics.append(QJsonObject{{"name",name+"_action_reachability"},{"body_action_count",actions},{"table_action_count",tableActions},{"body_actions_applicable",actions>0},{"title_close_bounds",QJsonArray{closeBounds.x(),closeBounds.y(),closeBounds.width(),closeBounds.height()}},{"actions",actionBounds}});
        if(!check(name+"_actions_reachable",actionsReachable))return false;
        if(capture){foreground(dialog);const auto image=captureWidget(dialog);if(!check(name+(desktopValidation?"_owned_desktop_capture":"_qt_raster_capture"),image.error.isEmpty()&&!image.image.isNull()))return false;image.image.save(QFileInfo(report).absolutePath()+"/"+name+".png");}
        return true;
    }
    bool clickStandard(QDialog *dialog,QDialogButtonBox::StandardButton button,const QString &name){
        auto box=dialog->findChild<QDialogButtonBox*>();auto control=box?box->button(button):nullptr;
        if(!check(name,control&&control->isVisibleTo(dialog)&&control->isEnabled()))return false;control->click();return true;
    }
    bool modalProbe(const QString &name,const QString &objectName,std::function<void(QDialog*)> interact,std::function<void()> invoke){
        auto observed=std::make_shared<bool>(false);auto timerContext=new QObject(this);
        QTimer::singleShot(250,timerContext,[this,name,objectName,interact,observed]{
            if(done)return;
            *observed=true;auto host=ownedDialog(objectName);
            QJsonArray candidates;for(auto widget:qApp->topLevelWidgets())if(auto dialog=qobject_cast<QDialog*>(widget)){QJsonArray owners;for(auto owner=static_cast<QObject*>(dialog);owner;owner=owner->parent())owners.append(QJsonObject{{"class",QString::fromLatin1(owner->metaObject()->className())},{"object_name",owner->objectName()}});candidates.append(QJsonObject{{"object_name",dialog->objectName()},{"visible",dialog->isVisible()},{"flags",double(quint64(dialog->windowFlags()))},{"owned",belongsToTestWindow(dialog)},{"parent_chain",owners}});}layoutDiagnostics.append(QJsonObject{{"name",name+"_host_lookup"},{"expected_object_name",objectName},{"host_found",host!=nullptr},{"candidates",candidates}});
            if(!dialogShell(host,name)){if(host)host->reject();else if(auto active=qobject_cast<QDialog*>(qApp->activeModalWidget()))if(belongsToTestWindow(active))active->reject();return;}
            interact(host);
        });
        invoke();
        timerContext->deleteLater();
        return !done&&check(name+"_exec_received_qt_interaction",*observed);
    }
    void recordFilePicker(const QString &name,const QString &phase,QFileDialog *picker,QDialog *host,const QString &target,bool invoked=false){
        auto field=picker->findChild<QLineEdit*>("fileNameEdit");QJsonArray selected;
        for(const auto &path:picker->selectedFiles())selected.append(QJsonObject{{"path",path},{"exists",QFileInfo::exists(path)},{"directory",QFileInfo(path).isDir()}});
        layoutDiagnostics.append(QJsonObject{{"name",name+"_picker_"+phase},{"selected_files",selected},{"filename_edit",field?field->text():QString()},{"target",target},{"target_exists",QFileInfo::exists(target)},{"directory",picker->directory().absolutePath()},{"file_mode",int(picker->fileMode())},{"accept_mode",int(picker->acceptMode())},{"picker_result",picker->result()},{"host_result",host->result()},{"host_visible",host->isVisible()},{"invoked",invoked}});
    }
    bool setPickerFilename(const QString &name,QFileDialog *picker,QDialog *host,const QString &text){
        auto field=picker->findChild<QLineEdit*>("fileNameEdit");
        if(!check(name+"_selection_filename_field",field!=nullptr)){host->reject();return false;}
        // QFileDialog::selectFile intentionally leaves focused, visible filename input unchanged.
        // Set the real text input, then use its real Open/Save button and Qt's validation.
        field->setText(text);return true;
    }
    void clickFilePickerAccept(const QString &name,QFileDialog *picker,QDialog *host,const QString &target){
        auto box=picker->findChild<QDialogButtonBox*>();auto button=box?box->button(picker->acceptMode()==QFileDialog::AcceptSave?QDialogButtonBox::Save:QDialogButtonBox::Open):nullptr;
        recordFilePicker(name,"before_accept",picker,host,target);
        if(!check(name+"_accept_button_enabled",button&&button->isVisibleTo(host)&&button->isEnabled())){host->reject();return;}
        QTimer::singleShot(2000,host,[this,name,picker,host,target]{if(done||!host->isVisible())return;recordFilePicker(name,"accept_still_visible",picker,host,target);finish(false,name+": Qt picker accept left host visible");host->reject();});
        const bool invoked=QMetaObject::invokeMethod(button,"click",Qt::DirectConnection);
        recordFilePicker(name,"after_accept",picker,host,target,invoked);
        if(!check(name+"_accept_button_invoked",invoked))host->reject();
    }
    void helperDialogs(int stage=0){
        if(done)return;const QString title=QString::fromUtf8("界面回归 · 隔离素材"),text=QString::fromUtf8("验证自定义标题、键盘、默认操作和取消返回值。");
        if(stage<4){
            QMessageBox::StandardButton result=QMessageBox::NoButton;const auto expected=stage==0?QMessageBox::Yes:QMessageBox::No;
            const QStringList names{"message_question_yes","message_question_no","message_question_close","message_question_default"};
            if(!modalProbe(names[stage],"glassMessageDialog",[this,stage](QDialog *dialog){
                if(stage==0||stage==1)clickStandard(dialog,stage==0?QDialogButtonBox::Yes:QDialogButtonBox::No,QString("message_explicit_button_%1").arg(stage));
                else if(stage==2){auto close=dialog->findChild<QToolButton*>("windowClose");if(check("message_title_close_available",close!=nullptr))close->click();}
                else {auto box=dialog->findChild<QDialogButtonBox*>();auto no=box?box->button(QDialogButtonBox::No):nullptr;if(!check("message_requested_default_button",no&&no->isDefault())){dialog->reject();return;}QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(dialog,&enter);}
            },[&]{result=GlassMessages::question(window,title,text,QMessageBox::Yes|QMessageBox::No,QMessageBox::No);}))return;
            if(!check(names[stage]+"_return_value",result==expected))return;
        }else if(stage==4){
            QMessageBox::StandardButton result=QMessageBox::NoButton;
            if(!modalProbe("message_question_escape","glassMessageDialog",[](QDialog *dialog){escapeDialog(dialog);},[&]{result=GlassMessages::question(window,title,text,QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel,QMessageBox::Yes);}))return;
            if(!check("message_escape_returns_cancel",result==QMessageBox::Cancel))return;
        }else if(stage>=5&&stage<=7){
            const QStringList names{"message_information","message_warning","message_about"};const auto name=names[stage-5];
            if(!modalProbe(name,"glassMessageDialog",[this,name](QDialog *dialog){clickStandard(dialog,QDialogButtonBox::Ok,name+"_acknowledgement_button");},[&]{if(stage==5)GlassMessages::information(window,title,text);else if(stage==6)GlassMessages::warning(window,title,text);else GlassMessages::about(window,title,text);}))return;
        }else if(stage>=8&&stage<=11){
            const bool combo=stage>=10,accept=stage%2==0;bool ok=true;QString result;
            const auto name=QString("input_%1_%2").arg(combo?"combo":"text",accept?"accept":"cancel");
            if(!modalProbe(name,"glassInputDialog",[this,combo,accept,name](QDialog *dialog){
                if(combo){auto field=dialog->findChild<QComboBox*>("glassInputField");if(!check(name+"_has_real_items",field&&field->count()==3)){dialog->reject();return;}field->setCurrentIndex(2);}
                else{auto field=dialog->findChild<QLineEdit*>("glassInputField");if(!check(name+"_has_real_field",field!=nullptr)){dialog->reject();return;}field->setText(QString::fromUtf8("回归输入"));}
                if(accept)clickStandard(dialog,QDialogButtonBox::Ok,name+"_confirm_available");else escapeDialog(dialog);
            },[&]{if(combo)result=GlassInputs::getItem(window,title,text,{"first","second","third"},1,false,&ok);else result=GlassInputs::getText(window,title,text,QLineEdit::Normal,"initial",&ok);}))return;
            if(!check(name+"_value_and_ok",ok==accept&&(accept?result==(combo?QString("third"):QString::fromUtf8("回归输入")):result.isEmpty())))return;
        }else if(stage>=12&&stage<=19){
            const int mode=(stage-12)/2;const bool accept=stage%2==0;const QStringList families{"open","save","multi","directory"};const auto name="file_"+families[mode]+(accept?"_accept":"_cancel");
            QString result;QStringList results;const auto target=fixtureDirectory+"/"+(mode==1?"ui-dialog-save-unused.png":"UI color.png");
            if(!modalProbe(name,"glassFileDialog",[this,mode,accept,target,name](QDialog *dialog){
                auto picker=dialog->findChild<QFileDialog*>("glassFilePicker");
                if(!check(name+"_uses_qt_widget_dialog",picker&&picker->testOption(QFileDialog::DontUseNativeDialog)&&!picker->isWindow())){dialog->reject();return;}
                if(!accept){escapeDialog(dialog);return;}
                if(mode==3)picker->setDirectory(fixtureDirectory);else if(!setPickerFilename(name,picker,dialog,mode==2?QString("\"UI color.png\" \"UI normal.png\""):target))return;
                if(!check(name+"_target_existence_matches_mode",mode==1?!QFileInfo::exists(target):QFileInfo::exists(mode==3?fixtureDirectory:target))){dialog->reject();return;}
                clickFilePickerAccept(name,picker,dialog,mode==3?fixtureDirectory:target);
            },[&]{if(mode==0)result=GlassFiles::getOpenFileName(window,title,fixtureDirectory,"PNG (*.png)");else if(mode==1)result=GlassFiles::getSaveFileName(window,title,fixtureDirectory+"/ui-dialog-save-unused.png","PNG (*.png)");else if(mode==2)results=GlassFiles::getOpenFileNames(window,title,fixtureDirectory,"PNG (*.png)");else result=GlassFiles::getExistingDirectory(window,title,fixtureDirectory);}))return;
            bool valid=false;
            if(mode==2){auto expected=QStringList{fixtureDirectory+"/UI color.png",fixtureDirectory+"/UI normal.png"};for(auto &path:results)path=QDir::fromNativeSeparators(path);results.sort();expected.sort();valid=accept?results==expected:results.isEmpty();}
            else valid=accept?QDir::cleanPath(result)==QDir::cleanPath(mode==3?fixtureDirectory:target):result.isEmpty();
            if(!check(name+"_selected_value",valid))return;
            if(!check(name+"_save_selection_does_not_create_file",!QFileInfo::exists(fixtureDirectory+"/ui-dialog-save-unused.png")))return;
        }else if(stage==20){
            QMessageBox::StandardButton result=QMessageBox::NoButton;
            if(!modalProbe("message_question_implicit_default","glassMessageDialog",[this](QDialog *dialog){auto box=dialog->findChild<QDialogButtonBox*>();auto yes=box?box->button(QDialogButtonBox::Yes):nullptr;if(!check("message_implicit_default_is_qt_yes",yes&&yes->isDefault())){dialog->reject();return;}QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(dialog,&enter);},[&]{result=GlassMessages::question(window,title,text,QMessageBox::Yes|QMessageBox::No);}))return;
            if(!check("message_implicit_default_return_value",result==QMessageBox::Yes))return;
        }else {fileValidationDialogs();return;}
        QTimer::singleShot(50,this,[this,stage]{helperDialogs(stage+1);});
    }
    QByteArray fixtureDigest(const QString &path){QFile source(path);return source.open(QIODevice::ReadOnly)?QCryptographicHash::hash(source.readAll(),QCryptographicHash::Sha256):QByteArray{};}
    void fileValidationDialogs(int stage=0){
        const auto source=fixtureDirectory+"/UI color.png";const auto before=fixtureDigest(source);auto observed=std::make_shared<bool>(false);QString result;
        const auto name=stage==0?QString("file_open_missing_path"):QString("file_save_existing_no_overwrite");
        if(!modalProbe(name,"glassFileDialog",[this,stage,source,observed,name](QDialog *host){
            auto picker=host->findChild<QFileDialog*>("glassFilePicker");if(!check(name+"_real_picker",picker!=nullptr)){host->reject();return;}
            const auto target=stage==0?fixtureDirectory+"/not-present-ui-fixture.png":source;if(!setPickerFilename(name,picker,host,target))return;
            if(!check(name+"_target_existence_matches_case",QFileInfo::exists(target)==(stage==1))){host->reject();return;}
            QPointer<QDialog> owner=host;
            QTimer::singleShot(250,this,[this,stage,owner,observed,name]{
                auto box=qobject_cast<QMessageBox*>(qApp->activeModalWidget());
                if(!check(stage==0?"file_missing_path_uses_real_qt_message":"file_overwrite_uses_real_qt_message",box&&box->objectName()=="glassMessageDialog"&&belongsToTestWindow(box))){if(box&&belongsToTestWindow(box))box->reject();if(owner)owner->reject();return;}
                *observed=true;
                if(!dialogShell(box,stage==0?"file_missing_path_message":"file_overwrite_confirmation")){box->reject();if(owner)owner->reject();return;}
                auto button=box->button(stage==0?QMessageBox::Ok:QMessageBox::No);
                if(!check(stage==0?"file_missing_path_can_acknowledge":"file_overwrite_no_available",button!=nullptr)){box->reject();if(owner)owner->reject();return;}
                button->click();
                QTimer::singleShot(0,this,[this,owner,name]{if(owner){if(!check(name+"_returns_to_picker_after_message",owner->isVisible()))return;escapeDialog(owner);}});
            });
            if(stage==0){
                auto box=picker->findChild<QDialogButtonBox*>();auto open=box?box->button(QDialogButtonBox::Open):nullptr;
                if(!check(name+"_open_disabled_for_missing_file",open&&open->isVisibleTo(host)&&!open->isEnabled())){host->reject();return;}
                auto field=picker->findChild<QLineEdit*>("fileNameEdit");field->setFocus(Qt::OtherFocusReason);
                recordFilePicker(name,"before_filename_return",picker,host,target);
                // Qt's real filename returnPressed connection invokes validation even while Open is disabled.
                // Deliver the user keyboard path, preserving the original QFileDialog warning and its buttons.
                QKeyEvent press(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier),release(QEvent::KeyRelease,Qt::Key_Return,Qt::NoModifier);
                const bool delivered=QApplication::sendEvent(field,&press);QApplication::sendEvent(field,&release);
                recordFilePicker(name,"after_filename_return",picker,host,target,delivered);
                if(!check(name+"_filename_return_delivered",delivered))host->reject();
            }else clickFilePickerAccept(name,picker,host,target);
        },[&]{if(stage==0)result=GlassFiles::getOpenFileName(window,QString::fromUtf8("不存在的隔离素材"),fixtureDirectory,"PNG (*.png)");else result=GlassFiles::getSaveFileName(window,QString::fromUtf8("现有隔离素材的覆盖确认"),source,"PNG (*.png)");}))return;
        if(!check(name+"_cancelled_without_source_write",*observed&&result.isEmpty()&&!before.isEmpty()&&fixtureDigest(source)==before))return;
        QTimer::singleShot(50,this,[this,stage]{if(stage==0)fileValidationDialogs(1);else applicationDialogs();});
    }
    void applicationDialogs(int stage=0){
        if(done)return;
        if(stage>=7){window->resize(initialGeometry.size());finish(true);return;}
        window->resize(960,640);auto prior=qApp->topLevelWidgets();
        const QStringList names{"jobs","issues","roles","ordered_relations","ordered_previews","asset_picker","import_confirmation"};
        if(stage==0)window->showJobs();
        else if(stage==1)window->showIssues();
        else if(stage==2){QAction *entry=nullptr;for(auto action:window->findChildren<QAction*>())if(action->text()==QString::fromUtf8("管理关联标签…"))entry=action;if(!check("roles_dialog_has_real_menu_entry",entry!=nullptr))return;entry->trigger();}
        else if(stage==3||stage==4){call("asset.get",{{"asset_id",modelId}},[this,stage](const QJsonObject &asset){auto dialog=new OrderedEditor(rpc,new ThumbnailStore(rpc,this),asset,stage==3?OrderedEditor::Relations:OrderedEditor::Previews,window);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();QTimer::singleShot(250,this,[this,dialog,stage]{verifyApplicationDialog(dialog,stage==3?"ordered_relations":"ordered_previews",stage);});});return;}
        else if(stage==5){auto dialog=new AssetPicker(rpc,new ThumbnailStore(rpc,this),QString::fromUtf8("隔离素材选择"),{},window);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();QTimer::singleShot(250,this,[this,dialog,stage]{verifyApplicationDialog(dialog,"asset_picker",stage);});return;}
        else window->currentTab()->importPaths({fixtureDirectory+"/UI color.png"});
        QTimer::singleShot(250,this,[this,stage,prior,names]{QDialog *dialog=nullptr;for(auto candidate:qApp->topLevelWidgets())if(!prior.contains(candidate)&&candidate->isVisible()&&belongsToTestWindow(candidate))if(auto created=qobject_cast<QDialog*>(candidate))dialog=created;verifyApplicationDialog(dialog,names[stage],stage);});
    }
    void verifyApplicationDialog(QDialog *dialog,const QString &family,int stage){
        const auto name="application_"+family+"_960";
        if(!check(name+"_opened_from_actual_entry_or_class",dialog!=nullptr))return;
        // Exercise the dialog under the same 960 x 640 logical work area as the small main-window regression.
        const auto available=dialog->screen()->availableGeometry();dialog->resize(dialog->size().boundedTo(QSize(960,640)).boundedTo(available.size()));
        QTimer::singleShot(100,this,[this,dialog,name,stage]{
            if(!check(name+"_bounded_size",dialog->width()<=960&&dialog->height()<=640))return;
            if(!dialogShell(dialog,name))return;
            bool noClipping=true;for(auto scroll:dialog->findChildren<QScrollArea*>())if(scroll->isVisibleTo(dialog))noClipping&=scroll->horizontalScrollBar()->maximum()==0;
            if(!check(name+"_content_has_no_horizontal_clipping",noClipping))return;
            const QPointer<QDialog> guard=dialog;escapeDialog(dialog);
            QTimer::singleShot(0,this,[this,guard,name,stage]{if(!check(name+"_escape_closes",!guard||!guard->isVisible()))return;applicationDialogs(stage+1);});
        });
    }
    void dialog(){
        foreground(window);auto deadline=std::make_shared<QElapsedTimer>();deadline->start();waitForDialogInvoker(deadline);
    }
    void waitForDialogInvoker(const std::shared_ptr<QElapsedTimer> &deadline){
        if(done)return;
        if(!window->isActiveWindow()&&deadline->elapsed()<1000){QTimer::singleShot(qBound(1,1000-int(deadline->elapsed()),50),this,[this,deadline]{waitForDialogInvoker(deadline);});return;}
        recordFocusDiagnostic("dialog_invoker_before_focus_search",window);
        if(!check("dialog_invoker_main_window_is_active",window->isActiveWindow()))return;
        auto search=window->currentTab()->findChild<QLineEdit*>("assetSearch");window->currentTab()->focusSearch();
        recordFocusDiagnostic("dialog_invoker_after_focus_search",search);
        if(!check("dialog_invoker_search_has_actual_focus",search&&search->isVisible()&&search->isEnabled()&&search->hasFocus()&&QApplication::focusWidget()==search))return;
        dialogOrigin=search;window->showSettings();
        QTimer::singleShot(400,this,[this]{
            if(done)return;GlassDialog *settings=nullptr;for(auto candidate:window->findChildren<GlassDialog*>())if(candidate->isVisible())settings=candidate;
            if(!check("glass_dialog_frameless",settings&&settings->windowFlags().testFlag(Qt::FramelessWindowHint)))return;
            if(!check("glass_dialog_content_below_titlebar",settings->layout()&&settings->layout()->contentsMargins().top()>=44))return;
            recordFocusDiagnostic("settings_dialog_before_content_focus_check",settings);
            if(!check("dialog_focus_starts_in_content",QApplication::focusWidget()&&settings->isAncestorOf(QApplication::focusWidget())&&QApplication::focusWidget()->objectName()!="windowClose"))return;
            const auto capture=captureWidget(settings);if(capture.error.isEmpty())capture.image.save(QFileInfo(report).absolutePath()+"/ui-settings-dialog.png");else captureWarnings.append(capture.error);
            escapeDialog(settings);if(!check("esc_closes_glass_dialog",!settings->isVisible()))return;
            recordFocusDiagnostic("dialog_focus_immediately_after_escape",dialogOrigin);auto deadline=std::make_shared<QElapsedTimer>();deadline->start();waitForDialogFocusReturn(deadline);
        });
    }
    void waitForDialogFocusReturn(const std::shared_ptr<QElapsedTimer> &deadline){
        if(done)return;const bool restored=dialogOrigin&&QApplication::focusWidget()==dialogOrigin;
        if(!restored&&deadline->elapsed()<1000){QTimer::singleShot(qBound(1,1000-int(deadline->elapsed()),50),this,[this,deadline]{waitForDialogFocusReturn(deadline);});return;}
        recordFocusDiagnostic(restored?"dialog_focus_return_observed":"dialog_focus_return_timeout",dialogOrigin);
        if(!check("dialog_returns_focus_to_invoker",restored))return;helperDialogs();
    }
    void navigation(){auto tab=window->currentTab();tab->openAsset(imageId);if(!check("association_history_push",tab->session()["history"].toArray()==QJsonArray{modelId,imageId}))return;tab->back();if(!check("association_back_returns_model",tab->session()["history"].toArray()==QJsonArray{modelId}))return;tab->home();if(!check("home_clears_history",tab->session()["history"].toArray().isEmpty()&&!tab->session()["detail"].toBool()))return;QJsonObject state{{"filter",QJsonObject{{"query","UI"},{"types",QJsonArray{"image","video"}},{"tags",QJsonArray{"ui-test","color"}},{"status","favorite"},{"sort","name"}}},{"history",QJsonArray{modelId,imageId}},{"detail",true},{"asset_id",imageId}};auto duplicate=window->addTab(state);if(!check("duplicate_tab_keeps_independent_filters",duplicate->session()["filter"]==state["filter"]&&tab->session()["history"].toArray().isEmpty()))return;second=new MainWindow(rpc,new ThumbnailStore(rpc,this));second->show();second->currentTab()->openAsset(imageId);call("asset.get",{{"asset_id",imageId}},[this](const QJsonObject &asset){call("asset.update",{{"asset_id",imageId},{"revision",asset["revision"]},{"values",QJsonObject{{"name","UI synchronized.png"}}}},[this](const QJsonObject &){rpc->refresh();QTimer::singleShot(2300,this,[this]{if(!window||!second){finish(false,"window closed during test");return;}if(!check("two_windows_share_asset_identity",window->currentTab()->currentAssetId()==imageId&&second->currentTab()->currentAssetId()==imageId))return;auto hasName=[](MainWindow *w){for(auto title:w->findChildren<QLabel*>("inspectorAssetTitle"))if(title->text()=="UI synchronized.png")return true;return false;};if(!check("cross_window_update_visible",hasName(window)&&hasName(second)))return;window->currentTab()->home();window->currentTab()->restoreSession({{"filter",QJsonObject{}},{"history",QJsonArray{}},{"detail",false}});attempts=0;waitForThumbnail();});});});}
};
}
void runUiSelfTest(MainWindow *window,RpcClient *rpc,const QString &reportPath,const QString &screenshotPath,bool desktopValidation){auto state=new State(window,rpc,reportPath,screenshotPath,desktopValidation);QTimer::singleShot(0,state,[state]{state->start();});QTimer::singleShot(90000,state,[state]{if(!state->done)state->finish(false,"self-test timeout");});}
