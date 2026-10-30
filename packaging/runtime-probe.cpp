// Desktop runtime check only: never reads credentials or contacts north.
#include <QApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QSslSocket>
#include <QTextStream>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget window; window.setWindowTitle("qtNorth — package verification");
    window.resize(480, 320); window.show(); app.processEvents();
    const auto formats = QImageReader::supportedImageFormats();
    const bool tls = QSslSocket::supportsSsl();
    QJsonArray available;
    for (const auto &format : formats) available.append(QString::fromLatin1(format));
    QTextStream(stdout) << QJsonDocument(QJsonObject{
        {"qt", QString::fromLatin1(qVersion())}, {"platform", app.platformName()},
        {"plugins", QLibraryInfo::path(QLibraryInfo::PluginsPath)},
        {"tls", tls}, {"imageFormats", available}}).toJson(QJsonDocument::Compact) << Qt::endl;
    return tls && formats.contains("jpeg") && formats.contains("webp") && formats.contains("svg") ? 0 : 1;
}
