#include "apiclient/ApiClient.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {
const char* kGenericLoginError = "Invalid username or password.";
}

ApiClient::ApiClient(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {}

void ApiClient::login(const QString& username, const QString& password) {
    if (mock_) {
        mockLogin(username, password);
        return;
    }

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
            token_ = json.value("token").toString();
            const QJsonObject user = json.value("user").toObject();
            emit loginSucceeded(user.value("username").toString(),
                                user.value("fullName").toString(),
                                user.value("role").toString());
            return;
        }
        if (status == 400) {
            emit loginFailed(QStringLiteral("Please enter your username and password."));
            return;
        }
        if (status == 401) {
            // Same text for wrong password, unknown user and locked account (SR-008).
            emit loginFailed(QString::fromLatin1(kGenericLoginError));
            return;
        }
        emit loginFailed(QStringLiteral("Something went wrong (code %1). Ref: %2")
                             .arg(status)
                             .arg(json.value("requestId").toString()));
    });
}

// Demo mode: pretends to be the server so the UI can be built and tested now.
// Accounts: admin/admin123, teacher/teacher123, student/student123.
// Five wrong attempts lock the account, and it still answers with the generic error.
void ApiClient::mockLogin(const QString& username, const QString& password) {
    static const QHash<QString, QPair<QString, QPair<QString, QString>>> users = {
        {"admin", {"admin123", {"Demo Admin", "admin"}}},
        {"teacher", {"teacher123", {"Demo Teacher", "teacher"}}},
        {"student", {"student123", {"Demo Student", "student"}}},
    };
    static QHash<QString, int> failures;

    QTimer::singleShot(300, this, [=]() {
        const auto it = users.constFind(username);
        const bool locked = failures.value(username) >= 5;
        if (it != users.constEnd() && !locked && it->first == password) {
            failures.remove(username);
            token_ = QStringLiteral("mock-token");
            emit loginSucceeded(username, it->second.first, it->second.second);
            return;
        }
        if (it != users.constEnd()) {
            failures[username] += 1;
        }
        emit loginFailed(QString::fromLatin1(kGenericLoginError));
    });
}