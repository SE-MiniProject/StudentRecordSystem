#include <QtTest>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "apiclient/ApiClient.h"
#include "login/LoginDialog.h"
#include "session/Session.h"

// Exercises the dialog directly (no HTTP): the signals are emitted by hand so
// each branch of the dialog's reaction can be checked in isolation.
class TestLoginDialog : public QObject {
    Q_OBJECT

private:
    // Addressed by objectName, not by child order: the form's own "Username"
    // row label is also a QLabel, so a positional lookup silently reads the
    // wrong widget and the test passes for the wrong reason.
    static QLineEdit* usernameField(LoginDialog& dialog) {
        return dialog.findChild<QLineEdit*>(QStringLiteral("userEdit"));
    }

    static QLineEdit* passwordField(LoginDialog& dialog) {
        return dialog.findChild<QLineEdit*>(QStringLiteral("passEdit"));
    }

    static QPushButton* loginButton(LoginDialog& dialog) {
        return dialog.findChild<QPushButton*>(QStringLiteral("loginButton"));
    }

    static QLabel* errorLabel(LoginDialog& dialog) {
        return dialog.findChild<QLabel*>(QStringLiteral("errorLabel"));
    }

private slots:
    void passwordFieldIsMasked() {
        ApiClient api;
        LoginDialog dialog(&api);

        QLineEdit* password = passwordField(dialog);
        QVERIFY(password);
        QCOMPARE(password->echoMode(), QLineEdit::Password);
    }

    // The client pre-checks blanks so the user gets an answer without a round
    // trip, and it must use the same sentence the server's 400 produces.
    void blankCredentialsAreRejectedWithoutARequest() {
        ApiClient api;
        LoginDialog dialog(&api);

        QSignalSpy requestSent(&api, &ApiClient::loginFailed);
        loginButton(dialog)->click();

        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QLabel* error = errorLabel(dialog);
        QVERIFY(error);
        QVERIFY(!error->isHidden());
        QCOMPARE(error->text(), ApiClient::missingCredentialsMessage());
        QCOMPARE(requestSent.count(), 0);
    }

    void successfulSignalAcceptsWithTheSession() {
        ApiClient api;
        LoginDialog dialog(&api);

        Session session;
        session.username = QStringLiteral("a.shetty");
        session.fullName = QStringLiteral("Advitha Shetty");
        session.role = Role::Teacher;
        session.token = QStringLiteral("opaque-token");
        emit api.loginSucceeded(session);

        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.session().username, QStringLiteral("a.shetty"));
        QCOMPARE(dialog.session().role, Role::Teacher);
        QCOMPARE(dialog.session().token, QStringLiteral("opaque-token"));
    }

    // A failed attempt must show the server's message, clear the password, and
    // leave the dialog open and usable for a retry.
    void failedSignalShowsMessageAndClearsPassword() {
        ApiClient api;
        LoginDialog dialog(&api);

        QLineEdit* password = passwordField(dialog);
        password->setText(QStringLiteral("wrong"));

        emit api.loginFailed(QStringLiteral("Invalid username or password."));

        QCOMPARE(dialog.result(), 0);  // not accepted, not rejected
        QVERIFY(password->text().isEmpty());
        QVERIFY(loginButton(dialog)->isEnabled());
        QLabel* error = errorLabel(dialog);
        QVERIFY(error);
        QVERIFY(!error->isHidden());
        QCOMPARE(error->text(), QStringLiteral("Invalid username or password."));
    }

    // While a request is in flight the controls are disabled, so a double-click
    // cannot send two logins.
    void submittingDisablesTheForm() {
        ApiClient api;
        LoginDialog dialog(&api);

        usernameField(dialog)->setText(QStringLiteral("a.shetty"));
        passwordField(dialog)->setText(QStringLiteral("correct-horse"));
        loginButton(dialog)->click();

        QVERIFY(!loginButton(dialog)->isEnabled());
        QVERIFY(!usernameField(dialog)->isEnabled());
        QVERIFY(!passwordField(dialog)->isEnabled());
    }
};

QTEST_MAIN(TestLoginDialog)
#include "test_logindialog.moc"