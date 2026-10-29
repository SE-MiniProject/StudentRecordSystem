#pragma once
#include <QMainWindow>

#include "session/Session.h"

// The post-login shell. Menus are role-aware per SRS-15: each action declares
// which roles may use it, and the window enables exactly those. This is a
// usability gate only — the server rejects anything outside the role regardless,
// which is what makes the gate safe to get wrong on the client.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const Session& session, QWidget* parent = nullptr);

    // True when the signed-in role may use the named action. Exposed for tests:
    // the menu itself is not easily interrogated, and the rule is the thing
    // worth asserting.
    bool canUse(const QString& actionName) const;

private:
    Session session_;
};