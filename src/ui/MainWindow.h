#pragma once
#include "LibraryTab.h"
#include <QMainWindow>
#include <QTabBar>
#include <QTabWidget>
class QSystemTrayIcon;

class DetachableTabBar : public QTabBar {
    Q_OBJECT
public: explicit DetachableTabBar(QWidget *parent=nullptr);
signals: void detached(int index,const QPoint &position);void tabReceived(const QString &token);
protected:
    void mousePressEvent(QMouseEvent *) override;void mouseMoveEvent(QMouseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;void dragMoveEvent(QDragMoveEvent *) override;void dropEvent(QDropEvent *) override;
private: QPoint m_press;int m_index=-1;
};
class AssetTabs : public QTabWidget {
    Q_OBJECT
public: explicit AssetTabs(QWidget *p=nullptr):QTabWidget(p){bar=new DetachableTabBar;setTabBar(bar);setTabsClosable(true);setMovable(true);setDocumentMode(true);}DetachableTabBar *bar;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(RpcClient *rpc,ThumbnailStore *thumbs,QWidget *parent=nullptr);
    LibraryTab *addTab(const QJsonObject &state={});
    LibraryTab *currentTab()const;
    static void applyTheme(bool light);
    void showJobs();void showIssues();void showSettings();
protected: void closeEvent(QCloseEvent *) override;
private:
    RpcClient *m_rpc;ThumbnailStore *m_thumbs;AssetTabs *m_tabs;QSystemTrayIcon *m_tray;
    QPointer<QDialog> m_jobs,m_issues;bool m_exit=false;int m_activeJobs=0;
    void bindTab(LibraryTab *tab);void receiveTab(const QString &token);void detach(int index,const QPoint &position);
    void exportLibrary();void importLibrary();void openLibrary();void manageRoles();
    void exitApplication();
};
