#pragma once

#include <QColor>
#include <QLinearGradient>
#include <QRectF>
#include <QString>

class QPainter;

enum class GlassLayer { Panel, Control, Card };

// One translucent material for stylesheet controls and custom-painted surfaces.
// The window's existing bloom remains visible beneath these fills.
QLinearGradient glassGradient(const QRectF &rect, GlassLayer layer,
                              bool emphasized = false, bool pressed = false);
QString glassStyleGradient(GlassLayer layer, bool emphasized = false,
                           bool pressed = false);
QColor glassEdge(GlassLayer layer, bool emphasized = false, bool pressed = false);
void paintGlassOutline(QPainter &painter, const QRectF &rect, qreal radius,
                       GlassLayer layer, bool emphasized = false,
                       bool pressed = false, bool circular = false);
void paintGlassSurface(QPainter &painter, const QRectF &rect, qreal radius,
                       GlassLayer layer, bool emphasized = false,
                       bool pressed = false, bool circular = false,
                       bool outline = true);
