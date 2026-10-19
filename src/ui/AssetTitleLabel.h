#pragma once
#include <QLabel>
#include <QResizeEvent>

// Keep long asset names readable without cutting a line through its glyphs.
class AssetTitleLabel final : public QLabel {
public:
    using QLabel::QLabel;
    void setTitle(const QString &title) {
        m_title=title;
        setToolTip(title);
        setAccessibleName(title);
        updateElision();
    }
protected:
    void resizeEvent(QResizeEvent *event) override { QLabel::resizeEvent(event);updateElision(); }
    void changeEvent(QEvent *event) override {
        QLabel::changeEvent(event);
        if(event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange)updateElision();
    }
private:
    QString m_title;
    void updateElision() { QLabel::setText(fontMetrics().elidedText(m_title,Qt::ElideMiddle,qMax(0,contentsRect().width()-4))); }
};
