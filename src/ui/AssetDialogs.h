#pragma once
#include "AssetGrid.h"
#include "GlassDialog.h"
class QListWidget;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QPushButton;

class AssetPicker : public GlassDialog {
    Q_OBJECT
public:
    AssetPicker(RpcClient *rpc,ThumbnailStore *thumbs,const QString &title,const QStringList &types={},QWidget *parent=nullptr);
    QJsonArray selectedAssets() const;
private: AssetGrid *m_grid; AssetModel *m_model;
};

class OrderedEditor : public GlassDialog {
    Q_OBJECT
public:
    enum Mode { Relations,Previews };
    OrderedEditor(RpcClient *rpc,ThumbnailStore *thumbs,const QJsonObject &asset,Mode mode,QWidget *parent=nullptr);
signals: void saved(const QJsonObject &asset);
private:
    RpcClient *m_rpc;ThumbnailStore *m_thumbs;QJsonObject m_asset;Mode m_mode;
    QListWidget *m_list;QComboBox *m_role,*m_normal;QSpinBox *m_slot;QLineEdit *m_label;QPushButton *m_save;
    QString m_cover;bool m_changing=false;
    void addItem(QJsonObject item);
    void selected();void editSelected();void save();void addLibrary();void move(int direction);
};
