#include "apiclient/ApiClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
// One sentence, one place: the dialog pre-checks the same condition the server
// answers 400 for, so both paths must say the same thing.
const char* kMissingCredentials = "Please enter your username and password.";
// Same text for wrong password, unknown user and locked account (SR-008).
const char* kGenericLoginError = "Invalid username or password.";
}

ApiClient::ApiClient(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {}

QString ApiClient::missingCredentialsMessage() {
    return QString::fromLatin1(kMissingCredentials);
}

void ApiClient::login(const QString& username, const QString& password) {
    QNetworkRequest req(QUrl(baseUrl_ + QStringLiteral("/api/auth/login")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const QJsonObject body{{"username", username}, {"password", password}};
    QNetworkReply* reply = net_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const QJsonObject json = QJsonDocument::fromJson(data).object();

        if (status == 0) {  // no HTTP response at all
            emit loginFailed(QStringLiteral("Cannot reach the server. Is it running?"));
            return;
        }
        if (status == 200) {
            // A 200 that is not a well-formed login response must not open the app.
            const QString token = json.value("token").toString();
            const QJsonObject user = json.value("user").toObject();
            if (token.isEmpty() || user.isEmpty()) {
                emit loginFailed(QStringLiteral("The server sent an unexpected response."));
                return;
            }
            Session session;
            session.username = user.value("username").toString();
            session.fullName = user.value("fullName").toString();
            session.role = roleFromString(user.value("role").toString());
            session.token = token;
            if (!session.isValid()) {
                emit loginFailed(QStringLiteral("The server sent an unexpected response."));
                return;
            }
            session_ = session;
            emit loginSucceeded(session_);
            return;
        }
        if (status == 400) {
            emit loginFailed(QString::fromLatin1(kMissingCredentials));
            return;
        }
        if (status == 401) {
            emit loginFailed(QString::fromLatin1(kGenericLoginError));
            return;
        }
        emit loginFailed(QStringLiteral("Something went wrong (code %1). Ref: %2")
                             .arg(status)
                             .arg(json.value("requestId").toString()));
    });
}
