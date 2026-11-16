#ifndef PLAYER_HPP
#define PLAYER_HPP

#include <QAtomicInt>
#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include <bb/multimedia/MediaError>
#include <bb/multimedia/MediaPlayer>
#include <bb/multimedia/MediaState>
#include <bb/multimedia/NowPlayingConnection>

class Downloads;

// Owns the system media player on a background thread. Opening a network
// stream blocks for seconds, which must not happen on the interface thread.
class PlayerWorker : public QObject
{
    Q_OBJECT

public:
    PlayerWorker();

    // Number of the most recent load request; older ones are skipped.
    static QAtomicInt latest;

public slots:
    void init();
    void load(const QString &url, int generation);
    void resume();
    void pause();
    void seek(int ms);

signals:
    void started(int generation);
    void failed(int generation);
    void completed(int generation);
    void playingChanged(bool playing);
    void position(int ms);
    void duration(int ms);

private slots:
    void onMediaState(bb::multimedia::MediaState::Type state);
    void onPosition(unsigned int ms);
    void onDuration(unsigned int ms);
    void onCompleted();
    void onError(bb::multimedia::MediaError::Type error, unsigned int position);

private:
    bb::multimedia::MediaPlayer *m_mp;
    int m_generation;
    int m_lastPosition;
    bool m_playing;
};

// Play queue. Registers with the system "now playing" service so the lock
// screen, the media key and headset buttons control it.
class Player : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged)
    Q_PROPERTY(QString cover READ cover NOTIFY trackChanged)
    Q_PROPERTY(int position READ position NOTIFY positionChanged)
    Q_PROPERTY(int duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(QVariantMap track READ track NOTIFY trackChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY trackChanged)

public:
    explicit Player(QObject *parent = 0);
    ~Player();

    bool hasTrack() const { return m_pos >= 0; }
    QString title() const { return m_track.value("title").toString(); }
    QString artist() const { return m_track.value("artist").toString(); }
    QString album() const { return m_track.value("album").toString(); }
    QString cover() const { return m_track.value("img").toString(); }
    int position() const { return m_position; }
    int duration() const { return m_duration; }
    bool playing() const { return m_playing; }
    bool loading() const { return m_loading; }
    QString status() const { return m_status; }
    bool shuffle() const { return m_shuffle; }
    QVariantMap track() const { return m_track; }
    bool offline() const { return m_offline; }
    void setDownloads(Downloads *downloads) { m_downloads = downloads; }
    void setShuffle(bool on);

    // tracks: list of maps with title, artist, album, img, stream, duration (s)
    Q_INVOKABLE void playList(const QVariantList &tracks, int index);

    // The answer to needStreamUrl: a TIDAL stream link, or empty on failure.
    Q_INVOKABLE void provideStream(int generation, const QString &url);

public slots:
    void toggle();
    void play();
    void pause();
    void next();
    void previous();
    void seek(int ms);

signals:
    void trackChanged();
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void loadingChanged();
    void statusChanged();
    void shuffleChanged();
    void needStreamUrl(const QString &trackId, int generation);

private slots:
    void onAcquired();
    void onRevoked();
    void onStarted(int generation);
    void onFailed(int generation);
    void onCompleted(int generation);
    void onPlaying(bool playing);
    void onPosition(int ms);
    void onDuration(int ms);

private:
    void playAt(int index);
    void startLoad();
    void loadUrl(const QString &url);
    void shuffleAround(int index);
    void setLoading(bool loading);
    void setStatus(const QString &text);

    QThread m_thread;
    PlayerWorker *m_worker;
    bb::multimedia::NowPlayingConnection *m_npc;
    QVariantList m_queue;      // play order
    QVariantList m_original;   // order as listed
    bool m_shuffle;
    bool m_offline;
    Downloads *m_downloads;
    QVariantMap m_track;
    int m_pos;
    int m_generation;
    int m_position;
    int m_duration;
    bool m_playing;
    bool m_loading;
    bool m_acquired;
    bool m_pendingLoad;
    int m_failures;
    QString m_status;
};

#endif
