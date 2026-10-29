#include <QtTest>

#include "session/Session.h"

// The role string is parsed once, at the login boundary. These tests pin the
// mapping and the validity rule that stops an empty 200 body opening the app.
class TestSession : public QObject {
    Q_OBJECT

private slots:
    void roleFromString_data() {
        QTest::addColumn<QString>("value");
        QTest::addColumn<Role>("expected");

        QTest::newRow("admin") << QStringLiteral("admin") << Role::Admin;
        QTest::newRow("teacher") << QStringLiteral("teacher") << Role::Teacher;
        QTest::newRow("student") << QStringLiteral("student") << Role::Student;
    }

    void parsesKnownRoles_data() {
        QTest::addColumn<QString>("value");
        QTest::addColumn<Role>("expected");

        QTest::newRow("admin") << QStringLiteral("admin") << Role::Admin;
        QTest::newRow("teacher") << QStringLiteral("teacher") << Role::Teacher;
        QTest::newRow("student") << QStringLiteral("student") << Role::Student;
    }

    void parsesKnownRoles() {
        QFETCH(QString, value);
        QFETCH(Role, expected);
        QCOMPARE(::roleFromString(value), expected);
    }

    // An unrecognised role must not fall through to something privileged.
    void unknownRoleIsLeastPrivileged_data() {
        QTest::addColumn<QString>("value");
        QTest::newRow("empty") << QString();
        QTest::newRow("garbage") << QStringLiteral("superuser");
        QTest::newRow("case") << QStringLiteral("Admin");
        QTest::newRow("whitespace") << QStringLiteral(" admin ");
    }

    void unknownRoleIsLeastPrivileged() {
        QFETCH(QString, value);
        QCOMPARE(roleFromString(value), Role::Student);
    }

    void sessionIsInvalidWithoutTokenOrUsername() {
        Session session;
        QVERIFY(!session.isValid());

        session.username = QStringLiteral("a.shetty");
        QVERIFY(!session.isValid());

        session.token = QStringLiteral("opaque-token");
        QVERIFY(session.isValid());
    }

    void validSessionCarriesItsFields() {
        Session session;
        session.username = QStringLiteral("a.shetty");
        session.fullName = QStringLiteral("Advitha Shetty");
        session.role = Role::Admin;
        session.token = QStringLiteral("opaque-token");

        QVERIFY(session.isValid());
        QCOMPARE(session.role, Role::Admin);
    }
};

QTEST_MAIN(TestSession)
#include "test_session.moc"