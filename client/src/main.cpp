#include <QApplication>

#include "apiclient/ApiClient.h"
#include "login/LoginDialog.h"
#include "shell/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    ApiClient api;
    // Demo mode (fake server) while the real backend has no users: set SRS_MOCK=1.
    api.setMockMode(qEnvironmentVariableIsSet("SRS_MOCK"));

    LoginDialog login(&api);
    if (login.exec() != QDialog::Accepted) {
        return 0;
    }

    MainWindow window(login.username(), login.fullName(), login.role());
    window.show();
    return app.exec();
}