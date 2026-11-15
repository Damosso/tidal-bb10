#ifndef WEBIMAGEVIEW_HPP
#define WEBIMAGEVIEW_HPP

#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QUrl>

#include <bb/cascades/Container>
#include <bb/cascades/ImageView>

class QNetworkAccessManager;
class QNetworkReply;

// ImageView that loads its picture from an http URL (cover art).
class WebImageView : public bb::cascades::ImageView
{
    Q_OBJECT
    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)

public:
    explicit WebImageView(bb::cascades::Container *parent = 0);

    QUrl url() const { return m_url; }
    void setUrl(const QUrl &url);

signals:
    void urlChanged();

private slots:
    void onFinished();

private:
    QUrl m_url;
    QNetworkReply *m_reply;

    static QNetworkAccessManager *s_net;
    static QHash<QString, QByteArray> s_cache;
    static QStringList s_cacheOrder;
};

#endif
