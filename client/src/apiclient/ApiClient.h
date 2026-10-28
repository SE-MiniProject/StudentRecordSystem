#pragma once
#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Talks to the server. All HTTP lives here so screens never build requests.
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject* parent = nullptr);

    void setBaseUrl(const QString& url) { baseUrl_ = url; }
    void setMockMode(bool on) { mock_ = on; }
    bool mockMode() const { return mock_; }

    void login(const QString& username, const QString& password);
    const QString& token() const { return token_; }

signals:
    void loginSucceeded(const QString& username, const QString& fullName, const QString& role);
    void loginFailed(const QString& message);

private:
    void mockLogin(const QString& username, const QString& password);

    QNetworkAccessManager* net_;
    QString baseUrl_ = QStringLiteral("http://localhost:8080");
    QString token_;
    bool mock_ = false;
};