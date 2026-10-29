#include <QtTest>

#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "apiclient/ApiClient.h"

// A throwaway HTTP server that answers every request with one canned response.
// Driving the real QNetworkAccessManager is what makes these tests worth having:
// a mocked-out client would pass even if the URL, verb, or parsing were wrong.
class StubServer : public QObject {
    Q_OBJECT
public:
    struct Response {
        int status = 200;
        QByteArray body;
    };

    explicit StubServer(Response response, QObject* parent = nullptr)
        : QObject(parent), response_(std::move(response)) {
        connect(&server_, &QTcpServer::newConnection, this, &StubServer::onConnection);
        if (!server_.listen(QHostAddress::LocalHost, 0)) {
            qFatal("stub server could not listen");
        }
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort());
    }

    QByteArray lastRequest() const { return lastRequest_; }

private slots:
    void onConnection() {
        QTcpSocket* socket = server_.nextPendingConnection();
        // Per-socket request buffer. A member would mix partial requests from
        // concurrent connections, and each test creates a fresh server anyway.
        auto* request = new QByteArray;
        auto* answered = new bool(false);

        auto tryAnswer = [this, socket, request, answered]() {
            if (*answered) return;
            request->append(socket->readAll());
            if (!request->contains("\r\n\r\n")) return;  // headers not complete yet
            *answered = true;

            lastRequest_ = *request;
            // The HTTP version token is mandatory: QNetworkAccessManager fails
            // the reply with "Connection closed" if the status line starts with
            // the bare code, so omitting it makes every test look like a dead server.
            QByteArray head = "HTTP/1.1 " + QByteArray::number(response_.status) + " Stub\r\n";
            head += "Content-Type: application/json\r\n";
            head += "Content-Length: " + QByteArray::number(response_.body.size()) + "\r\n";
            head += "Connection: close\r\n\r\n";
            socket->write(head + response_.body);
            socket->flush();
        };

        connect(socket, &QTcpSocket::readyRead, socket, tryAnswer);
        // The request may already have arrived between newConnection and the
        // connect above; Qt does not replay readyRead for buffered data, so poll
        // the socket once before relying on the signal.
        if (socket->bytesAvailable() > 0) tryAnswer();

        connect(socket, &QTcpSocket::disconnected, socket, [socket, request, answered]() {
            delete answered;
            delete request;
            socket->deleteLater();
        });
    }

private:
    QTcpServer server_;
    Response response_;
    QByteArray lastRequest_;
};

class TestApiClient : public QObject {
    Q_OBJECT

private:
    // Runs one login against the stub and returns the outcome. Exactly one of
    // the two signals must fire; anything else fails the calling test.
    struct Outcome {
        bool succeeded = false;
        bool timedOut = false;
        Session session;
        QString error;
    };

    // Waits without QVERIFY: this returns a value, and QVERIFY expands to a bare
    // `return;`, which cannot compile here. Callers assert on the outcome.
    Outcome runLogin(const QByteArray& body, int status) {
        StubServer server({status, body});
        ApiClient api;
        api.setBaseUrl(server.baseUrl());

        Outcome outcome;
        connect(&api, &ApiClient::loginSucceeded, this, [&outcome](const Session& session) {
            outcome.succeeded = true;
            outcome.session = session;
        });
        connect(&api, &ApiClient::loginFailed, this,
                [&outcome](const QString& message) { outcome.error = message; });

        QSignalSpy okSpy(&api, &ApiClient::loginSucceeded);
        QSignalSpy failSpy(&api, &ApiClient::loginFailed);
        api.login(QStringLiteral("a.shetty"), QStringLiteral("correct-horse"));

        // Wait for whichever signal fires first. Waiting on okSpy and then on
        // failSpy burns the full timeout on the first when the failure signal
        // was the one emitted, and the spy has already missed it by then.
        QElapsedTimer elapsed;
        elapsed.start();
        while (okSpy.isEmpty() && failSpy.isEmpty() && elapsed.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }
        if (okSpy.isEmpty() && failSpy.isEmpty()) {
            outcome.timedOut = true;
        }
        return outcome;
    }

    static QByteArray okBody() {
        return R"({"token":"opaque-token","expiresAt":"2026-10-26T22:03:00Z",)"
               R"("user":{"username":"a.shetty","fullName":"Advitha Shetty","role":"admin"}})";
    }

private slots:
    // A well-formed 200 is the happy path: the session must carry every field
    // api.md promises, so the shell does not open with blank labels.
    void successfulLoginYieldsFullSession() {
        const Outcome outcome = runLogin(okBody(), 200);

        QVERIFY(!outcome.timedOut);
        QVERIFY2(outcome.succeeded, qPrintable(outcome.error));
        QCOMPARE(outcome.session.username, QStringLiteral("a.shetty"));
        QCOMPARE(outcome.session.fullName, QStringLiteral("Advitha Shetty"));
        QCOMPARE(outcome.session.role, Role::Admin);
        QCOMPARE(outcome.session.token, QStringLiteral("opaque-token"));
    }

    // The regression that mattered: a 200 with no token used to accept the
    // dialog and open the app unauthenticated.
    void successWithoutTokenIsRejected_data() {
        QTest::addColumn<QByteArray>("body");

        QTest::newRow("no token") << QByteArray(R"({"user":{"username":"a","role":"admin"}})");
        QTest::newRow("empty token") << QByteArray(R"({"token":"","user":{"username":"a","role":"admin"}})");
        QTest::newRow("no user") << QByteArray(R"({"token":"opaque-token"})");
        QTest::newRow("empty user") << QByteArray(R"({"token":"opaque-token","user":{}})");
        QTest::newRow("empty body") << QByteArray();
        QTest::newRow("not json") << QByteArray("<html>gateway</html>");
    }

    void successWithoutTokenIsRejected() {
        QFETCH(QByteArray, body);
        const Outcome outcome = runLogin(body, 200);

        QVERIFY(!outcome.timedOut);
        QVERIFY2(!outcome.succeeded, "a malformed 200 must not open the app");
        QVERIFY(!outcome.error.isEmpty());
    }

    // SR-008: wrong password, unknown user and lockout must be indistinguishable
    // to the user, so one generic sentence covers all 401s.
    void unauthenticatedIsGeneric() {
        const Outcome outcome =
            runLogin(R"({"error":"unauthenticated","requestId":"b1e4"})", 401);

        QVERIFY(!outcome.timedOut);
        QVERIFY(!outcome.succeeded);
        QCOMPARE(outcome.error, QStringLiteral("Invalid username or password."));
    }

    void missingFieldUsesSharedWording() {
        const Outcome outcome =
            runLogin(R"({"error":"validation_failed","requestId":"b1e4"})", 400);

        QVERIFY(!outcome.timedOut);
        QVERIFY(!outcome.succeeded);
        QCOMPARE(outcome.error, ApiClient::missingCredentialsMessage());
    }

    // Any other status is unexpected, and the message must carry the server's
    // request id so a user report can be traced in the server log.
    void unexpectedStatusMentionsStatusAndRequestId() {
        const Outcome outcome = runLogin(R"({"requestId":"c0ffee"})", 500);

        QVERIFY(!outcome.timedOut);
        QVERIFY(!outcome.succeeded);
        QVERIFY2(outcome.error.contains(QStringLiteral("500")), qPrintable(outcome.error));
        QVERIFY2(outcome.error.contains(QStringLiteral("c0ffee")), qPrintable(outcome.error));
    }

    // A dead server must say so rather than appear to hang.
    void unreachableServerReportsClearly() {
        ApiClient api;
        // Port 1 on loopback: nothing listens, so the connect fails fast.
        api.setBaseUrl(QStringLiteral("http://127.0.0.1:1"));

        QSignalSpy failed(&api, &ApiClient::loginFailed);
        QSignalSpy succeeded(&api, &ApiClient::loginSucceeded);
        api.login(QStringLiteral("a"), QStringLiteral("b"));

        QVERIFY(failed.wait(5000));
        QCOMPARE(succeeded.count(), 0);
        QVERIFY(!failed.first().first().toString().isEmpty());
    }

    // The credentials must go to the documented endpoint as JSON, not form data.
    void postsCredentialsToLoginEndpoint() {
        StubServer server({200, okBody()});
        ApiClient api;
        api.setBaseUrl(server.baseUrl());

        QSignalSpy done(&api, &ApiClient::loginSucceeded);
        api.login(QStringLiteral("a.shetty"), QStringLiteral("correct-horse"));
        QVERIFY(done.wait(5000));

        const QByteArray request = server.lastRequest();
        QVERIFY2(request.startsWith("POST /api/auth/login "), request.constData());
        QVERIFY2(request.contains("Content-Type: application/json"), request.constData());
        QVERIFY2(request.contains("\"username\":\"a.shetty\""), request.constData());
    }

    void tokenIsEmptyBeforeLogin() {
        ApiClient api;
        QVERIFY(api.token().isEmpty());
    }
};

QTEST_MAIN(TestApiClient)
#include "test_apiclient.moc"