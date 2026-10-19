#pragma once

#include <QImage>
#include <QRect>
#include <QString>

// Explicit self-tests only. The caller must first validate that physicalBounds
// is wholly inside its own visible, unobscured window on one physical monitor.
// Only this GPU-cropped region is ever transferred to CPU memory.
QImage captureDesktopRegion(const QRect &physicalBounds, QString &error);
