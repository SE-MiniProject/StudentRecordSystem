#include "apiclient/ApiClient.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QJsonArray>
#include <QUrlQuery>

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

void ApiClient::searchStudents(const QString& search, int semester, int limit, int offset) {
    if (mock_) {
        mockSearchStudents(search, semester, limit, offset);
        return;
    }

    QUrl url(baseUrl_ + QStringLiteral("/api/students"));
    QUrlQuery query;
    if (!search.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("search"), search.trimmed());
    }
    if (semester > 0) {
        query.addQueryItem(QStringLiteral("semester"), QString::number(semester));
    }
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    query.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + token_.toUtf8());
    QNetworkReply* reply = net_->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());

        if (status == 0) {
            emit studentsFailed(QStringLiteral("Cannot reach the server. Is it running?"));
            return;
        }
        if (status == 200) {
            QJsonArray items;
            int total = 0;
            if (doc.isArray()) {  // old server shape: bare array
                items = doc.array();
                total = items.size();
            } else {              // contract shape: {items,total,limit,offset}
                const QJsonObject obj = doc.object();
                items = obj.value(QStringLiteral("items")).toArray();
                total = obj.value(QStringLiteral("total")).toInt(items.size());
            }
            emit studentsLoaded(items, total);
            return;
        }
        if (status == 401) {
            emit studentsFailed(QStringLiteral("Your session has expired. Please log in again."));
            return;
        }
        if (status == 403) {
            emit studentsFailed(QStringLiteral("You do not have permission to view students."));
            return;
        }
        emit studentsFailed(QStringLiteral("Something went wrong (code %1). Ref: %2")
                                .arg(status)
                                .arg(doc.object().value(QStringLiteral("requestId")).toString()));
    });
}

// Demo mode: 12 fake students so search and paging can be tested without the server.
void ApiClient::mockSearchStudents(const QString& search, int semester, int limit, int offset) {
    QTimer::singleShot(300, this, [=]() {
        static const char* names[] = {"Aarav Sharma", "Advitha Shetty ", "Ananya Rao",
                                      "Bhavya Nair",  "Chirag Patel",   "Divya Kulkarni",
                                      "Eshan Gupta",  "Farhan Khan",    "Gayathri Iyer",
                                      "Harsh Verma",  "Ishita Menon",   "Jayant Reddy"};
        const QString q = search.trimmed();
        QJsonArray matches;
        for (int i = 0; i < 12; ++i) {
            const QString roll = QStringLiteral("PES2UG23CS%1").arg(101 + i);
            const QString name = QString::fromLatin1(names[i]);
            const int sem = 1 + (i % 8);
            if (!q.isEmpty() && !name.contains(q, Qt::CaseInsensitive) &&
                !roll.contains(q, Qt::CaseInsensitive)) {
                continue;
            }
            if (semester > 0 && sem != semester) {
                continue;
            }
            matches.append(QJsonObject{{"rollNo", roll},
                                       {"fullName", name},
                                       {"department", "CSE-AIML"},
                                       {"semester", sem},
                                       {"contact", QString("student%1@example.edu").arg(i + 1)},
                                       {"dateOfBirth", "2005-04-11"}});
        }
        QJsonArray page;
        for (int i = offset; i < matches.size() && i < offset + limit; ++i) {
            page.append(matches.at(i));
        }
        emit studentsLoaded(page, matches.size());
    });
}