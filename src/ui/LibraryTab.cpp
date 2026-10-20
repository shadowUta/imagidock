#include "GlassDialog.h"
#include "LibraryTab.h"
#include "UiMetrics.h"
#include "UiIcons.h"
#include "InspectorSummary.h"
#include "InspectorPreviewLoader.h"
#include <QToolButton>
#include <QAction>
#include "AssetTitleLabel.h"
#include "AssetDialogs.h"
#include "PreviewPane.h"
#include <QCheckBox>
#include <QEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDialogButtonBox>
#include "GlassDialogs.h"
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

static QPushButton *button(const QString &text,QBoxLayout *layout){auto b=new QPushButton(text);layout->addWidget(b);return b;}

LibraryTab::LibraryTab(RpcClient *rpc,ThumbnailStore *thumbs,QWidget *parent):QWidget(parent),m_rpc(rpc),m_thumbs(thumbs){

    auto outer=new QVBoxLayout(this);
    outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    auto splitter=new QSplitter;splitter->setObjectName("libraryPanels");
    splitter->setHandleWidth(UiMetrics::Gap);splitter->setChildrenCollapsible(false);outer->addWidget(splitter,1);

    // Navigation stays outside the content's location and view bars.
    auto sidebar=new QWidget;sidebar->setObjectName("sidebar");
    auto nav=new QVBoxLayout(sidebar);nav->setContentsMargins(12,12,12,12);nav->setSpacing(8);
    auto workspaceTitle=new QLabel(tr("素材工作区"));workspaceTitle->setObjectName("workspaceTitle");nav->addWidget(workspaceTitle);
    auto allButton=new QPushButton(uiIcon("grid"),tr("全部素材"));allButton->setObjectName("navAll");allButton->setProperty("navAction",true);allButton->setCheckable(true);allButton->setChecked(true);nav->addWidget(allButton);
    auto favoriteButton=new QPushButton(uiIcon("star"),tr("已收藏"));favoriteButton->setObjectName("navFavorites");favoriteButton->setProperty("navAction",true);favoriteButton->setCheckable(true);nav->addWidget(favoriteButton);
    nav->addSpacing(8);
    auto dirs=new QHBoxLayout;dirs->setSpacing(4);
    auto navTitle=new QLabel(tr("库内目录"));navTitle->setObjectName("sectionTitle");dirs->addWidget(navTitle);dirs->addStretch();
    auto newDir=button(QString(),dirs),renameDir=button(QString(),dirs);
    newDir->setIcon(uiIcon("folder-add"));newDir->setToolTip(tr("新建库内目录"));newDir->setAccessibleName(tr("新建库内目录"));
    renameDir->setIcon(uiIcon("dots"));renameDir->setToolTip(tr("重命名所选目录"));renameDir->setAccessibleName(tr("重命名所选目录"));
    for(auto control:{newDir,renameDir}){control->setProperty("quiet",true);control->setProperty("iconOnly",true);control->setProperty("smallIcon",true);control->setFixedSize(24,24);control->setIconSize({14,14});}
    nav->addLayout(dirs);
    m_directories=new QTreeWidget;m_directories->setAccessibleName(tr("库内目录筛选"));m_directories->setHeaderHidden(true);m_directories->setIndentation(14);m_directories->setIconSize({14,14});m_directories->setMinimumHeight(96);m_directories->setMaximumHeight(180);nav->addWidget(m_directories,1);
    auto typeTitle=new QLabel(tr("素材类型"));typeTitle->setObjectName("sectionTitle");nav->addWidget(typeTitle);
    m_types=new QListWidget;m_types->setAccessibleName(tr("素材类型筛选，多选取并集"));m_types->setObjectName("typeNavigation");m_types->setFixedHeight(150);m_types->setIconSize({14,14});m_types->setSelectionMode(QAbstractItemView::MultiSelection);
    for(const auto &[title,id]:QList<QPair<QString,QString>>{{tr("图片"),"image"},{tr("模型"),"model"},{tr("视频"),"video"},{tr("音频"),"audio"},{tr("创作源文件"),"source"}}){auto item=new QListWidgetItem(uiIcon(id.toLatin1().constData()),title,m_types);item->setData(Qt::UserRole,id);item->setSizeHint({0,28});}
    nav->addWidget(m_types);
    auto tagTitle=new QLabel(tr("标签"));tagTitle->setObjectName("sectionTitle");tagTitle->setToolTip(tr("同类筛选多选取并集"));nav->addWidget(tagTitle);
    m_tags=new QListWidget;m_tags->setAccessibleName(tr("标签筛选，多选取并集"));m_tags->setIconSize({12,12});m_tags->setMinimumHeight(64);m_tags->setSelectionMode(QAbstractItemView::MultiSelection);nav->addWidget(m_tags,1);
    auto reset=new QPushButton(tr("清除筛选"));reset->setProperty("quiet",true);nav->addWidget(reset);
    auto sidebarScroll=new QScrollArea;sidebarScroll->setObjectName("navigationScroll");sidebarScroll->setWidgetResizable(true);sidebarScroll->setFrameShape(QFrame::NoFrame);sidebarScroll->setWidget(sidebar);sidebarScroll->viewport()->setAutoFillBackground(false);splitter->addWidget(sidebarScroll);

    auto content=new QWidget;content->setObjectName("contentPanel");content->setMinimumWidth(440);
    auto contentLayout=new QVBoxLayout(content);contentLayout->setContentsMargins(0,0,0,0);contentLayout->setSpacing(0);
    auto location=new QWidget;location->setObjectName("locationBar");location->setFixedHeight(UiMetrics::LocationHeight);
    auto locationLayout=new QHBoxLayout(location);locationLayout->setContentsMargins(16,7,16,7);locationLayout->setSpacing(8);
    auto backButton=new QToolButton,homeButton=new QToolButton;
    backButton->setIcon(uiIcon("back"));backButton->setToolTip(tr("返回上一资产"));backButton->setAccessibleName(tr("返回上一资产"));
    homeButton->setIcon(uiIcon("home"));homeButton->setToolTip(tr("回到素材库"));homeButton->setAccessibleName(tr("回到素材库"));
    for(auto control:{backButton,homeButton}){control->setFixedSize(28,28);control->setIconSize({16,16});control->setProperty("quiet",true);control->setProperty("iconOnly",true);locationLayout->addWidget(control);}
    connect(backButton,&QToolButton::clicked,this,&LibraryTab::back);connect(homeButton,&QToolButton::clicked,this,&LibraryTab::home);
    m_locationTitle=new AssetTitleLabel;m_locationTitle->setObjectName("locationTitle");m_locationTitle->setTitle(tr("素材库 / 全部素材"));m_locationTitle->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);m_locationTitle->setMinimumWidth(36);locationLayout->addWidget(m_locationTitle,1);
    auto addButton=new QPushButton(uiIcon("plus"),tr("添加素材"));addButton->setObjectName("primaryAction");addButton->setFixedHeight(UiMetrics::ControlHeight);locationLayout->addWidget(addButton);connect(addButton,&QPushButton::clicked,this,&LibraryTab::importFiles);
    auto folderButton=new QToolButton;folderButton->setIcon(uiIcon("folder-add"));folderButton->setIconSize({16,16});folderButton->setToolTip(tr("添加文件夹"));folderButton->setAccessibleName(tr("添加文件夹"));folderButton->setProperty("quiet",true);folderButton->setProperty("iconOnly",true);folderButton->setFixedSize(28,28);locationLayout->addWidget(folderButton);connect(folderButton,&QToolButton::clicked,this,&LibraryTab::importFolder);
    contentLayout->addWidget(location);

    auto tools=new QWidget;tools->setObjectName("libraryTools");tools->setFixedHeight(UiMetrics::ViewHeight);
    auto toolsLayout=new QHBoxLayout(tools);toolsLayout->setContentsMargins(16,6,16,6);toolsLayout->setSpacing(8);
    m_search=new QLineEdit;m_search->setObjectName("assetSearch");m_search->setAccessibleName(tr("搜索素材"));m_search->setToolTip(tr("按名称或文件名搜索 · Ctrl+F"));m_search->setPlaceholderText(tr("搜索素材…"));m_search->addAction(uiIcon("search"),QLineEdit::LeadingPosition);m_search->setClearButtonEnabled(true);m_search->setMinimumWidth(112);m_search->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);toolsLayout->addWidget(m_search,1);
    m_status=new QComboBox;m_status->setAccessibleName(tr("资产状态筛选"));for(const auto &[title,id]:QList<QPair<QString,QString>>{{tr("全部状态"),"all"},{tr("已收藏"),"favorite"},{tr("已备份"),"backed_up"},{tr("来源丢失"),"missing"},{tr("备份过期"),"stale"},{tr("回收站"),"deleted"}})m_status->addItem(title,id);toolsLayout->addWidget(m_status);
    m_sort=new QComboBox;m_sort->setAccessibleName(tr("资产排序"));m_sort->addItem(tr("名称排序"),"name");m_sort->addItem(tr("最近修改"),"modified");m_sort->addItem(tr("最近导入"),"imported");toolsLayout->addWidget(m_sort);
    for(auto control:QList<QWidget*>{m_search,m_status,m_sort})control->setFixedHeight(UiMetrics::ControlHeight);
    contentLayout->addWidget(tools);

    m_pages=new QStackedWidget;m_pages->setObjectName("assetPages");contentLayout->addWidget(m_pages,1);
    auto browse=new QWidget;browse->setObjectName("browseHost");auto browseLayout=new QVBoxLayout(browse);browseLayout->setContentsMargins(16,12,16,16);browseLayout->setSpacing(8);
    auto heading=new QHBoxLayout;auto title=new QLabel(tr("所有素材"));title->setObjectName("pageTitle");title->setFixedHeight(24);heading->addWidget(title);heading->addStretch();m_count=new QLabel(tr("连接素材库…"));m_count->setObjectName("muted");heading->addWidget(m_count);browseLayout->addLayout(heading);
    m_empty=new QLabel(tr("将素材拖入这里，或点击“添加素材”。\n建立索引后，即可预览、关联和整理。"));m_empty->setWordWrap(true);m_empty->setObjectName("emptyState");m_empty->setAlignment(Qt::AlignCenter);browseLayout->addWidget(m_empty);
    m_grid=new AssetGrid;m_model=new AssetModel(rpc,thumbs,this);m_grid->setModel(m_model);m_grid->setContextMenuPolicy(Qt::CustomContextMenu);browseLayout->addWidget(m_grid,1);m_pages->addWidget(browse);

    auto detail=new QWidget;detail->setObjectName("detailHost");detail->installEventFilter(this);auto detailLayout=new QVBoxLayout(detail);detailLayout->setContentsMargins(16,12,16,16);detailLayout->setSpacing(8);
    auto detailHeading=new QHBoxLayout;m_detailTitle=new AssetTitleLabel;m_detailTitle->setObjectName("pageTitle");m_detailTitle->setTextFormat(Qt::PlainText);m_detailTitle->setWordWrap(false);m_detailTitle->setFixedHeight(UiMetrics::SectionHeight);m_detailTitle->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);m_detailTitle->setFocusPolicy(Qt::StrongFocus);detailHeading->addWidget(m_detailTitle,1);auto openTab=button(tr("新页签打开"),detailHeading);openTab->setProperty("quiet",true);detailLayout->addLayout(detailHeading);
    m_detailStatus=new QLabel;m_detailStatus->setObjectName("muted");m_detailStatus->setWordWrap(true);m_detailStatus->setMaximumHeight(36);m_detailStatus->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);detailLayout->addWidget(m_detailStatus);
    m_preview=new PreviewPane(rpc);detailLayout->addWidget(m_preview,1);
    auto organization=new QWidget;organization->setObjectName("assetOrganization");auto organizationLayout=new QVBoxLayout(organization);organizationLayout->setContentsMargins(0,0,0,0);organizationLayout->setSpacing(8);
    auto previewHeader=new QHBoxLayout;auto previewLabel=new QLabel(tr("预览图"));previewLabel->setObjectName("sectionTitle");previewHeader->addWidget(previewLabel);previewHeader->addStretch();auto mainPreview=button(tr("主文件"),previewHeader),navigatePreview=button(tr("打开预览资产"),previewHeader),managePreviews=button(tr("管理预览"),previewHeader);for(auto b:{mainPreview,navigatePreview,managePreviews})b->setProperty("quiet",true);organizationLayout->addLayout(previewHeader);
    m_previewsEmpty=new QLabel(tr("添加参考图，或保存模型快照。"));m_previewsEmpty->setObjectName("muted");organizationLayout->addWidget(m_previewsEmpty);
    m_previews=new QListWidget;m_previews->setObjectName("previewStrip");m_previews->setViewMode(QListView::IconMode);m_previews->setFlow(QListView::LeftToRight);m_previews->setWrapping(false);m_previews->setIconSize({64,48});m_previews->setFixedHeight(80);organizationLayout->addWidget(m_previews);
    auto relHeader=new QHBoxLayout;auto relationLabel=new QLabel(tr("关联资产"));relationLabel->setObjectName("sectionTitle");relHeader->addWidget(relationLabel);relHeader->addStretch();auto manageRelations=button(tr("管理关联 / 材质"),relHeader);manageRelations->setObjectName("manageRelations");manageRelations->setProperty("quiet",true);organizationLayout->addLayout(relHeader);
    m_relationsEmpty=new QLabel(tr("关联贴图、设计稿或相关素材。"));m_relationsEmpty->setObjectName("muted");organizationLayout->addWidget(m_relationsEmpty);
    m_relations=new QListWidget;m_relations->setObjectName("relationStrip");m_relations->setViewMode(QListView::IconMode);m_relations->setFlow(QListView::LeftToRight);m_relations->setWrapping(false);m_relations->setIconSize({32,32});m_relations->setGridSize({136,64});m_relations->setTextElideMode(Qt::ElideRight);m_relations->setFixedHeight(72);organizationLayout->addWidget(m_relations);
    auto detailScroll=new QScrollArea;m_detailScroll=detailScroll;detailScroll->viewport()->installEventFilter(this);detailScroll->setObjectName("detailScroll");detailScroll->setWidgetResizable(true);detailScroll->setFrameShape(QFrame::NoFrame);detailScroll->setWidget(organization);detailScroll->setFixedHeight(180);detailLayout->addWidget(detailScroll);m_pages->addWidget(detail);splitter->addWidget(content);

    auto scroll=new QScrollArea;scroll->setObjectName("inspectorScroll");scroll->viewport()->setAutoFillBackground(false);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto inspector=new QWidget;m_inspector=inspector;inspector->setObjectName("inspector");auto inspectorLayout=new QVBoxLayout(inspector);inspectorLayout->setContentsMargins(0,0,0,0);inspectorLayout->setSpacing(0);
    m_inspectorSummary=new InspectorSummary;inspectorLayout->addWidget(m_inspectorSummary);
    m_inspectorLoader=new InspectorPreviewLoader(m_rpc,this);
    connect(m_inspectorSummary,&InspectorSummary::previewSelected,m_inspectorLoader,&InspectorPreviewLoader::select);
    connect(m_inspectorLoader,&InspectorPreviewLoader::ready,m_inspectorSummary,&InspectorSummary::setPreviewImage);
    connect(m_inspectorLoader,&InspectorPreviewLoader::failed,m_inspectorSummary,&InspectorSummary::setPreviewError);
    connect(m_inspectorSummary,&InspectorSummary::renameRequested,this,&LibraryTab::renameInspectorAsset);
    connect(m_inspectorSummary,&InspectorSummary::addTagRequested,this,&LibraryTab::addInspectorTag);
    connect(m_inspectorSummary,&InspectorSummary::removeTagRequested,this,&LibraryTab::removeInspectorTag);
    connect(m_inspectorSummary,&InspectorSummary::favoriteToggled,this,[this](bool value){if(!m_current.isEmpty())perform("asset.update",{{"asset_id",m_current["id"]},{"revision",m_current["revision"]},{"values",QJsonObject{{"favorite",value}}}});});
    m_inspectorBody=new QWidget;inspectorLayout->addWidget(m_inspectorBody);auto form=new QVBoxLayout(m_inspectorBody);form->setContentsMargins(16,0,16,16);form->setSpacing(8);
    auto propertyLabel=[form](const QString &text){auto label=new QLabel(text);label->setObjectName("propertyLabel");form->addWidget(label);};
    m_meta=new QLabel;m_meta->setTextFormat(Qt::PlainText);m_meta->setWordWrap(true);m_meta->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);m_meta->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);form->addWidget(m_meta);
    auto move=button(tr("移动到库内目录"),form);move->setIcon(uiIcon("folder"));
    form->addSpacing(8);propertyLabel(tr("来源"));m_path=new QLabel;m_path->setTextFormat(Qt::PlainText);m_path->setWordWrap(true);m_path->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);m_path->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);m_path->setAccessibleName(tr("素材来源路径"));form->addWidget(m_path);
    auto fileActions=new QHBoxLayout;fileActions->setSpacing(4);m_locate=button(tr("定位文件"),fileActions);m_locate->setIcon(uiIcon("folder"));m_locate->setAccessibleName(tr("在文件夹中显示"));m_external=button(tr("打开文件"),fileActions);m_external->setIcon(uiIcon("open"));m_external->setAccessibleName(tr("用默认程序打开"));form->addLayout(fileActions);
    auto relink=button(tr("重新链接文件"),form);relink->setIcon(uiIcon("open"));form->addSpacing(8);
    auto transferActions=new QHBoxLayout;transferActions->setSpacing(4);m_backup=button(tr("创建备份"),transferActions);m_backup->setIcon(uiIcon("backup"));m_export=button(tr("取用文件"),transferActions);m_export->setIcon(uiIcon("download"));form->addLayout(transferActions);
    m_restore=button(tr("从回收站恢复"),form);auto remove=button(tr("移到回收站"),form);remove->setObjectName("danger");inspectorLayout->addStretch();scroll->setWidget(inspector);splitter->addWidget(scroll);
    splitter->setSizes({212,880,280});splitter->setStretchFactor(1,1);sidebarScroll->setMinimumWidth(180);sidebarScroll->setMaximumWidth(320);scroll->setMinimumWidth(225);scroll->setMaximumWidth(360);
    auto debounce=new QTimer(this);debounce->setInterval(230);debounce->setSingleShot(true);connect(m_search,&QLineEdit::textChanged,debounce,qOverload<>(&QTimer::start));connect(debounce,&QTimer::timeout,this,&LibraryTab::applyFilter);connect(m_status,&QComboBox::currentIndexChanged,this,&LibraryTab::applyFilter);connect(m_sort,&QComboBox::currentIndexChanged,this,&LibraryTab::applyFilter);connect(m_types,&QListWidget::itemSelectionChanged,this,&LibraryTab::applyFilter);connect(m_tags,&QListWidget::itemSelectionChanged,this,&LibraryTab::applyFilter);connect(m_directories,&QTreeWidget::itemSelectionChanged,this,&LibraryTab::applyFilter);connect(reset,&QPushButton::clicked,this,[this]{m_rebuilding=true;m_search->clear();m_status->setCurrentIndex(0);m_types->clearSelection();m_tags->clearSelection();m_directories->setCurrentItem(m_directories->topLevelItem(0));m_rebuilding=false;applyFilter();});
    connect(m_thumbs,&ThumbnailStore::available,this,[this](const QString &){updateRelatedIcons();});connect(m_model,&AssetModel::loaded,this,[this](int count,int total){m_count->setText(tr("%1 个资产 · 已加载 %2").arg(total).arg(count));m_empty->setVisible(total==0);});connect(m_model,&AssetModel::error,this,[this](const QString &err){m_count->setText(tr("加载失败"));emit message(err);});connect(m_grid->selectionModel(),&QItemSelectionModel::currentChanged,this,[this](const QModelIndex &i,const QModelIndex &){if(i.isValid())inspect(m_model->asset(i.row())["id"].toString());});connect(m_grid,&QListView::activated,this,[this](const QModelIndex &i){openAsset(m_model->asset(i.row())["id"].toString());});connect(m_grid,&AssetGrid::filesDropped,this,&LibraryTab::importPaths);connect(m_grid,&QWidget::customContextMenuRequested,this,&LibraryTab::showContext);
    connect(m_locate,&QPushButton::clicked,this,&LibraryTab::locate);connect(m_external,&QPushButton::clicked,this,&LibraryTab::external);connect(relink,&QPushButton::clicked,this,&LibraryTab::link);connect(m_backup,&QPushButton::clicked,this,&LibraryTab::backupSelected);connect(m_export,&QPushButton::clicked,this,&LibraryTab::exportSelected);connect(remove,&QPushButton::clicked,this,&LibraryTab::deleteSelected);connect(m_restore,&QPushButton::clicked,this,[this]{perform("asset.restore",{{"asset_ids",selectedIds()}});});connect(openTab,&QPushButton::clicked,this,[this]{if(!currentAssetId().isEmpty())emit openInTab(currentAssetId());});
    connect(mainPreview,&QPushButton::clicked,this,[this]{if(!m_current.isEmpty())m_preview->setAsset(m_current);});connect(managePreviews,&QPushButton::clicked,this,[this]{manage(true);});connect(manageRelations,&QPushButton::clicked,this,[this]{manage(false);});connect(m_relations,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){const auto o=item->data(Qt::UserRole).toJsonObject();openAsset(o["target_id"].toString());});connect(m_previews,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){const auto o=item->data(Qt::UserRole).toJsonObject();if(!o["path"].toString().isEmpty())m_preview->setImagePath(o["path"].toString(),o["label"].toString());else if(!o["target_id"].toString().isEmpty())m_rpc->call("asset.get",{{"asset_id",o["target_id"]}},this,[this](const QJsonObject &a,const QString &err){if(err.isEmpty())m_preview->setAsset(a);else emit message(err);});});auto navigate=[this]{if(auto item=m_previews->currentItem()){const auto id=item->data(Qt::UserRole).toJsonObject()["target_id"].toString();if(!id.isEmpty())openAsset(id);else emit message(tr("外部预览图没有独立资产索引。"));}};connect(navigatePreview,&QPushButton::clicked,this,navigate);connect(m_previews,&QListWidget::itemDoubleClicked,this,[navigate](QListWidgetItem *){navigate();});connect(m_preview,&PreviewPane::metadataReady,this,[this](const QJsonObject &meta){auto current=m_current["metadata"].toObject();for(auto i=meta.begin();i!=meta.end();++i)current[i.key()]=i.value();m_current["metadata"]=current;QStringList materialSlots;for(const auto &v:meta["materials"].toArray()){const auto slot=v.toObject();materialSlots<<tr("%1 · %2").arg(slot["slot"].toInt()).arg(slot["name"].toString());}m_meta->setText(materialSlots.isEmpty()?QString():tr("材质槽位")+"\n"+materialSlots.join("\n"));m_meta->setVisible(!materialSlots.isEmpty());});connect(m_preview,&PreviewPane::snapshotSaved,this,[this](const QString &path){auto items=m_current["previews"].toArray();items.append(QJsonObject{{"path",path},{"label",tr("模型快照")}});perform("preview.set",{{"asset_id",m_current["id"]},{"revision",m_current["revision"]},{"items",items}});});
    m_directories->setContextMenuPolicy(Qt::CustomContextMenu);connect(m_directories,&QWidget::customContextMenuRequested,this,[this](const QPoint &pos){auto item=m_directories->itemAt(pos);if(!item)return;m_directories->setCurrentItem(item);QMenu menu;menu.addAction(tr("取用此目录的全部资产…"),this,[this,item]{const auto destination=GlassFiles::getExistingDirectory(this,tr("选择目录取用位置"));if(!destination.isEmpty())perform("export.assets",{{"directory_id",item->data(0,Qt::UserRole).toString()},{"destination",destination}},false);});menu.exec(m_directories->viewport()->mapToGlobal(pos));});connect(newDir,&QPushButton::clicked,this,[this]{bool ok=false;const auto name=GlassInputs::getText(this,tr("新建库内目录"),tr("目录名称"),QLineEdit::Normal,{},&ok);if(ok&&!name.trimmed().isEmpty()){QJsonObject params{{"name",name.trimmed()}};if(auto item=m_directories->currentItem())params["parent_id"]=item->data(0,Qt::UserRole).toString();perform("directory.create",params);}});connect(renameDir,&QPushButton::clicked,this,[this]{auto item=m_directories->currentItem();if(!item||item->data(0,Qt::UserRole).toString().isEmpty())return;bool ok=false;const auto name=GlassInputs::getText(this,tr("重命名库内目录"),tr("新名称"),QLineEdit::Normal,item->text(0),&ok);if(ok&&!name.trimmed().isEmpty())perform("directory.update",{{"directory_id",item->data(0,Qt::UserRole).toString()},{"name",name.trimmed()}});});connect(move,&QPushButton::clicked,this,[this]{if(m_current.isEmpty())return;m_rpc->call("directory.list",{},this,[this](const QJsonObject &r,const QString &err){if(!err.isEmpty()){emit message(err);return;}QStringList names{tr("根目录")},ids{""};for(const auto &v:r["items"].toArray()){const auto d=v.toObject();names<<d["path"].toString(d["name"].toString());ids<<d["id"].toString();}bool ok=false;const auto choice=GlassInputs::getItem(this,tr("移动资产"),tr("库内目录（不移动外部文件）"),names,0,false,&ok);if(ok)perform("asset.update",{{"asset_id",m_current["id"]},{"revision",m_current["revision"]},{"values",QJsonObject{{"directory_id",ids.value(names.indexOf(choice))}}}});});});
    connect(allButton,&QPushButton::clicked,this,[this,reset,allButton,favoriteButton]{reset->click();home();allButton->setChecked(true);favoriteButton->setChecked(false);});
    connect(favoriteButton,&QPushButton::clicked,this,[this,allButton,favoriteButton]{m_status->setCurrentIndex(m_status->findData("favorite"));home();allButton->setChecked(false);favoriteButton->setChecked(true);});
    connect(m_status,&QComboBox::currentIndexChanged,this,[this,allButton,favoriteButton]{allButton->setChecked(m_status->currentData()=="all");favoriteButton->setChecked(m_status->currentData()=="favorite");});
    connect(m_relations->horizontalScrollBar(),&QScrollBar::valueChanged,this,[this]{updateRelatedIcons();});
    connect(rpc,&RpcClient::changed,this,[this]{rebuildNavigation();refresh();if(!currentAssetId().isEmpty())inspect(currentAssetId());});rebuildNavigation();applyFilter();showAsset({},false);QWidget::setTabOrder(m_search,m_status);QWidget::setTabOrder(m_status,m_sort);QWidget::setTabOrder(m_sort,m_directories);QWidget::setTabOrder(m_directories,m_types);QWidget::setTabOrder(m_types,m_tags);QWidget::setTabOrder(m_tags,m_grid);
}
void LibraryTab::focusSearch(){m_search->setFocus(Qt::ShortcutFocusReason);m_search->selectAll();}
void LibraryTab::setCardSize(int v){m_grid->setCardSize(v);}
void LibraryTab::rebuildNavigation(){
    m_rpc->call("directory.list",{},this,[this](const QJsonObject &r,const QString &err){if(!err.isEmpty())return;if(m_directories->topLevelItemCount()&&r["items"].toArray()==m_directoryData)return;m_directoryData=r["items"].toArray();const QString current=m_filter["directory_id"].toString();m_rebuilding=true;m_directories->clear();auto root=new QTreeWidgetItem(m_directories,{tr("所有目录")});root->setIcon(0,uiIcon("folder"));root->setData(0,Qt::UserRole,"");QHash<QString,QTreeWidgetItem*> nodes;nodes[""]=root;const auto items=r["items"].toArray();for(const auto &v:items){const auto d=v.toObject();auto item=new QTreeWidgetItem({d["name"].toString()});item->setIcon(0,uiIcon("folder"));item->setData(0,Qt::UserRole,d["id"].toString());item->setToolTip(0,d["path"].toString());nodes[d["id"].toString()]=item;}for(const auto &v:items){const auto d=v.toObject();auto item=nodes[d["id"].toString()];auto parent=nodes.value(d["parent_id"].toString(),root);if(parent==item)parent=root;parent->addChild(item);}root->setExpanded(true);m_directories->setCurrentItem(nodes.value(current,root));m_rebuilding=false;});
    m_rpc->call("tag.list",{},this,[this](const QJsonObject &r,const QString &err){if(!err.isEmpty())return;if(r["items"].toArray()==m_tagData)return;m_tagData=r["items"].toArray();QStringList selected;for(const auto &v:m_filter["tags"].toArray())selected<<v.toString();m_rebuilding=true;m_tags->clear();for(const auto &v:r["items"].toArray()){auto item=new QListWidgetItem(uiIcon("tag"),v.toString(),m_tags);item->setSelected(selected.contains(item->text()));}m_rebuilding=false;});
}
void LibraryTab::applyFilter(){if(m_rebuilding)return;QJsonArray types,tags;for(auto i:m_types->selectedItems())types.append(i->data(Qt::UserRole).toString());for(auto i:m_tags->selectedItems())tags.append(i->text());m_filter={{"query",m_search->text()},{"types",types},{"tags",tags},{"status",m_status->currentData().toString()},{"sort",m_sort->currentData().toString()}};if(auto item=m_directories->currentItem())m_filter["directory_id"]=item->data(0,Qt::UserRole).toString();m_model->setFilter(m_filter);m_count->setText(tr("正在检索…"));updateLocation();}
void LibraryTab::refresh(bool keep){if(!keep){m_model->setFilter(m_filter);return;}const int columns=qMax(1,m_grid->viewport()->width()/m_grid->gridSize().width());const int first=qMax(0,(m_grid->verticalScrollBar()->value()/m_grid->gridSize().height()-1)*columns);const int count=(m_grid->viewport()->height()*2/m_grid->gridSize().height()+3)*columns;m_model->refreshRange(first,count);}
void LibraryTab::inspect(const QString &id){if(id.isEmpty())return;const auto generation=++m_selectGeneration;m_rpc->call("asset.get",{{"asset_id",id}},this,[this,id,generation](const QJsonObject &r,const QString &err){if(generation!=m_selectGeneration)return;if(!err.isEmpty()){emit message(err);return;}showAsset(r,m_pages->currentIndex()==1);});}
void LibraryTab::openAsset(const QString &id,bool push){if(id.isEmpty())return;if(push&&(m_history.isEmpty()||m_history.last()!=id))m_history.append(id);m_detailKey.clear();m_pages->setCurrentIndex(1);m_detailScroll->verticalScrollBar()->setValue(0);inspect(id);}
QString LibraryTab::currentAssetId()const{return m_current["id"].toString();}
void LibraryTab::home(){m_history.clear();m_preview->stop();m_pages->setCurrentIndex(0);m_grid->setFocus(Qt::OtherFocusReason);updateLocation();emit titleChanged(tr("素材库"));}
void LibraryTab::back(){if(m_pages->currentIndex()==0)return;if(m_history.size()>1){m_history.removeLast();openAsset(m_history.last(),false);}else home();}
void LibraryTab::showAsset(const QJsonObject &a,bool detail){
    m_current=a;m_inspectorLoader->setAsset(a);m_inspectorSummary->setAsset(a);updateInspectorPreviews();
    m_inspectorBody->setVisible(!a.isEmpty());m_path->setText(a["source_path"].toString());
    QStringList materialSlots;for(const auto &v:a["metadata"].toObject()["materials"].toArray()){
        const auto slot=v.toObject();materialSlots<<tr("%1 · %2").arg(slot["slot"].toInt()).arg(slot["name"].toString());
    }
    m_meta->setText(materialSlots.isEmpty()?QString():tr("材质槽位")+"\n"+materialSlots.join("\n"));m_meta->setVisible(!materialSlots.isEmpty());
    const QString backup=a["backup_state"].toString();
    bool usableBackup=backup=="ready"||backup=="stale";
    for(const auto &issue:a["issues"].toArray())if(issue.toObject()["kind"]=="backup_corrupt")usableBackup=false;
    const QString state=a["placeholder"].toBool()?tr("库内占位"):a["deleted"].toBool()?tr("位于回收站"):a["missing"].toBool()?tr("来源丢失"):tr("来源有效");
    m_locate->setEnabled(!a.isEmpty()&&!a["missing"].toBool()&&QFileInfo::exists(a["source_path"].toString()));
    m_external->setEnabled(m_locate->isEnabled());m_backup->setEnabled(!a.isEmpty());
    m_export->setEnabled(!a.isEmpty()&&(m_locate->isEnabled()||usableBackup));m_restore->setVisible(a["deleted"].toBool());
    if(detail){
        updateLocation();m_detailTitle->setTitle(a["name"].toString());m_detailStatus->setToolTip(a["virtual_path"].toString());
        m_detailStatus->setText(a["virtual_path"].toString()+"  ·  "+state);emit titleChanged(a["name"].toString());
        const QString detailKey=a["id"].toString()+":"+a["revision"].toVariant().toString();
        if(detailKey!=m_detailKey){m_detailKey=detailKey;m_preview->setAsset(a);m_detailTitle->setFocus(Qt::OtherFocusReason);displayPreviews(a);}
    }
}
void LibraryTab::displayPreviews(const QJsonObject &a){m_previews->clear();m_relations->clear();for(const auto &v:a["previews"].toArray()){const auto o=v.toObject();auto item=new QListWidgetItem(o["label"].toString(tr("预览图")),m_previews);item->setData(Qt::UserRole,o);item->setToolTip(o["target_id"].toString().isEmpty()?tr("外部派生预览"):tr("库内预览 · 双击打开资产"));const QString p=o["path"].toString();if(!p.isEmpty()){QPixmap pix(p);if(!pix.isNull())item->setIcon(QIcon(pix.scaled(72,54,Qt::KeepAspectRatio,Qt::SmoothTransformation)));}}for(const auto &v:a["relations"].toArray()){const auto o=v.toObject();const auto target=o["target"].toObject();QString name=o["label"].toString(target["name"].toString());if(name.isEmpty())name=o["target_id"].toString();if(o["missing"].toBool()||target["deleted"].toBool())name+=tr(" · 丢失");auto item=new QListWidgetItem(name,m_relations);item->setData(Qt::UserRole,o);item->setToolTip(target["source_path"].toString()+"\n"+o["role"].toString()+" / "+QString::number(o["slot"].toInt(1)));const auto pix=m_thumbs->get(target);item->setIcon(pix.isNull()?uiIcon(target["type"].toString().toLatin1().constData()):QIcon(pix));}m_previews->setVisible(m_previews->count()>0);m_previewsEmpty->setVisible(m_previews->count()==0);m_relations->setVisible(m_relations->count()>0);m_relationsEmpty->setVisible(m_relations->count()==0);}
QJsonArray LibraryTab::selectedIds()const{QJsonArray ids;if(m_pages->currentIndex()==0)for(const auto &i:m_grid->selectionModel()->selectedIndexes())ids.append(m_model->asset(i.row())["id"]);if(ids.isEmpty()&&!m_current.isEmpty())ids.append(m_current["id"]);return ids;}
void LibraryTab::perform(const QString &method,QJsonObject params,bool reload){m_rpc->call(method,params,this,[this,reload](const QJsonObject &r,const QString &err){if(!err.isEmpty()){GlassMessages::warning(this,tr("操作未完成"),err);return;}if(!r["job_id"].toString().isEmpty()){emit jobStarted(r["job_id"].toString());emit message(tr("任务已加入后台队列"));}else emit message(tr("操作完成"));if(reload){m_rpc->refresh();refresh();if(!currentAssetId().isEmpty())inspect(currentAssetId());}});}
void LibraryTab::renameInspectorAsset(){
    if(m_current.isEmpty())return;const auto id=currentAssetId();bool ok=false;
    const auto name=GlassInputs::getText(this,tr("重命名资产"),tr("资产名称（不修改外部原文件）"),QLineEdit::Normal,m_current["name"].toString(),&ok).trimmed();
    if(!ok||name.isEmpty()||id!=currentAssetId()||name==m_current["name"].toString())return;
    perform("asset.update",{{"asset_id",id},{"revision",m_current["revision"]},{"values",QJsonObject{{"name",name}}}});
}
void LibraryTab::addInspectorTag(){
    if(m_current.isEmpty())return;const auto id=currentAssetId();bool ok=false;
    const auto value=GlassInputs::getText(this,tr("添加标签"),tr("标签名称，可用逗号添加多个"),QLineEdit::Normal,{},&ok);
    if(!ok||id!=currentAssetId())return;auto tags=m_current["tags"].toArray();const auto original=tags;
    for(const auto &part:value.split(QRegularExpression("[,，\\n]"),Qt::SkipEmptyParts)){
        const auto tag=part.trimmed();if(!tag.isEmpty()&&!tags.contains(tag))tags.append(tag);
    }
    if(tags!=original)perform("asset.update",{{"asset_id",id},{"revision",m_current["revision"]},{"values",QJsonObject{{"tags",tags}}}});
}
void LibraryTab::removeInspectorTag(const QString &tag){
    if(m_current.isEmpty())return;QJsonArray tags;for(const auto &value:m_current["tags"].toArray())if(value.toString()!=tag)tags.append(value);
    perform("asset.update",{{"asset_id",m_current["id"]},{"revision",m_current["revision"]},{"values",QJsonObject{{"tags",tags}}}});
}

void LibraryTab::manage(bool previews){if(m_current.isEmpty())return;auto editor=new OrderedEditor(m_rpc,m_thumbs,m_current,previews?OrderedEditor::Previews:OrderedEditor::Relations,this);editor->setAttribute(Qt::WA_DeleteOnClose);connect(editor,&OrderedEditor::saved,this,[this](const QJsonObject &a){showAsset(a,true);});editor->open();}
void LibraryTab::importFiles(){const auto paths=GlassFiles::getOpenFileNames(this,tr("选择要入库的素材"),{},tr("常用素材 (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff *.tga *.psd *.psb *.sai *.sai2 *.glb *.gltf *.obj *.fbx *.wav *.mp3 *.flac *.ogg *.m4a *.mp4 *.webm *.mov *.mkv *.blend *.aseprite *.ase);;所有文件 (*)"));if(!paths.isEmpty())importPaths(paths);}
void LibraryTab::importFolder(){const auto path=GlassFiles::getExistingDirectory(this,tr("选择文件夹（递归扫描）"));if(!path.isEmpty())importPaths({path});}
void LibraryTab::importPaths(const QStringList &paths){if(paths.isEmpty())return;auto dialog=new GlassDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(tr("添加到素材库"));auto layout=new QVBoxLayout(dialog);auto label=new QLabel(tr("将扫描 %1 个文件或文件夹，保留文件夹结构。\n原文件保持原位，扫描与预览在后台进行。").arg(paths.size()));label->setWordWrap(true);layout->addWidget(label);auto backup=new QCheckBox(tr("同时创建压缩备份（包含必要依赖）"));layout->addWidget(backup);m_rpc->call("library.info",{},dialog,[backup](const QJsonObject &r,const QString &){backup->setChecked(r["settings"].toObject()["default_backup"].toBool());});auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,paths,backup]{QJsonObject params{{"paths",QJsonArray::fromStringList(paths)},{"backup",backup->isChecked()}};if(auto item=m_directories->currentItem())params["directory_id"]=item->data(0,Qt::UserRole).toString();perform("asset.import",params);dialog->accept();});dialog->open();}
void LibraryTab::deleteSelected(){const auto ids=selectedIds();if(ids.isEmpty())return;if(GlassMessages::question(this,tr("移到回收站"),tr("将 %1 个库内资产移到回收站？外部原始文件不会删除，关联将显示丢失。").arg(ids.size()))==QMessageBox::Yes)perform("asset.delete",{{"asset_ids",ids}});}
void LibraryTab::backupSelected(){const auto ids=selectedIds();if(!ids.isEmpty())perform("backup.start",{{"asset_ids",ids},{"enabled",true}});}
void LibraryTab::exportSelected(){const auto ids=selectedIds();if(ids.isEmpty())return;const auto path=GlassFiles::getExistingDirectory(this,tr("选择取用目标目录（同名文件自动改名）"));if(!path.isEmpty())perform("export.assets",{{"asset_ids",ids},{"destination",path}},false);}
void LibraryTab::link(){if(m_current.isEmpty())return;const auto path=GlassFiles::getOpenFileName(this,tr("重新链接原始文件"));if(!path.isEmpty())perform("asset.relink",{{"asset_id",m_current["id"]},{"path",path}});}
void LibraryTab::locate(){const QString path=m_current["source_path"].toString();if(!QFileInfo::exists(path))return;
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe",{"/select,",QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}
void LibraryTab::external(){const auto path=m_current["source_path"].toString();if(QFileInfo::exists(path))QDesktopServices::openUrl(QUrl::fromLocalFile(path));}
void LibraryTab::showContext(const QPoint &pos){const auto index=m_grid->indexAt(pos);if(index.isValid()&&!m_grid->selectionModel()->isSelected(index))m_grid->setCurrentIndex(index);QMenu menu;menu.addAction(tr("打开详情"),this,[this,index]{if(index.isValid())openAsset(m_model->asset(index.row())["id"].toString());});menu.addAction(tr("在新页签打开"),this,[this,index]{if(index.isValid())emit openInTab(m_model->asset(index.row())["id"].toString());});menu.addSeparator();menu.addAction(tr("备份所选"),this,&LibraryTab::backupSelected);menu.addAction(tr("取用所选"),this,&LibraryTab::exportSelected);menu.addAction(tr("关闭所选备份标记"),this,[this]{const auto ids=selectedIds();if(!ids.isEmpty())perform("backup.start",{{"asset_ids",ids},{"enabled",false}});});menu.addAction(tr("移到回收站"),this,&LibraryTab::deleteSelected);menu.exec(m_grid->viewport()->mapToGlobal(pos));}
QJsonObject LibraryTab::session()const{return {{"filter",m_filter},{"history",QJsonArray::fromStringList(m_history)},{"detail",m_pages->currentIndex()==1},{"asset_id",currentAssetId()},{"scroll",m_grid->verticalScrollBar()->value()}};}
void LibraryTab::restoreSession(const QJsonObject &state){m_rebuilding=true;m_filter=state["filter"].toObject();m_search->setText(m_filter["query"].toString());m_status->setCurrentIndex(qMax(0,m_status->findData(m_filter["status"].toString())));m_sort->setCurrentIndex(qMax(0,m_sort->findData(m_filter["sort"].toString())));for(int i=0;i<m_types->count();++i)m_types->item(i)->setSelected(m_filter["types"].toArray().contains(m_types->item(i)->data(Qt::UserRole).toString()));for(int i=0;i<m_tags->count();++i)m_tags->item(i)->setSelected(m_filter["tags"].toArray().contains(m_tags->item(i)->text()));for(QTreeWidgetItemIterator it(m_directories);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==m_filter["directory_id"].toString()){m_directories->setCurrentItem(*it);break;}m_rebuilding=false;m_model->setFilter(m_filter);m_history.clear();for(const auto &v:state["history"].toArray())m_history<<v.toString();if(state["detail"].toBool())openAsset(state["asset_id"].toString(),false);QTimer::singleShot(400,this,[this,state]{m_grid->verticalScrollBar()->setValue(state["scroll"].toInt());});}

void LibraryTab::fitPreviewHeight(){if(!m_detailScroll||!m_detailScroll->isVisible())return;auto host=m_pages->widget(1);const int height=qBound(52,host->height()-500,250);if(m_detailScroll->height()!=height)m_detailScroll->setFixedHeight(height);}
bool LibraryTab::eventFilter(QObject *watched,QEvent *event){if(m_detailScroll&&(watched==m_detailScroll->viewport()||watched==m_pages->widget(1))&&(event->type()==QEvent::Resize||event->type()==QEvent::LayoutRequest||event->type()==QEvent::Show))QTimer::singleShot(0,this,&LibraryTab::fitPreviewHeight);return QWidget::eventFilter(watched,event);}

void LibraryTab::updateLocation(){
    if(!m_locationTitle)return;
    if(m_pages->currentIndex()==1&&!m_current.isEmpty()){m_locationTitle->setTitle(tr("素材库 / %1").arg(m_current["name"].toString()));return;}
    QString scope=tr("全部素材");const auto directory=m_filter["directory_id"].toString();
    for(const auto &entry:m_directoryData){const auto object=entry.toObject();if(object["id"]==directory){scope=object["path"].toString(object["name"].toString());break;}}
    if(m_filter["status"]=="favorite")scope=tr("已收藏");
    m_locationTitle->setTitle(tr("素材库 / %1").arg(scope));
}
void LibraryTab::updateRelatedIcons(){
    for(int i=0;i<m_relations->count();++i){auto item=m_relations->item(i);if(!m_relations->visualItemRect(item).intersects(m_relations->viewport()->rect()))continue;const auto target=item->data(Qt::UserRole).toJsonObject()["target"].toObject();if(!target.isEmpty()){const auto pixmap=m_thumbs->get(target);if(!pixmap.isNull())item->setIcon(pixmap);}}
}

void LibraryTab::updateInspectorPreviews(){
    if(m_current.isEmpty()){m_inspectorSummary->setPreviewPages({});m_inspectorLoader->select({});return;}
    QJsonArray pages{QJsonObject{{"key","main"},{"label",tr("主文件")}}};
    for(const auto &value:m_current["previews"].toArray()){
        const auto preview=value.toObject();if(preview["id"].toString().isEmpty())continue;
        pages.append(QJsonObject{{"key",preview["id"]},{"label",preview["label"].toString(tr("预览图"))}});
    }
    auto preferred=m_current["cover_id"].toString();if(preferred.isEmpty()||preferred=="auto")preferred="main";
    m_inspectorSummary->setPreviewPages(pages,preferred);
    m_inspectorLoader->select(m_inspectorSummary->currentPreviewKey());
}
