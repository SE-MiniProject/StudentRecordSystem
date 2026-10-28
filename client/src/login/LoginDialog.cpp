#include "login/LoginDialog.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "apiclient/ApiClient.h"

LoginDialog::LoginDialog(ApiClient* api, QWidget* parent) : QDialog(parent), api_(api) {
    setWindowTitle(QStringLiteral("Student Record System - Login"));
    setModal(true);

    userEdit_ = new QLineEdit;
    passEdit_ = new QLineEdit;
    passEdit_->setEchoMode(QLineEdit::Password);

    errorLabel_ = new QLabel;
    errorLabel_->setStyleSheet(QStringLiteral("color: #c0392b;"));
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();

    loginButton_ = new QPushButton(QStringLiteral("Log in"));
    loginButton_->setDefault(true);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Username"), userEdit_);
    form->addRow(QStringLiteral("Password"), passEdit_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(errorLabel_);
    layout->addWidget(loginButton_);

    connect(loginButton_, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(passEdit_, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);

    connect(api_, &ApiClient::loginSucceeded, this,
            [this](const QString& u, const QString& n, const QString& r) {
                username_ = u;
                fullName_ = n;
                role_ = r;
                accept();
            });
    connect(api_, &ApiClient::loginFailed, this, [this](const QString& message) {
        setBusy(false);
        errorLabel_->setText(message);
        errorLabel_->show();
        passEdit_->clear();
        passEdit_->setFocus();
    });
}

void LoginDialog::onLoginClicked() {
    const QString u = userEdit_->text().trimmed();
    const QString p = passEdit_->text();
    if (u.isEmpty() || p.isEmpty()) {  // the server would reply 400; catch it here first
        errorLabel_->setText(QStringLiteral("Please enter your username and password."));
        errorLabel_->show();
        return;
    }
    errorLabel_->hide();
    setBusy(true);
    api_->login(u, p);
}

void LoginDialog::setBusy(bool busy) {
    loginButton_->setEnabled(!busy);
    userEdit_->setEnabled(!busy);
    passEdit_->setEnabled(!busy);
}