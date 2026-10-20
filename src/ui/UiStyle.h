#pragma once

#include <QString>

class QStyle;

// Shared by the desktop application and offscreen widget checks.
QStyle *createUiStyle();
void applyUiTheme(const QString &requested);
