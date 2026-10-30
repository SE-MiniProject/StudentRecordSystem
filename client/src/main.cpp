#include <QApplication>
#include <QDialog>
#include <QString>
#include <cstdlib>

#include "apiclient/ApiClient.h"
#include "login/LoginDialog.h"
#include "shell/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    ApiClient api;

    const char* mock = std::getenv("SRS_MOCK");
    if (mock && QString::fromUtf8(mock) == QLatin1String("1")) {
        api.setMockMode(true);
    }
    const char* base = std::getenv("SRS_API_BASE");
    if (base && *base) {
        api.setBaseUrl(QString::fromUtf8(base));
    }

    LoginDialog login(&api);
    if (login.exec() != QDialog::Accepted) {
        return 0;  // the user closed the dialog without signing in
    }

    MainWindow window(login.session(), &api);
    window.show();
    return app.exec();
}