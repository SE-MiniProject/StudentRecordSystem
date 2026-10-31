#pragma once
#include <QJsonArray>
#include <QWidget>

class ApiClient;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class StudentsWidget : public QWidget {
    Q_OBJECT
public:
    explicit StudentsWidget(ApiClient* api, QWidget* parent = nullptr);

private slots:
    void onSearchClicked();
    void onPrev();
    void onNext();
    void onLoaded(const QJsonArray& items, int total);
    void onFailed(const QString& message);

private:
    void runSearch();
    void setBusy(bool busy);
    void updatePaging();

    static constexpr int kPageSize = 10;

    ApiClient* api_;
    QLineEdit* searchEdit_;
    QComboBox* semesterBox_;
    QPushButton* searchButton_;
    QPushButton* prevButton_;
    QPushButton* nextButton_;
    QLabel* statusLabel_;
    QLabel* pageLabel_;
    QTableWidget* table_;

    QString lastSearch_;
    int lastSemester_ = 0;
    int offset_ = 0;
    int total_ = 0;
};