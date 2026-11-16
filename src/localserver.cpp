#include "localserver.hpp"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegExp>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTime>

// ---------------------------------------------------------------- cipher

void Cipher::apply(char *data, qint64 length, qint64 offset)
{
    for (qint64 i = 0; i < length; ++i) {
        const qint64 pos = offset + i;
        const qint64 block = pos / 20;
        if (block != m_block) {
            QByteArray seed = m_key;
            seed.append(reinterpret_cast<const char *>(&block), sizeof(block));
            m_stream = QCryptographicHash::hash(seed, QCryptographicHash::Sha1);
            m_block = block;
        }
        data[i] ^= m_stream.at(int(pos % 20));
    }
}

// ---------------------------------------------------------------- server

static const qint64 CHUNK = 64 * 1024;
static const qint64 BUFFERED = 256 * 1024;

LocalServer::LocalServer(const QByteArray &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
    , m_server(new QTcpServer(this))
    , m_net(new QNetworkAccessManager(this))
    , m_next(0)
{
    // A random path part so other apps on the phone can't guess the URLs.
    m_token = QString::fromLatin1(QCryptographicHash::hash(
        key + QTime::currentTime().toString("hhmmsszzz").toLatin1(),
        QCryptographicHash::Sha1).toHex().left(16));
    connect(m_server, SIGNAL(newConnection()), this, SLOT(onNewConnection()));
    if (!m_server->listen(QHostAddress::LocalHost, 0))
        qWarning() << "LocalServer: cannot listen:" << m_server->errorString();
}

QString LocalServer::addEntry(const Entry &entry)
{
    // Reuse the id when the same file or link is played again.
    QHash<QString, Entry>::const_iterator it = m_entries.constBegin();
    for (; it != m_entries.constEnd(); ++it) {
        if (it.value().path == entry.path && it.value().remote == entry.remote)
            return QString("http://127.0.0.1:%1/%2/%3").arg(m_server->serverPort()).arg(m_token).arg(it.key());
    }
    // Stream links are signed and short-lived; don't keep hundreds around.
    if (m_entries.size() > 300)
        m_entries.clear();
    const QString id = QString::number(++m_next);
    m_entries.insert(id, entry);
    return QString("http://127.0.0.1:%1/%2/%3").arg(m_server->serverPort()).arg(m_token).arg(id);
}

QString LocalServer::url(const QString &path, const QString &mimeType)
{
    Entry e;
    e.path = path;
    e.mime = mimeType.isEmpty() ? QString("audio/mp4") : mimeType;
    return addEntry(e);
}

QString LocalServer::proxyUrl(const QString &remote)
{
    Entry e;
    e.remote = remote;
    e.mime = remote.contains(".flac") ? QString("audio/flac") : QString("audio/mp4");
    return addEntry(e);
}

void LocalServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        Conn *conn = new Conn;
        conn->file = 0;
        conn->pos = 0;
        conn->end = -1;
        conn->sending = false;
        conn->upstream = 0;
        conn->headerSent = false;
        conn->cipher = Cipher(m_key);
        m_conns.insert(socket, conn);
        connect(socket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
        connect(socket, SIGNAL(bytesWritten(qint64)), this, SLOT(onBytesWritten(qint64)));
        connect(socket, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
    }
}

void LocalServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    Conn *conn = m_conns.value(socket);
    if (!conn || conn->sending)
        return;
    conn->request += socket->readAll();
    if (conn->request.size() > 16 * 1024) {
        fail(socket, "400 Bad Request");
        return;
    }
    if (conn->request.contains("\r\n\r\n"))
        respond(socket, conn);
}

void LocalServer::respond(QTcpSocket *socket, Conn *conn)
{
    const QString text = QString::fromLatin1(conn->request);
    const QStringList lines = text.split("\r\n");
    const QStringList first = lines.value(0).split(' ');
    const QString method = first.value(0);
    const QStringList parts = first.value(1).split('/', QString::SkipEmptyParts);
    if ((method != "GET" && method != "HEAD") || parts.size() != 2 || parts.at(0) != m_token
            || !m_entries.contains(parts.at(1))) {
        fail(socket, "404 Not Found");
        return;
    }
    const Entry entry = m_entries.value(parts.at(1));

    if (!entry.remote.isEmpty()) {
        QString range;
        foreach (const QString &line, lines) {
            if (line.startsWith("range:", Qt::CaseInsensitive))
                range = line.mid(6).trimmed();
        }
        conn->sending = true;
        startProxy(socket, conn, entry, method == "HEAD" ? QString() : range);
        return;
    }

    conn->file = new QFile(entry.path);
    if (!conn->file->open(QIODevice::ReadOnly)) {
        fail(socket, "404 Not Found");
        return;
    }
    const qint64 size = conn->file->size();
    qint64 start = 0;
    qint64 end = size - 1;
    bool partial = false;
    QRegExp range("^range:\\s*bytes=(\\d*)-(\\d*)", Qt::CaseInsensitive);
    foreach (const QString &line, lines) {
        if (range.indexIn(line) == 0) {
            partial = true;
            if (!range.cap(1).isEmpty()) {
                start = range.cap(1).toLongLong();
                if (!range.cap(2).isEmpty())
                    end = qMin(range.cap(2).toLongLong(), size - 1);
            } else if (!range.cap(2).isEmpty()) {   // the last N bytes
                start = qMax(qint64(0), size - range.cap(2).toLongLong());
            }
        }
    }
    if (start > end && size > 0) {
        QByteArray head = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */"
            + QByteArray::number(size) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        socket->write(head);
        socket->disconnectFromHost();
        return;
    }

    QByteArray head = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
    if (partial)
        head += "Content-Range: bytes " + QByteArray::number(start) + "-" + QByteArray::number(end)
            + "/" + QByteArray::number(size) + "\r\n";
    head += "Content-Type: " + entry.mime.toLatin1() + "\r\n";
    head += "Content-Length: " + QByteArray::number(end - start + 1) + "\r\n";
    head += "Accept-Ranges: bytes\r\nConnection: close\r\n\r\n";
    socket->write(head);

    if (method == "HEAD") {
        socket->disconnectFromHost();
        return;
    }
    conn->sending = true;
    conn->pos = start;
    conn->end = end;
    conn->file->seek(start);
    sendMore(socket, conn);
}

void LocalServer::sendMore(QTcpSocket *socket, Conn *conn)
{
    while (conn->pos <= conn->end && socket->bytesToWrite() < BUFFERED) {
        QByteArray data = conn->file->read(qMin(CHUNK, conn->end - conn->pos + 1));
        if (data.isEmpty())
            break;
        conn->cipher.apply(data.data(), data.size(), conn->pos);
        conn->pos += data.size();
        socket->write(data);
    }
    if (conn->pos > conn->end || conn->file->atEnd()) {
        conn->sending = false;
        conn->end = -2;   // done; waiting for the socket to drain
        socket->disconnectFromHost();
    }
}

void LocalServer::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    Conn *conn = m_conns.value(socket);
    if (!conn)
        return;
    if (conn->upstream)
        drainUpstream(socket, conn);
    else if (conn->sending)
        sendMore(socket, conn);
}

// ---------------------------------------------------------------- relay

void LocalServer::startProxy(QTcpSocket *socket, Conn *conn, const Entry &entry, const QString &range)
{
    QNetworkRequest request(QUrl::fromEncoded(entry.remote.toLatin1()));
    if (!range.isEmpty())
        request.setRawHeader("Range", range.toLatin1());
    QNetworkReply *reply = m_net->get(request);
    reply->setReadBufferSize(BUFFERED * 2);   // don't download faster than it plays
    reply->setProperty("mime", entry.mime);
    conn->upstream = reply;
    m_upstream.insert(reply, socket);
    connect(reply, SIGNAL(metaDataChanged()), this, SLOT(onUpstreamHeaders()));
    connect(reply, SIGNAL(readyRead()), this, SLOT(onUpstreamData()));
    connect(reply, SIGNAL(finished()), this, SLOT(onUpstreamFinished()));
}

void LocalServer::onUpstreamHeaders()
{
    sendUpstreamHeaders(qobject_cast<QNetworkReply *>(sender()));
}

void LocalServer::sendUpstreamHeaders(QNetworkReply *reply)
{
    QTcpSocket *socket = m_upstream.value(reply);
    Conn *conn = m_conns.value(socket);
    if (!conn || conn->headerSent)
        return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status <= 0)
        return;   // not known yet
    conn->headerSent = true;
    QByteArray reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toByteArray();
    if (reason.isEmpty())
        reason = status == 206 ? "Partial Content" : (status == 200 ? "OK" : "Error");
    QByteArray type = reply->rawHeader("Content-Type");
    if (type.isEmpty() || type.contains("octet-stream"))
        type = reply->property("mime").toByteArray();
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    head += "Content-Type: " + type + "\r\n";
    if (reply->hasRawHeader("Content-Length"))
        head += "Content-Length: " + reply->rawHeader("Content-Length") + "\r\n";
    if (reply->hasRawHeader("Content-Range"))
        head += "Content-Range: " + reply->rawHeader("Content-Range") + "\r\n";
    head += "Accept-Ranges: bytes\r\nConnection: close\r\n\r\n";
    if (status >= 400)
        qWarning() << "LocalServer: TIDAL answered" << status << "for a stream";
    socket->write(head);
}

void LocalServer::drainUpstream(QTcpSocket *socket, Conn *conn)
{
    QNetworkReply *reply = conn->upstream;
    if (!reply || !conn->headerSent)
        return;
    while (reply->bytesAvailable() > 0 && socket->bytesToWrite() < BUFFERED)
        socket->write(reply->read(CHUNK));
}

void LocalServer::onUpstreamData()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    QTcpSocket *socket = m_upstream.value(reply);
    Conn *conn = m_conns.value(socket);
    if (!conn)
        return;
    if (!conn->headerSent) {
        // Some replies deliver data before metaDataChanged; send headers first.
        sendUpstreamHeaders(reply);
    }
    drainUpstream(socket, conn);
}

void LocalServer::onUpstreamFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    QTcpSocket *socket = m_upstream.take(reply);
    Conn *conn = m_conns.value(socket);
    reply->deleteLater();
    if (!conn)
        return;
    if (!conn->headerSent) {
        qWarning() << "LocalServer: stream failed:" << reply->errorString();
        conn->upstream = 0;
        fail(socket, "502 Bad Gateway");
        return;
    }
    // Hand over everything that is left, then close.
    while (reply->bytesAvailable() > 0)
        socket->write(reply->read(CHUNK));
    conn->upstream = 0;
    conn->sending = false;
    socket->disconnectFromHost();
}

void LocalServer::fail(QTcpSocket *socket, const char *status)
{
    socket->write(QByteArray("HTTP/1.1 ") + status + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    socket->disconnectFromHost();
}

void LocalServer::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    Conn *conn = m_conns.take(socket);
    if (conn) {
        if (conn->upstream) {
            // The player stopped reading (seek, next song): stop the download.
            QNetworkReply *reply = conn->upstream;
            m_upstream.remove(reply);
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
        delete conn->file;
        delete conn;
    }
    if (socket)
        socket->deleteLater();
}
