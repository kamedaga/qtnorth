#pragma once
#include <QUrl>
#include <QWidget>
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;

class LoginPage : public QWidget {
    Q_OBJECT
public:
    explicit LoginPage(QWidget *parent = nullptr);
    void setBusy(bool busy, const QString &message = {});
    void setMessage(const QString &message);
    void setSavedAvailable(bool available);
    void showDeviceCode(const QString &code, const QUrl &url);
    void clearCredentials();
    void enableMessages();
signals:
    void settingsRequested();
    void personalTokenRequested(QString token);
    void deviceLoginRequested(QString clientId, bool writeAccess, bool messages);
    void savedLoginRequested();
    void forgetRequested();
    void cancelDeviceRequested();
private:
    QTabWidget *m_tabs;
    QLineEdit *m_token;
    QLabel *m_message, *m_code;
    QPushButton *m_resume, *m_forget, *m_openBrowser, *m_cancel;
    QUrl m_verificationUrl;
    class QCheckBox *m_messages;
};
