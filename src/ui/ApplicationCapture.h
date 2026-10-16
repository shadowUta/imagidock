#pragma once
#include <QPixmap>
#include <QScreen>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

struct ApplicationCapture {
    QPixmap image;
    QString error;
};

// Test-only capture: never save pixels belonging to a lock screen or another app.
inline ApplicationCapture captureApplicationWidget(QWidget *widget,bool exactNativeTarget=false) {
    if(!widget||!widget->isVisible()||!widget->screen())return {{},QStringLiteral("capture_blocked: target is not visible")};
#ifdef Q_OS_WIN
    const auto top=reinterpret_cast<HWND>(widget->window()->winId());
    const auto target=exactNativeTarget?reinterpret_cast<HWND>(widget->winId()):nullptr;
    RECT topRect{};
    if(!GetWindowRect(top,&topRect))return {{},QStringLiteral("capture_blocked: native window bounds unavailable")};
    const int inset=qMin(32,qMin(widget->width(),widget->height())/4);
    const QList<QPoint> points{widget->rect().center(),QPoint(inset,inset),QPoint(widget->width()-inset-1,inset),QPoint(inset,widget->height()-inset-1),QPoint(widget->width()-inset-1,widget->height()-inset-1)};
    for(const auto &point:points) {
        const auto local=widget->mapTo(widget->window(),point);
        const POINT physical{topRect.left+qRound(local.x()*widget->devicePixelRatioF()),topRect.top+qRound(local.y()*widget->devicePixelRatioF())};
        const auto hit=WindowFromPoint(physical);DWORD process=0;GetWindowThreadProcessId(hit,&process);
        if(process!=GetCurrentProcessId()||(target&&hit!=target))return {{},QStringLiteral("capture_blocked: target is occluded, off screen, or desktop is locked (point=%1,%2 hit=%3 pid=%4 expected=%5)").arg(physical.x).arg(physical.y).arg(qulonglong(reinterpret_cast<quintptr>(hit))).arg(process).arg(qulonglong(reinterpret_cast<quintptr>(target)))};
    }
#else
    Q_UNUSED(exactNativeTarget);
#endif
    // QScreen scales these screen-local coordinates; the Windows backend then adds
    // its physical monitor origin. Passing global coordinates adds that origin twice.
    const auto origin=widget->mapToGlobal(QPoint(0,0))-widget->screen()->geometry().topLeft();
    auto pixels=widget->screen()->grabWindow(0,origin.x(),origin.y(),widget->width(),widget->height());
    if(pixels.isNull())return {{},QStringLiteral("capture_blocked: desktop capture unavailable")};
    return {pixels,{}};
}
