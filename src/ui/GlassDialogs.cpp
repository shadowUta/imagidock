#include "GlassDialogs.h"
#include "GlassDialog.h"
#include "UiMetrics.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString buttonText(QDialogButtonBox::StandardButton button) {
    switch(button) {
    case QDialogButtonBox::Ok:return QObject::tr("确定");
    case QDialogButtonBox::Save:return QObject::tr("保存");
    case QDialogButtonBox::SaveAll:return QObject::tr("全部保存");
    case QDialogButtonBox::Open:return QObject::tr("打开");
    case QDialogButtonBox::Yes:return QObject::tr("是");
    case QDialogButtonBox::YesToAll:return QObject::tr("全部选是");
    case QDialogButtonBox::No:return QObject::tr("否");
    case QDialogButtonBox::NoToAll:return QObject::tr("全部选否");
    case QDialogButtonBox::Abort:return QObject::tr("中止");
    case QDialogButtonBox::Retry:return QObject::tr("重试");
    case QDialogButtonBox::Ignore:return QObject::tr("忽略");
    case QDialogButtonBox::Close:return QObject::tr("关闭");
    case QDialogButtonBox::Cancel:return QObject::tr("取消");
    case QDialogButtonBox::Discard:return QObject::tr("放弃");
    case QDialogButtonBox::Help:return QObject::tr("帮助");
    case QDialogButtonBox::Apply:return QObject::tr("应用");
    case QDialogButtonBox::Reset:return QObject::tr("重置");
    case QDialogButtonBox::RestoreDefaults:return QObject::tr("恢复默认");
    default:return {};
    }
}

void localizeButtons(QDialogButtonBox *buttons) {
    for(auto button:buttons->buttons()) {
        const auto text=buttonText(buttons->standardButton(button));
        if(!text.isEmpty())button->setText(text);
        button->setAccessibleName(button->text());
        button->setMinimumHeight(UiMetrics::ControlHeight);
    }
}

class DialogShell final : public GlassDialog {
public:
    using GlassDialog::GlassDialog;
    QPointer<QWidget> initialFocus;
protected:
    void showEvent(QShowEvent *event) override {
        GlassDialog::showEvent(event);
        const auto focus=initialFocus;
        QTimer::singleShot(0,this,[focus]{if(focus&&focus->isVisible()&&focus->isEnabled())focus->setFocus(Qt::OtherFocusReason);});
    }
};

void fitDialog(DialogShell &dialog,const QSize &preferred) {
    auto screen=dialog.parentWidget()?dialog.parentWidget()->screen():QApplication::primaryScreen();
    const auto available=screen?screen->availableGeometry():QRect(0,0,1024,768);
    const auto room=available.adjusted(16,16,-16,-16);
    if(dialog.layout())dialog.layout()->setSizeConstraint(QLayout::SetNoConstraint);
    dialog.setMinimumSize(qMin(320,room.width()),qMin(160,room.height()));
    dialog.setMaximumSize(room.size());
    dialog.resize(preferred.boundedTo(room.size()));
    const auto center=dialog.parentWidget()?dialog.parentWidget()->window()->frameGeometry().center():room.center();
    dialog.move(qBound(room.left(),center.x()-dialog.width()/2,room.right()-dialog.width()+1),
                qBound(room.top(),center.y()-dialog.height()/2,room.bottom()-dialog.height()+1));
}

QScrollArea *scrollingLabel(const QString &text,const QString &name,QWidget *parent,QLabel **labelOut=nullptr) {
    auto scroll=new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto label=new QLabel(text);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft|Qt::AlignTop);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setAccessibleName(text);
    scroll->setWidget(label);
    scroll->viewport()->setAutoFillBackground(false);
    label->setAutoFillBackground(false);
    if(labelOut)*labelOut=label;
    return scroll;
}

QMessageBox::StandardButton dismissButton(QMessageBox::StandardButtons buttons) {
    // Closing the window never confirms a destructive or affirmative action.
    for(const auto button:{QMessageBox::Cancel,QMessageBox::No,QMessageBox::Close,QMessageBox::Abort,QMessageBox::Ok})
        if(buttons.testFlag(button))return button;
    return QMessageBox::NoButton;
}

QMessageBox::StandardButton showMessage(QWidget *parent,const QString &title,const QString &text,
    QMessageBox::Icon icon,QMessageBox::StandardButtons buttons,QMessageBox::StandardButton defaultButton) {
    DialogShell dialog(parent);
    dialog.setObjectName("glassMessageDialog");
    dialog.setWindowTitle(title);
    auto layout=new QVBoxLayout(&dialog);
    layout->setSpacing(UiMetrics::Gap);
    auto body=new QHBoxLayout;
    if(icon!=QMessageBox::NoIcon) {
        auto symbol=new QLabel;
        const auto standard=icon==QMessageBox::Warning?QStyle::SP_MessageBoxWarning:
            icon==QMessageBox::Question?QStyle::SP_MessageBoxQuestion:QStyle::SP_MessageBoxInformation;
        symbol->setPixmap(dialog.style()->standardIcon(standard).pixmap(32,32));
        symbol->setAlignment(Qt::AlignTop);
        body->addWidget(symbol);
    }
    body->addWidget(scrollingLabel(text,"glassMessageText",&dialog),1);
    layout->addLayout(body,1);
    auto box=new QDialogButtonBox(QDialogButtonBox::StandardButtons(int(buttons)),&dialog);
    box->setObjectName("glassDialogButtons");
    localizeButtons(box);
    layout->addWidget(box);
    auto result=dismissButton(buttons);
    QObject::connect(box,&QDialogButtonBox::clicked,&dialog,[&](QAbstractButton *button){
        result=QMessageBox::StandardButton(box->standardButton(button));
        dialog.accept();
    });
    auto chosen=defaultButton;
    if(chosen==QMessageBox::NoButton||!buttons.testFlag(chosen)) {
        chosen=QMessageBox::NoButton;
        for(auto button:box->buttons())if(box->buttonRole(button)==QDialogButtonBox::AcceptRole){chosen=QMessageBox::StandardButton(box->standardButton(button));break;}
        // With Yes/No there is no AcceptRole; Qt gives keyboard focus to its first button.
        // Make that same target explicit so the shell's close control cannot take Enter.
        if(chosen==QMessageBox::NoButton)for(uint mask=QMessageBox::FirstButton;mask<=QMessageBox::LastButton;mask<<=1)
            if(buttons.testFlag(QMessageBox::StandardButton(mask))){chosen=QMessageBox::StandardButton(mask);break;}
    }
    for(auto button:box->buttons())if(auto push=qobject_cast<QPushButton*>(button)){push->setAutoDefault(false);push->setDefault(false);}
    if(auto button=box->button(QDialogButtonBox::StandardButton(chosen))){button->setDefault(true);dialog.initialFocus=button;}
    const int lineHeight=dialog.fontMetrics().lineSpacing();
    fitDialog(dialog,QSize(500,qBound(160,(text.count('\n')+2)*lineHeight+108,460)));
    dialog.exec();
    return result;
}

QDialogButtonBox *inputButtons(DialogShell &dialog,QVBoxLayout *layout) {
    auto box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    box->setObjectName("glassDialogButtons");
    localizeButtons(box);
    box->button(QDialogButtonBox::Ok)->setDefault(true);
    layout->addWidget(box);
    QObject::connect(box,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    QObject::connect(box,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    return box;
}

// QFileDialog owns validation, overwrite confirmation and file-management behavior.
// Its internal messages keep their original buttons/events and receive only our chrome.
class FileMessageChrome final : public QObject {
public:
    explicit FileMessageChrome(QFileDialog *picker):QObject(picker),m_picker(picker){qApp->installEventFilter(this);}
    ~FileMessageChrome() override {qApp->removeEventFilter(this);}
protected:
    bool eventFilter(QObject *object,QEvent *event) override {
        if(event->type()!=QEvent::Polish)return false;
        auto message=qobject_cast<QMessageBox*>(object);
        if(!message)return false;
        bool owned=false;
        for(auto ancestor=message->parent();ancestor;ancestor=ancestor->parent())if(ancestor==m_picker){owned=true;break;}
        if(!owned)return false;
        message->setObjectName("glassMessageDialog");
        message->setTextFormat(Qt::PlainText);
        decorateGlassDialog(message);
        if(auto box=message->findChild<QDialogButtonBox*>())localizeButtons(box);
        return false;
    }
private:
    QFileDialog *m_picker;
};

QStringList chooseFiles(QWidget *parent,const QString &caption,const QString &dir,const QString &filter,
    QString *selectedFilter,QFileDialog::Options options,QFileDialog::FileMode mode,QFileDialog::AcceptMode acceptMode) {
    DialogShell dialog(parent);
    dialog.setObjectName("glassFileDialog");
    dialog.setWindowTitle(caption.isEmpty()?(mode==QFileDialog::Directory?QObject::tr("选择目录"):
        acceptMode==QFileDialog::AcceptSave?QObject::tr("保存文件"):QObject::tr("选择文件")):caption);
    auto layout=new QVBoxLayout(&dialog);
    auto scroll=new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto picker=new QFileDialog(&dialog,dialog.windowTitle(),dir,filter);
    picker->setOption(QFileDialog::DontUseNativeDialog,true);
    picker->setOptions(options|QFileDialog::DontUseNativeDialog);
    picker->setWindowFlags(Qt::Widget);
    picker->setObjectName("glassFilePicker");
    picker->setAccessibleName(dialog.windowTitle());
    picker->setWindowTitle(dialog.windowTitle());
    picker->setFileMode(mode);
    picker->setAcceptMode(acceptMode);
    if(selectedFilter&&!selectedFilter->isEmpty())picker->selectNameFilter(*selectedFilter);
    picker->setLabelText(QFileDialog::LookIn,QObject::tr("位置："));
    picker->setLabelText(QFileDialog::FileName,mode==QFileDialog::Directory?QObject::tr("目录："):QObject::tr("文件名："));
    picker->setLabelText(QFileDialog::FileType,QObject::tr("文件类型："));
    picker->setLabelText(QFileDialog::Accept,mode==QFileDialog::Directory?QObject::tr("选择目录"):
        acceptMode==QFileDialog::AcceptSave?QObject::tr("保存"):QObject::tr("打开"));
    picker->setLabelText(QFileDialog::Reject,QObject::tr("取消"));
    picker->setStyleSheet("QFileDialog#glassFilePicker { background: transparent; }");
    picker->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    picker->setMinimumSize(520,320);
    for(auto combo:picker->findChildren<QComboBox*>()) {
        combo->setMinimumContentsLength(1);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    }
    if(auto filename=picker->findChild<QLineEdit*>("fileNameEdit")) {
        filename->setAccessibleName(mode==QFileDialog::Directory?QObject::tr("所选目录"):QObject::tr("所选文件名"));
        dialog.initialFocus=filename;
    }
    if(auto box=picker->findChild<QDialogButtonBox*>()) {
        localizeButtons(box);
        // Keep the directory-specific accept label set above.
        picker->setLabelText(QFileDialog::Accept,mode==QFileDialog::Directory?QObject::tr("选择目录"):
            acceptMode==QFileDialog::AcceptSave?QObject::tr("保存"):QObject::tr("打开"));
    }
    new FileMessageChrome(picker);
    scroll->setWidget(picker);
    picker->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    layout->addWidget(scroll,1);
    QObject::connect(picker,&QDialog::accepted,&dialog,&QDialog::accept);
    QObject::connect(picker,&QDialog::rejected,&dialog,&QDialog::reject);
    fitDialog(dialog,QSize(860,600));
    if(dialog.exec()!=QDialog::Accepted)return {};
    if(selectedFilter)*selectedFilter=picker->selectedNameFilter();
    return picker->selectedFiles();
}
}

QMessageBox::StandardButton GlassMessages::information(QWidget *parent,const QString &title,const QString &text,QMessageBox::StandardButtons buttons,QMessageBox::StandardButton defaultButton){return showMessage(parent,title,text,QMessageBox::Information,buttons,defaultButton);}
QMessageBox::StandardButton GlassMessages::warning(QWidget *parent,const QString &title,const QString &text,QMessageBox::StandardButtons buttons,QMessageBox::StandardButton defaultButton){return showMessage(parent,title,text,QMessageBox::Warning,buttons,defaultButton);}
QMessageBox::StandardButton GlassMessages::question(QWidget *parent,const QString &title,const QString &text,QMessageBox::StandardButtons buttons,QMessageBox::StandardButton defaultButton){return showMessage(parent,title,text,QMessageBox::Question,buttons,defaultButton);}
void GlassMessages::about(QWidget *parent,const QString &title,const QString &text){showMessage(parent,title,text,QMessageBox::NoIcon,QMessageBox::Ok,QMessageBox::Ok);}

QString GlassInputs::getText(QWidget *parent,const QString &title,const QString &label,QLineEdit::EchoMode echo,const QString &text,bool *ok) {
    if(ok)*ok=false;
    DialogShell dialog(parent);
    dialog.setObjectName("glassInputDialog");
    dialog.setWindowTitle(title);
    auto layout=new QVBoxLayout(&dialog);
    QLabel *prompt=nullptr;
    layout->addWidget(scrollingLabel(label,"glassInputPrompt",&dialog,&prompt),1);
    auto field=new QLineEdit(text,&dialog);
    field->setObjectName("glassInputField");
    field->setAccessibleName(label);
    field->setEchoMode(echo);
    field->selectAll();
    prompt->setBuddy(field);
    layout->addWidget(field);
    inputButtons(dialog,layout);
    dialog.initialFocus=field;
    fitDialog(dialog,QSize(460,220));
    const bool accepted=dialog.exec()==QDialog::Accepted;
    if(ok)*ok=accepted;
    return accepted?field->text():QString();
}

QString GlassInputs::getItem(QWidget *parent,const QString &title,const QString &label,const QStringList &items,int current,bool editable,bool *ok) {
    if(ok)*ok=false;
    DialogShell dialog(parent);
    dialog.setObjectName("glassInputDialog");
    dialog.setWindowTitle(title);
    auto layout=new QVBoxLayout(&dialog);
    QLabel *prompt=nullptr;
    layout->addWidget(scrollingLabel(label,"glassInputPrompt",&dialog,&prompt),1);
    auto field=new QComboBox(&dialog);
    field->setObjectName("glassInputField");
    field->setAccessibleName(label);
    field->addItems(items);
    field->setEditable(editable);
    field->setCurrentIndex(current);
    if(editable&&field->lineEdit())field->lineEdit()->selectAll();
    field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    field->setMinimumContentsLength(1);
    prompt->setBuddy(field);
    layout->addWidget(field);
    inputButtons(dialog,layout);
    dialog.initialFocus=field;
    fitDialog(dialog,QSize(460,220));
    const bool accepted=dialog.exec()==QDialog::Accepted;
    if(ok)*ok=accepted;
    return accepted?field->currentText():QString();
}

QString GlassFiles::getOpenFileName(QWidget *parent,const QString &caption,const QString &dir,const QString &filter,QString *selectedFilter,QFileDialog::Options options){return chooseFiles(parent,caption,dir,filter,selectedFilter,options,QFileDialog::ExistingFile,QFileDialog::AcceptOpen).value(0);}
QStringList GlassFiles::getOpenFileNames(QWidget *parent,const QString &caption,const QString &dir,const QString &filter,QString *selectedFilter,QFileDialog::Options options){return chooseFiles(parent,caption,dir,filter,selectedFilter,options,QFileDialog::ExistingFiles,QFileDialog::AcceptOpen);}
QString GlassFiles::getSaveFileName(QWidget *parent,const QString &caption,const QString &dir,const QString &filter,QString *selectedFilter,QFileDialog::Options options){return chooseFiles(parent,caption,dir,filter,selectedFilter,options,QFileDialog::AnyFile,QFileDialog::AcceptSave).value(0);}
QString GlassFiles::getExistingDirectory(QWidget *parent,const QString &caption,const QString &dir,QFileDialog::Options options){return chooseFiles(parent,caption,dir,{},nullptr,options,QFileDialog::Directory,QFileDialog::AcceptOpen).value(0);}
