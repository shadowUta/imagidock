#include "UiStyle.h"
#include "GlassMaterial.h"
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
            const bool active = option->state & (State_Selected | State_HasFocus);
            painter->save();
            paintGlassSurface(*painter, option->rect, UiMetrics::ControlRadius,
                              GlassLayer::Control, active, false, false, false);
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

void applyUiTheme(const QString &requested)
{
    const auto t = themeColors(requested);
    qApp->setProperty("imagidockTheme", t.id);
    QPalette palette;
    palette.setColor(QPalette::Window, t.top);
    palette.setColor(QPalette::WindowText, t.text);
    palette.setColor(QPalette::Base, t.top);
    palette.setColor(QPalette::AlternateBase, t.card);
    palette.setColor(QPalette::Text, t.text);
    palette.setColor(QPalette::Button, t.panel);
    palette.setColor(QPalette::ButtonText, t.text);
    palette.setColor(QPalette::Highlight, t.accent);
    palette.setColor(QPalette::HighlightedText, t.light ? QColor("#ffffff") : t.bottom);
    palette.setColor(QPalette::PlaceholderText, t.muted);
    palette.setColor(QPalette::ToolTipBase, t.top);
    palette.setColor(QPalette::ToolTipText, t.text);
    palette.setColor(QPalette::Disabled, QPalette::Text, t.muted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, t.muted);
    qApp->setPalette(palette);

    const auto rgba = [](const QColor &c) {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    };
    const auto gradientCss = [&rgba](const QGradientStops &stops) {
        return QStringLiteral("qlineargradient(x1:0,y1:0,x2:0.22,y2:1,stop:0 %1,stop:0.46 %2,stop:1 %3)")
            .arg(rgba(stops[0].second), rgba(stops[1].second), rgba(stops[2].second));
    };
    auto popupStops = glassGradient(QRectF(0, 0, 1, 1), GlassLayer::Panel).stops();
    for (auto &stop : popupStops) {
        // Popup surfaces need their own readable backing, unlike in-window glass.
        const auto c = stop.second;
        const qreal a = c.alphaF();
        stop.second = QColor(qRound(c.red() * a + t.top.red() * (1 - a)),
                             qRound(c.green() * a + t.top.green() * (1 - a)),
                             qRound(c.blue() * a + t.top.blue() * (1 - a)), 248);
    }
    auto disabledStops = glassGradient(QRectF(0, 0, 1, 1), GlassLayer::Control).stops();
    for (auto &stop : disabledStops) stop.second.setAlpha(stop.second.alpha() / 2);
    QColor disabledEdge = glassEdge(GlassLayer::Control);
    disabledEdge.setAlpha(disabledEdge.alpha() / 2);
    QColor hairline = t.edge;
    hairline.setAlpha(t.light ? 110 : 24);
    QColor progress = t.accent;
    progress.setAlpha(t.light ? 120 : 135);

    QString qss = QStringLiteral(R"(
QWidget { font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif; font-size: 13px; color: {{text}}; }
QMainWindow { background: transparent; }
QDialog { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 {{top}},stop:1 {{bottom}}); }
QMenuBar { background: transparent; padding: 0; border: 0; }
QLabel#windowBrand { font-size: 13px; font-weight: 600; padding: 0 4px; }
QWidget#windowHeader { background: {{panel}}; border: 1px solid {{panelEdge}}; border-radius: {{panelRadius}}px; }
QToolButton#windowButton, QToolButton#windowClose { border-width: 1px; border-style: solid; border-radius: 6px; padding: 0; }
QToolButton#windowClose:hover { background: qlineargradient(x1:0,y1:0,x2:0.22,y2:1,stop:0 rgba(224,138,145,155),stop:0.46 rgba(198,82,92,128),stop:1 rgba(181,63,76,145)); }
QToolButton#windowClose:pressed { background: qlineargradient(x1:0,y1:0,x2:0.22,y2:1,stop:0 rgba(203,103,114,170),stop:0.46 rgba(187,64,78,185),stop:1 rgba(176,53,70,190)); }
QMenuBar::item { background: {{control}}; border: 1px solid {{controlEdge}}; padding: 3px 7px; border-radius: 6px; }
QMenuBar::item:selected { background: {{active}}; border-color: {{activeEdge}}; }
QMenuBar::item:pressed { background: {{pressed}}; border-color: {{pressedEdge}}; }
QMenu { background: {{popup}}; border: 1px solid {{panelEdge}}; border-radius: 8px; padding: 6px; }
QMenu::item { background: transparent; padding: 6px 20px; border-radius: 6px; }
QMenu::item:selected { background: {{active}}; }
QMenu::item:disabled { color: {{muted}}; }
QMenu::separator { height: 1px; background: {{hairline}}; margin: 4px 8px; }
QToolBar { background: {{panel}}; border: 1px solid {{panelEdge}}; border-radius: 6px; padding: 0; spacing: 8px; }
QWidget#locationBar, QWidget#libraryTools { background: {{panel}}; padding: 0; border: 1px solid {{panelEdge}}; border-radius: {{panelRadius}}px; }
QWidget#previewTools { background: {{panel}}; border: 1px solid {{panelEdge}}; border-radius: 6px; }
QToolBar::separator { width: 1px; background: {{hairline}}; margin: 5px 3px; }
QToolButton, QPushButton { border: 1px solid {{controlEdge}}; border-radius: {{controlRadius}}px; min-height: 18px; padding: 4px 8px; background: {{control}}; color: {{text}}; }
QToolButton:hover, QPushButton:hover { background: {{active}}; border-color: {{activeEdge}}; }
QToolButton:focus, QPushButton:focus, QToolButton:checked, QPushButton:checked { background: {{active}}; border-color: {{activeEdge}}; }
QToolButton:pressed, QPushButton:pressed { background: {{pressed}}; border-color: {{pressedEdge}}; }
QToolButton[iconOnly="true"], QPushButton[iconOnly="true"] { padding: 0; min-width: 26px; min-height: 26px; }
QPushButton[smallIcon="true"] { min-width: 22px; min-height: 22px; }
QPushButton#primaryAction { background: {{active}}; border: 1px solid {{activeEdge}}; font-weight: 600; }
QPushButton#primaryAction:hover, QPushButton#primaryAction:focus, QPushButton#primaryAction:checked { background: {{cardActive}}; border-color: {{activeEdge}}; }
QPushButton#primaryAction:pressed { background: {{pressed}}; border-color: {{pressedEdge}}; }
QPushButton:disabled, QToolButton:disabled, QPushButton#primaryAction:disabled { color: {{muted}}; background: {{disabled}}; border-color: {{disabledEdge}}; }
QPushButton#danger { color: {{danger}}; }
QLineEdit, QComboBox, QAbstractSpinBox { background: {{control}}; border: 1px solid {{controlEdge}}; border-radius: {{controlRadius}}px; min-height: 18px; padding: 4px 8px; color: {{text}}; }
QLineEdit:hover, QComboBox:hover, QAbstractSpinBox:hover { background: {{active}}; border-color: {{activeEdge}}; }
QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { background: {{active}}; border-color: {{activeEdge}}; }
QLineEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled { background: {{disabled}}; color: {{muted}}; border-color: {{disabledEdge}}; }
QTextEdit, QPlainTextEdit { background: {{control}}; border: 1px solid {{controlEdge}}; border-radius: {{controlRadius}}px; padding: 6px; }
QTextEdit:focus, QPlainTextEdit:focus { background: {{active}}; border-color: {{activeEdge}}; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { border: 0; background: transparent; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: {{active}}; }
QAbstractSpinBox::up-button:pressed, QAbstractSpinBox::down-button:pressed { background: {{pressed}}; }
QComboBox { min-width: 82px; padding-right: 20px; }
QComboBox#headerTheme { min-width: 0; min-height: 16px; padding: 4px 8px; }
QComboBox::drop-down { border: 0; width: 18px; }
QComboBox QAbstractItemView { background: {{popup}}; border: 1px solid {{panelEdge}}; border-radius: {{panelRadius}}px; selection-background-color: {{active}}; padding: 4px; }
QComboBox QAbstractItemView:focus { border-color: {{panelEdge}}; }
QListView, QTreeView, QTableView { border: 1px solid transparent; outline: 0; background: transparent; gridline-color: transparent; }
QTreeView { selection-color: {{text}}; selection-background-color: transparent; }
QGraphicsView, QScrollArea { border: 0; background: transparent; }
QListView:focus, QTreeView:focus, QTableView:focus { border-color: transparent; }
QCheckBox, QRadioButton { background: {{control}}; border: 1px solid {{controlEdge}}; border-radius: 6px; padding: 2px; }
QCheckBox:hover, QRadioButton:hover, QCheckBox:focus, QRadioButton:focus { background: {{active}}; border-color: {{activeEdge}}; }
QCheckBox:disabled, QRadioButton:disabled { background: {{disabled}}; color: {{muted}}; border-color: {{disabledEdge}}; }
QLabel#pageTitle:focus { background: {{active}}; border-radius: 6px; }
QListView::item, QTableView::item { border: 0; padding: 4px 6px; margin: 0; border-radius: {{controlRadius}}px; }
QListView::item:hover, QTableView::item:hover { background: {{control}}; }
QListView::item:selected, QTableView::item:selected,
QListView::item:focus, QTableView::item:focus { background: {{active}}; color: {{text}}; }
QTreeView::item { padding: 4px 6px; }
QListWidget#typeNavigation::item { padding: 0 6px; }
QTabWidget::pane { border: 0; background: transparent; }
QTabBar { background: transparent; }
QTabBar::tab { color: {{muted}}; background: {{control}}; height: 26px; min-height: 26px; max-height: 26px; padding: 0 12px; margin: 0 4px 4px 0; min-width: 88px; max-width: 220px; border: 1px solid {{controlEdge}}; border-radius: {{controlRadius}}px; }
QToolButton#tabClose { border-width: 1px; border-style: solid; border-radius: 4px; padding: 0; }
QTabBar::tab:hover { background: {{active}}; border-color: {{activeEdge}}; }
QTabBar::tab:selected { color: {{text}}; background: {{cardActive}}; border-color: {{cardActiveEdge}}; }
QTabBar::tab:focus { color: {{text}}; background: {{cardActive}}; border-color: {{cardActiveEdge}}; }
QLabel#pageTitle { font-size: 18px; font-weight: 600; padding: 0; border: 1px solid transparent; }
QWidget#browseHost QLabel#pageTitle { font-size: 13px; }
QLabel#workspaceTitle { font-size: 14px; font-weight: 600; padding: 2px 6px 6px 6px; }
QLabel#locationTitle, QLabel#inspectorTitle { font-size: 13px; font-weight: 600; border: 0; padding: 0; }
QLabel#sectionTitle, QLabel#propertyLabel { font-size: 11px; font-weight: 500; color: {{muted}}; padding: 0; }
QLabel#muted { color: {{muted}}; font-size: 12px; }
QLabel#emptyState { color: {{muted}}; padding: 36px 16px; font-size: 13px; }
QScrollArea#navigationScroll, QStackedWidget#assetPages { background: transparent; border: 0; }
QWidget#contentPanel, QScrollArea#inspectorScroll { background: {{panel}}; border: 1px solid {{panelEdge}}; border-radius: {{panelRadius}}px; }
QWidget#sidebar { background: {{panel}}; border: 1px solid {{panelEdge}}; border-radius: {{panelRadius}}px; }
QWidget#inspector { background: transparent; border: 0; }
QPushButton[navAction="true"] { text-align: left; padding: 5px 8px; border-width: 1px; border-style: solid; }
QPushButton[navAction="true"]:checked { font-weight: 600; }
QWidget#inspector QPushButton { text-align: left; }
QListWidget#previewStrip, QListWidget#relationStrip { font-size: 11px; }
QListWidget#previewStrip::item, QListWidget#relationStrip::item { background: {{card}}; padding: 4px; border: 1px solid {{cardEdge}}; border-radius: {{controlRadius}}px; }
QListWidget#previewStrip::item:hover, QListWidget#relationStrip::item:hover,
QListWidget#previewStrip::item:selected, QListWidget#relationStrip::item:selected { background: {{active}}; border-color: {{activeEdge}}; }
QGroupBox { background: {{panel}}; border: 1px solid {{panelEdge}}; border-radius: {{panelRadius}}px; }
QSplitter::handle { background: transparent; width: 8px; }
QScrollBar:vertical { width: 8px; background: transparent; margin: 4px 0; }
QScrollBar::handle:vertical { background: {{edge}}; border-radius: 4px; min-height: 36px; }
QScrollBar:horizontal { height: 8px; background: transparent; }
QScrollBar::handle:horizontal { background: {{edge}}; border-radius: 4px; min-width: 36px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical, QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
QSlider::groove:horizontal { height: 4px; border: 0; border-radius: 2px; background: {{edge}}; }
QSlider::sub-page:horizontal { border: 0; border-radius: 2px; background: {{accent}}; }
QSlider::handle:horizontal { width: 12px; margin: -4px 0; border: 0; border-radius: 6px; background: {{accent}}; }
QSlider::handle:horizontal:hover, QSlider::handle:horizontal:focus { background: {{text}}; }
QSlider::groove:vertical { width: 4px; border: 0; border-radius: 2px; background: {{edge}}; }
QSlider::sub-page:vertical { border: 0; border-radius: 2px; background: {{accent}}; }
QSlider::handle:vertical { height: 12px; margin: 0 -4px; border: 0; border-radius: 6px; background: {{accent}}; }
QSlider::handle:vertical:hover, QSlider::handle:vertical:focus { background: {{text}}; }
QProgressBar { background: {{control}}; border: 1px solid {{controlEdge}}; border-radius: 6px; text-align: center; }
QProgressBar::chunk { background: {{progress}}; border: 0; border-radius: 5px; }
QStatusBar { background: {{panel}}; border: 0; color: {{muted}}; padding: 2px 16px 4px 16px; font-size: 11px; }
QStatusBar::item, QTableCornerButton::section { border: 0; background: transparent; }
QPushButton#footerAction { font-size: 11px; padding: 2px 8px; min-height: 16px; }
QHeaderView::section { background: {{control}}; border: 0; border-bottom: 1px solid {{hairline}}; padding: 8px; color: {{muted}}; }
QToolTip { background: {{popup}}; color: {{text}}; border: 1px solid {{panelEdge}}; border-radius: 6px; padding: 6px; }
)");
    qss.replace("{{text}}", t.text.name());
    qss.replace("{{top}}", t.top.name());
    qss.replace("{{bottom}}", t.bottom.name());
    qss.replace("{{muted}}", t.muted.name());
    qss.replace("{{accent}}", t.accent.name());
    qss.replace("{{edge}}", rgba(t.edge));
    qss.replace("{{panelEdge}}", rgba(glassEdge(GlassLayer::Panel)));
    qss.replace("{{controlEdge}}", rgba(glassEdge(GlassLayer::Control)));
    qss.replace("{{activeEdge}}", rgba(glassEdge(GlassLayer::Control, true)));
    qss.replace("{{pressedEdge}}", rgba(glassEdge(GlassLayer::Control, true, true)));
    qss.replace("{{cardEdge}}", rgba(glassEdge(GlassLayer::Card)));
    qss.replace("{{cardActiveEdge}}", rgba(glassEdge(GlassLayer::Card, true)));
    qss.replace("{{disabledEdge}}", rgba(disabledEdge));
    qss.replace("{{hairline}}", rgba(hairline));
    qss.replace("{{progress}}", rgba(progress));
    qss.replace("{{danger}}", t.light ? "#9f6868" : "#cb999f");
    qss.replace("{{controlRadius}}", QString::number(UiMetrics::ControlRadius));
    qss.replace("{{panelRadius}}", QString::number(UiMetrics::PanelRadius));
    qss.replace("{{panel}}", glassStyleGradient(GlassLayer::Panel));
    qss.replace("{{control}}", glassStyleGradient(GlassLayer::Control));
    qss.replace("{{card}}", glassStyleGradient(GlassLayer::Card));
    qss.replace("{{active}}", glassStyleGradient(GlassLayer::Control, true));
    qss.replace("{{pressed}}", glassStyleGradient(GlassLayer::Control, true, true));
    qss.replace("{{cardActive}}", glassStyleGradient(GlassLayer::Card, true));
    qss.replace("{{popup}}", gradientCss(popupStops));
    qss.replace("{{disabled}}", gradientCss(disabledStops));
    qApp->setStyleSheet(qss);
}
