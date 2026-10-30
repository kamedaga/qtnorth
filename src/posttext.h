#pragma once
#include <QLabel>

class PostText : public QLabel {
    Q_OBJECT
public:
    explicit PostText(QString text, QWidget *parent = nullptr);
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    void setFoldingAllowed(bool allowed);
    void toggleExpanded();
    QString displayText() const { return m_visible; }
signals:
    void foldStateChanged(bool available, bool expanded);
protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    void refreshText();
    void updateFolding();
    int collapsedHeight(int width, bool *overflow = nullptr) const;
    QString m_plain, m_visible;
    mutable int m_cacheWidth = -1, m_cacheHeight = 0;
    mutable QFont m_cacheFont;
    mutable bool m_cacheOverflow = false;
    bool m_expanded = false, m_allowed = true, m_updating = false;
};
