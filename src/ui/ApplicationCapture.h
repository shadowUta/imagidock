#pragma once
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QWidget>
#include "DesktopCapture.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

struct ApplicationCapture {
    QPixmap image;
    QString error;
};

// Only used by explicit self-test/smoke commands, on their own transient window.
inline void foregroundApplicationTestWindow(QWidget *widget) {
    if(!widget)return;
    auto top=widget->window();
#ifdef Q_OS_WIN
    // A one-shot idle reset for explicit visual tests. It does not change power
    // policy, hold a persistent request, or unlock the desktop.
    SetThreadExecutionState(ES_DISPLAY_REQUIRED|ES_SYSTEM_REQUIRED);
    // Keep only this helper's current transient window topmost. Otherwise the
    // preceding test window can remain above a newly detached one after queued
    // activation events. Never change another process or a normal app window.
    static QPointer<QWidget> previousTop;
    constexpr UINT positionFlags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
    if(previousTop&&previousTop!=top&&previousTop->internalWinId())
        SetWindowPos(reinterpret_cast<HWND>(previousTop->internalWinId()),HWND_NOTOPMOST,0,0,0,0,positionFlags);
    SetWindowPos(reinterpret_cast<HWND>(top->winId()),HWND_TOPMOST,0,0,0,0,positionFlags);
    previousTop=top;
#endif
    top->raise();
    top->activateWindow();
}

// Test-only capture: never save pixels belonging to a lock screen or another app.
inline ApplicationCapture captureApplicationWidget(QWidget *widget,bool exactNativeTarget=false) {
    if(!widget||!widget->isVisible()||!widget->screen())return {{},QStringLiteral("capture_blocked: target is not visible")};
    const QRect logicalBounds(widget->mapToGlobal(QPoint()),widget->size());
    if(!widget->screen()->geometry().contains(logicalBounds))
        return {{},QStringLiteral("capture_blocked: target extends outside its screen")};
#ifdef Q_OS_WIN
    QPointer<QWidget> guardedWidget(widget);
    const auto validateTarget=[&](QRect &bounds,qreal &ratio)->QString {
        if(!guardedWidget||!guardedWidget->isVisible()||!guardedWidget->screen())return QStringLiteral("capture_blocked: target disappeared during capture");
        const QRect currentLogicalBounds(guardedWidget->mapToGlobal(QPoint()),guardedWidget->size());
        if(!guardedWidget->screen()->geometry().contains(currentLogicalBounds))return QStringLiteral("capture_blocked: target moved outside its screen");
        const auto inputDesktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
        if(!inputDesktop)return QStringLiteral("capture_blocked: input desktop is unavailable or locked");
        wchar_t desktopName[256]{};DWORD desktopNameBytes=0;
        const bool desktopReadable=GetUserObjectInformationW(inputDesktop,UOI_NAME,desktopName,sizeof(desktopName),&desktopNameBytes);
        CloseDesktop(inputDesktop);
        if(!desktopReadable||QString::fromWCharArray(desktopName).compare("Default",Qt::CaseInsensitive)!=0)
            return QStringLiteral("capture_blocked: input desktop is not the unlocked default desktop");
        const auto top=reinterpret_cast<HWND>(widget->window()->winId());
        const auto target=exactNativeTarget?reinterpret_cast<HWND>(widget->winId()):nullptr;
        RECT topRect{};
        if(!GetWindowRect(top,&topRect))return QStringLiteral("capture_blocked: native window bounds unavailable");
        const int inset=qMin(32,qMin(widget->width(),widget->height())/4);
        const QList<QPoint> points{widget->rect().center(),QPoint(inset,inset),QPoint(widget->width()-inset-1,inset),QPoint(inset,widget->height()-inset-1),QPoint(widget->width()-inset-1,widget->height()-inset-1)};
        for(const auto &point:points) {
            const auto local=widget->mapTo(widget->window(),point);
            const POINT physical{topRect.left+qRound(local.x()*widget->devicePixelRatioF()),topRect.top+qRound(local.y()*widget->devicePixelRatioF())};
            const auto hit=WindowFromPoint(physical);DWORD process=0;GetWindowThreadProcessId(hit,&process);
            if(process!=GetCurrentProcessId()||(target&&hit!=target))return QStringLiteral("capture_blocked: target is occluded, off screen, or desktop is locked (point=%1,%2 hit=%3 pid=%4 expected=%5)").arg(physical.x).arg(physical.y).arg(qulonglong(reinterpret_cast<quintptr>(hit))).arg(process).arg(qulonglong(reinterpret_cast<quintptr>(target)));
        }
        const auto local=widget->mapTo(widget->window(),QPoint());ratio=widget->devicePixelRatioF();
        bounds=QRect(topRect.left+qRound(local.x()*ratio),topRect.top+qRound(local.y()*ratio),qRound(widget->width()*ratio),qRound(widget->height()*ratio));
        return {};
    };
    QRect physicalBounds;qreal ratio=1;
    if(const auto error=validateTarget(physicalBounds,ratio);!error.isEmpty())return {{},error};
    // Give static dialogs a bitmap update as well; desktop acquisition rejects
    // pointer-only notifications until a real image update has arrived.
    widget->window()->update();
    QString error;const auto pixels=captureDesktopRegion(physicalBounds,error);
    if(pixels.isNull())return {{},error};
    QRect currentBounds;qreal currentRatio=1;
    if(const auto validationError=validateTarget(currentBounds,currentRatio);!validationError.isEmpty())return {{},validationError};
    if(currentBounds!=physicalBounds||currentRatio!=ratio)return {{},QStringLiteral("capture_blocked: target geometry changed during capture")};
    auto result=QPixmap::fromImage(pixels);result.setDevicePixelRatio(ratio);return {result,{}};
#else
    Q_UNUSED(exactNativeTarget);
    // QScreen scales these screen-local coordinates; the Windows backend then adds
    // its physical monitor origin. Passing global coordinates adds that origin twice.
    const auto origin=widget->mapToGlobal(QPoint(0,0))-widget->screen()->geometry().topLeft();
    auto pixels=widget->screen()->grabWindow(0,origin.x(),origin.y(),widget->width(),widget->height());
    if(pixels.isNull())return {{},QStringLiteral("capture_blocked: desktop capture unavailable")};
    return {pixels,{}};
#endif
}
