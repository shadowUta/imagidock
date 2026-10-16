#pragma once
#include <QDialog>
#include <QPixmap>
#include <QPointer>
class QToolButton;

// Keeps application dialogs inside the same opaque, native-renderer-safe shell.
class GlassDialog : public QDialog {
    Q_OBJECT
public:
    explicit GlassDialog(QWidget *parent=nullptr);
protected:
    void paintEvent(QPaintEvent *) override;
    void done(int result) override;
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool nativeEvent(const QByteArray &,void *,qintptr *) override;
private:
    QToolButton *m_close;
    QPointer<QWidget> m_restoreFocus;
    QPixmap m_background;
    QString m_theme;
    bool m_inset=false;
    void updateShape();
};
