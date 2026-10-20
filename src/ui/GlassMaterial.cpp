#include "GlassMaterial.h"
#include "Theme.h"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

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
    QColor body;
    switch (layer) {
    case GlassLayer::Panel:
        body = colors.panel;
        body.setAlpha(colors.light ? 60 : 16);
        break;
    case GlassLayer::Control:
        body = colors.card;
        body.setAlpha(colors.light ? 65 : 14);
        break;
    case GlassLayer::Card:
        body = colors.card;
        body.setAlpha(colors.light ? 104 : 14);
        break;
    }
    if (emphasized || pressed) {
        body = colors.accent;
        body.setAlpha(pressed ? (colors.light ? 55 : 45)
                             : layer == GlassLayer::Card ? (colors.light ? 40 : 36)
                                                         : (colors.light ? 30 : 24));
    }
    // Uniform translucency restores the matte finish. There is no light band.
    return {{0., body}, {.46, body}, {1., body}};
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
    return rgba(stops[0].second);
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
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    if (circular) shape.addEllipse(rect);
    else shape.addRoundedRect(rect, radius, radius);
    painter.setClipPath(shape, Qt::IntersectClip);
    painter.fillRect(rect, glassGradient(rect, layer, emphasized, pressed));

    painter.fillRect(rect, QBrush(frostGrain()));
    painter.restore();
    if (outline) paintGlassOutline(painter, rect, radius, layer,
                                   emphasized, pressed, circular);
}
