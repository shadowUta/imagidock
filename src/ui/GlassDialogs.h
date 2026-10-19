#pragma once
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>

// Static-dialog conveniences with the application's own window shell.
namespace GlassMessages {
QMessageBox::StandardButton information(QWidget *parent,const QString &title,const QString &text,
    QMessageBox::StandardButtons buttons=QMessageBox::Ok,QMessageBox::StandardButton defaultButton=QMessageBox::NoButton);
QMessageBox::StandardButton warning(QWidget *parent,const QString &title,const QString &text,
    QMessageBox::StandardButtons buttons=QMessageBox::Ok,QMessageBox::StandardButton defaultButton=QMessageBox::NoButton);
QMessageBox::StandardButton question(QWidget *parent,const QString &title,const QString &text,
    QMessageBox::StandardButtons buttons=QMessageBox::Yes|QMessageBox::No,QMessageBox::StandardButton defaultButton=QMessageBox::NoButton);
void about(QWidget *parent,const QString &title,const QString &text);
}

namespace GlassInputs {
QString getText(QWidget *parent,const QString &title,const QString &label,QLineEdit::EchoMode echo=QLineEdit::Normal,
    const QString &text={},bool *ok=nullptr);
QString getItem(QWidget *parent,const QString &title,const QString &label,const QStringList &items,
    int current=0,bool editable=true,bool *ok=nullptr);
}

namespace GlassFiles {
QString getOpenFileName(QWidget *parent=nullptr,const QString &caption={},const QString &dir={},
    const QString &filter={},QString *selectedFilter=nullptr,QFileDialog::Options options={});
QStringList getOpenFileNames(QWidget *parent=nullptr,const QString &caption={},const QString &dir={},
    const QString &filter={},QString *selectedFilter=nullptr,QFileDialog::Options options={});
QString getSaveFileName(QWidget *parent=nullptr,const QString &caption={},const QString &dir={},
    const QString &filter={},QString *selectedFilter=nullptr,QFileDialog::Options options={});
QString getExistingDirectory(QWidget *parent=nullptr,const QString &caption={},const QString &dir={},
    QFileDialog::Options options=QFileDialog::ShowDirsOnly);
}
