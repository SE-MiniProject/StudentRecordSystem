#include <QtTest>

#include "session/Session.h"
#include "shell/MainWindow.h"

// SRS-15: each role sees only its own scope. The rule is the thing worth
// asserting, so the test drives canUse() rather than scraping menu state.
class TestMainWindow : public QObject {
    Q_OBJECT

private:
    static Session SessionWith(Role role) {
        Session s;
        s.username = QStringLiteral("demo");
        s.fullName = QStringLiteral("Demo User");
        s.role = role;
        s.token = QStringLiteral("token");
        return s;
    }

private slots:
    void adminSeesEverything() {
        MainWindow w(SessionWith(Role::Admin));
        QVERIFY(w.canUse(QStringLiteral("View / Search")));
        QVERIFY(w.canUse(QStringLiteral("Add / Edit / Delete")));
        QVERIFY(w.canUse(QStringLiteral("Grades")));
        QVERIFY(w.canUse(QStringLiteral("Attendance")));
        QVERIFY(w.canUse(QStringLiteral("Record Attendance / Grades")));
        QVERIFY(w.canUse(QStringLiteral("Manage Accounts")));
    }

    void teacherSeesAcademicsButNotAccounts() {
        MainWindow w(SessionWith(Role::Teacher));
        QVERIFY(w.canUse(QStringLiteral("View / Search")));
        QVERIFY(w.canUse(QStringLiteral("Grades")));
        QVERIFY(w.canUse(QStringLiteral("Attendance")));
        QVERIFY(w.canUse(QStringLiteral("Record Attendance / Grades")));
        QVERIFY(!w.canUse(QStringLiteral("Manage Accounts")));
        QVERIFY(!w.canUse(QStringLiteral("Add / Edit / Delete")));
    }

    void studentSeesOnlyOwnScope() {
        MainWindow w(SessionWith(Role::Student));
        QVERIFY(w.canUse(QStringLiteral("View / Search")));
        QVERIFY(w.canUse(QStringLiteral("Grades")));
        QVERIFY(w.canUse(QStringLiteral("Attendance")));
        QVERIFY(!w.canUse(QStringLiteral("Record Attendance / Grades")));
        QVERIFY(!w.canUse(QStringLiteral("Add / Edit / Delete")));
        QVERIFY(!w.canUse(QStringLiteral("Manage Accounts")));
    }

    void unknownActionIsDenied() {
        MainWindow w(SessionWith(Role::Admin));
        QVERIFY(!w.canUse(QStringLiteral("Something Not In The Table")));
    }
};

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"