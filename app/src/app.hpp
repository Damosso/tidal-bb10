#ifndef APP_HPP
#define APP_HPP

#include <QObject>
#include <QString>

// Settings and a small in-app log (there is no console on the phone).
class App : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)

public:
    explicit App(QObject *parent = 0);

    QString server() const;
    void setServer(const QString &value);

    QString password() const;
    void setPassword(const QString &value);

    // Full address for a server path such as "/api/search?q=x", with the
    // password added when one is set.
    Q_INVOKABLE QString url(const QString &path) const;

    Q_INVOKABLE QString logText() const;

    static void messageHandler(QtMsgType type, const char *msg);

signals:
    void serverChanged();
    void passwordChanged();

private:
    QString m_server;
    QString m_password;
};

#endif
