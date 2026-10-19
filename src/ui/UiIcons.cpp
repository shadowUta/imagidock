#include "UiIcons.h"
#include "Theme.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>

#include <algorithm>
#include <utility>

namespace {

void drawIcon(QPainter &p, const QString &name)
{
    const auto line = [&p](qreal x1, qreal y1, qreal x2, qreal y2) {
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    const auto roundedBox = [&p](qreal x, qreal y, qreal width, qreal height, qreal radius = 1.5) {
        p.drawRoundedRect(QRectF(x, y, width, height), radius, radius);
    };
    const auto folder = [&p] {
        QPainterPath path;
        path.moveTo(2, 4.5);
        path.quadTo(2, 3, 3.5, 3);
        path.lineTo(6.25, 3);
        path.lineTo(7.75, 4.5);
        path.lineTo(12.5, 4.5);
        path.quadTo(14, 4.5, 14, 6);
        path.lineTo(14, 11.5);
        path.quadTo(14, 13, 12.5, 13);
        path.lineTo(3.5, 13);
        path.quadTo(2, 13, 2, 11.5);
        path.closeSubpath();
        p.drawPath(path);
    };

    if (name == QLatin1String("back")) {
        line(13.25, 8, 2.75, 8);
        line(7, 3.75, 2.75, 8);
        line(2.75, 8, 7, 12.25);
    } else if (name == QLatin1String("home")) {
        QPainterPath path;
        path.moveTo(1.75, 7.25);
        path.lineTo(8, 2);
        path.lineTo(14.25, 7.25);
        path.moveTo(3.25, 6.25);
        path.lineTo(3.25, 13.5);
        path.lineTo(6.25, 13.5);
        path.lineTo(6.25, 9);
        path.lineTo(9.75, 9);
        path.lineTo(9.75, 13.5);
        path.lineTo(12.75, 13.5);
        path.lineTo(12.75, 6.25);
        p.drawPath(path);
    } else if (name == QLatin1String("plus")) {
        line(8, 3.25, 8, 12.75);
        line(3.25, 8, 12.75, 8);
    } else if (name == QLatin1String("folder") || name == QLatin1String("folder-add")) {
        folder();
        if (name == QLatin1String("folder-add")) {
            line(8, 6.75, 8, 10.75);
            line(6, 8.75, 10, 8.75);
        }
    } else if (name == QLatin1String("dots")) {
        const auto color = p.pen().color();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (const qreal x : {3.5, 8.0, 12.5})
            p.drawEllipse(QPointF(x, 8), 1, 1);
    } else if (name == QLatin1String("search")) {
        p.drawEllipse(QRectF(2.25, 2.25, 8.5, 8.5));
        line(9.5, 9.5, 13.75, 13.75);
    } else if (name == QLatin1String("grid")) {
        for (const qreal x : {2.5, 9.0})
            for (const qreal y : {2.5, 9.0})
                roundedBox(x, y, 4.5, 4.5, 0.9);
    } else if (name == QLatin1String("image")) {
        roundedBox(2, 2.5, 12, 11, 1.5);
        p.drawEllipse(QPointF(5.25, 5.75), 1, 1);
        QPainterPath path;
        path.moveTo(2.5, 11);
        path.lineTo(6.5, 7.5);
        path.lineTo(9, 10);
        path.lineTo(11, 8.25);
        path.lineTo(13.5, 10.5);
        p.drawPath(path);
    } else if (name == QLatin1String("model")) {
        QPainterPath path;
        path.moveTo(8, 1.75);
        path.lineTo(13.5, 4.75);
        path.lineTo(13.5, 11.25);
        path.lineTo(8, 14.25);
        path.lineTo(2.5, 11.25);
        path.lineTo(2.5, 4.75);
        path.closeSubpath();
        path.moveTo(2.5, 4.75);
        path.lineTo(8, 7.75);
        path.lineTo(13.5, 4.75);
        path.moveTo(8, 7.75);
        path.lineTo(8, 14.25);
        p.drawPath(path);
    } else if (name == QLatin1String("video")) {
        roundedBox(1.75, 3.5, 8.75, 9, 1.5);
        QPainterPath path;
        path.moveTo(10.5, 6.25);
        path.lineTo(14.25, 4.5);
        path.lineTo(14.25, 11.5);
        path.lineTo(10.5, 9.75);
        p.drawPath(path);
    } else if (name == QLatin1String("audio")) {
        line(6, 11.25, 6, 3.5);
        line(6, 3.5, 12.5, 2.25);
        line(12.5, 2.25, 12.5, 10);
        p.drawEllipse(QRectF(2, 10, 4, 3));
        p.drawEllipse(QRectF(8.5, 8.75, 4, 3));
    } else if (name == QLatin1String("source")) {
        QPainterPath path;
        path.moveTo(9.25, 1.75);
        path.lineTo(13, 5.5);
        path.lineTo(13, 12.75);
        path.quadTo(13, 14, 11.75, 14);
        path.lineTo(4.25, 14);
        path.quadTo(3, 14, 3, 12.75);
        path.lineTo(3, 3);
        path.quadTo(3, 1.75, 4.25, 1.75);
        path.closeSubpath();
        path.moveTo(9, 2);
        path.lineTo(9, 5.75);
        path.lineTo(12.75, 5.75);
        p.drawPath(path);
        line(6.5, 8, 5.25, 9.25);
        line(5.25, 9.25, 6.5, 10.5);
        line(9.5, 8, 10.75, 9.25);
        line(10.75, 9.25, 9.5, 10.5);
    } else if (name == QLatin1String("tag")) {
        QPainterPath path;
        path.moveTo(2.25, 2.25);
        path.lineTo(8, 2.25);
        path.lineTo(13.75, 8);
        path.quadTo(14.25, 8.5, 13.75, 9);
        path.lineTo(9, 13.75);
        path.quadTo(8.5, 14.25, 8, 13.75);
        path.lineTo(2.25, 8);
        path.closeSubpath();
        p.drawPath(path);
        p.drawEllipse(QPointF(5.25, 5.25), 0.85, 0.85);
    } else if (name == QLatin1String("star")) {
        QPainterPath path;
        path.moveTo(8, 1.75);
        path.lineTo(9.85, 5.75);
        path.lineTo(14.25, 6.25);
        path.lineTo(11, 9.3);
        path.lineTo(11.9, 13.75);
        path.lineTo(8, 11.55);
        path.lineTo(4.1, 13.75);
        path.lineTo(5, 9.3);
        path.lineTo(1.75, 6.25);
        path.lineTo(6.15, 5.75);
        path.closeSubpath();
        p.drawPath(path);
    } else if (name == QLatin1String("backup")) {
        roundedBox(2, 4.75, 12, 8.75, 1.5);
        roundedBox(3.5, 2.25, 9, 2.5, 0.75);
        line(8, 7, 8, 11);
        line(6.25, 9.25, 8, 11);
        line(8, 11, 9.75, 9.25);
    } else if (name == QLatin1String("open")) {
        QPainterPath path;
        path.moveTo(7, 3);
        path.lineTo(3.5, 3);
        path.quadTo(2, 3, 2, 4.5);
        path.lineTo(2, 12.5);
        path.quadTo(2, 14, 3.5, 14);
        path.lineTo(11.5, 14);
        path.quadTo(13, 14, 13, 12.5);
        path.lineTo(13, 9);
        path.moveTo(8.5, 2);
        path.lineTo(14, 2);
        path.lineTo(14, 7.5);
        path.moveTo(14, 2);
        path.lineTo(7, 9);
        p.drawPath(path);
    } else if (name == QLatin1String("download")) {
        line(8, 2, 8, 10);
        line(4.75, 7, 8, 10.25);
        line(8, 10.25, 11.25, 7);
        QPainterPath path;
        path.moveTo(2.5, 10.25);
        path.lineTo(2.5, 12.5);
        path.quadTo(2.5, 14, 4, 14);
        path.lineTo(12, 14);
        path.quadTo(13.5, 14, 13.5, 12.5);
        path.lineTo(13.5, 10.25);
        p.drawPath(path);
    } else if (name == QLatin1String("settings")) {
        QPainterPath path;
        path.moveTo(6.5, 1.75);
        path.lineTo(9.5, 1.75);
        path.lineTo(10, 3.5);
        path.lineTo(11.25, 4.25);
        path.lineTo(13, 3.75);
        path.lineTo(14.5, 6.25);
        path.lineTo(13.25, 7.5);
        path.lineTo(13.25, 8.5);
        path.lineTo(14.5, 9.75);
        path.lineTo(13, 12.25);
        path.lineTo(11.25, 11.75);
        path.lineTo(10, 12.5);
        path.lineTo(9.5, 14.25);
        path.lineTo(6.5, 14.25);
        path.lineTo(6, 12.5);
        path.lineTo(4.75, 11.75);
        path.lineTo(3, 12.25);
        path.lineTo(1.5, 9.75);
        path.lineTo(2.75, 8.5);
        path.lineTo(2.75, 7.5);
        path.lineTo(1.5, 6.25);
        path.lineTo(3, 3.75);
        path.lineTo(4.75, 4.25);
        path.lineTo(6, 3.5);
        path.closeSubpath();
        p.drawPath(path);
        p.drawEllipse(QPointF(8, 8), 2.25, 2.25);
    } else if (name == QLatin1String("minimize")) {
        line(3.25, 8, 12.75, 8);
    } else if (name == QLatin1String("maximize")) {
        roundedBox(3, 3, 10, 10, 0.5);
    } else if (name == QLatin1String("restore")) {
        QPainterPath path;
        path.moveTo(5.5, 3.5);
        path.lineTo(5.5, 2);
        path.lineTo(14, 2);
        path.lineTo(14, 10.5);
        path.lineTo(12.5, 10.5);
        p.drawPath(path);
        roundedBox(2, 5.5, 8.5, 8.5, 0.5);
    } else if (name == QLatin1String("close")) {
        line(3.75, 3.75, 12.25, 12.25);
        line(12.25, 3.75, 3.75, 12.25);
    } else {
        roundedBox(3, 3, 10, 10, 2);
    }
}

class UiIconEngine final : public QIconEngine {
public:
    explicit UiIconEngine(QString name) : name_(std::move(name)) {}

    QIconEngine *clone() const override { return new UiIconEngine(name_); }
    QString key() const override { return QStringLiteral("ImagiDock.UiIcon.") + name_; }
    QString iconName() override { return name_; }
    bool isNull() override { return false; }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        if (!painter || rect.isEmpty())
            return;
        const auto colors = themeColors();
        auto color = mode == QIcon::Disabled ? colors.muted : colors.text;
        if (mode == QIcon::Disabled)
            color.setAlphaF(color.alphaF() * 0.58);
        const qreal side = std::min(rect.width(), rect.height());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - side) / 2,
                           rect.y() + (rect.height() - side) / 2);
        painter->scale(side / 16, side / 16);
        painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        drawIcon(*painter, name_);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override
    {
        if (size.isEmpty() || scale <= 0)
            return {};
        QPixmap result(QSize(qRound(size.width() * scale), qRound(size.height() * scale)));
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

private:
    QString name_;
};

} // namespace

QIcon uiIcon(const char *name)
{
    return QIcon(new UiIconEngine(QString::fromLatin1(name ? name : "")));
}
