#include "shell/MainWindow.h"

#include <QAction>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>

namespace {

// The permission table, in one place. api.md and the sprint plan agree on the
// split: Admin sees everything; Teacher sees the academic screens and read-only
// student lookup; Student sees only their own records. Anything not listed here
// is Admin-only by default, so a new action cannot silently become public.
struct ActionRule {
    const char* name;
    Role minimum;  // Admin outranks Teacher outranks Student.
};

constexpr ActionRule kRules[] = {
    {"View / Search", Role::Student},
    {"Add / Edit / Delete", Role::Admin},
    {"Grades", Role::Student},
    {"Attendance", Role::Student},
    {"Record Attendance / Grades", Role::Teacher},
    {"Manage Accounts", Role::Admin},
};

// Admin >= Teacher >= Student. Kept as a function rather than relying on enum
// ordering so the comparison cannot be broken by reordering the enum.
int Rank(Role role) {
    switch (role) {
        case Role::Admin: return 3;
        case Role::Teacher: return 2;
        case Role::Student: return 1;
    }
    return 0;
}

bool Allowed(Role role, Role minimum) { return Rank(role) >= Rank(minimum); }

}  // namespace

MainWindow::MainWindow(const Session& session, QWidget* parent)
    : QMainWindow(parent), session_(session) {
    setWindowTitle(QStringLiteral("Student Record System"));
    resize(900, 600);

    auto addAction = [this](QMenu* menu, const char* name) {
        QAction* action = menu->addAction(QString::fromUtf8(name));
        action->setObjectName(QString::fromUtf8(name));
        for (const auto& rule : kRules) {
            if (QString::fromUtf8(rule.name) == QString::fromUtf8(name)) {
                action->setEnabled(Allowed(session_.role, rule.minimum));
                return;
            }
        }
        // Not in the table: deny rather than assume, so a forgotten entry fails
        // closed instead of quietly granting access.
        action->setEnabled(false);
    };

    QMenu* students = menuBar()->addMenu(QStringLiteral("&Students"));
    addAction(students, "View / Search");
    addAction(students, "Add / Edit / Delete");

    QMenu* academics = menuBar()->addMenu(QStringLiteral("&Academics"));
    addAction(academics, "Grades");
    addAction(academics, "Attendance");
    addAction(academics, "Record Attendance / Grades");

    QMenu* admin = menuBar()->addMenu(QStringLiteral("A&dmin"));
    addAction(admin, "Manage Accounts");

    setCentralWidget(new QLabel(QStringLiteral("Welcome, %1").arg(session_.fullName), this));
    statusBar()->showMessage(QStringLiteral("Logged in as %1").arg(session_.username));
}

bool MainWindow::canUse(const QString& actionName) const {
    for (const auto& rule : kRules) {
        if (actionName == QString::fromUtf8(rule.name)) {
            return Allowed(session_.role, rule.minimum);
        }
    }
    return false;
}