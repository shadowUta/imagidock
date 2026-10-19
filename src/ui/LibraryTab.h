#pragma once
#include "AssetGrid.h"
#include <QWidget>
class QTreeWidget;
class QListWidget;
class QComboBox;
class QLineEdit;
class QLabel;
class QStackedWidget;
class QPushButton;
class PreviewPane;
class QScrollArea;
class AssetTitleLabel;

class LibraryTab : public QWidget {
    Q_OBJECT
public:
    LibraryTab(RpcClient *rpc,ThumbnailStore *thumbs,QWidget *parent=nullptr);
    void importPaths(const QStringList &paths);
    void importFiles();void importFolder();void refresh(bool keepPosition=true);void home();void back();
    void deleteSelected();void backupSelected();void exportSelected();
    void setCardSize(int value);
    void focusSearch();
    QJsonObject session() const;void restoreSession(const QJsonObject &state);
    QString currentAssetId() const;
    void openAsset(const QString &id,bool push=true);
signals:
    void titleChanged(const QString &title);
    void message(const QString &message);
    void jobStarted(const QString &jobId);
    void openInTab(const QString &assetId);
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
private:
    void fitPreviewHeight();
    QScrollArea *m_detailScroll=nullptr;
    RpcClient *m_rpc;ThumbnailStore *m_thumbs;AssetModel *m_model;AssetGrid *m_grid;
    QTreeWidget *m_directories;QListWidget *m_types,*m_tags;QLineEdit *m_search;QComboBox *m_status,*m_sort;
    QStackedWidget *m_pages;QLabel *m_count,*m_empty,*m_detailStatus;AssetTitleLabel *m_detailTitle;
    PreviewPane *m_preview;QListWidget *m_previews,*m_relations;
    QLineEdit *m_name,*m_assetTags;QLabel *m_meta,*m_path;QPushButton *m_favorite,*m_locate,*m_external,*m_backup,*m_export,*m_restore;
    QWidget *m_inspector;QLabel *m_inspectorThumbnail;
    QJsonObject m_current,m_filter;QJsonArray m_directoryData,m_tagData;QStringList m_history;QString m_detailKey;bool m_rebuilding=false;quint64 m_selectGeneration=0;
    QJsonArray selectedIds() const;void rebuildNavigation();void applyFilter();void inspect(const QString &id);
    void showAsset(const QJsonObject &asset,bool detail);void saveMetadata();void manage(bool previews);
    void perform(const QString &method,QJsonObject params,bool reload=true);void link();void locate();void external();
    void showContext(const QPoint &pos);void displayPreviews(const QJsonObject &asset);
};
