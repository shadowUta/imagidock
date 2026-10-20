#include "UiStyle.h"
#include "Theme.h"
#include "UiMetrics.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTreeView>

namespace {
class SurfaceStyle final : public QProxyStyle {
public:
    SurfaceStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override
    {
        // QStyleSheetStyle asks the base style to fill translucent tree rows
        // first. Fusion's opaque Highlight would show through as a branch square.
        if (element == PE_PanelItemViewRow && qobject_cast<const QTreeView *>(widget)) return;
        if (element == PE_PanelItemViewItem && qobject_cast<const QTreeView *>(widget)
            && option && (option->state & (State_Selected | State_HasFocus | State_MouseOver))) {
            const auto colors = themeColors();
            const bool active = option->state & (State_Selected | State_HasFocus);
            QColor fill = active ? colors.accent : colors.card;
            if (active) fill.setAlpha(38);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(option->rect, UiMetrics::ControlRadius, UiMetrics::ControlRadius);
            painter->restore();
            return;
        }
        if (element == PE_IndicatorBranch) {
            // Draw only the chevron so the tree's rounded row remains the sole fill.
            if (!option || !(option->state & State_Children)) return;
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const auto group = option->state & State_Enabled ? QPalette::Active : QPalette::Disabled;
            QColor ink = option->palette.color(group, QPalette::Text);
            ink.setAlpha(190);
            painter->setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            const QPointF center = QRectF(option->rect).center();
            QPainterPath arrow;
            if (option->state & State_Open) {
                arrow.moveTo(center + QPointF(-3, -1.5));
                arrow.lineTo(center + QPointF(0, 1.5));
                arrow.lineTo(center + QPointF(3, -1.5));
            } else {
                const qreal direction = option->direction == Qt::RightToLeft ? -1. : 1.;
                arrow.moveTo(center + QPointF(-1.5 * direction, -3));
                arrow.lineTo(center + QPointF(1.5 * direction, 0));
                arrow.lineTo(center + QPointF(-1.5 * direction, 3));
            }
            painter->drawPath(arrow);
            painter->restore();
            return;
        }
        // Focus feedback belongs to the themed control or item, never a native rectangle.
        if (element != PE_FrameFocusRect)
            QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};
}

QStyle *createUiStyle() { return new SurfaceStyle; }

void applyUiTheme(const QString &requested){const auto t=themeColors(requested);qApp->setProperty("imagidockTheme",t.id);QPalette palette;palette.setColor(QPalette::Window,t.top);palette.setColor(QPalette::WindowText,t.text);palette.setColor(QPalette::Base,t.top);palette.setColor(QPalette::AlternateBase,t.card);palette.setColor(QPalette::Text,t.text);palette.setColor(QPalette::Button,t.panel);palette.setColor(QPalette::ButtonText,t.text);palette.setColor(QPalette::Highlight,t.accent);palette.setColor(QPalette::HighlightedText,t.light?QColor("#ffffff"):t.bottom);palette.setColor(QPalette::PlaceholderText,t.muted);palette.setColor(QPalette::ToolTipBase,t.top);palette.setColor(QPalette::ToolTipText,t.text);palette.setColor(QPalette::Disabled,QPalette::Text,t.muted);palette.setColor(QPalette::Disabled,QPalette::ButtonText,t.muted);qApp->setPalette(palette);
    auto rgba=[](QColor c){return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());};
    QColor hover=t.card;hover.setAlpha(t.light?190:35);QColor selected=t.accent;selected.setAlpha(t.light?38:38);
    QColor input=t.light?QColor(255,255,255,65):QColor(235,243,255,7);
    QColor hairline=t.edge;hairline.setAlpha(t.light?110:24);
    QString qss=QString(R"(
QWidget { font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif; font-size: 13px; color: %1; }
QMainWindow { background: transparent; }
QDialog { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 %2,stop:1 %3); }
QMenuBar { background: transparent; padding: 0; border: 0; }
QLabel#windowBrand { font-size: 13px; font-weight: 600; padding: 0 4px; }
QWidget#windowHeader { background: transparent; }
QToolButton#windowButton, QToolButton#windowClose { border: 1px solid transparent; border-radius: 6px; background: transparent; padding: 0; }
QToolButton#windowButton:hover { background: %6; } QToolButton#windowClose:hover { background: rgba(198,82,92,205); }
QToolButton#windowButton:focus, QToolButton#windowClose:focus { background: %4; }
QMenuBar::item { padding: 4px 8px; border-radius: 6px; } QMenuBar::item:selected { background: %4; }
QMenu { background: %2; border: 1px solid transparent; border-radius: 8px; padding: 6px; } QMenu::item { padding: 6px 20px; border-radius: 6px; } QMenu::item:selected { background: %4; }
QToolBar { background: transparent; border: 0; padding: 0; spacing: 8px; }
QWidget#locationBar { padding: 0; border-bottom: 1px solid %15; }
QWidget#libraryTools { padding: 0; border-bottom: 1px solid %15; }
QToolBar::separator { width: 1px; background: %15; margin: 5px 3px; }
QToolButton, QPushButton { border: 1px solid transparent; border-radius: %13px; min-height: 18px; padding: 4px 8px; background: transparent; color: %1; }
QToolButton:hover, QPushButton:hover { background: %7; }
QToolButton:focus, QPushButton:focus, QToolButton:pressed, QPushButton:pressed, QToolButton:checked, QPushButton:checked { background: %4; }
QPushButton[quiet="true"], QToolButton[quiet="true"], QToolBar QToolButton { border-color: transparent; }
QToolButton[iconOnly="true"], QPushButton[iconOnly="true"] { padding: 0; min-width: 26px; min-height: 26px; }
QPushButton[smallIcon="true"] { min-width: 22px; min-height: 22px; }
QPushButton#primaryAction { background: %4; border: 1px solid transparent; font-weight: 600; }
QPushButton:disabled, QToolButton:disabled { color: %9; background: transparent; border-color: transparent; }
QPushButton#danger { color: %10; }
QLineEdit, QComboBox, QAbstractSpinBox { background: %11; border: 1px solid transparent; border-radius: %13px; min-height: 18px; padding: 4px 8px; color: %1; }
QLineEdit:hover, QComboBox:hover, QAbstractSpinBox:hover { background: %7; }
QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { background: %4; }
QTextEdit, QPlainTextEdit { background: %11; border: 1px solid transparent; border-radius: %13px; padding: 6px; }
QTextEdit:focus, QPlainTextEdit:focus { background: %4; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { border: 0; background: transparent; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: %4; }
QComboBox { min-width: 82px; padding-right: 20px; }
QComboBox#headerTheme { min-width: 0; min-height: 16px; padding: 4px 8px; } QComboBox::drop-down { border: 0; width: 18px; }
QComboBox QAbstractItemView { background: %2; border: 1px solid transparent; selection-background-color: %4; padding: 4px; }
QListView, QTreeView, QTableView { border: 1px solid transparent; outline: 0; background: transparent; gridline-color: transparent; }
QTreeView { selection-color: %1; selection-background-color: transparent; }
QGraphicsView, QScrollArea { border: 0; background: transparent; }
QListView:focus, QTreeView:focus, QTableView:focus { border-color: transparent; }
QCheckBox, QRadioButton { border: 1px solid transparent; border-radius: 6px; padding: 2px; }
QCheckBox:focus, QRadioButton:focus { background: %4; }
QLabel#pageTitle:focus { background: %4; border-radius: 6px; }
QListView::item, QTableView::item { border: 0; padding: 4px 6px; margin: 0; border-radius: %13px; }
QListView::item:hover, QTableView::item:hover { background: %6; }
QListView::item:selected, QTableView::item:selected,
QListView::item:focus, QTableView::item:focus { background: %4; color: %1; }
QTreeView::item { padding: 4px 6px; }
QListWidget#typeNavigation::item { padding: 0 6px; }
QTabWidget::pane { border: 0; background: transparent; } QTabBar { background: transparent; }
QTabBar::tab { color: %9; background: transparent; height: 26px; min-height: 26px; max-height: 26px; padding: 0 12px; margin: 0 4px 4px 0; min-width: 88px; max-width: 220px; border: 1px solid transparent; border-radius: %13px; }
QToolButton#tabClose { border: 1px solid transparent; border-radius: 4px; background: transparent; padding: 0; } QToolButton#tabClose:hover { background: %4; }
QToolButton#tabClose:focus { background: %4; }
QTabBar::tab:selected { color: %1; background: %6; }
QTabBar::tab:focus { color: %1; background: %4; }
QLabel#pageTitle { font-size: 18px; font-weight: 600; padding: 0; border: 1px solid transparent; }
QWidget#browseHost QLabel#pageTitle { font-size: 13px; }
QLabel#workspaceTitle { font-size: 14px; font-weight: 600; padding: 2px 6px 6px 6px; }
QLabel#locationTitle, QLabel#inspectorTitle { font-size: 13px; font-weight: 600; border: 0; padding: 0; }
QLabel#sectionTitle, QLabel#propertyLabel { font-size: 11px; font-weight: 500; color: %9; padding: 0; }
QLabel#muted { color: %9; font-size: 12px; } QLabel#emptyState { color: %9; padding: 36px 16px; font-size: 13px; }
QScrollArea#navigationScroll, QStackedWidget#assetPages { background: transparent; border: 0; }
QWidget#contentPanel, QScrollArea#inspectorScroll { background: %12; border: 1px solid transparent; border-radius: %14px; }
QWidget#sidebar, QWidget#inspector { background: transparent; border: 0; }
QPushButton[navAction="true"] { text-align: left; padding: 5px 8px; border: 1px solid transparent; background: transparent; }
QPushButton[navAction="true"]:hover { background: %6; }
QPushButton[navAction="true"]:checked { background: %4; font-weight: 600; }
QPushButton[navAction="true"]:focus { background: %4; }
QWidget#inspector QPushButton { text-align: left; }
QWidget#inspector QPushButton:checked { background: %4; }
QListWidget#previewStrip, QListWidget#relationStrip { font-size: 11px; }
QListWidget#previewStrip::item, QListWidget#relationStrip::item { padding: 4px; border: 1px solid transparent; }
QSplitter::handle { background: transparent; width: 8px; }
QScrollBar:vertical { width: 8px; background: transparent; margin: 4px 0; } QScrollBar::handle:vertical { background: %5; border-radius: 4px; min-height: 36px; }
QScrollBar:horizontal { height: 8px; background: transparent; } QScrollBar::handle:horizontal { background: %5; border-radius: 4px; min-width: 36px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical, QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
QSlider::groove:horizontal { height: 4px; border: 0; border-radius: 2px; background: %5; }
QSlider::sub-page:horizontal { border: 0; border-radius: 2px; background: %8; }
QSlider::handle:horizontal { width: 12px; margin: -4px 0; border: 0; border-radius: 6px; background: %8; }
QSlider::handle:horizontal:hover, QSlider::handle:horizontal:focus { background: %1; }
QSlider::groove:vertical { width: 4px; border: 0; border-radius: 2px; background: %5; }
QSlider::sub-page:vertical { border: 0; border-radius: 2px; background: %8; }
QSlider::handle:vertical { height: 12px; margin: 0 -4px; border: 0; border-radius: 6px; background: %8; }
QSlider::handle:vertical:hover, QSlider::handle:vertical:focus { background: %1; }
QStatusBar { background: transparent; border: 0; color: %9; padding: 2px 16px 4px 16px; font-size: 11px; }
QStatusBar::item, QTableCornerButton::section { border: 0; background: transparent; }
QPushButton#footerAction { border-color: transparent; font-size: 11px; padding: 2px 8px; min-height: 16px; }
QHeaderView::section { background: %6; border: 0; border-bottom: 1px solid %15; padding: 8px; color: %9; }
QToolTip { background: %2; color: %1; border: 1px solid transparent; border-radius: 6px; padding: 6px; }
)" ).arg(t.text.name(),t.top.name(),t.bottom.name(),rgba(selected),rgba(t.edge),rgba(t.card),rgba(hover),t.accent.name(),t.muted.name(),t.light?"#9f6868":"#cb999f",rgba(input),rgba(t.panel)).arg(UiMetrics::ControlRadius).arg(UiMetrics::PanelRadius).arg(rgba(hairline));
    qApp->setStyleSheet(qss);
}
