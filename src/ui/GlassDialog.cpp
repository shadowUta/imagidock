#include "GlassDialog.h"
#include "Theme.h"
#include "GlassMaterial.h"
#include "UiMetrics.h"
#include <QLayout>
#include <QApplication>
#include <QTimer>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QToolButton>
#include <QAbstractNativeEventFilter>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScreen>
#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
void paintGlassDialog(QPainter &painter,QWidget *dialog,QPixmap &background,QString &themeId) {
    const auto theme=themeColors();
    const auto size=dialog->size()*dialog->devicePixelRatioF();
    if(background.isNull()||background.size()!=size||themeId!=theme.id) {
        themeId=theme.id;
        background=QPixmap(size);
        background.setDevicePixelRatio(dialog->devicePixelRatioF());
        background.fill(theme.bottom);
        QPainter surface(&background);
        surface.setRenderHint(QPainter::Antialiasing);
        QLinearGradient base(0,0,dialog->width(),dialog->height());
        base.setColorAt(0,theme.top);base.setColorAt(1,theme.bottom);
        surface.fillRect(dialog->rect(),base);
        QRadialGradient glow(QPointF(dialog->width()*.15,dialog->height()*.1),dialog->width());
        auto color=theme.glow;color.setAlpha(theme.light?145:38);glow.setColorAt(0,color);
        color.setAlpha(0);glow.setColorAt(1,color);surface.fillRect(dialog->rect(),glow);
        paintGlassSurface(surface,QRectF(dialog->rect()),UiMetrics::DialogRadius,GlassLayer::Panel,false,false,false,false);
        paintGlassOutline(surface,QRectF(dialog->rect()),UiMetrics::DialogRadius,GlassLayer::Panel);
    }
    painter.drawPixmap(0,0,background);
    auto font=painter.font();font.setBold(true);font.setPixelSize(13);painter.setFont(font);
    painter.setPen(theme.text);
    painter.drawText(QRect(20,8,dialog->width()-76,30),Qt::AlignVCenter,painter.fontMetrics().elidedText(dialog->windowTitle(),Qt::ElideRight,dialog->width()-76));
}

void roundDialog(QWidget *dialog) {
#ifdef Q_OS_WIN
    if(dialog->testAttribute(Qt::WA_WState_Created)) {
        const auto ratio=dialog->devicePixelRatioF();
        auto region=CreateRoundRectRgn(0,0,qRound(dialog->width()*ratio)+1,qRound(dialog->height()*ratio)+1,qRound(2*UiMetrics::DialogRadius*ratio),qRound(2*UiMetrics::DialogRadius*ratio));
        if(!SetWindowRgn(reinterpret_cast<HWND>(dialog->winId()),region,TRUE))DeleteObject(region);
    }
#endif
}

class DialogBackground final : public QWidget {
public:
    explicit DialogBackground(QDialog *dialog):QWidget(dialog),m_dialog(dialog){setAttribute(Qt::WA_TransparentForMouseEvents);setFocusPolicy(Qt::NoFocus);}
protected:
    void paintEvent(QPaintEvent *) override {QPainter painter(this);paintGlassDialog(painter,m_dialog,m_background,m_theme);}
private:
    QDialog *m_dialog;
    QPixmap m_background;
    QString m_theme;
};

// The underlying Qt dialog still owns acceptance, rejection and all button roles.
class BorrowedDialogChrome final : public QObject,public QAbstractNativeEventFilter {
public:
    explicit BorrowedDialogChrome(QDialog *dialog):QObject(dialog),m_dialog(dialog) {
        auto flags=dialog->windowFlags();
        flags|=Qt::FramelessWindowHint;
        flags&=~Qt::WindowTitleHint;
        dialog->setWindowFlags(flags);
        m_background=new DialogBackground(dialog);
        m_background->setGeometry(dialog->rect());
        m_background->lower();
        m_close=new QToolButton(dialog);
        m_close->setObjectName("windowClose");
        m_close->setText(QString::fromUtf8("×"));
        m_close->setToolTip(QObject::tr("关闭"));
        m_close->setAccessibleName(QObject::tr("关闭对话框"));
        m_close->setFixedSize(32,28);
        connect(m_close,&QToolButton::clicked,dialog,&QWidget::close);
        inset();
        makeMessageTextScrollable();
        dialog->installEventFilter(this);
        qApp->installNativeEventFilter(this);
    }
    ~BorrowedDialogChrome() override {qApp->removeNativeEventFilter(this);}
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Show||event->type()==QEvent::Resize) {
            inset();
            m_background->setGeometry(m_dialog->rect());
            m_background->lower();
            m_close->move(m_dialog->width()-44,10);
            m_close->raise();
            m_dialog->setAccessibleName(m_dialog->windowTitle());
            roundDialog(m_dialog);
        }
        if(event->type()==QEvent::Show||event->type()==QEvent::LayoutRequest)queueFit();
        if(event->type()==QEvent::WindowTitleChange||event->type()==QEvent::StyleChange)m_background->update();
        return false;
    }
    bool nativeEventFilter(const QByteArray &,void *message,qintptr *result) override {
#ifdef Q_OS_WIN
        auto msg=static_cast<MSG*>(message);
        if(msg->message==WM_NCHITTEST&&m_dialog->testAttribute(Qt::WA_WState_Created)&&msg->hwnd==reinterpret_cast<HWND>(m_dialog->winId())) {
            POINT point{GET_X_LPARAM(msg->lParam),GET_Y_LPARAM(msg->lParam)};ScreenToClient(msg->hwnd,&point);
            const QPoint local(qRound(point.x/m_dialog->devicePixelRatioF()),qRound(point.y/m_dialog->devicePixelRatioF()));
            const bool left=local.x()<6,right=local.x()>=m_dialog->width()-6,top=local.y()<6,bottom=local.y()>=m_dialog->height()-6;
            if(left||right||top||bottom){*result=top?(left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP):bottom?(left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM):left?HTLEFT:HTRIGHT;return true;}
            if(local.y()<43&&!m_close->geometry().contains(local)){*result=HTCAPTION;return true;}
        }
#endif
        return false;
    }
private:
    QDialog *m_dialog;
    DialogBackground *m_background;
    QToolButton *m_close;
    QPointer<QScrollArea> m_textScroll;
    bool m_inset=false;
    bool m_fitPending=false;
    void inset(){if(!m_inset&&m_dialog->layout()){const auto margins=m_dialog->layout()->contentsMargins();m_dialog->layout()->setContentsMargins(qMax(16,margins.left()),margins.top()+44,qMax(16,margins.right()),qMax(16,margins.bottom()));m_inset=true;}}
    QRect room() const {const auto screen=m_dialog->screen();return (screen?screen->availableGeometry():QRect(0,0,1024,768)).adjusted(16,16,-16,-16);}
    void makeMessageTextScrollable() {
        // Keep Qt's label and all validation/button state; only its layout slot scrolls.
        auto grid=qobject_cast<QGridLayout*>(m_dialog->layout());
        auto label=m_dialog->findChild<QLabel*>("qt_msgbox_label");
        if(!grid||!label)return;
        const auto index=grid->indexOf(label);
        if(index<0)return;
        int row,column,rows,columns;
        grid->getItemPosition(index,&row,&column,&rows,&columns);
        const auto item=grid->takeAt(index);
        const auto alignment=item->alignment();
        delete item;
        auto scroll=new QScrollArea(m_dialog);
        scroll->setObjectName("glassMessageScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        const auto area=room();
        const auto bodyWidth=qMax(40,area.width()-120),bodyHeight=qMax(32,area.height()-160);
        scroll->setMinimumWidth(qMin(420,bodyWidth));
        scroll->setMaximumWidth(bodyWidth);
        scroll->setMinimumHeight(qMin(72,bodyHeight));
        scroll->setMaximumHeight(qMin(360,bodyHeight));
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        scroll->setWidget(label);
        label->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        grid->addWidget(scroll,row,column,rows,columns,alignment);
        m_textScroll=scroll;
    }
    void queueFit() {
        if(m_fitPending)return;
        m_fitPending=true;
        QTimer::singleShot(0,this,[this]{
            m_fitPending=false;
            if(!m_dialog->isVisible())return;
            const auto area=room();
            // QMessageBox computes a fixed size after Show/LayoutRequest. Bound it after
            // that computation so long paths cannot push its original actions off screen.
            m_dialog->setMinimumSize(0,0);
            m_dialog->setMaximumSize(area.size());
            m_dialog->resize(m_dialog->size().boundedTo(area.size()));
            m_dialog->move(qBound(area.left(),m_dialog->x(),area.right()-m_dialog->width()+1),
                           qBound(area.top(),m_dialog->y(),area.bottom()-m_dialog->height()+1));
        });
    }
};
}

void decorateGlassDialog(QDialog *dialog) {
    if(!dialog||qobject_cast<GlassDialog*>(dialog)||dialog->property("glassDialogDecorated").toBool())return;
    dialog->setProperty("glassDialogDecorated",true);
    new BorrowedDialogChrome(dialog);
}

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
    roundDialog(this);
    update();
}

void GlassDialog::paintEvent(QPaintEvent *event) {
    QDialog::paintEvent(event);
    QPainter painter(this);paintGlassDialog(painter,this,m_background,m_theme);
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
