#include "MainWindow.h"
#include "UiSelfTest.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <memory>

int main(int argc,char **argv){
    QApplication app(argc,argv);QCoreApplication::setApplicationName("ImagiDock");QCoreApplication::setOrganizationName("ImagiDock");QCoreApplication::setApplicationVersion("0.1.0");app.setStyle("Fusion");MainWindow::applyTheme(false);
    QCommandLineParser parser;parser.setApplicationDescription(QString::fromUtf8("创想坞 · 本地素材资产管理器"));parser.addHelpOption();parser.addVersionOption();parser.addOption({"library","Library folder","path"});parser.addOption({"smoke-test","Capture the window and exit after initialization"});parser.addOption({"screenshot","Screenshot path, used with --smoke-test","path"});parser.addOption({"self-test","Run UI integration assertions in an explicitly selected empty test library"});parser.addOption({"test-report","JSON receipt path for --self-test","path"});parser.addOption({"asset","Open an asset ID after connecting","id"});parser.process(app);
    const QString library=parser.isSet("library")?QDir(parser.value("library")).absolutePath():QDir(QCoreApplication::applicationDirPath()+"/data/library").absolutePath();auto rpc=new RpcClient(library,&app);auto thumbs=new ThumbnailStore(rpc,&app);auto window=new MainWindow(rpc,thumbs);window->show();
    auto settingsApplied=std::make_shared<bool>(false);QObject::connect(rpc,&RpcClient::ready,window,[window,thumbs,settingsApplied,&parser](const QJsonObject &info){if(*settingsApplied)return;*settingsApplied=true;const auto settings=info["settings"].toObject();MainWindow::applyTheme(settings["theme"]=="light");window->currentTab()->setCardSize(settings["card_size"].toInt(176));thumbs->setMemoryBudget(settings["thumb_memory_mb"].toInt(256));if(parser.isSet("asset"))window->currentTab()->openAsset(parser.value("asset"));});
    if(parser.isSet("self-test")){if(!parser.isSet("library"))return 2;runUiSelfTest(window,rpc,parser.value("test-report"),parser.value("screenshot"));}
    if(parser.isSet("smoke-test")&&!parser.isSet("self-test")){QTimer::singleShot(6500,&app,[window,&app,&parser]{const QString path=parser.value("screenshot");if(!path.isEmpty()){QDir().mkpath(QFileInfo(path).absolutePath());window->grab().save(path);}app.quit();});}
    return app.exec();
}
