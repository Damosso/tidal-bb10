#include "app.hpp"

#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSslCertificate>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QStringList>
#include <QUrl>

#include <dlfcn.h>
#include <stdio.h>

static QStringList s_log;
static QMutex s_logMutex;

// Public client identifier used by the tidalapi library (github.com/EbbLabs/python-tidal).
static const char *DEFAULT_CLIENT_ID = "ZlgySnhkbW50WldLMGl4VA==";
static const char *DEFAULT_CLIENT_SECRET = "MU5uOUFmREFqeHJnSkZKYktOV0xlQXlLR1ZHbUlOdVhQUExIVlhBdnhBZz0=";

App::App(QObject *parent)
    : QObject(parent)
    , m_testNet(0)
    , m_testPending(0)
{
    QSettings settings("tidalbb", "tidalbb");
    m_access = settings.value("login/access").toString();
    m_refresh = settings.value("login/refresh").toString();
    m_userId = settings.value("login/userId").toString();
    m_country = settings.value("login/country").toString();
}

QString App::clientId() const
{
    QString v = QSettings("tidalbb", "tidalbb").value("clientId").toString();
    return v.isEmpty() ? QString::fromLatin1(QByteArray::fromBase64(DEFAULT_CLIENT_ID)) : v;
}

void App::setClientId(const QString &value)
{
    QSettings("tidalbb", "tidalbb").setValue("clientId", value.trimmed());
    emit clientChanged();
}

QString App::clientSecret() const
{
    QString v = QSettings("tidalbb", "tidalbb").value("clientSecret").toString();
    return v.isEmpty() ? QString::fromLatin1(QByteArray::fromBase64(DEFAULT_CLIENT_SECRET)) : v;
}

void App::setClientSecret(const QString &value)
{
    QSettings("tidalbb", "tidalbb").setValue("clientSecret", value.trimmed());
    emit clientChanged();
}

QString App::quality() const
{
    return QSettings("tidalbb", "tidalbb").value("quality", "HIGH").toString();
}

void App::setQuality(const QString &value)
{
    if (value == quality())
        return;
    QSettings("tidalbb", "tidalbb").setValue("quality", value);
    emit qualityChanged();
}

void App::saveTokens(const QString &access, const QString &refresh)
{
    m_access = access;
    if (!refresh.isEmpty())
        m_refresh = refresh;
    QSettings settings("tidalbb", "tidalbb");
    settings.setValue("login/access", m_access);
    settings.setValue("login/refresh", m_refresh);
    emit loginChanged();
}

void App::saveAccount(const QString &userId, const QString &countryCode)
{
    m_userId = userId;
    m_country = countryCode;
    QSettings settings("tidalbb", "tidalbb");
    settings.setValue("login/userId", m_userId);
    settings.setValue("login/country", m_country);
    emit loginChanged();
}

void App::logout()
{
    m_access.clear();
    m_refresh.clear();
    m_userId.clear();
    m_country.clear();
    QSettings settings("tidalbb", "tidalbb");
    settings.remove("login");
    emit loginChanged();
}

// ---------------------------------------------------------------- TLS

// BB10's own root certificates are from around 2017; use the bundled,
// current Mozilla set (assets/cacert.pem) for every secure connection.
void App::setupCertificates()
{
    QList<QSslCertificate> certs = QSslCertificate::fromPath("app/native/assets/cacert.pem");
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    if (!certs.isEmpty()) {
        config.setCaCertificates(certs + QSslSocket::systemCaCertificates());
        QSslSocket::setDefaultCaCertificates(certs + QSslSocket::systemCaCertificates());
    }
    config.setProtocol(QSsl::SecureProtocols);
    QSslConfiguration::setDefaultConfiguration(config);
    qDebug() << "TLS: OpenSSL" << QSslSocket::supportsSsl() << "bundled root certificates:" << certs.size();

    // The app ships a QtNetwork with OpenSSL 1.1.1 built in (lib/); check it is the one loaded.
    typedef const char *(*VersionFn)(int);
    void *qtnet = dlopen("libQtNetwork.so.4", RTLD_NOW);
    VersionFn version = qtnet ? (VersionFn) dlsym(qtnet, "OpenSSL_version") : 0;
    qDebug() << "TLS: library" << (version ? version(0) : "BB10 system OpenSSL (the bundled QtNetwork did not load)");

    // Which ciphers this phone's TLS library offers (TIDAL's API needs AES-GCM).
    QStringList gcm;
    int total = 0;
    foreach (const QSslCipher &c, QSslSocket::supportedCiphers()) {
        total++;
        if (c.name().contains("GCM"))
            gcm.append(c.name() + " " + c.protocolString());
    }
    qDebug() << "TLS: ciphers offered:" << total << "with GCM:" << gcm.size();
    if (!gcm.isEmpty())
        qDebug() << "TLS: GCM ciphers:" << QStringList(gcm.mid(0, 4)).join(", ");
}

void App::testConnection()
{
    if (!m_testNet)
        m_testNet = new QNetworkAccessManager(this);
    if (m_testPending > 0)
        return;
    const char *urls[] = {
        "https://auth.tidal.com/v1/oauth2/token",
        "https://api.tidal.com/v1/sessions",
        "https://resources.tidal.com/images/b66a5c40/c34d/4507/a0dc/5f98e46fdd20/160x160.jpg",
        // Needs TLS 1.2, but accepts older ciphers and no server name:
        "https://images.tidal.com/0/EMACGMAC/CAEQCCIDMzE5KgcjRkJEMjkzMAQ",
        "https://link.tidal.com/",
        0
    };
    m_testFailures.clear();
    for (int i = 0; urls[i]; ++i) {
        QNetworkReply *reply = m_testNet->get(QNetworkRequest(QUrl(QString::fromLatin1(urls[i]))));
        connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(onTestSslErrors(QList<QSslError>)));
        connect(reply, SIGNAL(finished()), this, SLOT(onTestFinished()));
        m_testPending++;
    }
}

void App::onTestSslErrors(const QList<QSslError> &errors)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    foreach (const QSslError &e, errors)
        qWarning() << "TLS error for" << (reply ? reply->url().host() : QString()) << ":" << e.errorString();
}

void App::onTestFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    const QString host = reply->url().host();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // Any HTTP status (even 400 or 401) means the secure connection worked.
    if (status > 0) {
        const QSslCipher cipher = reply->sslConfiguration().sessionCipher();
        qDebug() << "Connection test:" << host << "OK, HTTP" << status
                 << "using" << cipher.protocolString() << cipher.name();
    } else {
        qWarning() << "Connection test:" << host << "FAILED:" << reply->errorString();
        m_testFailures.append(host);
    }
    reply->deleteLater();
    if (--m_testPending == 0) {
        const bool ok = m_testFailures.isEmpty();
        emit connectionTested(ok, ok ? QString("All TIDAL servers reachable")
                                     : "Cannot reach " + m_testFailures.join(", "));
    }
}

QString App::logText() const
{
    QMutexLocker lock(&s_logMutex);
    return s_log.isEmpty() ? QString("(nothing logged)") : s_log.join("\n");
}

void App::messageHandler(QtMsgType type, const char *msg)
{
    Q_UNUSED(type);
    fprintf(stderr, "%s\n", msg);
    QMutexLocker lock(&s_logMutex);
    s_log.append(QString::fromUtf8(msg));
    while (s_log.size() > 300)
        s_log.removeFirst();
}
