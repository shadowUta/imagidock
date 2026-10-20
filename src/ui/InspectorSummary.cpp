#include "InspectorSummary.h"

#include "Theme.h"
#include "UiIcons.h"
#include "UiMetrics.h"

#include <QAbstractButton>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStringList>
#include <QTextLayout>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {

QString readableSize(qint64 size)
{
    const qreal bytes = std::max<qint64>(0, size);
    if (bytes < 1024)
        return QString::number(qint64(bytes)) + QStringLiteral(" B");
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024, 'f', 1) + QStringLiteral(" KB");
    if (bytes < 1024.0 * 1024 * 1024)
        return QString::number(bytes / (1024 * 1024), 'f', 1) + QStringLiteral(" MB");
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + QStringLiteral(" GB");
}

QFont textFont(const QWidget *widget, int pixelSize, QFont::Weight weight = QFont::Normal)
{
    QFont font = widget->font();
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    return font;
}

// Wrap at character boundaries as needed for paths, CJK names and long tags.
QVector<QTextLine> layoutLines(QTextLayout &layout, int width, int maxLines = 0)
{
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    QVector<QTextLine> lines;
    layout.beginLayout();
    qreal y = 0;
    while (!maxLines || lines.size() < maxLines) {
        auto line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(std::max(1, width));
        line.setPosition(QPointF(0, y));
        y += line.height();
        lines.append(line);
    }
    layout.endLayout();
    return lines;
}

class TitleLabel final : public QLabel {
public:
    using QLabel::QLabel;
    std::function<void()> rename;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        QTextLayout textLayout(text(), textFont(this, 16, QFont::DemiBold));
        const auto lines = layoutLines(textLayout, width, 2);
        return lines.isEmpty() ? 23 : int(std::ceil(lines.last().y() + lines.last().height()));
    }
    QSize sizeHint() const override { return QSize(180, heightForWidth(180)); }
    QSize minimumSizeHint() const override { return QSize(0, 23); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const auto font = textFont(this, 16, QFont::DemiBold);
        QTextLayout textLayout(text(), font);
        const auto lines = layoutLines(textLayout, width(), 2);
        painter.setPen(themeColors().text);
        for (int i = 0; i < lines.size(); ++i) {
            const auto &line = lines.at(i);
            if (i == lines.size() - 1 && line.textStart() + line.textLength() < text().size()) {
                const auto remaining = text().mid(line.textStart());
                const auto elided = QFontMetrics(font).elidedText(remaining, Qt::ElideRight, width());
                painter.setFont(font);
                painter.drawText(QPointF(0, line.y() + line.ascent()), elided);
            } else {
                line.draw(&painter, QPointF());
            }
        }
        if (hasFocus()) {
            QColor edge = themeColors().accent;
            edge.setAlpha(150);
            painter.setPen(QPen(edge, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 4, 4);
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rename) {
            rename();
            event->accept();
            return;
        }
        QLabel::mouseDoubleClickEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_F2 && rename) {
            rename();
            event->accept();
            return;
        }
        QLabel::keyPressEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu;
        QPointer<TitleLabel> guard(this);
        const auto callback = rename;
        const auto action = menu.addAction(tr("重命名"));
        if (menu.exec(event->globalPos()) == action && guard && callback)
            callback();
    }
};

class MutedLabel final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(themeColors().muted);
        painter.setFont(textFont(this, 11));
        painter.drawText(rect(), alignment(), text());
    }
};

class StatusLabel final : public QLabel {
public:
    explicit StatusLabel(QWidget *parent) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setFixedHeight(24);
    }
    void setState(bool valid, const QString &text, const QString &description)
    {
        setText(text);
        setProperty("valid", valid);
        setProperty("text", text);
        setAccessibleName(text);
        setToolTip(description);
        updateGeometry();
        update();
    }
    QSize sizeHint() const override
    {
        return QSize(21 + QFontMetrics(textFont(this, 11)).horizontalAdvance(text()), 24);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto colors = themeColors();
        const bool valid = property("valid").toBool();
        const QColor color = valid ? (colors.light ? QColor("#428467") : QColor("#8acbaa"))
                                   : (colors.light ? QColor("#b36368") : QColor("#e09aa1"));
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const qreal cy = height() / 2.0;
        painter.drawEllipse(QPointF(7.5, cy), 6.25, 6.25);
        if (valid) {
            QPainterPath tick;
            tick.moveTo(4.5, cy);
            tick.lineTo(6.5, cy + 2);
            tick.lineTo(10.5, cy - 2.5);
            painter.drawPath(tick);
        } else {
            painter.drawLine(QPointF(5.5, cy - 2), QPointF(9.5, cy + 2));
            painter.drawLine(QPointF(9.5, cy - 2), QPointF(5.5, cy + 2));
        }
        painter.setFont(textFont(this, 11));
        painter.drawText(QRect(21, 0, width() - 21, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
    }
};

enum class ButtonKind { Previous, Next, Favorite, AddTag, Dot };

class SummaryButton final : public QAbstractButton {
public:
    SummaryButton(ButtonKind kind, QWidget *parent) : QAbstractButton(parent), m_kind(kind)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(kind == ButtonKind::Dot ? QSize(18, 22) : QSize(28, 28));
        if (kind == ButtonKind::Favorite)
            setCheckable(true);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto colors = themeColors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (m_kind == ButtonKind::Dot) {
            auto color = property("selected").toBool() ? colors.text : colors.muted;
            if (!property("selected").toBool())
                color.setAlphaF(.55);
            QColor backing = colors.top;
            backing.setAlpha(125);
            painter.setPen(Qt::NoPen);
            painter.setBrush(backing);
            painter.drawEllipse(QPointF(width() / 2., height() / 2.), 5, 5);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(width() / 2., height() / 2.),
                                property("selected").toBool() ? 3.5 : 2.5,
                                property("selected").toBool() ? 3.5 : 2.5);
            if (hasFocus()) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(colors.accent, 1));
                painter.drawEllipse(QPointF(width() / 2., height() / 2.), 7, 7);
            }
            return;
        }
        const bool circular = m_kind == ButtonKind::AddTag;
        const bool overlay = m_kind == ButtonKind::Previous || m_kind == ButtonKind::Next;
        QColor fill = colors.card;
        if (overlay) {
            fill = colors.top;
            fill.setAlpha(205);
        } else if (!underMouse() && !isDown() && !isChecked()) {
            fill.setAlpha(0);
        }
        QColor edge = colors.edge;
        if (m_kind == ButtonKind::Favorite && !hasFocus())
            edge.setAlpha(0);
        painter.setPen(QPen(hasFocus() ? colors.accent : edge, 1));
        painter.setBrush(fill);
        const auto bounds = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
        if (circular)
            painter.drawEllipse(bounds);
        else
            painter.drawRoundedRect(bounds, UiMetrics::ControlRadius, UiMetrics::ControlRadius);
        const auto iconBounds = QRect(6, 6, 16, 16);
        if (m_kind == ButtonKind::Favorite) {
            painter.translate(6, 6);
            QPainterPath star;
            star.moveTo(8, 1.75);
            star.lineTo(9.85, 5.75);
            star.lineTo(14.25, 6.25);
            star.lineTo(11, 9.3);
            star.lineTo(11.9, 13.75);
            star.lineTo(8, 11.55);
            star.lineTo(4.1, 13.75);
            star.lineTo(5, 9.3);
            star.lineTo(1.75, 6.25);
            star.lineTo(6.15, 5.75);
            star.closeSubpath();
            painter.setPen(QPen(isChecked() ? colors.accent : colors.muted, 1.5,
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(isChecked() ? QBrush(colors.accent) : QBrush(Qt::NoBrush));
            painter.drawPath(star);
        } else if (m_kind == ButtonKind::AddTag) {
            uiIcon("plus").paint(&painter, iconBounds);
        } else {
            painter.setPen(QPen(colors.text, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const qreal start = m_kind == ButtonKind::Previous ? 16 : 12;
            const qreal end = m_kind == ButtonKind::Previous ? 12 : 16;
            painter.drawLine(QPointF(start, 9), QPointF(end, 14));
            painter.drawLine(QPointF(end, 14), QPointF(start, 19));
        }
    }
private:
    ButtonKind m_kind;
};

class TagButton final : public QAbstractButton {
public:
    explicit TagButton(const QString &tag, QWidget *parent) : QAbstractButton(parent)
    {
        setText(tag);
        setToolTip(tag + tr("\n右键移除标签"));
        setAccessibleName(tr("标签：%1").arg(tag));
        setProperty("tagName", tag);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        auto policy = QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    std::function<void()> remove;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        QTextLayout layout(text(), textFont(this, 11));
        const auto lines = layoutLines(layout, std::max(1, width - 16));
        return std::max(28, lines.isEmpty() ? 28 : int(std::ceil(lines.last().y() + lines.last().height())) + 12);
    }
    QSize sizeHint() const override
    {
        const int width = std::min(220, QFontMetrics(textFont(this, 11)).horizontalAdvance(text()) + 16);
        return QSize(width, heightForWidth(width));
    }
    QSize minimumSizeHint() const override { return QSize(28, 28); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto colors = themeColors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor background = colors.card;
        background.setAlpha(colors.light ? (underMouse() ? 165 : 95) : (underMouse() ? 30 : 14));
        auto edge = colors.edge;
        edge.setAlpha(colors.light ? 165 : 35);
        painter.setPen(QPen(hasFocus() ? colors.accent : edge, 1));
        painter.setBrush(background);
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5),
                                UiMetrics::ControlRadius, UiMetrics::ControlRadius);
        painter.setPen(colors.text);
        QTextLayout layout(text(), textFont(this, 11));
        layoutLines(layout, std::max(1, width() - 16));
        layout.draw(&painter, QPointF(8, 6));
    }
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu;
        QPointer<TagButton> guard(this);
        const auto callback = remove;
        const auto action = menu.addAction(tr("移除标签"));
        if (menu.exec(event->globalPos()) == action && guard && callback)
            callback();
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && remove) {
            remove();
            event->accept();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }
};

class TagFlowLayout final : public QLayout {
public:
    explicit TagFlowLayout(QWidget *parent) : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(6);
    }
    ~TagFlowLayout() override
    {
        while (auto item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override { return QSize(160, heightForWidth(160)); }
    QSize minimumSize() const override { return QSize(28, m_items.isEmpty() ? 0 : 28); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }
private:
    int arrange(const QRect &bounds, bool apply) const
    {
        int x = bounds.x(), y = bounds.y(), lineHeight = 0;
        const int available = std::max(1, bounds.width());
        for (auto item : m_items) {
            const int width = std::min(available, item->sizeHint().width());
            const int height = item->hasHeightForWidth() ? item->heightForWidth(width) : item->sizeHint().height();
            if (x != bounds.x() && x + width > bounds.x() + available) {
                x = bounds.x();
                y += lineHeight + spacing();
                lineHeight = 0;
            }
            if (apply)
                item->setGeometry(QRect(x, y, width, height));
            x += width + spacing();
            lineHeight = std::max(lineHeight, height);
        }
        return m_items.isEmpty() ? 0 : y - bounds.y() + lineHeight;
    }
    QList<QLayoutItem *> m_items;
};

class Carousel final : public QWidget {
public:
    explicit Carousel(QWidget *parent) : QWidget(parent)
    {
        setObjectName("inspectorCarousel");
        auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
        setFocusPolicy(Qt::StrongFocus);
        previous = new SummaryButton(ButtonKind::Previous, this);
        previous->setObjectName("inspectorPreviewPrevious");
        previous->setToolTip(tr("上一张预览图"));
        previous->setAccessibleName(tr("上一张预览图"));
        next = new SummaryButton(ButtonKind::Next, this);
        next->setObjectName("inspectorPreviewNext");
        next->setToolTip(tr("下一张预览图"));
        next->setAccessibleName(tr("下一张预览图"));
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return std::max(1, width); }
    QSize sizeHint() const override { return QSize(280, 280); }
    QSize minimumSizeHint() const override { return QSize(1, 1); }

    QPixmap image;
    QString message;
    bool hasAsset = false;
    int selectedPage = -1;
    SummaryButton *previous = nullptr;
    SummaryButton *next = nullptr;
    QList<SummaryButton *> dots;
    std::function<void(int)> select;

    QRectF imageRect() const
    {
        if (image.isNull())
            return {};
        const QSizeF fitted = image.deviceIndependentSize().scaled(size(), Qt::KeepAspectRatio);
        return QRectF((width() - fitted.width()) / 2., (height() - fitted.height()) / 2.,
                      fitted.width(), fitted.height());
    }

    void arrangeControls()
    {
        previous->move(10, std::max(0, (height() - previous->height()) / 2));
        next->move(std::max(0, width() - next->width() - 10), std::max(0, (height() - next->height()) / 2));
        const int visibleCount = std::min(int(dots.size()), std::max(1, (width() - 24) / 18));
        const int start = std::clamp(selectedPage - visibleCount / 2, 0, std::max(0, int(dots.size()) - visibleCount));
        const int x = (width() - visibleCount * 18) / 2;
        for (int i = 0; i < dots.size(); ++i) {
            dots[i]->setVisible(hasAsset && !dots.isEmpty() && i >= start && i < start + visibleCount);
            dots[i]->setGeometry(x + (i - start) * 18, height() - 28, 18, 22);
        }
        previous->setVisible(hasAsset && dots.size() > 1);
        next->setVisible(hasAsset && dots.size() > 1);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (minimumHeight() != width() || maximumHeight() != width())
            setFixedHeight(width());
        arrangeControls();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.moveTo(0, height());
        clip.lineTo(0, UiMetrics::PanelRadius);
        clip.quadTo(0, 0, UiMetrics::PanelRadius, 0);
        clip.lineTo(width() - UiMetrics::PanelRadius, 0);
        clip.quadTo(width(), 0, width(), UiMetrics::PanelRadius);
        clip.lineTo(width(), height());
        clip.closeSubpath();
        painter.setClipPath(clip);
        if (!image.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawPixmap(imageRect(), image, QRectF(image.rect()));
        } else {
            painter.setPen(themeColors().muted);
            painter.setFont(textFont(this, 12));
            painter.drawText(rect().adjusted(20, 24, -20, -32), Qt::AlignCenter | Qt::TextWordWrap,
                             hasAsset ? (message.isEmpty() ? tr("正在加载预览…") : message)
                                      : tr("单击卡片查看信息"));
        }
        if (hasFocus()) {
            auto color = themeColors().accent;
            color.setAlpha(130);
            painter.setPen(QPen(color, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5));
        }
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (hasAsset && dots.size() > 1 && select &&
            (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
            select(selectedPage + (event->key() == Qt::Key_Left ? -1 : 1));
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }
};

} // namespace

InspectorSummary::InspectorSummary(QWidget *parent) : QWidget(parent)
{
    setObjectName("inspectorSummary");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);
    auto carousel = new Carousel(this);
    m_carousel = carousel;
    layout->addWidget(carousel);

    m_body = new QWidget(this);
    m_body->setObjectName("inspectorSummaryBody");
    auto bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(UiMetrics::PanelInset, 14, UiMetrics::PanelInset, UiMetrics::PanelInset);
    bodyLayout->setSpacing(UiMetrics::Gap);
    bodyLayout->setAlignment(Qt::AlignTop);
    auto titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(6);
    auto title = new TitleLabel(m_body);
    title->setObjectName("inspectorAssetTitle");
    title->setTextFormat(Qt::PlainText);
    title->setFont(textFont(title, 16, QFont::Bold));
    title->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    title->setFocusPolicy(Qt::StrongFocus);
    auto titlePolicy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titlePolicy.setHeightForWidth(true);
    title->setSizePolicy(titlePolicy);
    title->rename = [this] { emit renameRequested(); };
    m_title = title;
    titleRow->addWidget(title, 1);
    m_favorite = new SummaryButton(ButtonKind::Favorite, m_body);
    m_favorite->setObjectName("inspectorFavorite");
    titleRow->addWidget(m_favorite, 0, Qt::AlignTop);
    connect(m_favorite, &QAbstractButton::toggled, this, [this](bool favorite) {
        m_favorite->setToolTip(favorite ? tr("取消收藏") : tr("收藏资产"));
        m_favorite->setAccessibleName(m_favorite->toolTip());
        emit favoriteToggled(favorite);
    });
    bodyLayout->addLayout(titleRow);

    auto metrics = new QHBoxLayout;
    metrics->setContentsMargins(0, 0, 0, 0);
    metrics->setSpacing(8);
    m_size = new MutedLabel(m_body);
    m_size->setObjectName("inspectorFileSize");
    m_size->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_size->setFixedHeight(18);
    m_format = new MutedLabel(m_body);
    m_format->setObjectName("inspectorFormat");
    m_format->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_format->setFixedHeight(18);
    metrics->addWidget(m_size, 1);
    metrics->addWidget(m_format);
    bodyLayout->addLayout(metrics);

    auto statuses = new QHBoxLayout;
    statuses->setContentsMargins(0, 0, 0, 0);
    statuses->setSpacing(12);
    m_source = new StatusLabel(m_body);
    m_source->setObjectName("inspectorSourceStatus");
    m_backup = new StatusLabel(m_body);
    m_backup->setObjectName("inspectorBackupStatus");
    statuses->addWidget(m_source);
    statuses->addWidget(m_backup);
    statuses->addStretch(1);
    bodyLayout->addLayout(statuses);

    m_tags = new QWidget(m_body);
    m_tags->setObjectName("inspectorTags");
    m_tags->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    new TagFlowLayout(m_tags);
    bodyLayout->addWidget(m_tags);
    layout->addWidget(m_body);

    carousel->select = [this](int page) { selectPreview(page); };
    connect(carousel->previous, &QAbstractButton::clicked, this, [this] { selectPreview(m_currentPage - 1); });
    connect(carousel->next, &QAbstractButton::clicked, this, [this] { selectPreview(m_currentPage + 1); });
    setAsset({});
}

void InspectorSummary::setAsset(const QJsonObject &asset)
{
    const bool exists = !asset.isEmpty();
    const QString assetId = asset["id"].toString();
    auto carousel = static_cast<Carousel *>(m_carousel);
    if (m_assetId != assetId || !exists) {
        m_assetId = assetId;
        m_pages = {};
        m_currentPage = -1;
        carousel->image = {};
        carousel->message.clear();
        carousel->setProperty("displayedPreviewKey", QString());
        updatePreviewControls();
    }
    carousel->hasAsset = exists;
    m_body->setVisible(exists);
    m_title->setText(asset["name"].toString());
    m_title->setAccessibleName(asset["name"].toString());
    m_title->setToolTip(asset["name"].toString() + tr("\n双击或按 F2 重命名"));
    m_title->updateGeometry();
    m_size->setText(readableSize(asset["size"].toVariant().toLongLong()));
    m_format->setText(asset["extension"].toString().toUpper());
    m_size->setAccessibleName(tr("文件大小：%1").arg(m_size->text()));
    m_format->setAccessibleName(tr("文件格式：%1").arg(m_format->text()));
    const bool favorite = asset["favorite"].toBool();
    {
        QSignalBlocker blocker(m_favorite);
        m_favorite->setChecked(favorite);
    }
    m_favorite->setToolTip(favorite ? tr("取消收藏") : tr("收藏资产"));
    m_favorite->setAccessibleName(m_favorite->toolTip());
    const bool validSource = !asset["missing"].toBool() && !asset["deleted"].toBool() && !asset["placeholder"].toBool();
    static_cast<StatusLabel *>(m_source)->setState(validSource, validSource ? tr("来源有效") : tr("来源失效"),
        asset["placeholder"].toBool() ? tr("库内占位记录，来源文件不可用")
                                    : asset["deleted"].toBool() ? tr("资产位于回收站") : asset["source_path"].toString());
    const auto backup = asset["backup_state"].toString();
    bool corrupt = false;
    for (const auto &value : asset["issues"].toArray())
        corrupt |= value.toObject()["kind"].toString() == QLatin1String("backup_corrupt");
    const bool hasBackup = !corrupt && (backup == QLatin1String("ready") || backup == QLatin1String("stale"));
    const auto backupText = corrupt ? tr("备份失效") : hasBackup ? tr("已备份")
                           : backup == QLatin1String("failed") ? tr("备份失败") : tr("尚未备份");
    const auto backupDescription = corrupt ? tr("备份损坏，请在错误中心修复")
        : backup == QLatin1String("stale") ? tr("已有备份可取用；来源已变化，建议更新备份")
        : hasBackup ? tr("已备份，可从库中取用文件")
        : backup == QLatin1String("failed") ? tr("备份未完成，请重试或查看错误中心") : tr("尚未创建备份");
    static_cast<StatusLabel *>(m_backup)->setState(hasBackup, backupText, backupDescription);
    const auto tags = asset["tags"].toArray();
    if (tags != m_tagValues || m_tags->layout()->count() == 0) {
        m_tagValues = tags;
        rebuildTags(tags);
    }
    carousel->arrangeControls();
    carousel->update();
    updateGeometry();
}

void InspectorSummary::setPreviewPages(const QJsonArray &pages, const QString &preferredKey)
{
    const QString previousKey = currentPreviewKey();
    QJsonArray cleaned;
    QStringList seen;
    for (const auto &value : pages) {
        const auto object = value.toObject();
        const QString key = object["key"].toString();
        if (!key.isEmpty() && !seen.contains(key)) {
            seen.append(key);
            cleaned.append(object);
        }
    }
    // Unrelated library refreshes must not destroy the currently focused dot.
    if (cleaned == m_pages && m_currentPage >= 0 && m_currentPage < m_pages.size())
        return;
    m_pages = cleaned;
    int selected = seen.indexOf(previousKey);
    if (selected < 0)
        selected = seen.indexOf(preferredKey);
    if (selected < 0 && !m_pages.isEmpty())
        selected = 0;
    m_currentPage = selected;
    auto carousel = static_cast<Carousel *>(m_carousel);
    if (previousKey != currentPreviewKey()) {
        carousel->image = {};
        carousel->message.clear();
        carousel->setProperty("displayedPreviewKey", QString());
    }
    updatePreviewControls();
    if (currentPreviewKey().isEmpty()) {
        carousel->message = tr("暂无预览图");
        carousel->update();
    } else if (previousKey != currentPreviewKey()) {
        emit previewSelected(currentPreviewKey());
    }
}

QString InspectorSummary::currentPreviewKey() const
{
    return m_currentPage >= 0 && m_currentPage < m_pages.size()
        ? m_pages[m_currentPage].toObject()["key"].toString() : QString();
}

void InspectorSummary::setPreviewImage(const QString &key, const QPixmap &image)
{
    if (key.isEmpty() || key != currentPreviewKey())
        return;
    auto carousel = static_cast<Carousel *>(m_carousel);
    carousel->image = image;
    carousel->message = image.isNull() ? tr("无法读取预览图") : QString();
    carousel->setProperty("displayedPreviewKey", image.isNull() ? QString() : key);
    carousel->update();
}

void InspectorSummary::setPreviewError(const QString &key, const QString &message)
{
    if (key.isEmpty() || key != currentPreviewKey())
        return;
    auto carousel = static_cast<Carousel *>(m_carousel);
    carousel->image = {};
    carousel->message = message.isEmpty() ? tr("无法读取预览图") : message;
    carousel->setProperty("displayedPreviewKey", QString());
    carousel->update();
}

QRectF InspectorSummary::previewImageRect() const
{
    const auto carousel = static_cast<const Carousel *>(m_carousel);
    return carousel->imageRect();
}

QSize InspectorSummary::previewImageSize() const
{
    return static_cast<const Carousel *>(m_carousel)->image.size();
}

void InspectorSummary::selectPreview(int index)
{
    if (m_pages.size() < 2)
        return;
    index = (index % m_pages.size() + m_pages.size()) % m_pages.size();
    if (index == m_currentPage)
        return;
    m_currentPage = index;
    auto carousel = static_cast<Carousel *>(m_carousel);
    carousel->image = {};
    carousel->message.clear();
    carousel->setProperty("displayedPreviewKey", QString());
    carousel->selectedPage = index;
    carousel->setProperty("currentPreviewKey", currentPreviewKey());
    for (int i = 0; i < carousel->dots.size(); ++i) {
        carousel->dots[i]->setProperty("selected", i == index);
        carousel->dots[i]->setChecked(i == index);
        carousel->dots[i]->update();
    }
    carousel->setAccessibleDescription(tr("第 %1 张，共 %2 张预览图").arg(index + 1).arg(m_pages.size()));
    carousel->arrangeControls();
    carousel->update();
    emit previewSelected(currentPreviewKey());
}

void InspectorSummary::updatePreviewControls()
{
    auto carousel = static_cast<Carousel *>(m_carousel);
    qDeleteAll(carousel->dots);
    carousel->dots.clear();
    for (int i = 0; i < m_pages.size(); ++i) {
        auto dot = new SummaryButton(ButtonKind::Dot, carousel);
        dot->setObjectName(QStringLiteral("inspectorPreviewDot_%1").arg(i));
        dot->setCheckable(true);
        dot->setAutoExclusive(true);
        dot->setChecked(i == m_currentPage);
        dot->setProperty("selected", i == m_currentPage);
        const auto label = m_pages[i].toObject()["label"].toString();
        const QString description = tr("第 %1 张，共 %2 张：%3").arg(i + 1).arg(m_pages.size()).arg(label);
        dot->setToolTip(description);
        dot->setAccessibleName(description);
        connect(dot, &QAbstractButton::clicked, this, [this, i] { selectPreview(i); });
        carousel->dots.append(dot);
    }
    carousel->selectedPage = m_currentPage;
    carousel->setProperty("currentPreviewKey", currentPreviewKey());
    carousel->setAccessibleDescription(m_pages.isEmpty() ? tr("暂无预览图")
        : tr("第 %1 张，共 %2 张预览图").arg(m_currentPage + 1).arg(m_pages.size()));
    carousel->arrangeControls();
    carousel->update();
}

void InspectorSummary::rebuildTags(const QJsonArray &tags)
{
    auto flow = m_tags->layout();
    while (auto item = flow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int index = 0;
    for (const auto &value : tags) {
        const QString tag = value.toString();
        if (tag.isEmpty())
            continue;
        auto chip = new TagButton(tag, m_tags);
        chip->setObjectName(QStringLiteral("inspectorTag_%1").arg(index++));
        chip->remove = [this, tag] { emit removeTagRequested(tag); };
        flow->addWidget(chip);
    }
    auto add = new SummaryButton(ButtonKind::AddTag, m_tags);
    add->setObjectName("inspectorAddTag");
    add->setAccessibleName(tr("添加标签"));
    add->setToolTip(tr("添加标签"));
    connect(add, &QAbstractButton::clicked, this, &InspectorSummary::addTagRequested);
    flow->addWidget(add);
    flow->invalidate();
    m_tags->updateGeometry();
}

void InspectorSummary::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        updateGeometry();
        for (auto child : findChildren<QWidget *>())
            child->update();
        update();
    }
}
