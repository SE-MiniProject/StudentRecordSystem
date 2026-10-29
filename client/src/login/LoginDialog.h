#pragma once
#include <QDialog>

#include "session/Session.h"

class ApiClient;
class QLabel;
class QLineEdit;
class QPushButton;

// Asks for credentials and, on success, hands back the established Session.
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(ApiClient* api, QWidget* parent = nullptr);

    const Session& session() const { return session_; }

private slots:
    void onLoginClicked();

private:
    void setBusy(bool busy);

    ApiClient* api_;
    QLineEdit* userEdit_;
    QLineEdit* passEdit_;
    QLabel* errorLabel_;
    QPushButton* loginButton_;
    Session session_;
};
