#include "credentialstore.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <qt6keychain/keychain.h>
#include <memory>

CredentialStore::CredentialStore(QString key, QObject *parent) : QObject(parent), m_key(std::move(key)) {}

void CredentialStore::read(QObject *context, Callback callback) { enqueue({Read, {}, context, std::move(callback)}); }
void CredentialStore::write(QObject *context, const QByteArray &data, Callback callback) { enqueue({Write, data, context, std::move(callback)}); }
void CredentialStore::erase(QObject *context, Callback callback) { enqueue({Erase, {}, context, std::move(callback)}); }

void CredentialStore::enqueue(Task task)
{
    m_queue.enqueue(std::move(task));
    QTimer::singleShot(0, this, &CredentialStore::pump);
}

void CredentialStore::pump()
{
    if (m_running || m_queue.isEmpty()) return;
    m_running = true;
    auto task = std::make_shared<Task>(m_queue.dequeue());
    perform(task->action, task->data, [this, task](CredentialResult result) {
        const QPointer<CredentialStore> guard(this);
        if (task->context) task->callback(std::move(result));
        if (!guard) return;
        m_running = false;
        if (m_queue.isEmpty()) emit idle();
        else QTimer::singleShot(0, this, &CredentialStore::pump);
    });
}

namespace {
class KeychainStore : public CredentialStore {
public:
    using CredentialStore::CredentialStore;
protected:
    void perform(Action action, const QByteArray &data, Callback callback) override {
        QKeychain::Job *job = nullptr;
        if (action == Read) job = new QKeychain::ReadPasswordJob("qtNorth", this);
        else if (action == Write) {
            auto *write = new QKeychain::WritePasswordJob("qtNorth", this);
            write->setBinaryData(data); job = write;
        } else job = new QKeychain::DeletePasswordJob("qtNorth", this);
        job->setKey(key());
        job->setInsecureFallback(false);
        connect(job, &QKeychain::Job::finished, this, [job, action, callback] {
            if (job->error() != QKeychain::NoError && !(action != Write && job->error() == QKeychain::EntryNotFound)) {
                callback({{}, QObject::tr("資格情報ストアにアクセスできませんでした")}); return;
            }
            QByteArray bytes;
            if (action == Read && job->error() == QKeychain::NoError) bytes = static_cast<QKeychain::ReadPasswordJob *>(job)->binaryData();
            callback({bytes, {}});
        });
        job->start();
    }
};

#ifdef Q_OS_LINUX
// WSL usually has no Secret Service. Use its Windows user's Credential Manager.
class WindowsHostStore : public CredentialStore {
public:
    using CredentialStore::CredentialStore;
protected:
    void perform(Action action, const QByteArray &data, Callback callback) override {
        QFile file(":/scripts/windows-credentials.ps1");
        if (!file.open(QIODevice::ReadOnly)) { callback({{}, QObject::tr("資格情報ストアを開けませんでした")}); return; }
        const auto script = QString::fromUtf8(file.readAll());
        QByteArray utf16;
        for (const auto character : script) {
            const ushort u = character.unicode();
            utf16.append(char(u & 0xff)); utf16.append(char(u >> 8));
        }
        auto *process = new QProcess(this);
        auto *timeout = new QTimer(process);
        timeout->setSingleShot(true);
        auto completed = std::make_shared<bool>(false);
        auto finish = [process, completed, callback](CredentialResult result) {
            if (*completed) return;
            *completed = true;
            callback(std::move(result));
            process->deleteLater();
        };
        connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError) {
            finish({{}, QObject::tr("Windowsの資格情報ストアを開けませんでした")});
        });
        connect(timeout, &QTimer::timeout, process, [process, finish] {
            process->kill(); finish({{}, QObject::tr("資格情報ストアが応答しませんでした")});
        });
        connect(process, &QProcess::finished, this, [process, timeout, finish](int code, QProcess::ExitStatus status) {
            timeout->stop();
            if (status != QProcess::NormalExit || code != 0) {
                finish({{}, QObject::tr("Windowsの資格情報ストアにアクセスできませんでした")}); return;
            }
            const auto result = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
            if (!result.value("ok").toBool()) { finish({{}, QObject::tr("資格情報を保存・取得できませんでした")}); return; }
            finish({QByteArray::fromBase64(result.value("data").toString().toLatin1()), {}});
        });
        process->setProgram("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe");
        // Only fixed helper code is in the command line. Credentials travel over stdin.
        process->setArguments({"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-EncodedCommand", QString::fromLatin1(utf16.toBase64())});
        process->setWorkingDirectory("/mnt/c/Windows/Temp");
        process->start();
        const QJsonObject request{{"operation", action == Read ? "read" : action == Write ? "write" : "erase"},
            {"key", "qtNorth:session:" + key()}, {"data", QString::fromLatin1(data.toBase64())}};
        process->write(QJsonDocument(request).toJson(QJsonDocument::Compact));
        process->closeWriteChannel();
        timeout->start(15000);
    }
};
#endif
}

CredentialStore *createCredentialStore(const QUrl &apiBase, QObject *parent)
{
    const auto key = QString::fromLatin1(QCryptographicHash::hash(apiBase.toEncoded(), QCryptographicHash::Sha256).toHex().left(24));
#ifdef Q_OS_LINUX
    if (QFileInfo::exists("/proc/sys/fs/binfmt_misc/WSLInterop") &&
        QFileInfo::exists("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")) return new WindowsHostStore(key, parent);
#endif
    return new KeychainStore(key, parent);
}
