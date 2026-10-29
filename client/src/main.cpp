#include <QApplication>

#include "apiclient/ApiClient.h"
#include "login/LoginDialog.h"
#include "shell/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    ApiClient api;
    LoginDialog login(&api);
    if (login.exec() != QDialog::Accepted) {
        return 0;  // the user closed the dialog without signing in
    }

    MainWindow window(login.session());
    window.show();
    return app.exec();
}
