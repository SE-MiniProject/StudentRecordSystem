#pragma once
#include <QMainWindow>

class ApiClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(ApiClient* api, const QString& username, const QString& fullName,
               const QString& role, QWidget* parent = nullptr);
};