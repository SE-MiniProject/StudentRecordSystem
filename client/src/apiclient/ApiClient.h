#pragma once
#include <QJsonArray>
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
    // Demo mode (SRS_MOCK=1): answers locally so the UI works without a server.
    void setMockMode(bool on) { mock_ = on; }

    void login(const QString& username, const QString& password);
    const QString& token() const { return session_.token; }

    void searchStudents(const QString& search, int semester, int limit, int offset);

    // Shown when either credential is blank. Shared so the dialog's pre-check and
    // the server's 400 answer cannot drift apart.
    static QString missingCredentialsMessage();

signals:
    void loginSucceeded(const Session& session);
    void loginFailed(const QString& message);
    void studentsLoaded(const QJsonArray& items, int total);
    void studentsFailed(const QString& message);

private:
    void mockLogin(const QString& username, const QString& password);
    void mockSearchStudents(const QString& search, int semester, int limit, int offset);

    QNetworkAccessManager* net_;
    QString baseUrl_ = QStringLiteral("http://localhost:8080");
    Session session_;
    bool mock_ = false;
};