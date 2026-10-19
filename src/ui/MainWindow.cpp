#include "MainWindow.h"
#include "Theme.h"
#include "UiMetrics.h"
#include "UiIcons.h"
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QSignalBlocker>
#include <QScreen>
#include <QApplication>
#include <QAbstractButton>
#include <QResizeEvent>
#include <QWindow>
#include <QWindowStateChangeEvent>
#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include "GlassDialog.h"
#include <QDialogButtonBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QVBoxLayout>
#include <QUuid>
#include <memory>

namespace {QHash<QString,QPointer<LibraryTab>> draggedTabs;QSet<MainWindow*> windows;
QIcon appIcon(){QPixmap p(64,64);p.fill(Qt::transparent);QPainter paint(&p);paint.setRenderHint(QPainter::Antialiasing);paint.setPen(Qt::NoPen);paint.setBrush(QColor("#67c8b6"));paint.drawRoundedRect(3,3,58,58,15,15);paint.setPen(QPen(QColor("#132b2b"),4,Qt::SolidLine,Qt::RoundCap));paint.drawLine(18,45,18,20);paint.drawLine(30,45,30,20);paint.drawLine(18,20,43,20);paint.drawLine(30,45,46,32);paint.drawLine(46,32,34,24);return QIcon(p);}
QString stateText(const QString &s){static const QHash<QString,QString> names{{"queued",QString::fromUtf8("排队")},{"running",QString::fromUtf8("进行中")},{"completed",QString::fromUtf8("完成")},{"failed",QString::fromUtf8("失败")},{"cancelled",QString::fromUtf8("已取消")},{"interrupted",QString::fromUtf8("已中断")}};return names.value(s,s);}
}
DetachableTabBar::DetachableTabBar(QWidget *p):QTabBar(p){setAcceptDrops(true);setMovable(true);setElideMode(Qt::ElideRight);setDrawBase(false);setExpanding(false);setObjectName("workspaceTabBar");}
void DetachableTabBar::mousePressEvent(QMouseEvent *e){m_press=e->pos();m_index=tabAt(m_press);QTabBar::mousePressEvent(e);}
void DetachableTabBar::mouseMoveEvent(QMouseEvent *e){if(m_index>=0&&(e->buttons()&Qt::LeftButton)&&qAbs(e->pos().y()-m_press.y())>QApplication::startDragDistance()*2){const int index=m_index;m_index=-1;auto tabs=qobject_cast<QTabWidget*>(parentWidget());if(!tabs)return;auto tab=qobject_cast<LibraryTab*>(tabs->widget(index));if(!tab)return;const QString token=QUuid::createUuid().toString();draggedTabs[token]=tab;auto drag=new QDrag(this);auto mime=new QMimeData;mime->setData("application/x-imagidock-tab",token.toUtf8());drag->setMimeData(mime);drag->setPixmap(tab->grab().scaled(260,160,Qt::KeepAspectRatio,Qt::SmoothTransformation));const auto action=drag->exec(Qt::MoveAction);if(action==Qt::IgnoreAction&&draggedTabs.contains(token))emit detached(index,QCursor::pos());draggedTabs.remove(token);return;}QTabBar::mouseMoveEvent(e);}
void DetachableTabBar::dragEnterEvent(QDragEnterEvent *e){if(e->mimeData()->hasFormat("application/x-imagidock-tab"))e->acceptProposedAction();else QTabBar::dragEnterEvent(e);}
void DetachableTabBar::dragMoveEvent(QDragMoveEvent *e){if(e->mimeData()->hasFormat("application/x-imagidock-tab"))e->acceptProposedAction();else QTabBar::dragMoveEvent(e);}
void DetachableTabBar::dropEvent(QDropEvent *e){if(e->mimeData()->hasFormat("application/x-imagidock-tab")){emit tabReceived(QString::fromUtf8(e->mimeData()->data("application/x-imagidock-tab")));e->setDropAction(Qt::MoveAction);e->accept();}else QTabBar::dropEvent(e);}

AssetTabs::AssetTabs(QWidget *parent):QTabWidget(parent){bar=new DetachableTabBar;setTabBar(bar);setTabsClosable(false);setMovable(true);setDocumentMode(true);bar->setDrawBase(false);setObjectName("workspaceTabs");}
void AssetTabs::tabInserted(int index){QTabWidget::tabInserted(index);auto close=new QToolButton(bar);close->setObjectName("tabClose");close->setIcon(uiIcon("close"));close->setIconSize({12,12});close->setToolTip(tr("关闭页签"));close->setAccessibleName(tr("关闭页签"));close->setFixedSize(20,20);close->setFocusPolicy(Qt::StrongFocus);bar->setTabButton(index,QTabBar::RightSide,close);connect(close,&QToolButton::clicked,this,[this,close]{for(int i=0;i<count();++i)if(bar->tabButton(i,QTabBar::RightSide)==close){emit tabCloseRequested(i);break;}});}

MainWindow::MainWindow(RpcClient *rpc,ThumbnailStore *thumbs,QWidget *parent):QMainWindow(parent),m_rpc(rpc),m_thumbs(thumbs){
    windows.insert(this);setWindowFlags(Qt::Window|Qt::FramelessWindowHint|Qt::WindowSystemMenuHint|Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint);setAttribute(Qt::WA_DeleteOnClose);setWindowTitle(tr("创想坞 · ImagiDock"));setWindowIcon(appIcon());setMinimumSize(960,640);const auto initialRoom=screen()->availableGeometry().size()-QSize(32,32);resize(QSize(1440,940).boundedTo(initialRoom).expandedTo(minimumSize()));m_tabs=new AssetTabs;m_tabs->setContentsMargins(0,0,0,0);m_tabs->installEventFilter(this);m_tabs->bar->installEventFilter(this);auto workspace=new QWidget;auto workspaceLayout=new QVBoxLayout(workspace);workspaceLayout->setContentsMargins(UiMetrics::PanelInset,0,UiMetrics::PanelInset,0);workspaceLayout->setSpacing(0);workspaceLayout->addWidget(m_tabs);setCentralWidget(workspace);auto plus=new QToolButton;plus->setProperty("iconOnly",true);plus->setIcon(uiIcon("plus"));plus->setIconSize({14,14});plus->setToolTip(tr("新建页签 · Ctrl+T"));plus->setAccessibleName(tr("新建页签"));plus->setFixedSize(28,28);auto tabActions=new QWidget;auto tabActionsLayout=new QHBoxLayout(tabActions);tabActionsLayout->setContentsMargins(0,0,0,UiMetrics::TabBottomInset);tabActionsLayout->setSpacing(0);tabActionsLayout->addWidget(plus);m_tabs->setCornerWidget(tabActions,Qt::TopRightCorner);connect(plus,&QToolButton::clicked,this,[this]{addTab();});
    m_titleBar=new QWidget;m_titleBar->setObjectName("windowHeader");m_titleBar->setFixedHeight(UiMetrics::HeaderHeight);m_titleBar->installEventFilter(this);auto headerLayout=new QHBoxLayout(m_titleBar);headerLayout->setContentsMargins(16,6,12,6);headerLayout->setSpacing(UiMetrics::Gap);auto brandIcon=new QLabel;brandIcon->setPixmap(appIcon().pixmap(20,20));brandIcon->setAttribute(Qt::WA_TransparentForMouseEvents);headerLayout->addWidget(brandIcon);auto brand=new QLabel(tr("创想坞  ·  ImagiDock"));brand->setObjectName("windowBrand");brand->setAttribute(Qt::WA_TransparentForMouseEvents);headerLayout->addWidget(brand);auto appMenu=new QMenuBar;appMenu->setNativeMenuBar(false);headerLayout->addSpacing(8);headerLayout->addWidget(appMenu);headerLayout->addStretch(1);
    auto appearanceLabel=new QLabel(tr("主题"));appearanceLabel->setAttribute(Qt::WA_TransparentForMouseEvents);headerLayout->addWidget(appearanceLabel);
    m_themeSelector=new QComboBox;m_themeSelector->setObjectName("headerTheme");m_themeSelector->setAccessibleName(tr("切换主题"));m_themeSelector->setToolTip(tr("切换四种主题；拖动顶部空白区域移动窗口"));m_themeSelector->setFixedSize(112,UiMetrics::ControlHeight);
    for(const QString id:{QString("ivory"),QString("graphite"),QString("ocean"),QString("mint")})m_themeSelector->addItem(themeColors(id).name.section(" · ",0,0),id);
    m_themeSelector->setCurrentIndex(m_themeSelector->findData(themeColors().id));headerLayout->addWidget(m_themeSelector);headerLayout->addSpacing(8);
    connect(m_themeSelector,&QComboBox::activated,this,[this](int index){
        const QString id=m_themeSelector->itemData(index).toString();m_themeSelector->setEnabled(false);
        m_rpc->call("library.settings",{{"values",QJsonObject{{"theme",id}}}},this,[this,id](const QJsonObject &,const QString &error){
            m_themeSelector->setEnabled(true);
            if(error.isEmpty())applyTheme(id);
            else {QSignalBlocker blocker(m_themeSelector);m_themeSelector->setCurrentIndex(m_themeSelector->findData(themeColors().id));statusBar()->showMessage(tr("主题未保存：%1").arg(error),8000);}
        });
    });
    auto minimize=new QToolButton;minimize->setIcon(uiIcon("minimize"));minimize->setToolTip(tr("最小化"));minimize->setAccessibleName(tr("最小化窗口"));minimize->setObjectName("windowButton");m_maximize=new QToolButton;m_maximize->setIcon(uiIcon("maximize"));m_maximize->setToolTip(tr("最大化"));m_maximize->setAccessibleName(tr("最大化窗口"));m_maximize->setObjectName("windowButton");auto close=new QToolButton;close->setIcon(uiIcon("close"));close->setToolTip(tr("关闭窗口"));close->setAccessibleName(tr("关闭窗口"));close->setObjectName("windowClose");for(auto control:{minimize,m_maximize,close}){control->setFixedSize(32,28);control->setIconSize({14,14});control->setFocusPolicy(Qt::StrongFocus);headerLayout->addWidget(control);}connect(minimize,&QToolButton::clicked,this,&QWidget::showMinimized);connect(m_maximize,&QToolButton::clicked,this,[this]{if(isMaximized())showNormal();else showMaximized();});connect(close,&QToolButton::clicked,this,&QWidget::close);setMenuWidget(m_titleBar);
    auto file=appMenu->addMenu(tr("文件"));file->addAction(tr("添加素材文件…"),this,[this]{currentTab()->importFiles();},QKeySequence::Open);file->addAction(tr("添加素材文件夹…"),this,[this]{currentTab()->importFolder();});file->addSeparator();file->addAction(tr("打开 / 新建素材库…"),this,&MainWindow::openLibrary);file->addAction(tr("导入库包…"),this,&MainWindow::importLibrary);file->addAction(tr("导出当前素材库…"),this,&MainWindow::exportLibrary);file->addSeparator();file->addAction(tr("退出"),this,[this]{exitApplication();},QKeySequence::Quit);
    auto edit=appMenu->addMenu(tr("资产"));edit->addAction(tr("备份所选"),this,[this]{currentTab()->backupSelected();});edit->addAction(tr("取用所选…"),this,[this]{currentTab()->exportSelected();});edit->addAction(tr("移到回收站"),this,[this]{currentTab()->deleteSelected();},QKeySequence::Delete);edit->addSeparator();edit->addAction(tr("管理关联标签…"),this,&MainWindow::manageRoles);
    auto view=appMenu->addMenu(tr("窗口"));view->addAction(tr("新建页签"),this,[this]{addTab();},QKeySequence::AddTab);view->addAction(tr("复制当前页签"),this,[this]{addTab(currentTab()->session());},QKeySequence("Ctrl+Shift+T"));view->addAction(tr("分离当前页签"),this,[this]{detach(m_tabs->currentIndex(),pos()+QPoint(40,40));});view->addAction(tr("新建窗口"),this,[this]{auto w=new MainWindow(m_rpc,m_thumbs);w->show();},QKeySequence("Ctrl+N"));view->addAction(tr("关闭页签"),this,[this]{const int i=m_tabs->currentIndex();auto widget=m_tabs->widget(i);m_tabs->removeTab(i);widget->deleteLater();if(!m_tabs->count())addTab();},QKeySequence::Close);view->addSeparator();view->addAction(tr("刷新"),this,[this]{m_rpc->refresh();currentTab()->refresh();},QKeySequence::Refresh);view->addAction(tr("设置…"),this,&MainWindow::showSettings,QKeySequence("Ctrl+,"));
    auto tools=appMenu->addMenu(tr("工具"));tools->addAction(tr("后台任务"),this,&MainWindow::showJobs,QKeySequence("Ctrl+J"));tools->addAction(tr("错误中心"),this,&MainWindow::showIssues,QKeySequence("Ctrl+E"));auto help=appMenu->addMenu(tr("帮助"));help->addAction(tr("快捷操作"),this,[this]{QMessageBox::information(this,tr("快捷操作"),tr("单击：右侧信息\n双击：详情预览\nEsc / Alt+左：返回上一资产\nCtrl+T：新建页签\nCtrl+N：新建窗口\nCtrl+J：后台任务\nCtrl+E：错误中心\n拖动页签离开页签栏：分离窗口\n拖回另一窗口页签栏：合并\n图片：滚轮缩放，拖动平移\n多选筛选：同维度并集，跨维度交集"));});help->addAction(tr("关于创想坞"),this,[this]{QMessageBox::about(this,tr("创想坞 · ImagiDock"),tr("创想坞 0.1.0-rc.1+linear.20261020\n面向创作素材的本地桌面资产管理器\nC++20 · Qt 6 · Filament\n素材索引、预览、关联与压缩备份\n所有权与第三方许可详见发行包说明。"));});
    auto jobs=new QPushButton(tr("后台任务"));jobs->setObjectName("footerAction");jobs->setFlat(true);statusBar()->addPermanentWidget(jobs);connect(jobs,&QPushButton::clicked,this,&MainWindow::showJobs);auto issues=new QPushButton(tr("错误中心"));issues->setObjectName("footerAction");issues->setFlat(true);statusBar()->addPermanentWidget(issues);connect(issues,&QPushButton::clicked,this,&MainWindow::showIssues);statusBar()->showMessage(tr("正在连接本地素材库…"));connect(rpc,&RpcClient::connectionChanged,this,[this](bool,const QString &text){statusBar()->showMessage(text,6000);});
    connect(m_tabs,&QTabWidget::tabCloseRequested,this,[this](int index){auto tab=m_tabs->widget(index);m_tabs->removeTab(index);tab->deleteLater();if(!m_tabs->count())addTab();});connect(m_tabs->bar,&DetachableTabBar::detached,this,&MainWindow::detach);connect(m_tabs->bar,&DetachableTabBar::tabReceived,this,&MainWindow::receiveTab);
    auto searchShortcut=new QShortcut(QKeySequence::Find,this);connect(searchShortcut,&QShortcut::activated,this,[this]{currentTab()->focusSearch();});auto back=new QShortcut(QKeySequence(Qt::Key_Escape),this);connect(back,&QShortcut::activated,this,[this]{if(currentTab())currentTab()->back();});auto backAlt=new QShortcut(QKeySequence("Alt+Left"),this);connect(backAlt,&QShortcut::activated,this,[this]{if(currentTab())currentTab()->back();});
    m_tray=new QSystemTrayIcon(appIcon(),this);m_tray->setToolTip(tr("创想坞 · 后台任务"));auto trayMenu=new QMenu(this);trayMenu->addAction(tr("显示创想坞"),this,[this]{showNormal();raise();activateWindow();m_tray->hide();});trayMenu->addAction(tr("退出"),this,[this]{exitApplication();});m_tray->setContextMenu(trayMenu);connect(m_tray,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){if(reason==QSystemTrayIcon::DoubleClick){showNormal();raise();m_tray->hide();}});
    auto poll=new QTimer(this);poll->setInterval(2500);connect(poll,&QTimer::timeout,this,[this,jobs]{m_rpc->call("job.list",{},this,[this,jobs](const QJsonObject &r,const QString &err){if(!err.isEmpty())return;m_activeJobs=0;for(const auto &v:r["items"].toArray()){const auto state=v.toObject()["state"].toString();if(state=="queued"||state=="running")++m_activeJobs;}jobs->setText(m_activeJobs?tr("后台任务 · %1").arg(m_activeJobs):tr("后台任务"));if(m_activeJobs==0&&m_tray->isVisible())m_tray->setToolTip(tr("创想坞 · 任务已完成"));});});poll->start();connect(this,&QObject::destroyed,qApp,[this]{windows.remove(this);});addTab();
}
LibraryTab *MainWindow::currentTab()const{return qobject_cast<LibraryTab*>(m_tabs->currentWidget());}
void MainWindow::moveCurrentTabTo(MainWindow *target){if(!target||target==this||!currentTab())return;const auto token=QUuid::createUuid().toString();draggedTabs[token]=currentTab();target->receiveTab(token);draggedTabs.remove(token);}
void MainWindow::bindTab(LibraryTab *tab){disconnect(tab,nullptr,this,nullptr);connect(tab,&LibraryTab::titleChanged,this,[this,tab](const QString &title){const int i=m_tabs->indexOf(tab);if(i>=0){m_tabs->setTabText(i,title);m_tabs->setTabToolTip(i,title);}});connect(tab,&LibraryTab::message,this,[this](const QString &text){statusBar()->showMessage(text,9000);});connect(tab,&LibraryTab::jobStarted,this,[this](const QString &){statusBar()->showMessage(tr("任务已在后台运行，可继续浏览。"),8000);});connect(tab,&LibraryTab::openInTab,this,[this](const QString &id){addTab()->openAsset(id);});}
LibraryTab *MainWindow::addTab(const QJsonObject &state){auto tab=new LibraryTab(m_rpc,m_thumbs);m_tabs->addTab(tab,tr("素材库"));m_tabs->setCurrentWidget(tab);bindTab(tab);if(!state.isEmpty())tab->restoreSession(state);return tab;}
void MainWindow::detach(int index,const QPoint &position){auto tab=qobject_cast<LibraryTab*>(m_tabs->widget(index));if(!tab)return;const QString title=m_tabs->tabText(index);m_tabs->removeTab(index);disconnect(tab,nullptr,this,nullptr);auto w=new MainWindow(m_rpc,m_thumbs);auto initial=w->m_tabs->widget(0);w->m_tabs->removeTab(0);initial->deleteLater();tab->setParent(w->m_tabs);w->m_tabs->addTab(tab,title);w->m_tabs->setCurrentWidget(tab);w->bindTab(tab);w->move(position-QPoint(120,15));w->show();if(!m_tabs->count())addTab();}
void MainWindow::receiveTab(const QString &token){auto tab=draggedTabs.value(token);if(!tab)return;auto previous=qobject_cast<MainWindow*>(tab->window());if(previous==this)return;if(previous&&previous->m_rpc->libraryPath()!=m_rpc->libraryPath()){statusBar()->showMessage(tr("不同素材库的页签保留在各自窗口。"),6000);return;}const int oldIndex=previous?previous->m_tabs->indexOf(tab):-1;const QString title=oldIndex>=0?previous->m_tabs->tabText(oldIndex):tr("素材库");if(previous){previous->m_tabs->removeTab(oldIndex);disconnect(tab,nullptr,previous,nullptr);}tab->setParent(m_tabs);m_tabs->addTab(tab,title);m_tabs->setCurrentWidget(tab);bindTab(tab);draggedTabs.remove(token);if(previous&&!previous->m_tabs->count())previous->addTab();}
void MainWindow::closeEvent(QCloseEvent *e){if(!m_exit&&windows.size()==1&&m_activeJobs>0&&QSystemTrayIcon::isSystemTrayAvailable()){m_tray->show();hide();m_tray->showMessage(tr("创想坞继续在后台运行"),tr("%1 个任务正在运行。双击托盘图标可返回；托盘菜单可退出。").arg(m_activeJobs),QSystemTrayIcon::Information,4000);e->ignore();return;}QMainWindow::closeEvent(e);}

void MainWindow::showJobs(){if(m_jobs){m_jobs->show();m_jobs->raise();return;}auto dialog=new GlassDialog(this);m_jobs=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(tr("后台任务"));dialog->resize(860,460);auto layout=new QVBoxLayout(dialog);layout->addWidget(new QLabel(tr("导入、预览、备份和取用都在后台执行。取消保留已完成项目，失败与中断任务可重试。")));auto table=new QTableWidget(0,5);table->setHorizontalHeaderLabels({tr("任务"),tr("状态"),tr("进度"),tr("信息"),tr("操作")});table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);layout->addWidget(table,1);auto timer=new QTimer(dialog);timer->setInterval(1200);auto refreshing=std::make_shared<bool>(false);auto refresh=[this,dialog,table,refreshing]{if(*refreshing)return;*refreshing=true;m_rpc->call("job.list",{},dialog,[this,dialog,table,refreshing](const QJsonObject &r,const QString &error){*refreshing=false;if(!error.isEmpty())return;const auto jobs=r["items"].toArray();table->setRowCount(jobs.size());for(int row=0;row<jobs.size();++row){const auto job=jobs[row].toObject();const auto state=job["state"].toString();const QString message=job["error"].isString()?job["error"].toString():job["message"].toString();table->setItem(row,0,new QTableWidgetItem(job["kind"].toString()));table->setItem(row,1,new QTableWidgetItem(stateText(state)));table->setItem(row,2,new QTableWidgetItem(QString::number(qRound(job["progress"].toDouble()*100))+"%"));table->setItem(row,3,new QTableWidgetItem(message));auto b=new QPushButton(state=="running"||state=="queued"?tr("取消"):tr("重试"));b->setEnabled(state!="completed");table->setCellWidget(row,4,b);connect(b,&QPushButton::clicked,dialog,[this,dialog,job,state,b]{b->setEnabled(false);m_rpc->call(state=="running"||state=="queued"?"job.cancel":"job.retry",{{"job_id",job["id"]}},dialog,[dialog](const QJsonObject &,const QString &err){if(!err.isEmpty())QMessageBox::warning(dialog,tr("任务操作失败"),err);});});}});};connect(timer,&QTimer::timeout,dialog,refresh);timer->start();refresh();dialog->show();}

void MainWindow::showIssues(){if(m_issues){m_issues->show();m_issues->raise();return;}auto dialog=new GlassDialog(this);m_issues=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(tr("错误中心"));dialog->resize(940,530);auto layout=new QVBoxLayout(dialog);auto help=new QLabel(tr("来源丢失与预览失败分别列出。重新链接与恢复沿用原资产 ID；占位只恢复库内记录，不写入磁盘文件。"));help->setWordWrap(true);layout->addWidget(help);auto table=new QTableWidget(0,3);table->setHorizontalHeaderLabels({tr("问题"),tr("说明"),tr("路径")});table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);table->setSelectionMode(QAbstractItemView::SingleSelection);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setEditTriggers(QAbstractItemView::NoEditTriggers);layout->addWidget(table,1);auto rows=std::make_shared<QJsonArray>();auto refresh=std::make_shared<std::function<void()>>();*refresh=[this,dialog,table,rows]{m_rpc->call("issue.list",{},dialog,[table,rows](const QJsonObject &r,const QString &error){if(!error.isEmpty())return;*rows=r["items"].toArray();table->setRowCount(rows->size());for(int i=0;i<rows->size();++i){const auto issue=rows->at(i).toObject();table->setItem(i,0,new QTableWidgetItem(issue["kind"].toString()));table->setItem(i,1,new QTableWidgetItem(issue["message"].toString()));table->setItem(i,2,new QTableWidgetItem(issue["path"].toString()));}});};auto actions=new QHBoxLayout;auto scan=new QPushButton(tr("重新扫描")),relink=new QPushButton(tr("重新链接")),placeholder=new QPushButton(tr("恢复占位")),recover=new QPushButton(tr("从备份取用")),retry=new QPushButton(tr("重试预览")),prefix=new QPushButton(tr("批量替换路径"));for(auto b:{scan,relink,placeholder,recover,retry,prefix})actions->addWidget(b);layout->addLayout(actions);auto selected=[table,rows]{const int row=table->currentRow();return row>=0&&row<rows->size()?rows->at(row).toObject():QJsonObject();};auto perform=[this,dialog,refresh](const QString &method,const QJsonObject &params){m_rpc->call(method,params,dialog,[dialog,refresh](const QJsonObject &,const QString &err){if(!err.isEmpty())QMessageBox::warning(dialog,tr("修复未完成"),err);else (*refresh)();});};connect(scan,&QPushButton::clicked,dialog,[perform]{perform("issue.scan",{});});connect(relink,&QPushButton::clicked,dialog,[dialog,selected,perform]{const auto issue=selected();if(issue.isEmpty())return;const auto path=QFileDialog::getOpenFileName(dialog,QString::fromUtf8("选择有效源文件"));if(!path.isEmpty())perform("asset.relink",{{"asset_id",issue["asset_id"]},{"path",path}});});connect(placeholder,&QPushButton::clicked,dialog,[selected,perform]{const auto issue=selected();if(!issue.isEmpty())perform("asset.placeholder",{{"asset_id",issue["asset_id"]}});});connect(recover,&QPushButton::clicked,dialog,[dialog,selected,perform]{const auto issue=selected();if(issue.isEmpty())return;const auto path=QFileDialog::getExistingDirectory(dialog,QString::fromUtf8("选择取用目录"));if(!path.isEmpty())perform("export.assets",{{"asset_ids",QJsonArray{issue["asset_id"]}},{"destination",path}});});connect(retry,&QPushButton::clicked,dialog,[selected,perform]{const auto issue=selected();if(!issue.isEmpty())perform("preview.resolve",{{"asset_id",issue["asset_id"]},{"size",2048}});});connect(prefix,&QPushButton::clicked,dialog,[dialog,perform]{bool ok=false;const auto old=QInputDialog::getText(dialog,QString::fromUtf8("批量重新链接"),QString::fromUtf8("旧目录前缀"),QLineEdit::Normal,{},&ok);if(!ok||old.isEmpty())return;const auto next=QFileDialog::getExistingDirectory(dialog,QString::fromUtf8("选择替换后的目录"));if(!next.isEmpty())perform("issue.relink_prefix",{{"old_prefix",old},{"new_prefix",next}});});auto timer=new QTimer(dialog);timer->setInterval(3000);connect(timer,&QTimer::timeout,dialog,[refresh]{(*refresh)();});timer->start();(*refresh)();dialog->show();}

void MainWindow::showSettings(){auto dialog=new GlassDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(tr("设置"));dialog->resize(600,430);auto layout=new QFormLayout(dialog);auto theme=new QComboBox;for(const QString id:{QString("ivory"),QString("graphite"),QString("ocean"),QString("mint")})theme->addItem(themeColors(id).name,id);layout->addRow(tr("主题"),theme);auto card=new QSpinBox;card->setRange(128,320);card->setSingleStep(16);card->setValue(176);layout->addRow(tr("卡片尺寸"),card);auto cache=new QSpinBox;cache->setRange(64,2048);cache->setSuffix(" MB");cache->setValue(256);layout->addRow(tr("缩略图内存"),cache);auto concurrency=new QSpinBox;concurrency->setRange(1,8);concurrency->setValue(2);layout->addRow(tr("后台任务并发"),concurrency);auto defaultBackup=new QCheckBox(tr("导入默认勾选备份"));layout->addRow(defaultBackup);auto blender=new QLineEdit,aseprite=new QLineEdit;layout->addRow(tr("Blender 程序路径"),blender);layout->addRow(tr("Aseprite 程序路径"),aseprite);auto location=new QLabel(m_rpc->libraryPath());location->setWordWrap(true);location->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addRow(tr("当前库位置"),location);auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);layout->addRow(buttons);m_rpc->call("library.info",{},dialog,[theme,card,cache,concurrency,defaultBackup,blender,aseprite](const QJsonObject &r,const QString &){const auto s=r["settings"].toObject();theme->setCurrentIndex(qMax(0,theme->findData(normalizeTheme(s["theme"].toString("graphite")))));card->setValue(s["card_size"].toInt(176));cache->setValue(s["thumb_memory_mb"].toInt(256));concurrency->setValue(s["job_concurrency"].toInt(2));defaultBackup->setChecked(s["default_backup"].toBool());blender->setText(s["blender_path"].toString());aseprite->setText(s["aseprite_path"].toString());});connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,theme,card,cache,concurrency,defaultBackup,blender,aseprite]{QJsonObject values{{"theme",theme->currentData().toString()},{"card_size",card->value()},{"thumb_memory_mb",cache->value()},{"job_concurrency",concurrency->value()},{"default_backup",defaultBackup->isChecked()},{"blender_path",blender->text()},{"aseprite_path",aseprite->text()}};m_rpc->call("library.settings",{{"values",values}},dialog,[this,dialog,values](const QJsonObject &,const QString &err){if(!err.isEmpty()){QMessageBox::warning(dialog,tr("设置未保存"),err);return;}applyTheme(values["theme"].toString());m_thumbs->setMemoryBudget(values["thumb_memory_mb"].toInt(256));for(auto w:windows)for(int i=0;i<w->m_tabs->count();++i)qobject_cast<LibraryTab*>(w->m_tabs->widget(i))->setCardSize(values["card_size"].toInt());dialog->accept();});});dialog->open();}
void MainWindow::manageRoles(){auto dialog=new GlassDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(tr("关联标签"));dialog->resize(430,440);auto layout=new QVBoxLayout(dialog);layout->addWidget(new QLabel(tr("固有标签不可增删；自定义标签可映射到渲染角色。")));auto list=new QListWidget;layout->addWidget(list,1);auto buttons=new QHBoxLayout;auto add=new QPushButton(tr("新增自定义标签")),remove=new QPushButton(tr("删除所选"));buttons->addWidget(add);buttons->addWidget(remove);auto up=new QPushButton(tr("上移")),down=new QPushButton(tr("下移"));buttons->addWidget(up);buttons->addWidget(down);layout->addLayout(buttons);auto refresh=std::make_shared<std::function<void()>>();*refresh=[this,dialog,list]{m_rpc->call("role.list",{},dialog,[list](const QJsonObject &r,const QString &err){if(!err.isEmpty())return;list->clear();for(const auto &v:r["items"].toArray()){auto role=v.toObject();auto item=new QListWidgetItem(role["name"].toString()+(role["builtin"].toBool()?QString::fromUtf8(" · 固有"):QString()),list);item->setData(Qt::UserRole,role);}});};connect(add,&QPushButton::clicked,dialog,[this,dialog,refresh]{bool ok=false;const auto name=QInputDialog::getText(dialog,tr("新增标签"),tr("标签名称"),QLineEdit::Normal,{},&ok);if(!ok||name.trimmed().isEmpty())return;const QStringList labels{tr("普通参考"),tr("固有色"),tr("法线"),tr("粗糙度"),tr("金属度"),tr("AO"),tr("自发光"),tr("透明度"),tr("ORM")};const QStringList ids{"","base_color","normal","roughness","metallic","ao","emissive","opacity","orm"};const auto choice=QInputDialog::getItem(dialog,tr("渲染角色"),tr("需要驱动材质时选择对应角色"),labels,0,false,&ok);if(!ok)return;m_rpc->call("role.create",{{"name",name.trimmed()},{"render_role",ids.value(labels.indexOf(choice))}},dialog,[dialog,refresh](const QJsonObject &,const QString &err){if(!err.isEmpty())QMessageBox::warning(dialog,QString::fromUtf8("无法新增"),err);else (*refresh)();});});connect(remove,&QPushButton::clicked,dialog,[this,dialog,list,refresh]{auto item=list->currentItem();if(!item)return;const auto role=item->data(Qt::UserRole).toJsonObject();if(role["builtin"].toBool()){QMessageBox::information(dialog,tr("固有标签"),tr("此标签为固定渲染角色，不允许删除。"));return;}m_rpc->call("role.delete",{{"role_id",role["id"]}},dialog,[dialog,refresh](const QJsonObject &,const QString &err){if(!err.isEmpty())QMessageBox::warning(dialog,QString::fromUtf8("无法删除"),err);else (*refresh)();});});auto moveRole=[this,dialog,list,refresh](int direction){const int row=list->currentRow(),to=row+direction;if(row<0||to<0||to>=list->count())return;if(list->item(row)->data(Qt::UserRole).toJsonObject()["builtin"].toBool()||list->item(to)->data(Qt::UserRole).toJsonObject()["builtin"].toBool())return;auto item=list->takeItem(row);list->insertItem(to,item);list->setCurrentItem(item);QJsonArray ids;for(int i=0;i<list->count();++i){const auto role=list->item(i)->data(Qt::UserRole).toJsonObject();if(!role["builtin"].toBool())ids.append(role["id"]);}m_rpc->call("role.reorder",{{"role_ids",ids}},dialog,[dialog,refresh](const QJsonObject &,const QString &err){if(!err.isEmpty()){QMessageBox::warning(dialog,QString::fromUtf8("排序未保存"),err);(*refresh)();}});};connect(up,&QPushButton::clicked,dialog,[moveRole]{moveRole(-1);});connect(down,&QPushButton::clicked,dialog,[moveRole]{moveRole(1);});(*refresh)();dialog->show();}
void MainWindow::exportLibrary(){const auto path=QFileDialog::getSaveFileName(this,tr("导出素材库"),"library.imagidock",tr("创想坞库包 (*.imagidock)"));if(path.isEmpty())return;const auto answer=QMessageBox::question(this,tr("导出范围"),tr("导出前是否补齐所有资产的备份？\n选择“否”只打包已有备份、索引与用户预览图。"),QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);if(answer==QMessageBox::Cancel)return;m_rpc->call("library.export",{{"destination",path},{"complete_backup",answer==QMessageBox::Yes}},this,[this](const QJsonObject &,const QString &err){if(!err.isEmpty())QMessageBox::warning(this,tr("导出失败"),err);else showJobs();});}
void MainWindow::importLibrary(){const auto path=QFileDialog::getOpenFileName(this,tr("导入素材库包"),{},tr("创想坞库包 (*.imagidock);;所有文件 (*)"));if(path.isEmpty())return;const auto destination=QFileDialog::getExistingDirectory(this,tr("选择新的独立库目录"));if(destination.isEmpty())return;m_rpc->call("library.import",{{"path",path},{"destination",destination}},this,[this](const QJsonObject &,const QString &err){if(!err.isEmpty())QMessageBox::warning(this,tr("导入失败"),err);else showJobs();});}
void MainWindow::openLibrary(){const auto path=QFileDialog::getExistingDirectory(this,tr("打开或创建独立素材库"));if(path.isEmpty())return;auto rpc=new RpcClient(path,qApp);auto thumbs=new ThumbnailStore(rpc,qApp);auto window=new MainWindow(rpc,thumbs);window->show();}
void MainWindow::applyTheme(const QString &requested){const auto t=themeColors(requested);qApp->setProperty("imagidockTheme",t.id);QPalette palette;palette.setColor(QPalette::Window,t.top);palette.setColor(QPalette::WindowText,t.text);palette.setColor(QPalette::Base,t.top);palette.setColor(QPalette::AlternateBase,t.card);palette.setColor(QPalette::Text,t.text);palette.setColor(QPalette::Button,t.panel);palette.setColor(QPalette::ButtonText,t.text);palette.setColor(QPalette::Highlight,t.accent);palette.setColor(QPalette::HighlightedText,t.light?QColor("#ffffff"):t.bottom);palette.setColor(QPalette::PlaceholderText,t.muted);palette.setColor(QPalette::ToolTipBase,t.top);palette.setColor(QPalette::ToolTipText,t.text);palette.setColor(QPalette::Disabled,QPalette::Text,t.muted);palette.setColor(QPalette::Disabled,QPalette::ButtonText,t.muted);qApp->setPalette(palette);
    auto rgba=[](QColor c){return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());};
    QColor hover=t.card;hover.setAlpha(t.light?190:35);QColor selected=t.accent;selected.setAlpha(t.light?38:38);
    QColor input=t.light?QColor(255,255,255,65):QColor(235,243,255,7);
    QColor hairline=t.edge;hairline.setAlpha(t.light?110:24);
    QString qss=QString(R"(
QWidget { font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif; font-size: 13px; color: %1; }
QMainWindow { background: transparent; }
QDialog { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 %2,stop:1 %3); }
QMenuBar { background: transparent; padding: 0; border: 0; }
QLabel#windowBrand { font-size: 13px; font-weight: 600; padding: 0 4px; }
QWidget#windowHeader { background: transparent; }
QToolButton#windowButton, QToolButton#windowClose { border: 1px solid transparent; border-radius: 6px; background: transparent; padding: 0; }
QToolButton#windowButton:hover { background: %6; } QToolButton#windowClose:hover { background: rgba(198,82,92,205); }
QMenuBar::item { padding: 4px 8px; border-radius: 6px; } QMenuBar::item:selected { background: %4; }
QMenu { background: %2; border: 1px solid %15; border-radius: 8px; padding: 6px; } QMenu::item { padding: 6px 20px; border-radius: 6px; } QMenu::item:selected { background: %4; }
QToolBar { background: transparent; border: 0; padding: 0; spacing: 8px; }
QWidget#locationBar { padding: 0; border-bottom: 1px solid %15; }
QWidget#libraryTools { padding: 0; border-bottom: 1px solid %15; }
QToolBar::separator { width: 1px; background: %15; margin: 5px 3px; }
QToolButton, QPushButton { border: 1px solid %15; border-radius: %13px; min-height: 18px; padding: 4px 8px; background: transparent; color: %1; }
QToolButton:hover, QPushButton:hover { background: %7; border-color: %5; }
QToolButton:pressed, QPushButton:pressed, QPushButton:checked { background: %4; border-color: %15; }
QPushButton[quiet="true"], QToolButton[quiet="true"], QToolBar QToolButton { border-color: transparent; }
QToolButton[iconOnly="true"], QPushButton[iconOnly="true"] { padding: 0; min-width: 26px; min-height: 26px; }
QPushButton[smallIcon="true"] { min-width: 22px; min-height: 22px; }
QPushButton#primaryAction { background: %4; border: 1px solid %15; font-weight: 600; }
QPushButton:disabled, QToolButton:disabled { color: %9; background: transparent; border-color: transparent; }
QPushButton#danger { color: %10; }
QLineEdit, QComboBox, QSpinBox { background: %11; border: 1px solid %15; border-radius: %13px; min-height: 18px; padding: 4px 8px; color: %1; }
QLineEdit:hover, QComboBox:hover, QSpinBox:hover { border-color: %5; }
QComboBox { min-width: 82px; padding-right: 20px; }
QComboBox#headerTheme { min-width: 0; min-height: 16px; padding: 4px 8px; } QComboBox::drop-down { border: 0; width: 18px; }
QComboBox QAbstractItemView { background: %2; border: 1px solid %15; selection-background-color: %4; padding: 4px; }
QListView, QTreeView, QTableView { border: 1px solid transparent; outline: 0; background: transparent; }
QGraphicsView, QScrollArea { border: 0; background: transparent; }
QPushButton:focus, QToolButton:focus, QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QListView:focus, QTreeView:focus, QTableView:focus { border: 1px solid %8; }
QListView#assetGrid:focus { border-color: transparent; }
QTabBar::tab:focus { border: 1px solid %8; }
QSlider::handle:horizontal:focus { border: 1px solid %8; }
QCheckBox { border: 1px solid transparent; border-radius: 6px; padding: 2px; } QCheckBox:focus { border-color: %8; }
QLabel#pageTitle:focus { border: 1px solid %8; border-radius: 6px; }
QListWidget::item, QTreeWidget::item { padding: 4px 6px; margin: 0; border-radius: %13px; }
QListWidget::item:selected, QTreeWidget::item:selected { background: %4; color: %1; }
QListWidget::item:hover, QTreeWidget::item:hover { background: %6; }
QListWidget#typeNavigation::item { padding: 0 6px; }
QTabWidget::pane { border: 0; background: transparent; } QTabBar { background: transparent; }
QTabBar::tab { color: %9; background: transparent; height: 26px; min-height: 26px; max-height: 26px; padding: 0 12px; margin: 0 4px 4px 0; min-width: 88px; max-width: 220px; border: 1px solid transparent; border-radius: %13px; }
QToolButton#tabClose { border: 1px solid transparent; border-radius: 4px; background: transparent; padding: 0; } QToolButton#tabClose:hover { background: %4; }
QTabBar::tab:selected { color: %1; background: %6; border-color: %15; }
QLabel#pageTitle { font-size: 18px; font-weight: 600; padding: 0; border: 1px solid transparent; }
QWidget#browseHost QLabel#pageTitle { font-size: 13px; }
QLabel#workspaceTitle { font-size: 14px; font-weight: 600; padding: 2px 6px 6px 6px; }
QLabel#locationTitle, QLabel#inspectorTitle { font-size: 13px; font-weight: 600; border: 0; padding: 0; }
QLabel#sectionTitle, QLabel#propertyLabel { font-size: 11px; font-weight: 500; color: %9; padding: 0; }
QLabel#muted { color: %9; font-size: 12px; } QLabel#emptyState { color: %9; padding: 36px 16px; font-size: 13px; }
QScrollArea#navigationScroll, QStackedWidget#assetPages { background: transparent; border: 0; }
QWidget#contentPanel, QScrollArea#inspectorScroll { background: %12; border: 1px solid %15; border-radius: %14px; }
QWidget#sidebar, QWidget#inspector { background: transparent; border: 0; }
QPushButton[navAction="true"] { text-align: left; padding: 5px 8px; border: 1px solid transparent; background: transparent; }
QPushButton[navAction="true"]:hover { background: %6; }
QPushButton[navAction="true"]:checked { background: %4; font-weight: 600; }
QWidget#inspector QPushButton { text-align: left; }
QWidget#inspector QPushButton:checked { background: %4; }
QListWidget#previewStrip, QListWidget#relationStrip { font-size: 11px; }
QListWidget#previewStrip::item, QListWidget#relationStrip::item { padding: 4px; border: 1px solid transparent; }
QSplitter::handle { background: transparent; width: 8px; }
QScrollBar:vertical { width: 8px; background: transparent; margin: 4px 0; } QScrollBar::handle:vertical { background: %5; border-radius: 4px; min-height: 36px; }
QScrollBar:horizontal { height: 8px; background: transparent; } QScrollBar::handle:horizontal { background: %5; border-radius: 4px; min-width: 36px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical, QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
QStatusBar { background: transparent; border: 0; color: %9; padding: 2px 16px 4px 16px; font-size: 11px; }
QPushButton#footerAction { border-color: transparent; font-size: 11px; padding: 2px 8px; min-height: 16px; }
QHeaderView::section { background: %6; border: 0; border-bottom: 1px solid %15; padding: 8px; color: %9; }
QToolTip { background: %2; color: %1; border: 1px solid %15; padding: 6px; }
)" ).arg(t.text.name(),t.top.name(),t.bottom.name(),rgba(selected),rgba(t.edge),rgba(t.card),rgba(hover),t.accent.name(),t.muted.name(),t.light?"#9f6868":"#cb999f",rgba(input),rgba(t.panel)).arg(UiMetrics::ControlRadius).arg(UiMetrics::PanelRadius).arg(rgba(hairline));
    qApp->setStyleSheet(qss);for(auto w:windows){if(w->m_themeSelector){QSignalBlocker blocker(w->m_themeSelector);w->m_themeSelector->setCurrentIndex(w->m_themeSelector->findData(t.id));}w->m_glassBackground=QPixmap();w->update();}}
void MainWindow::paintEvent(QPaintEvent *event){QMainWindow::paintEvent(event);const auto t=themeColors();const auto ratio=devicePixelRatioF();if(m_glassBackground.isNull()||m_glassBackground.deviceIndependentSize().toSize()!=size()||m_backgroundTheme!=t.id){m_backgroundTheme=t.id;m_glassBackground=QPixmap(size()*ratio);m_glassBackground.setDevicePixelRatio(ratio);m_glassBackground.fill(t.bottom);QPainter painter(&m_glassBackground);painter.setRenderHint(QPainter::Antialiasing);QLinearGradient base(0,0,width()*.8,height());base.setColorAt(0,t.top);base.setColorAt(1,t.bottom);painter.fillRect(rect(),base);auto glow=[&](QPointF center,qreal radius,QColor color,int alpha){QRadialGradient gradient(center,radius);color.setAlpha(alpha);gradient.setColorAt(0,color);color.setAlpha(alpha/3);gradient.setColorAt(.45,color);color.setAlpha(0);gradient.setColorAt(1,color);painter.fillRect(rect(),gradient);};glow(QPointF(width()*.13,height()*.08),width()*.58,t.glow,t.light?155:44);glow(QPointF(width()*.75,height()*.84),width()*.58,t.glow,t.light?110:49);glow(QPointF(width()*.92,height()*.18),width()*.33,t.accent,t.light?35:15);QColor wash=Qt::white;wash.setAlpha(t.light?19:3);painter.setBrush(wash);QColor edge=t.edge;edge.setAlpha(t.light?150:31);painter.setPen(QPen(edge,1));if(!isMaximized())painter.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),UiMetrics::WindowRadius,UiMetrics::WindowRadius);}QPainter painter(this);painter.drawPixmap(0,0,m_glassBackground);}

void MainWindow::exitApplication(){for(auto w:windows)w->m_exit=true;QSet<RpcClient*> clients;for(auto w:windows)clients.insert(w->m_rpc);auto remaining=std::make_shared<int>(clients.size());if(clients.isEmpty()){qApp->quit();return;}for(auto rpc:clients)rpc->call("service.shutdown",{{"cancel_jobs",true}},qApp,[remaining](const QJsonObject &,const QString &){if(--*remaining==0)qApp->quit();});QTimer::singleShot(4000,qApp,&QApplication::quit);}

void MainWindow::updateWindowShape(){
    m_glassBackground=QPixmap();
    if(m_maximize){m_maximize->setIcon(uiIcon(isMaximized()?"restore":"maximize"));m_maximize->setToolTip(isMaximized()?tr("还原"):tr("最大化"));m_maximize->setAccessibleName(isMaximized()?tr("还原窗口"):tr("最大化窗口"));}
#ifdef Q_OS_WIN
    if(!testAttribute(Qt::WA_WState_Created))return;
    const auto handle=reinterpret_cast<HWND>(winId());
    if(isMaximized())SetWindowRgn(handle,nullptr,TRUE);
    else {const auto scale=devicePixelRatioF();auto region=CreateRoundRectRgn(0,0,qRound(width()*scale)+1,qRound(height()*scale)+1,qRound(2*UiMetrics::WindowRadius*scale),qRound(2*UiMetrics::WindowRadius*scale));if(!SetWindowRgn(handle,region,TRUE))DeleteObject(region);}
#endif
    update();
}
void MainWindow::resizeEvent(QResizeEvent *event){QMainWindow::resizeEvent(event);updateWindowShape();}
void MainWindow::changeEvent(QEvent *event){
    QMainWindow::changeEvent(event);
#ifdef Q_OS_WIN
    if(event->type()==QEvent::WindowStateChange){
        const auto oldState=static_cast<QWindowStateChangeEvent*>(event)->oldState();
        if(!(oldState&Qt::WindowMaximized)&&isMaximized()&&normalGeometry().isValid())m_normalBeforeMaximize=normalGeometry();
        // Qt's frameless min|max -> normal path can restore the Win32 maximized placement
        // after moving to its saved normal bounds. Repair only this transition, after it completes.
        if((oldState&Qt::WindowMinimized)&&(oldState&Qt::WindowMaximized)&&!isMinimized()&&!isMaximized()&&m_normalBeforeMaximize.isValid()){
            const auto normal=m_normalBeforeMaximize;
            QTimer::singleShot(0,this,[this,normal]{if(!isMinimized()&&!isMaximized()&&!isFullScreen()&&geometry()!=normal)setGeometry(normal);});
        }
    }
#endif
    if(event->type()==QEvent::WindowStateChange||event->type()==QEvent::ScreenChangeInternal)updateWindowShape();
}
bool MainWindow::isCaptionPoint(const QPoint &position) const {
    if(!rect().contains(position))return false;
    // Interactive controls always keep their normal client input. In particular,
    // dragging a real tab must continue to reorder/detach it, not move the window.
    for(auto child=childAt(position);child&&child!=this;child=child->parentWidget()) {
        if(qobject_cast<QAbstractButton*>(child)||qobject_cast<QMenuBar*>(child)||
           qobject_cast<QComboBox*>(child)||qobject_cast<QLineEdit*>(child)||
           qobject_cast<QAbstractSpinBox*>(child)||qobject_cast<QAbstractSlider*>(child))return false;
        if(auto bar=qobject_cast<QTabBar*>(child))
            if(bar->tabAt(bar->mapFrom(this,position))>=0)return false;
    }
    if(m_titleBar&&m_titleBar->rect().contains(m_titleBar->mapFrom(this,position)))return true;
    if(m_tabs&&m_tabs->rect().contains(m_tabs->mapFrom(this,position))) {
        const auto bar=m_tabs->bar;
        const int y=bar->mapFrom(this,position).y();
        return bar->isVisible()&&y>=0&&y<bar->height();
    }
    return false;
}

bool MainWindow::eventFilter(QObject *watched,QEvent *event) {
    if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonDblClick) {
        const auto mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton&&isCaptionPoint(mapFromGlobal(mouse->globalPosition().toPoint()))) {
            if(event->type()==QEvent::MouseButtonDblClick){isMaximized()?showNormal():showMaximized();return true;}
            if(windowHandle()&&windowHandle()->startSystemMove())return true;
        }
    }
    return QMainWindow::eventFilter(watched,event);
}

bool MainWindow::nativeEvent(const QByteArray &type,void *message,qintptr *result){
#ifdef Q_OS_WIN
    const auto msg=static_cast<MSG*>(message);
    if(msg->message==WM_GETMINMAXINFO){auto bounds=reinterpret_cast<MINMAXINFO*>(msg->lParam);MONITORINFO info{};info.cbSize=sizeof(info);if(GetMonitorInfo(MonitorFromWindow(msg->hwnd,MONITOR_DEFAULTTONEAREST),&info)){bounds->ptMaxPosition={info.rcWork.left-info.rcMonitor.left,info.rcWork.top-info.rcMonitor.top};bounds->ptMaxSize={info.rcWork.right-info.rcWork.left,info.rcWork.bottom-info.rcWork.top};const auto ratio=devicePixelRatioF();bounds->ptMinTrackSize={qRound(minimumWidth()*ratio),qRound(minimumHeight()*ratio)};*result=0;return true;}}
    if(msg->message==WM_NCHITTEST){POINT point{GET_X_LPARAM(msg->lParam),GET_Y_LPARAM(msg->lParam)};ScreenToClient(msg->hwnd,&point);const auto scale=devicePixelRatioF();const QPoint local(qRound(point.x/scale),qRound(point.y/scale));if(!isMaximized()){const bool left=local.x()<7,right=local.x()>=width()-7,top=local.y()<7,bottom=local.y()>=height()-7;if(left||right||top||bottom){*result=top?(left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP):bottom?(left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM):left?HTLEFT:HTRIGHT;return true;}}
        if(isCaptionPoint(local)){*result=HTCAPTION;return true;}
    }
#endif
    return QMainWindow::nativeEvent(type,message,result);
}
