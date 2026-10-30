#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>

struct Request {
    QByteArray method;
    QUrl url;
    QMap<QByteArray, QByteArray> headers;
    QJsonObject body;
    QByteArray rawBody;
};
struct Response {
    int status = 200;
    QJsonObject json;
    QMap<QByteArray, QByteArray> headers;
    int delay = 0;
    QByteArray raw;
};
class Server : public QTcpServer {
public:
    QList<Request> requests;
    std::function<Response(Request)> handler;
    Server() {
        if (!listen(QHostAddress::LocalHost)) qFatal("Cannot listen on loopback");
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                auto consumed = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer, consumed] {
                    *buffer += socket->readAll();
                    if (*consumed) return;
                    const auto end = buffer->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const auto lines = buffer->left(end).split('\n');
                    const auto first = lines.first().trimmed().split(' ');
                    Request req;
                    req.method = first[0];
                    req.url = QUrl::fromEncoded(first[1]);
                    for (int i = 1; i < lines.size(); ++i) {
                        const auto colon = lines[i].indexOf(':');
                        if (colon > 0) req.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
                    }
                    const int length = req.headers.value("content-length").toInt();
                    if (buffer->size() < end + 4 + length) return;
                    req.rawBody = buffer->mid(end + 4, length);
                    req.body = QJsonDocument::fromJson(req.rawBody).object();
                    *consumed = true;
                    requests.append(req);
                    const auto response = handler ? handler(req) : Response{200, {{"data", QJsonObject{{"ok", true}}}}};
                    QTimer::singleShot(response.delay, socket, [socket, response] {
                        if (socket->state() != QAbstractSocket::ConnectedState) return;
                        const auto bytes = response.raw.isEmpty() ? QJsonDocument(response.json).toJson(QJsonDocument::Compact) : response.raw;
                        QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status) + " Status\r\nConnection: close\r\n";
                        if (!response.headers.contains("Content-Length")) head += "Content-Length: " + QByteArray::number(bytes.size()) + "\r\n";
                        if (!response.headers.contains("Content-Type")) head += "Content-Type: application/json\r\n";
                        for (auto it = response.headers.begin(); it != response.headers.end(); ++it) head += it.key() + ": " + it.value() + "\r\n";
                        socket->write(head + "\r\n" + bytes);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
    }
    QUrl base() const { return QUrl("http://127.0.0.1:" + QString::number(serverPort())); }
};
