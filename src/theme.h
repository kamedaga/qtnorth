#pragma once
#include <QObject>
#include <QPalette>
class QMenu;
class QWidget;
class QToolButton;

class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum Mode { System, Light, Dark };
    static ThemeManager *instance();
    Mode mode() const { return m_mode; }
    bool isDark() const;
    void setMode(Mode mode);
signals:
    void changed();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    ThemeManager();
    void apply();
    Mode m_mode = System;
    bool m_systemDark = false, m_applying = false;
};

QMenu *appearanceMenu(QWidget *parent);
QToolButton *appearanceButton(QWidget *parent);
