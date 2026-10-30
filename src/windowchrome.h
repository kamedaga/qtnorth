#pragma once
#include <QMenuBar>
class QPushButton;

class WindowTitleBar : public QMenuBar {
    Q_OBJECT
public:
    explicit WindowTitleBar(QWidget *window, bool fullControls = true, bool showCaption = true);
    void setMenusVisible(bool visible);
    QPushButton *closeButton() const { return m_close; }
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void toggleMaximized();
    void updateState();
    QWidget *m_window;
    QPushButton *m_maximize = nullptr, *m_close = nullptr;
};
