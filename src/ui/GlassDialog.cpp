#include "GlassDialog.h"
#include "Theme.h"
#include "UiMetrics.h"
#include <QLayout>
#include <QApplication>
#include <QTimer>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QToolButton>
#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

GlassDialog::GlassDialog(QWidget *parent):QDialog(parent) {
    m_restoreFocus=QApplication::focusWidget();
    setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint|Qt::WindowSystemMenuHint|Qt::WindowCloseButtonHint);
    m_close=new QToolButton(this);
    m_close->setObjectName("windowClose");
    m_close->setText(QString::fromUtf8("×"));
    m_close->setToolTip(tr("关闭"));
    m_close->setAccessibleName(tr("关闭对话框"));
    m_close->setFixedSize(32,28);
    connect(m_close,&QToolButton::clicked,this,&QDialog::reject);
}

void GlassDialog::showEvent(QShowEvent *event) {
    if(!m_inset&&layout()) {
        const auto margins=layout()->contentsMargins();
        layout()->setContentsMargins(qMax(16,margins.left()),margins.top()+44,qMax(16,margins.right()),qMax(16,margins.bottom()));
        m_inset=true;
    }
    QDialog::showEvent(event);
    updateShape();
    m_close->raise();
    setAccessibleName(windowTitle());
    QTimer::singleShot(0,this,[this]{for(auto candidate=nextInFocusChain();candidate&&candidate!=this;candidate=candidate->nextInFocusChain())if(candidate!=m_close&&isAncestorOf(candidate)&&candidate->isVisibleTo(this)&&candidate->isEnabled()&&(candidate->focusPolicy()&Qt::TabFocus)){candidate->setFocus(Qt::TabFocusReason);return;}m_close->setFocus(Qt::TabFocusReason);});
}

void GlassDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    m_close->move(width()-44,10);
    updateShape();
}

void GlassDialog::updateShape() {
    m_background=QPixmap();
#ifdef Q_OS_WIN
    if(testAttribute(Qt::WA_WState_Created)) {
        const auto ratio=devicePixelRatioF();
        auto region=CreateRoundRectRgn(0,0,qRound(width()*ratio)+1,qRound(height()*ratio)+1,qRound(2*UiMetrics::DialogRadius*ratio),qRound(2*UiMetrics::DialogRadius*ratio));
        if(!SetWindowRgn(reinterpret_cast<HWND>(winId()),region,TRUE))DeleteObject(region);
    }
#endif
    update();
}

void GlassDialog::paintEvent(QPaintEvent *event) {
    QDialog::paintEvent(event);
    const auto theme=themeColors();
    if(m_background.isNull()||m_theme!=theme.id) {
        m_theme=theme.id;
        m_background=QPixmap(size()*devicePixelRatioF());
        m_background.setDevicePixelRatio(devicePixelRatioF());
        m_background.fill(theme.bottom);
        QPainter painter(&m_background);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient base(0,0,width(),height());
        base.setColorAt(0,theme.top);base.setColorAt(1,theme.bottom);
        painter.fillRect(rect(),base);
        QRadialGradient glow(QPointF(width()*.15,height()*.1),width());
        auto color=theme.glow;color.setAlpha(theme.light?145:38);glow.setColorAt(0,color);
        color.setAlpha(0);glow.setColorAt(1,color);painter.fillRect(rect(),glow);
        painter.setPen(QPen(theme.edge,1));painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),UiMetrics::DialogRadius,UiMetrics::DialogRadius);
    }
    QPainter painter(this);painter.drawPixmap(0,0,m_background);
    auto font=painter.font();font.setBold(true);font.setPixelSize(13);painter.setFont(font);
    painter.setPen(theme.text);
    painter.drawText(QRect(20,8,width()-76,30),Qt::AlignVCenter,painter.fontMetrics().elidedText(windowTitle(),Qt::ElideRight,width()-76));
}

bool GlassDialog::nativeEvent(const QByteArray &type,void *message,qintptr *result) {
#ifdef Q_OS_WIN
    auto msg=static_cast<MSG*>(message);
    if(msg->message==WM_NCHITTEST) {
        POINT point{GET_X_LPARAM(msg->lParam),GET_Y_LPARAM(msg->lParam)};ScreenToClient(msg->hwnd,&point);
        const QPoint local(qRound(point.x/devicePixelRatioF()),qRound(point.y/devicePixelRatioF()));
        const bool left=local.x()<6,right=local.x()>=width()-6,top=local.y()<6,bottom=local.y()>=height()-6;
        if(left||right||top||bottom) {
            *result=top?(left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP):bottom?(left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM):left?HTLEFT:HTRIGHT;
            return true;
        }
        if(local.y()<43&&!m_close->geometry().contains(local)){*result=HTCAPTION;return true;}
    }
#endif
    return QDialog::nativeEvent(type,message,result);
}

void GlassDialog::done(int result){auto restore=m_restoreFocus;QDialog::done(result);QTimer::singleShot(0,qApp,[restore]{if(restore&&restore->isVisible()&&restore->isEnabled())restore->setFocus(Qt::OtherFocusReason);});}
