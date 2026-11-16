#include "player.hpp"

#include "downloads.hpp"

#include <QDebug>
#include <QMetaObject>
#include <QSettings>
#include <QTime>
#include <QUrl>

using namespace bb::multimedia;

// ---------------------------------------------------------------- worker

QAtomicInt PlayerWorker::latest(0);

PlayerWorker::PlayerWorker()
    : m_mp(0)
    , m_generation(0)
    , m_lastPosition(0)
    , m_playing(false)
{
}

void PlayerWorker::init()
{
    m_mp = new MediaPlayer(this);
    bool ok = true;
    ok &= connect(m_mp, SIGNAL(mediaStateChanged(bb::multimedia::MediaState::Type)),
                  this, SLOT(onMediaState(bb::multimedia::MediaState::Type)));
    ok &= connect(m_mp, SIGNAL(positionChanged(unsigned int)), this, SLOT(onPosition(unsigned int)));
    ok &= connect(m_mp, SIGNAL(durationChanged(unsigned int)), this, SLOT(onDuration(unsigned int)));
    ok &= connect(m_mp, SIGNAL(playbackCompleted()), this, SLOT(onCompleted()));
    ok &= connect(m_mp, SIGNAL(error(bb::multimedia::MediaError::Type, unsigned int)),
                  this, SLOT(onError(bb::multimedia::MediaError::Type, unsigned int)));
    if (!ok)
        qWarning() << "PlayerWorker: some signal connections failed";
}

void PlayerWorker::load(const QString &url, int generation)
{
    // The user may have tapped several tracks while an earlier one was
    // still opening; only the newest request is worth loading.
    if (generation != int(latest))
        return;
    m_generation = generation;
    m_lastPosition = 0;
    m_mp->reset();
    m_mp->setSourceUrl(QUrl(url));
    MediaError::Type err = m_mp->play();   // blocks while the stream opens
    if (err != MediaError::None) {
        qWarning() << "PlayerWorker: play() failed with media error" << int(err) << url;
        emit failed(generation);
        return;
    }
    emit started(generation);
}

void PlayerWorker::resume()
{
    m_mp->play();
}

void PlayerWorker::pause()
{
    m_mp->pause();
}

void PlayerWorker::seek(int ms)
{
    m_lastPosition = -100000;   // report the new position straight away
    m_mp->seekTime(ms < 0 ? 0 : ms);
}

void PlayerWorker::onMediaState(MediaState::Type state)
{
    bool playing = (state == MediaState::Started);
    if (playing != m_playing) {
        m_playing = playing;
        emit playingChanged(playing);
    }
}

void PlayerWorker::onPosition(unsigned int ms)
{
    int p = int(ms);
    if (p - m_lastPosition >= 500 || p < m_lastPosition) {
        m_lastPosition = p;
        emit position(p);
    }
}

void PlayerWorker::onDuration(unsigned int ms)
{
    if (ms > 0)
        emit duration(int(ms));
}

void PlayerWorker::onCompleted()
{
    emit completed(m_generation);
}

void PlayerWorker::onError(MediaError::Type error, unsigned int position)
{
    Q_UNUSED(position);
    qWarning() << "PlayerWorker: media error" << int(error);
    emit failed(m_generation);
}

// ---------------------------------------------------------------- player

Player::Player(QObject *parent)
    : QObject(parent)
    , m_worker(new PlayerWorker())
    , m_npc(new NowPlayingConnection("tidalbb", this))
    , m_shuffle(false)
    , m_offline(false)
    , m_downloads(0)
    , m_pos(-1)
    , m_generation(0)
    , m_position(0)
    , m_duration(0)
    , m_playing(false)
    , m_loading(false)
    , m_acquired(false)
    , m_pendingLoad(false)
    , m_failures(0)
{
    qsrand(uint(QTime(0, 0).msecsTo(QTime::currentTime())) ^ uint(quintptr(this)));
    m_shuffle = QSettings("tidalbb", "tidalbb").value("shuffle", false).toBool();

    m_worker->moveToThread(&m_thread);
    bool ok = true;
    ok &= connect(&m_thread, SIGNAL(started()), m_worker, SLOT(init()));
    ok &= connect(&m_thread, SIGNAL(finished()), m_worker, SLOT(deleteLater()));
    ok &= connect(m_worker, SIGNAL(started(int)), this, SLOT(onStarted(int)));
    ok &= connect(m_worker, SIGNAL(failed(int)), this, SLOT(onFailed(int)));
    ok &= connect(m_worker, SIGNAL(completed(int)), this, SLOT(onCompleted(int)));
    ok &= connect(m_worker, SIGNAL(playingChanged(bool)), this, SLOT(onPlaying(bool)));
    ok &= connect(m_worker, SIGNAL(position(int)), this, SLOT(onPosition(int)));
    ok &= connect(m_worker, SIGNAL(duration(int)), this, SLOT(onDuration(int)));

    ok &= connect(m_npc, SIGNAL(acquired()), this, SLOT(onAcquired()));
    ok &= connect(m_npc, SIGNAL(revoked()), this, SLOT(onRevoked()));
    ok &= connect(m_npc, SIGNAL(play()), this, SLOT(play()));
    ok &= connect(m_npc, SIGNAL(pause()), this, SLOT(pause()));
    ok &= connect(m_npc, SIGNAL(next()), this, SLOT(next()));
    ok &= connect(m_npc, SIGNAL(previous()), this, SLOT(previous()));
    if (!ok)
        qWarning() << "Player: some signal connections failed";

    m_thread.start();
}

Player::~Player()
{
    m_thread.quit();
    m_thread.wait(3000);
}

void Player::playList(const QVariantList &tracks, int index)
{
    if (index < 0 || index >= tracks.size())
        return;
    m_original = tracks;
    m_queue = tracks;
    m_failures = 0;
    if (m_shuffle) {
        shuffleAround(index);
        playAt(0);
    } else {
        playAt(index);
    }
}

// Reorders the queue: the given track first, the rest of the list after it
// in random order.
void Player::shuffleAround(int index)
{
    QVariantList rest = m_original;
    QVariant first = rest.takeAt(index);
    for (int i = rest.size() - 1; i > 0; --i)
        rest.swap(i, qrand() % (i + 1));
    m_queue.clear();
    m_queue.append(first);
    m_queue += rest;
    m_pos = 0;
}

void Player::setShuffle(bool on)
{
    if (on == m_shuffle)
        return;
    m_shuffle = on;
    QSettings("tidalbb", "tidalbb").setValue("shuffle", on);
    emit shuffleChanged();
    if (!hasTrack())
        return;

    // Keep the current track playing; only what comes after it changes.
    const QString id = m_track.value("id").toString();
    int index = 0;
    for (int i = 0; i < m_original.size(); ++i) {
        if (m_original.at(i).toMap().value("id").toString() == id) {
            index = i;
            break;
        }
    }
    if (on) {
        shuffleAround(index);
    } else {
        m_queue = m_original;
        m_pos = index;
    }
    if (m_acquired)
        m_npc->setNextEnabled(m_pos + 1 < m_queue.size());
}

void Player::playAt(int index)
{
    if (index < 0 || index >= m_queue.size())
        return;
    m_pos = index;
    m_track = m_queue.at(index).toMap();
    m_position = 0;
    m_duration = m_track.value("duration").toInt() * 1000;
    m_offline = m_downloads && !m_downloads->localUrl(m_track.value("id").toString()).isEmpty();
    setStatus(QString());
    setLoading(true);
    emit trackChanged();
    emit positionChanged();
    emit durationChanged();

    if (m_acquired) {
        startLoad();
    } else {
        m_pendingLoad = true;
        m_npc->acquire();   // continues in onAcquired()
    }
}

void Player::startLoad()
{
    m_pendingLoad = false;
    m_generation = PlayerWorker::latest.fetchAndAddOrdered(1) + 1;
    // A downloaded copy plays without the network; otherwise the
    // interface fetches a stream link from TIDAL (see provideStream).
    const QString local = m_downloads ? m_downloads->localUrl(m_track.value("id").toString()) : QString();
    if (!local.isEmpty())
        loadUrl(local);
    else
        emit needStreamUrl(m_track.value("id").toString(), m_generation);

    QVariantMap meta;
    meta["title"] = title();
    meta["artist"] = artist();
    meta["album"] = album();
    m_npc->setMetaData(meta);
    m_npc->setDuration(m_duration);
    m_npc->setPosition(0);
    m_npc->setNextEnabled(m_pos + 1 < m_queue.size());
    m_npc->setPreviousEnabled(true);
}

void Player::provideStream(int generation, const QString &url)
{
    if (generation != m_generation)
        return;   // the user has moved on to another song
    if (url.isEmpty()) {
        onFailed(generation);
        return;
    }
    loadUrl(m_downloads ? m_downloads->proxyUrl(url) : url);
}

void Player::loadUrl(const QString &url)
{
    QMetaObject::invokeMethod(m_worker, "load", Qt::QueuedConnection,
                              Q_ARG(QString, url),
                              Q_ARG(int, m_generation));
}

void Player::toggle()
{
    if (m_playing)
        pause();
    else
        play();
}

void Player::play()
{
    if (!hasTrack() || m_loading)
        return;
    if (m_acquired)
        QMetaObject::invokeMethod(m_worker, "resume", Qt::QueuedConnection);
    else
        m_npc->acquire();
}

void Player::pause()
{
    QMetaObject::invokeMethod(m_worker, "pause", Qt::QueuedConnection);
}

void Player::next()
{
    playAt(m_pos + 1);
}

void Player::previous()
{
    if (m_position > 4000 || m_pos <= 0)
        seek(0);
    else
        playAt(m_pos - 1);
}

void Player::seek(int ms)
{
    if (!hasTrack() || m_loading)
        return;
    QMetaObject::invokeMethod(m_worker, "seek", Qt::QueuedConnection, Q_ARG(int, ms));
}

void Player::onAcquired()
{
    m_acquired = true;
    if (m_pendingLoad)
        startLoad();
    else if (hasTrack())
        QMetaObject::invokeMethod(m_worker, "resume", Qt::QueuedConnection);
}

void Player::onRevoked()
{
    // Another app took over audio (a call, another player).
    m_acquired = false;
    pause();
}

void Player::onStarted(int generation)
{
    if (generation != m_generation)
        return;
    m_failures = 0;
    setLoading(false);
}

void Player::onFailed(int generation)
{
    if (generation != m_generation)
        return;
    setLoading(false);
    setStatus("Could not play this track");
    if (++m_failures < 3 && m_pos + 1 < m_queue.size())
        next();
}

void Player::onCompleted(int generation)
{
    if (generation != m_generation)
        return;
    if (m_pos + 1 < m_queue.size())
        next();
}

void Player::onPlaying(bool playing)
{
    if (m_acquired)
        m_npc->setMediaState(playing ? MediaState::Started : MediaState::Paused);
    if (playing != m_playing) {
        m_playing = playing;
        emit playingChanged();
    }
}

void Player::onPosition(int ms)
{
    if (m_loading)
        return;
    m_position = ms;
    if (m_acquired)
        m_npc->setPosition(ms);
    emit positionChanged();
}

void Player::onDuration(int ms)
{
    if (m_loading)
        return;
    m_duration = ms;
    if (m_acquired)
        m_npc->setDuration(ms);
    emit durationChanged();
}

void Player::setLoading(bool loading)
{
    if (loading == m_loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void Player::setStatus(const QString &text)
{
    if (text == m_status)
        return;
    m_status = text;
    emit statusChanged();
}
