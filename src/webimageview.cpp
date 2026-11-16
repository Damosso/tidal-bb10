#include "webimageview.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <bb/cascades/Image>

using namespace bb::cascades;

QNetworkAccessManager *WebImageView::s_net = 0;
QHash<QString, QByteArray> WebImageView::s_cache;
QStringList WebImageView::s_cacheOrder;

WebImageView::WebImageView(Container *parent)
    : ImageView(parent)
    , m_reply(0)
{
    if (!s_net)
        s_net = new QNetworkAccessManager();
}

void WebImageView::setUrl(const QUrl &url)
{
    if (url == m_url)
        return;
    m_url = url;
    emit urlChanged();

    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = 0;
    }
    resetImage();
    if (url.isEmpty())
        return;

    const QString key = url.toString();
    if (s_cache.contains(key)) {
        setImage(Image(s_cache.value(key)));
        return;
    }
    m_reply = s_net->get(QNetworkRequest(url));
    connect(m_reply, SIGNAL(finished()), this, SLOT(onFinished()));
}

void WebImageView::onFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = 0;
    if (!reply)
        return;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;
    const QByteArray data = reply->readAll();
    if (data.isEmpty())
        return;

    const QString key = m_url.toString();
    if (!s_cache.contains(key)) {
        s_cache.insert(key, data);
        s_cacheOrder.append(key);
        while (s_cacheOrder.size() > 150)
            s_cache.remove(s_cacheOrder.takeFirst());
    }
    setImage(Image(data));
}
