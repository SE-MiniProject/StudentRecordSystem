#pragma once
#include <QObject>
#include <QString>

#include "session/Session.h"

class QNetworkAccessManager;

// Talks to the server. All HTTP lives here so screens never build requests.
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject* parent = nullptr);

    void setBaseUrl(const QString& url) { baseUrl_ = url; }

    void login(const QString& username, const QString& password);
    const QString& token() const { return session_.token; }

    // Shown when either credential is blank. Shared so the dialog's pre-check and
    // the server's 400 answer cannot drift apart.
    static QString missingCredentialsMessage();

signals:
    void loginSucceeded(const Session& session);
    void loginFailed(const QString& message);

private:
    QNetworkAccessManager* net_;
    QString baseUrl_ = QStringLiteral("http://localhost:8080");
    Session session_;
};
