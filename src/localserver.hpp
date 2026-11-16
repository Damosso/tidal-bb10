#ifndef LOCALSERVER_HPP
#define LOCALSERVER_HPP

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;

// Keystream cipher for downloaded songs. Works at any byte offset, so files
// can be decrypted while seeking. Encrypting and decrypting are the same.
class Cipher
{
public:
    Cipher() : m_block(-1) {}
    explicit Cipher(const QByteArray &key) : m_key(key), m_block(-1) {}
    void apply(char *data, qint64 length, qint64 offset);

private:
    QByteArray m_key;
    QByteArray m_stream;
    qint64 m_block;
};

// Tiny HTTP server on 127.0.0.1 for the media player:
// - url(): plays an encrypted download, decrypting the file as it reads;
// - proxyUrl(): relays a song from TIDAL's servers, so the old BB10 media
//   player never has to make modern secure connections itself.
class LocalServer : public QObject
{
    Q_OBJECT

public:
    LocalServer(const QByteArray &key, QObject *parent = 0);

    // URL the media player can stream the encrypted file from.
    QString url(const QString &path, const QString &mimeType);

    // URL the media player can stream a remote (https) song from.
    QString proxyUrl(const QString &remote);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onBytesWritten(qint64 bytes);
    void onDisconnected();
    void onUpstreamHeaders();
    void onUpstreamData();
    void onUpstreamFinished();

private:
    struct Entry {
        QString path;
        QString mime;
        QString remote;   // set for relayed songs
    };
    struct Conn {
        QByteArray request;
        QFile *file;
        Cipher cipher;
        qint64 pos;
        qint64 end;
        bool sending;
        QNetworkReply *upstream;
        bool headerSent;
    };

    void respond(QTcpSocket *socket, Conn *conn);
    void sendMore(QTcpSocket *socket, Conn *conn);
    void fail(QTcpSocket *socket, const char *status);
    void startProxy(QTcpSocket *socket, Conn *conn, const Entry &entry, const QString &range);
    void drainUpstream(QTcpSocket *socket, Conn *conn);
    void sendUpstreamHeaders(QNetworkReply *reply);
    QString addEntry(const Entry &entry);

    QByteArray m_key;
    QString m_token;
    QTcpServer *m_server;
    QHash<QString, Entry> m_entries;
    QHash<QTcpSocket *, Conn *> m_conns;
    QHash<QNetworkReply *, QTcpSocket *> m_upstream;
    QNetworkAccessManager *m_net;
    int m_next;
};

#endif
