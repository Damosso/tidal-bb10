#include "app.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStringList>
#include <QUrl>

#include <stdio.h>

static QStringList s_log;
static QMutex s_logMutex;

App::App(QObject *parent)
    : QObject(parent)
{
    QSettings settings("tidalbb", "tidalbb");
    m_server = settings.value("server").toString();
    m_password = settings.value("password").toString();
}

QString App::password() const
{
    return m_password;
}

void App::setPassword(const QString &value)
{
    if (value == m_password)
        return;
    m_password = value;
    QSettings settings("tidalbb", "tidalbb");
    settings.setValue("password", m_password);
    emit passwordChanged();
}

QString App::url(const QString &path) const
{
    QString u = m_server + path;
    if (!m_password.isEmpty()) {
        u += path.contains('?') ? "&k=" : "?k=";
        u += QString::fromLatin1(QUrl::toPercentEncoding(m_password));
    }
    return u;
}

QString App::server() const
{
    return m_server;
}

void App::setServer(const QString &value)
{
    QString v = value.trimmed();
    while (v.endsWith('/'))
        v.chop(1);
    if (!v.isEmpty() && !v.startsWith("http://") && !v.startsWith("https://"))
        v.prepend("http://");
    if (v == m_server)
        return;
    m_server = v;
    QSettings settings("tidalbb", "tidalbb");
    settings.setValue("server", m_server);
    emit serverChanged();
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
    while (s_log.size() > 200)
        s_log.removeFirst();
}
