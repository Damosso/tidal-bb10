#ifndef APP_HPP
#define APP_HPP

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QSslError;

// Settings, the TIDAL login and a small in-app log (there is no console on
// the phone). The login is kept in the app's private settings on the phone;
// nothing about your account is part of the app package.
class App : public QObject
{
    Q_OBJECT
    // Your TIDAL login, written after the first-start login prompt.
    Q_PROPERTY(QString accessToken READ accessToken NOTIFY loginChanged)
    Q_PROPERTY(QString refreshToken READ refreshToken NOTIFY loginChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY loginChanged)
    Q_PROPERTY(QString countryCode READ countryCode NOTIFY loginChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loginChanged)
    // The app's identifier at TIDAL (not your account). Defaults to the one
    // the open-source tidalapi library uses; can be changed in Settings.
    Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY clientChanged)
    Q_PROPERTY(QString clientSecret READ clientSecret WRITE setClientSecret NOTIFY clientChanged)
    Q_PROPERTY(QString quality READ quality WRITE setQuality NOTIFY qualityChanged)

public:
    explicit App(QObject *parent = 0);

    QString accessToken() const { return m_access; }
    QString refreshToken() const { return m_refresh; }
    QString userId() const { return m_userId; }
    QString countryCode() const { return m_country; }
    bool loggedIn() const { return !m_refresh.isEmpty() && !m_userId.isEmpty(); }

    QString clientId() const;
    void setClientId(const QString &value);
    QString clientSecret() const;
    void setClientSecret(const QString &value);
    QString quality() const;
    void setQuality(const QString &value);

    Q_INVOKABLE void saveTokens(const QString &access, const QString &refresh);
    Q_INVOKABLE void saveAccount(const QString &userId, const QString &countryCode);
    Q_INVOKABLE void logout();

    // Checks that the phone can make secure connections to TIDAL; the
    // result goes to the Log screen.
    Q_INVOKABLE void testConnection();

    Q_INVOKABLE QString logText() const;

    static void messageHandler(QtMsgType type, const char *msg);
    static void setupCertificates();

signals:
    void connectionTested(bool ok, const QString &summary);
    void loginChanged();
    void clientChanged();
    void qualityChanged();

private slots:
    void onTestFinished();
    void onTestSslErrors(const QList<QSslError> &errors);

private:
    QNetworkAccessManager *m_testNet;
    int m_testPending;
    QStringList m_testFailures;
    QString m_access;
    QString m_refresh;
    QString m_userId;
    QString m_country;
};

#endif
