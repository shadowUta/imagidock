#include "MainWindow.h"
#include "ApplicationCapture.h"
#include "UiSelfTest.h"
#include "UiBenchmark.h"
#include "Theme.h"
#include "../render/ModelView.h"
#include <QJsonDocument>
#include <QSaveFile>
#include <QStackedWidget>
#include <QApplication>
#include <QTranslator>
#include <QLibraryInfo>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QScreen>
#include <memory>

int main(int argc,char **argv){
    QApplication app(argc,argv);QTranslator qtChinese;const QString translations=QCoreApplication::applicationDirPath()+"/translations";if(qtChinese.load("qtbase_zh_CN",translations)||qtChinese.load("qtbase_zh_CN",QLibraryInfo::path(QLibraryInfo::TranslationsPath)))app.installTranslator(&qtChinese);QCoreApplication::setApplicationName("ImagiDock");QCoreApplication::setOrganizationName("ImagiDock");QCoreApplication::setApplicationVersion("0.1.0-rc.1+linear.20261020.4");app.setStyle("Fusion");MainWindow::applyTheme("graphite");
    QCommandLineParser parser;parser.setApplicationDescription(QString::fromUtf8("创想坞 · 本地素材资产管理器"));parser.addHelpOption();parser.addVersionOption();parser.addOption({"library","Library folder","path"});parser.addOption({"smoke-test","Capture the window and exit after initialization"});parser.addOption({"smoke-functional","Capture only this window Qt raster content, without desktop presentation evidence"});parser.addOption({"smoke-dialog","Open settings, jobs or issues during a smoke capture","kind"});parser.addOption({"screenshot","Screenshot path, used with --smoke-test","path"});parser.addOption({"benchmark","Measure first screen and 600 scrolling ticks in an explicitly selected test library"});parser.addOption({"self-test","Run UI integration assertions in an explicitly selected empty test library"});parser.addOption({"self-test-functional","Run functional UI assertions without desktop presentation validation; Qt raster screenshots only"});parser.addOption({"test-report","JSON receipt path for --self-test","path"});parser.addOption({"asset","Open an asset ID after connecting","id"});parser.process(app);
    const QString library=parser.isSet("library")?QDir(parser.value("library")).absolutePath():QDir(QCoreApplication::applicationDirPath()+"/data/library").absolutePath();auto rpc=new RpcClient(library,&app);auto thumbs=new ThumbnailStore(rpc,&app);auto window=new MainWindow(rpc,thumbs);window->show();
    auto settingsApplied=std::make_shared<bool>(false);QObject::connect(rpc,&RpcClient::ready,window,[window,rpc,thumbs,settingsApplied,&parser](const QJsonObject &info){if(*settingsApplied)return;*settingsApplied=true;const auto settings=info["settings"].toObject();const QString savedTheme=settings["theme"].toString("graphite");MainWindow::applyTheme(savedTheme);if(savedTheme=="dark"||savedTheme=="light")rpc->call("library.settings",{{"values",QJsonObject{{"theme",normalizeTheme(savedTheme)}}}},window,[](const QJsonObject &,const QString &){});window->currentTab()->setCardSize(settings["card_size"].toInt(176));thumbs->setMemoryBudget(settings["thumb_memory_mb"].toInt(256));if(parser.isSet("asset"))window->currentTab()->openAsset(parser.value("asset"));if(parser.isSet("smoke-dialog")){const auto which=parser.value("smoke-dialog");if(which=="settings")window->showSettings();else if(which=="jobs")window->showJobs();else if(which=="issues")window->showIssues();}});
    if(parser.isSet("benchmark")){if(!parser.isSet("library")||!parser.isSet("test-report"))return 2;runUiBenchmark(window,rpc,parser.value("test-report"),parser.value("screenshot"));}
    if(parser.isSet("self-test")||parser.isSet("self-test-functional")){if(!parser.isSet("library"))return 2;runUiSelfTest(window,rpc,parser.value("test-report"),parser.value("screenshot"),!parser.isSet("self-test-functional"));}
    if((parser.isSet("smoke-test")||parser.isSet("smoke-functional"))&&!parser.isSet("self-test")&&!parser.isSet("self-test-functional")) {
        QTimer::singleShot(6000,window,[window]{foregroundApplicationTestWindow(window);});
        QTimer::singleShot(6500,&app,[window,&app,&parser]{
            const QString path=parser.value("screenshot");
            auto diagnostic=std::make_shared<QJsonObject>();
            if(!path.isEmpty()) {
                QDir().mkpath(QFileInfo(path).absolutePath());
                const bool functional=parser.isSet("smoke-functional");
                (*diagnostic)["capture_mode"]=functional?"Qt raster only; no desktop presentation evidence":"guarded GPU desktop";
                auto capture=functional?ApplicationCapture{window->grab(),{}}:captureApplicationWidget(window);
                if(capture.image.isNull()&&capture.error.isEmpty())capture.error="capture_failed: empty Qt raster";
                (*diagnostic)["capture_blocked"]=!capture.error.isEmpty();
                (*diagnostic)["capture_error"]=capture.error;
                if(capture.error.isEmpty()&&!capture.image.save(path)){(*diagnostic)["capture_blocked"]=true;(*diagnostic)["capture_error"]="capture_failed: cannot save image";}
                (*diagnostic)["asset_id"]=window->currentTab()->currentAssetId();
                QJsonArray stacks;
                for(auto stack:window->findChildren<QStackedWidget*>())stacks.append(QJsonObject{{"current",stack->currentIndex()},{"visible",stack->isVisible()},{"current_class",QString::fromLatin1(stack->currentWidget()->metaObject()->className())}});
                (*diagnostic)["stacks"]=stacks;
                if(auto model=window->currentTab()->findChild<ModelView*>()) {
                    (*diagnostic)["model_visible"]=model->isVisible();
                    (*diagnostic)["model_native_id"]=QString::number(model->winId());
                    (*diagnostic)["model_global"]=QJsonArray{model->mapToGlobal(QPoint(0,0)).x(),model->mapToGlobal(QPoint(0,0)).y(),model->width(),model->height()};
                    (*diagnostic)["model_error"]=model->lastError();
                    (*diagnostic)["render_before_snapshot"]=model->diagnostics();
                    if(model->isVisible())(*diagnostic)["model_snapshot"]=model->saveSnapshot(path+".model.png");
                    (*diagnostic)["render_after_snapshot"]=model->diagnostics();
                }
            }
            QTimer::singleShot(250,&app,[window,path,diagnostic,&app,&parser]{
                if(!path.isEmpty()) {
                    auto capture=parser.isSet("smoke-functional")?ApplicationCapture{window->grab(),{}}:captureApplicationWidget(window);
                    if(capture.image.isNull()&&capture.error.isEmpty())capture.error="capture_failed: empty Qt raster";
                    (*diagnostic)["after_capture_error"]=capture.error;
                    if(capture.error.isEmpty()&&!capture.image.save(path+".after.png"))(*diagnostic)["after_capture_error"]="capture_failed: cannot save image";
                    QSaveFile receipt(path+".json");
                    if(receipt.open(QIODevice::WriteOnly)){receipt.write(QJsonDocument(*diagnostic).toJson());receipt.commit();}
                }
                app.quit();
            });
        });
    }
    return app.exec();
}
