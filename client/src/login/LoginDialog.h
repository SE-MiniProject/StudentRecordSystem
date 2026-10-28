#pragma once
#include <QDialog>

class ApiClient;
class QLabel;
class QLineEdit;
class QPushButton;

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(ApiClient* api, QWidget* parent = nullptr);

    QString username() const { return username_; }
    QString fullName() const { return fullName_; }
    QString role() const { return role_; }

private slots:
    void onLoginClicked();

private:
    void setBusy(bool busy);

    ApiClient* api_;
    QLineEdit* userEdit_;
    QLineEdit* passEdit_;
    QLabel* errorLabel_;
    QPushButton* loginButton_;
    QString username_, fullName_, role_;
};