#include "downloads.hpp"

#include "app.hpp"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTime>
#include <QUrl>
#include <QtConcurrentRun>

static const int OFFLINE_DAYS = 30;
static const char *SD_ROOT = "/accounts/1000/removable/sdcard";

Downloads::Downloads(App *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_net(new QNetworkAccessManager(this))
    , m_server(0)
    , m_reply(0)
    , m_written(0)
    , m_progress(0)
    , m_busy(false)
    , m_moving(false)
{
    QDir().mkpath(privateDir());
    // The index always stays in private storage, also for songs on the card.
    m_index = new QSettings(privateDir() + "/index.ini", QSettings::IniFormat, this);
    m_cryptKey = loadKey();
    m_server = new LocalServer(m_cryptKey, this);
    connect(&m_moveWatcher, SIGNAL(finished()), this, SLOT(onMoveFinished()));
}

// The encryption key lives only in this app's private settings. Without it
// the files on the SD card can't be played anywhere else.
QByteArray Downloads::loadKey()
{
    QSettings settings("tidalbb", "tidalbb");
    QByteArray key = QByteArray::fromBase64(settings.value("offlineKey").toByteArray());
    if (key.size() == 32)
        return key;
    QFile random("/dev/urandom");
    if (random.open(QIODevice::ReadOnly))
        key = random.read(32);
    if (key.size() != 32) {
        qsrand(uint(QTime(0, 0).msecsTo(QTime::currentTime())));
        key.clear();
        for (int i = 0; i < 32; ++i)
            key.append(char(qrand() & 0xff));
    }
    settings.setValue("offlineKey", key.toBase64());
    return key;
}

QString Downloads::privateDir() const
{
    // The app's private data folder; other apps and the file manager can't see it.
    return QDir::homePath() + "/offline";
}

QString Downloads::sdDir() const
{
    return QString(SD_ROOT) + "/.tidal-bb10";
}

bool Downloads::sdCardAvailable() const
{
    QFileInfo root(SD_ROOT);
    return root.exists() && root.isDir() && root.isWritable();
}

bool Downloads::useSdCard() const
{
    return QSettings("tidalbb", "tidalbb").value("useSdCard", false).toBool();
}

void Downloads::setUseSdCard(bool on)
{
    if (on == useSdCard())
        return;
    QSettings("tidalbb", "tidalbb").setValue("useSdCard", on);
    emit changed();
}

QString Downloads::targetDir() const
{
    return (useSdCard() && sdCardAvailable()) ? sdDir() : privateDir();
}

int Downloads::count() const
{
    return m_index->childKeys().size();
}

int Downloads::misplaced() const
{
    const QString target = targetDir();
    int n = 0;
    foreach (const QString &k, m_index->childKeys()) {
        QVariantMap e = m_index->value(k).toMap();
        const QString file = e.value("file").toString();
        if (QFileInfo(file).path() != target || !e.value("enc").toBool())
            n++;
    }
    return n;
}

int Downloads::queued() const
{
    return m_queue.size() + (m_busy ? 1 : 0);
}

QString Downloads::current() const
{
    return m_cur.value("title").toString();
}

bool Downloads::expired() const
{
    if (count() == 0)
        return false;
    QDateTime verified = QSettings("tidalbb", "tidalbb").value("offlineVerified").toDateTime();
    return !verified.isValid() || verified.daysTo(QDateTime::currentDateTime()) > OFFLINE_DAYS;
}

void Downloads::markVerified()
{
    bool was = expired();
    QSettings("tidalbb", "tidalbb").setValue("offlineVerified", QDateTime::currentDateTime());
    if (was != expired())
        emit changed();
}

QString Downloads::usage() const
{
    qint64 phone = 0, card = 0;
    foreach (const QFileInfo &f, QDir(privateDir()).entryInfoList(QDir::Files))
        phone += f.size();
    if (sdCardAvailable()) {
        foreach (const QFileInfo &f, QDir(sdDir()).entryInfoList(QDir::Files))
            card += f.size();
    }
    const double mb = 1024.0 * 1024.0;
    QString text = QString::number(double(phone) / mb, 'f', 0) + " MB on the phone";
    if (card > 0)
        text += ", " + QString::number(double(card) / mb, 'f', 0) + " MB on the SD card";
    return text;
}

bool Downloads::inQueue(const QString &id) const
{
    foreach (const QVariantMap &t, m_queue) {
        if (t.value("id").toString() == id)
            return true;
    }
    return false;
}

QString Downloads::state(const QString &id) const
{
    if (m_busy && m_cur.value("id").toString() == id)
        return "downloading";
    if (inQueue(id))
        return "queued";
    if (m_index->contains(key(id)))
        return "done";
    return QString();
}

void Downloads::add(const QVariantMap &track)
{
    const QString id = track.value("id").toString();
    if (id.isEmpty() || !state(id).isEmpty())
        return;
    m_queue.append(track);
    emit changed();
    if (!m_busy)
        next();
}

void Downloads::addAll(const QVariantList &tracks)
{
    foreach (const QVariant &t, tracks)
        add(t.toMap());
}

void Downloads::cancelQueue()
{
    m_queue.clear();
    emit changed();
}

void Downloads::remove(const QString &id)
{
    QVariantMap e = m_index->value(key(id)).toMap();
    if (!e.isEmpty()) {
        QFile::remove(e.value("file").toString());
        QFile::remove(e.value("cover").toString());
        m_index->remove(key(id));
        m_index->sync();
    }
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (m_queue.at(i).value("id").toString() == id)
            m_queue.removeAt(i);
    }
    emit changed();
}

void Downloads::removeAll()
{
    m_queue.clear();
    foreach (const QString &k, m_index->childKeys()) {
        QVariantMap e = m_index->value(k).toMap();
        QFile::remove(e.value("file").toString());
        QFile::remove(e.value("cover").toString());
    }
    m_index->clear();
    m_index->sync();
    emit changed();
}

static bool newestFirst(const QVariant &a, const QVariant &b)
{
    return a.toMap().value("added").toDateTime() > b.toMap().value("added").toDateTime();
}

QVariantList Downloads::list() const
{
    QVariantList out;
    foreach (const QString &k, m_index->childKeys()) {
        QVariantMap e = m_index->value(k).toMap();
        QVariantMap row;
        row["type"] = "track";
        // Songs on a removed SD card are hidden until it is back.
        row["ok"] = QFile::exists(e.value("file").toString());
        row["id"] = e.value("id");
        row["title"] = e.value("title");
        row["artist"] = e.value("artist");
        row["album"] = e.value("album");
        row["albumId"] = e.value("albumId");
        row["artistId"] = e.value("artistId");
        row["duration"] = e.value("duration");
        row["added"] = e.value("added");
        const QString cover = e.value("cover").toString();
        row["imgUrl"] = QFile::exists(cover) ? QUrl::fromLocalFile(cover).toString() : QString();
        out.append(row);
    }
    qSort(out.begin(), out.end(), newestFirst);
    return out;
}

QString Downloads::localUrl(const QString &id) const
{
    if (expired())
        return QString();
    QVariantMap e = m_index->value(key(id)).toMap();
    const QString file = e.value("file").toString();
    if (file.isEmpty() || !QFile::exists(file))
        return QString();
    if (e.value("enc").toBool())
        return m_server->url(file, e.value("mime").toString());
    return QUrl::fromLocalFile(file).toString();   // downloaded before encryption existed
}

// ---------------------------------------------------------------- download

void Downloads::next()
{
    m_busy = false;
    if (m_queue.isEmpty()) {
        m_cur.clear();
        emit changed();
        return;
    }
    m_busy = true;
    m_cur = m_queue.takeFirst();
    m_progress = 0;
    m_written = 0;
    m_cipher = Cipher(m_cryptKey);
    m_curDir = targetDir();
    if (useSdCard() && m_curDir != sdDir())
        qWarning() << "Downloads: SD card not available, saving on the phone";
    QDir().mkpath(m_curDir);
    const QString id = m_cur.value("id").toString();

    emit changed();
    emit progressChanged();
    // The interface fetches a fresh stream link and calls provideStream().
    emit needStreamUrl(id);
}

void Downloads::provideStream(const QString &id, const QString &url)
{
    if (!m_busy || m_reply || m_cur.value("id").toString() != id)
        return;
    if (url.isEmpty()) {
        qWarning() << "Downloads: no stream link for" << current();
        emit failedOne(current());
        next();
        return;
    }
    m_file.setFileName(m_curDir + "/" + id + ".part");
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Downloads: cannot write" << m_file.fileName();
        emit failedOne(current());
        next();
        return;
    }
    m_reply = m_net->get(QNetworkRequest(QUrl::fromEncoded(url.toLatin1())));
    connect(m_reply, SIGNAL(readyRead()), this, SLOT(onAudioData()));
    connect(m_reply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(onAudioProgress(qint64, qint64)));
    connect(m_reply, SIGNAL(finished()), this, SLOT(onAudioFinished()));
    emit changed();
    emit progressChanged();
}

void Downloads::onAudioData()
{
    if (!m_reply)
        return;
    QByteArray data = m_reply->readAll();
    if (data.isEmpty())
        return;
    m_cipher.apply(data.data(), data.size(), m_written);
    m_written += data.size();
    m_file.write(data);
}

void Downloads::onAudioProgress(qint64 received, qint64 total)
{
    int p = total > 0 ? int(received * 100 / total) : 0;
    if (p != m_progress) {
        m_progress = p;
        emit progressChanged();
    }
}

void Downloads::onAudioFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    onAudioData();
    m_file.close();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const bool ok = reply->error() == QNetworkReply::NoError && status == 200 && m_file.size() > 0;
    reply->deleteLater();

    if (!ok) {
        qWarning() << "Downloads: song failed" << status << reply->errorString();
        QFile::remove(m_file.fileName());
        m_reply = 0;
        emit failedOne(current());
        next();
        return;
    }

    const QString id = m_cur.value("id").toString();
    const bool flac = type.contains("flac");
    m_curMime = flac ? "audio/flac" : "audio/mp4";
    m_audioPath = m_curDir + "/" + id + (flac ? ".flac.enc" : ".m4a.enc");
    QFile::remove(m_audioPath);
    QFile::rename(m_file.fileName(), m_audioPath);

    // Then the cover, large size.
    QString img = m_cur.value("img").toString();
    if (img.isEmpty() || !img.startsWith("http")) {
        m_reply = 0;
        finishCurrent();
        return;
    }
    img.replace("/160x160.jpg", "/320x320.jpg");   // the large cover
    m_reply = m_net->get(QNetworkRequest(QUrl(img)));
    connect(m_reply, SIGNAL(finished()), this, SLOT(onCoverFinished()));
}

void Downloads::onCoverFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = 0;
    if (reply) {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QFile f(m_curDir + "/" + m_cur.value("id").toString() + ".jpg");
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(reply->readAll());
        }
    }
    finishCurrent();
}

void Downloads::finishCurrent()
{
    const QString id = m_cur.value("id").toString();
    QVariantMap e;
    e["id"] = id;
    e["title"] = m_cur.value("title");
    e["artist"] = m_cur.value("artist");
    e["album"] = m_cur.value("album");
    e["albumId"] = m_cur.value("albumId");
    e["artistId"] = m_cur.value("artistId");
    e["duration"] = m_cur.value("duration");
    e["file"] = m_audioPath;
    e["cover"] = m_curDir + "/" + id + ".jpg";
    e["enc"] = true;
    e["mime"] = m_curMime;
    e["added"] = QDateTime::currentDateTime();
    m_index->setValue(key(id), e);
    m_index->sync();

    // A fresh download proves the subscription works right now.
    QSettings("tidalbb", "tidalbb").setValue("offlineVerified", QDateTime::currentDateTime());

    emit finishedOne(current());
    next();
}

// ---------------------------------------------------------------- moving

// Runs on a background thread: copies (and encrypts, for old downloads)
// each file to its new place, then deletes the original.
static QStringList runMoves(QList<Downloads::MoveJob> jobs, QByteArray key)
{
    QStringList done;
    foreach (const Downloads::MoveJob &job, jobs) {
        QDir().mkpath(QFileInfo(job.dst).path());
        QFile in(job.src);
        QFile out(job.dst + ".part");
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            continue;
        Cipher cipher(key);
        qint64 offset = 0;
        bool ok = true;
        while (!in.atEnd()) {
            QByteArray data = in.read(256 * 1024);
            if (data.isEmpty())
                break;
            if (job.encrypt)
                cipher.apply(data.data(), data.size(), offset);
            offset += data.size();
            if (out.write(data) != data.size()) {
                ok = false;   // card full or removed
                break;
            }
        }
        out.close();
        in.close();
        if (!ok || offset != in.size()) {
            QFile::remove(out.fileName());
            continue;
        }
        QFile::remove(job.dst);
        QFile::rename(out.fileName(), job.dst);
        if (QFile::exists(job.srcCover) && job.srcCover != job.dstCover) {
            QFile::remove(job.dstCover);
            QFile::copy(job.srcCover, job.dstCover);
            QFile::remove(job.srcCover);
        }
        if (job.src != job.dst)
            QFile::remove(job.src);
        done.append(job.id);
    }
    return done;
}

void Downloads::moveAll()
{
    if (m_moving)
        return;
    const QString target = targetDir();
    m_moveJobs.clear();
    foreach (const QString &k, m_index->childKeys()) {
        QVariantMap e = m_index->value(k).toMap();
        const QString file = e.value("file").toString();
        const bool enc = e.value("enc").toBool();
        if (!QFile::exists(file) || (QFileInfo(file).path() == target && enc))
            continue;
        MoveJob job;
        job.id = e.value("id").toString();
        job.encrypt = !enc;
        job.mime = enc ? e.value("mime").toString()
                       : (file.endsWith(".flac") ? QString("audio/flac") : QString("audio/mp4"));
        QString name = QFileInfo(file).fileName();
        if (!enc)
            name += ".enc";
        job.src = file;
        job.dst = target + "/" + name;
        job.srcCover = e.value("cover").toString();
        job.dstCover = target + "/" + job.id + ".jpg";
        m_moveJobs.append(job);
    }
    if (m_moveJobs.isEmpty()) {
        emit moveFinished(0, 0);
        return;
    }
    m_moving = true;
    emit changed();
    m_moveWatcher.setFuture(QtConcurrent::run(runMoves, m_moveJobs, m_cryptKey));
}

void Downloads::onMoveFinished()
{
    const QStringList done = m_moveWatcher.result();
    foreach (const MoveJob &job, m_moveJobs) {
        if (!done.contains(job.id))
            continue;
        QVariantMap e = m_index->value(key(job.id)).toMap();
        e["file"] = job.dst;
        e["cover"] = job.dstCover;
        e["enc"] = true;
        e["mime"] = job.mime;
        m_index->setValue(key(job.id), e);
    }
    m_index->sync();
    const int failed = m_moveJobs.size() - done.size();
    m_moveJobs.clear();
    m_moving = false;
    emit changed();
    emit moveFinished(done.size(), failed);
}
