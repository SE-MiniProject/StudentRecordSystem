#include "shell/MainWindow.h"

#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QAction>
#include <QMenu>

#include "students/StudentsWidget.h"

// Menus are enabled per role. This is cosmetic only: the server enforces access (SR-004).
MainWindow::MainWindow(ApiClient* api, const QString& username, const QString& fullName,
                       const QString& role, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Student Record System"));
    resize(900, 600);

    const bool isAdmin = role == QLatin1String("admin");
    const bool isTeacher = role == QLatin1String("teacher");

    QMenu* students = menuBar()->addMenu(QStringLiteral("&Students"));
    QAction* viewStudents = students->addAction(QStringLiteral("View / Search"));
    viewStudents->setEnabled(isAdmin || isTeacher);
    connect(viewStudents, &QAction::triggered, this,
            [this, api]() { setCentralWidget(new StudentsWidget(api, this)); });
    students->addAction(QStringLiteral("Add / Edit / Delete"))->setEnabled(isAdmin);

    QMenu* academics = menuBar()->addMenu(QStringLiteral("&Academics"));
    academics->addAction(QStringLiteral("Grades"));            // everyone, scoped by server
    academics->addAction(QStringLiteral("Attendance"));
    academics->addAction(QStringLiteral("Record Attendance / Grades"))->setEnabled(isAdmin || isTeacher);

    QMenu* admin = menuBar()->addMenu(QStringLiteral("A&dmin"));
    admin->addAction(QStringLiteral("Manage Accounts"))->setEnabled(isAdmin);

    setCentralWidget(new QLabel(QStringLiteral("Welcome, %1 (%2)").arg(fullName, role), this));
    statusBar()->showMessage(QStringLiteral("Logged in as %1").arg(username));
}