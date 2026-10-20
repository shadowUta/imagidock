#include "GlassMaterial.h"
#include "Theme.h"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <array>

namespace {
QColor blend(const QColor &base, const QColor &tint, qreal amount)
{
    return QColor(qRound(base.red() * (1 - amount) + tint.red() * amount),
                  qRound(base.green() * (1 - amount) + tint.green() * amount),
                  qRound(base.blue() * (1 - amount) + tint.blue() * amount));
}

QGradientStops materialStops(GlassLayer layer, bool emphasized, bool pressed)
{
    const auto colors = themeColors();
    std::array<int, 3> opacity;
    switch (layer) {
    case GlassLayer::Panel:
        opacity = colors.light ? std::array{74, 44, 53} : std::array{38, 20, 27};
        break;
    case GlassLayer::Control:
        opacity = colors.light ? std::array{87, 58, 70} : std::array{46, 28, 36};
        break;
    case GlassLayer::Card:
        opacity = colors.light ? std::array{104, 72, 84} : std::array{43, 25, 30};
        break;
    }

    QColor body = blend(colors.panel, colors.glow, colors.light ? .06 : .10);
    if (emphasized || pressed)
        body = blend(body, colors.accent, pressed ? .29 : .18);
    QColor top = blend(body, Qt::white, colors.light ? .32 : .09);
    QColor middle = body;
    QColor bottom = blend(body, colors.bottom, pressed ? .12 : .035);
    const int emphasis = emphasized ? (colors.light ? 15 : 12) : 0;
    top.setAlpha(opacity[0] + emphasis - (pressed ? 16 : 0));
    middle.setAlpha(opacity[1] + emphasis + (pressed ? 15 : 0));
    bottom.setAlpha(opacity[2] + emphasis + (pressed ? 17 : 0));
    // Broad reflection beneath the soft glass edge; never a native focus frame.
    return {{0., top}, {.46, middle}, {1., bottom}};
}

QString rgba(const QColor &color)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

const QPixmap &frostGrain()
{
    // A tiny deterministic tile, generated once per theme, never per widget/frame.
    // Four fixed themes bound the cache; no content capture or animation is involved.
    static QHash<QString, QPixmap> cache;
    const auto colors = themeColors();
    auto found = cache.constFind(colors.id);
    if (found != cache.cend()) return found.value();
    if (cache.size() >= 4) cache.clear();
    QImage tile(64, 64, QImage::Format_ARGB32_Premultiplied);
    quint32 seed = 0x6c8e9cf5u;
    for (int y = 0; y < tile.height(); ++y) {
        for (int x = 0; x < tile.width(); ++x) {
            seed = seed * 1664525u + 1013904223u;
            const int alpha = int((seed >> 27) & 3u);
            const QColor grain = (seed & 0x10000u)
                ? QColor(255, 255, 255, alpha)
                : QColor(0, 0, 0, colors.light ? alpha : alpha / 2);
            tile.setPixelColor(x, y, grain);
        }
    }
    return cache.insert(colors.id, QPixmap::fromImage(tile)).value();
}
}

QLinearGradient glassGradient(const QRectF &rect, GlassLayer layer,
                              bool emphasized, bool pressed)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft()
        + QPointF(rect.width() * .22, 0));
    gradient.setStops(materialStops(layer, emphasized, pressed));
    return gradient;
}

QString glassStyleGradient(GlassLayer layer, bool emphasized, bool pressed)
{
    const auto stops = materialStops(layer, emphasized, pressed);
    return QStringLiteral("qlineargradient(x1:0,y1:0,x2:0.22,y2:1,stop:0 %1,stop:0.46 %2,stop:1 %3)")
        .arg(rgba(stops[0].second), rgba(stops[1].second), rgba(stops[2].second));
}

QColor glassEdge(GlassLayer layer, bool emphasized, bool pressed)
{
    const auto colors = themeColors();
    QColor edge = blend(colors.glow, Qt::white, .90);
    int opacity = colors.light ? 154 : 45;
    if (layer == GlassLayer::Control) opacity = colors.light ? 170 : 54;
    if (layer == GlassLayer::Card) opacity = colors.light ? 162 : 49;
    if (emphasized || pressed) opacity += colors.light ? 14 : 10;
    if (pressed) ++opacity;
    edge.setAlpha(opacity);
    return edge;
}

void paintGlassOutline(QPainter &painter, const QRectF &rect, qreal radius,
                       GlassLayer layer, bool emphasized, bool pressed,
                       bool circular)
{
    if (rect.width() < 1 || rect.height() < 1) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(glassEdge(layer, emphasized, pressed), 1.,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QRectF inner = rect.adjusted(.5, .5, -.5, -.5);
    if (circular) painter.drawEllipse(inner);
    else painter.drawRoundedRect(inner, qMax(0., radius - .5), qMax(0., radius - .5));
    painter.restore();
}

void paintGlassSurface(QPainter &painter, const QRectF &rect, qreal radius,
                       GlassLayer layer, bool emphasized, bool pressed,
                       bool circular, bool outline)
{
    if (rect.isEmpty()) return;
    const auto colors = themeColors();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    if (circular) shape.addEllipse(rect);
    else shape.addRoundedRect(rect, radius, radius);
    painter.setClipPath(shape, Qt::IntersectClip);
    painter.fillRect(rect, glassGradient(rect, layer, emphasized, pressed));

    // A soft reflection occupies an area; it never traces the box's outer edge.
    const qreal spread = qMax(rect.width(), rect.height()) * .82;
    QRadialGradient reflection(rect.topLeft() + QPointF(rect.width() * .21, 0), spread);
    QColor light = blend(colors.glow, Qt::white, .60);
    light.setAlpha(pressed ? 3 : (colors.light ? 10 : 7));
    reflection.setColorAt(0, light);
    light.setAlpha(0);
    reflection.setColorAt(1, light);
    painter.fillRect(rect, reflection);
    painter.fillRect(rect, QBrush(frostGrain()));
    painter.restore();
    if (outline) paintGlassOutline(painter, rect, radius, layer,
                                   emphasized, pressed, circular);
}
