#pragma once
#include <QMainWindow>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const QString& username, const QString& fullName, const QString& role,
               QWidget* parent = nullptr);
};