#pragma once
#include <QFrame>
#include <QDeadlineTimer>
#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QTextEdit>
#include <QVector>

class NorthApi;
class QLabel;
class QPushButton;
class QLineEdit;
class QComboBox;
class QVBoxLayout;
class QGridLayout;
class QFileDialog;

class ComposerEditor : public QTextEdit {
    Q_OBJECT
public:
    explicit ComposerEditor(QWidget *parent = nullptr);
    QString northText() const;
    void toggleBold();
    void toggleItalic();
    void toggleList();
};

class PostComposer : public QFrame {
    Q_OBJECT
public:
    PostComposer(NorthApi *api, bool compact, QJsonObject reply = {}, QWidget *parent = nullptr);
    void setUser(const QJsonObject &user);
    bool addImages(const QStringList &paths);
    ComposerEditor *editor() const { return m_editor; }
    bool busy() const { return m_busy; }
signals:
    void posted(QJsonObject post);
private:
    void validate();
    void showError(const QString &message);
    void clearDraft();
    void pickImages();
    void renderImages();
    void showEmojiPicker();
    void submit();
    void uploadNext(quint64 attempt);
    void sendPost(quint64 attempt);
    void waitForImage(int index, quint64 attempt, int delay = 1500);
    struct Image { QString name, id; QByteArray bytes, mime; QImage preview; bool pending = false; };
    NorthApi *m_api;
    bool m_busy = false;
    quint64 m_attempt = 0;
    QJsonObject m_reply, m_pendingBody;
    QDeadlineTimer m_mediaDeadline{0};
    QByteArray m_key, m_lastPayload;
    QVector<Image> m_images;
    ComposerEditor *m_editor;
    QWidget *m_avatarHost, *m_imageHost, *m_pollOptions;
    QVBoxLayout *m_avatarLayout;
    QGridLayout *m_imageLayout;
    QLabel *m_error, *m_count;
    QPushButton *m_attach, *m_list, *m_emoji, *m_bold, *m_italic, *m_poll, *m_send;
    QVector<QLineEdit *> m_choices;
    QComboBox *m_duration;
    QPointer<QWidget> m_picker;
    QPointer<QFileDialog> m_filePicker;
};
