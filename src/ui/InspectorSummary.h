#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QRectF>
#include <QWidget>

class QAbstractButton;
class QLabel;
class QVBoxLayout;

// A presentation-only summary. LibraryTab owns loading and all mutations.
class InspectorSummary : public QWidget {
    Q_OBJECT
public:
    explicit InspectorSummary(QWidget *parent = nullptr);

    void setAsset(const QJsonObject &asset);
    void setPreviewPages(const QJsonArray &pages, const QString &preferredKey = {});
    QString currentPreviewKey() const;
    void setPreviewImage(const QString &key, const QPixmap &image);
    void setPreviewError(const QString &key, const QString &message);
    QRectF previewImageRect() const;
    QSize previewImageSize() const;

signals:
    void previewSelected(const QString &key);
    void favoriteToggled(bool favorite);
    void renameRequested();
    void addTagRequested();
    void removeTagRequested(const QString &tag);

protected:
    void changeEvent(QEvent *event) override;

private:
    void selectPreview(int index);
    void updatePreviewControls();
    void rebuildTags(const QJsonArray &tags);

    QString m_assetId;
    QJsonArray m_pages;
    QJsonArray m_tagValues;
    int m_currentPage = -1;
    QWidget *m_carousel = nullptr;
    QWidget *m_body = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_format = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_source = nullptr;
    QLabel *m_backup = nullptr;
    QAbstractButton *m_favorite = nullptr;
    QWidget *m_tags = nullptr;
};
