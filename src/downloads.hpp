#ifndef DOWNLOADS_HPP
#define DOWNLOADS_HPP

#include <QFile>
#include <QFutureWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "localserver.hpp"

class App;
class QNetworkAccessManager;
class QNetworkReply;
class QSettings;

// Songs saved for offline listening, on the phone or the SD card.
// Like TIDAL's own offline mode, files are encrypted with a key that only
// this app has, and only play while the app has confirmed the subscription
// with the bridge in the last 30 days.
class Downloads : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int queued READ queued NOTIFY changed)
    Q_PROPERTY(QString current READ current NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool expired READ expired NOTIFY changed)
    Q_PROPERTY(QString usage READ usage NOTIFY changed)
    Q_PROPERTY(bool useSdCard READ useSdCard WRITE setUseSdCard NOTIFY changed)
    Q_PROPERTY(bool sdCardAvailable READ sdCardAvailable NOTIFY changed)
    Q_PROPERTY(int misplaced READ misplaced NOTIFY changed)
    Q_PROPERTY(bool moving READ moving NOTIFY changed)

public:
    explicit Downloads(App *app, QObject *parent = 0);

    int count() const;
    int queued() const;          // waiting, including the one in progress
    QString current() const;     // title of the song downloading now
    int progress() const { return m_progress; }
    bool expired() const;
    QString usage() const;
    bool useSdCard() const;
    void setUseSdCard(bool on);
    bool sdCardAvailable() const;
    int misplaced() const;       // downloads not in the chosen storage
    bool moving() const { return m_moving; }

    // track: a list row with id, title, artist, album, albumId, artistId,
    // duration and img (cover URL)
    Q_INVOKABLE void add(const QVariantMap &track);
    Q_INVOKABLE void addAll(const QVariantList &tracks);
    Q_INVOKABLE QString state(const QString &id) const;   // "", "queued", "downloading", "done"
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void removeAll();
    Q_INVOKABLE void cancelQueue();
    Q_INVOKABLE QVariantList list() const;   // rows for the Downloads page
    Q_INVOKABLE void markVerified();         // TIDAL confirmed the subscription
    Q_INVOKABLE void moveAll();              // into the chosen storage

    // URL the player can use for a downloaded song, or empty if the song is
    // missing (SD card removed) or the 30 days have passed.
    QString localUrl(const QString &id) const;

    // URL the media player can use for a TIDAL stream link (relayed).
    QString proxyUrl(const QString &remote) { return m_server->proxyUrl(remote); }

    // The answer to needStreamUrl: a TIDAL stream link, or empty on failure.
    Q_INVOKABLE void provideStream(const QString &id, const QString &url);

    struct MoveJob {
        QString id;
        QString src, dst, srcCover, dstCover, mime;
        bool encrypt;
    };

signals:
    void changed();
    void progressChanged();
    void finishedOne(const QString &title);
    void failedOne(const QString &title);
    void moveFinished(int moved, int failed);
    void needStreamUrl(const QString &id);

private slots:
    void onAudioData();
    void onAudioProgress(qint64 received, qint64 total);
    void onAudioFinished();
    void onCoverFinished();
    void onMoveFinished();

private:
    void next();
    void finishCurrent();
    bool inQueue(const QString &id) const;
    QString privateDir() const;
    QString sdDir() const;
    QString targetDir() const;
    QString key(const QString &id) const { return "t" + id; }
    static QByteArray loadKey();

    App *m_app;
    QNetworkAccessManager *m_net;
    QSettings *m_index;
    QByteArray m_cryptKey;
    LocalServer *m_server;
    QList<QVariantMap> m_queue;
    QVariantMap m_cur;
    QString m_curDir;
    QString m_curMime;
    QNetworkReply *m_reply;
    QFile m_file;
    Cipher m_cipher;
    qint64 m_written;
    QString m_audioPath;
    int m_progress;
    bool m_busy;

    bool m_moving;
    QList<MoveJob> m_moveJobs;
    QFutureWatcher<QStringList> m_moveWatcher;
};

#endif
