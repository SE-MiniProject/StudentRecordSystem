#include "students/StudentsWidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "apiclient/ApiClient.h"

namespace {
// The contract says rollNo/fullName/contact; the old server said roll_no/name/contact_details.
QString pick(const QJsonObject& o, const char* a, const char* b) {
    const QString first = o.value(QLatin1String(a)).toString();
    return first.isEmpty() ? o.value(QLatin1String(b)).toString() : first;
}
}  // namespace

StudentsWidget::StudentsWidget(ApiClient* api, QWidget* parent) : QWidget(parent), api_(api) {
    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(QStringLiteral("Roll number or part of a name"));

    semesterBox_ = new QComboBox;
    semesterBox_->addItem(QStringLiteral("All semesters"), 0);
    for (int s = 1; s <= 8; ++s) {
        semesterBox_->addItem(QStringLiteral("Semester %1").arg(s), s);
    }

    searchButton_ = new QPushButton(QStringLiteral("Search"));
    searchButton_->setDefault(true);

    statusLabel_ = new QLabel;
    statusLabel_->setWordWrap(true);

    table_ = new QTableWidget(0, 5);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Roll No."), QStringLiteral("Name"), QStringLiteral("Department"),
         QStringLiteral("Semester"), QStringLiteral("Contact")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    prevButton_ = new QPushButton(QStringLiteral("< Previous"));
    nextButton_ = new QPushButton(QStringLiteral("Next >"));
    pageLabel_ = new QLabel;

    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(searchEdit_, 1);
    searchRow->addWidget(semesterBox_);
    searchRow->addWidget(searchButton_);

    auto* pageRow = new QHBoxLayout;
    pageRow->addWidget(prevButton_);
    pageRow->addWidget(pageLabel_, 1, Qt::AlignCenter);
    pageRow->addWidget(nextButton_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(searchRow);
    layout->addWidget(statusLabel_);
    layout->addWidget(table_, 1);
    layout->addLayout(pageRow);

    connect(searchButton_, &QPushButton::clicked, this, &StudentsWidget::onSearchClicked);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &StudentsWidget::onSearchClicked);
    connect(prevButton_, &QPushButton::clicked, this, &StudentsWidget::onPrev);
    connect(nextButton_, &QPushButton::clicked, this, &StudentsWidget::onNext);
    connect(api_, &ApiClient::studentsLoaded, this, &StudentsWidget::onLoaded);
    connect(api_, &ApiClient::studentsFailed, this, &StudentsWidget::onFailed);

    updatePaging();
    onSearchClicked();  // show the first page right away
}

void StudentsWidget::onSearchClicked() {
    lastSearch_ = searchEdit_->text().trimmed();
    lastSemester_ = semesterBox_->currentData().toInt();
    offset_ = 0;
    runSearch();
}

void StudentsWidget::onPrev() {
    offset_ = qMax(0, offset_ - kPageSize);
    runSearch();
}

void StudentsWidget::onNext() {
    offset_ += kPageSize;
    runSearch();
}

void StudentsWidget::runSearch() {
    setBusy(true);
    statusLabel_->setStyleSheet(QString());
    statusLabel_->setText(QStringLiteral("Loading..."));
    api_->searchStudents(lastSearch_, lastSemester_, kPageSize, offset_);
}

void StudentsWidget::onLoaded(const QJsonArray& items, int total) {
    setBusy(false);
    total_ = total;
    table_->setRowCount(0);

    for (const QJsonValue& value : items) {
        const QJsonObject o = value.toObject();
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(pick(o, "rollNo", "roll_no")));
        table_->setItem(row, 1, new QTableWidgetItem(pick(o, "fullName", "name")));
        table_->setItem(row, 2, new QTableWidgetItem(o.value(QLatin1String("department")).toString()));
        table_->setItem(row, 3, new QTableWidgetItem(QString::number(o.value(QLatin1String("semester")).toInt())));
        table_->setItem(row, 4, new QTableWidgetItem(pick(o, "contact", "contact_details")));
    }

    if (items.isEmpty()) {
        statusLabel_->setStyleSheet(QString());
        statusLabel_->setText(QStringLiteral("No students found."));  // AC: no-match message
    } else {
        statusLabel_->setText(QStringLiteral("Showing %1-%2 of %3")
                                  .arg(offset_ + 1)
                                  .arg(offset_ + items.size())
                                  .arg(total_));
    }
    updatePaging();
}

void StudentsWidget::onFailed(const QString& message) {
    setBusy(false);
    table_->setRowCount(0);
    statusLabel_->setStyleSheet(QStringLiteral("color: #c0392b;"));
    statusLabel_->setText(message);
    updatePaging();
}

void StudentsWidget::setBusy(bool busy) {
    searchButton_->setEnabled(!busy);
    prevButton_->setEnabled(!busy && offset_ > 0);
    nextButton_->setEnabled(!busy && offset_ + kPageSize < total_);
}

void StudentsWidget::updatePaging() {
    prevButton_->setEnabled(offset_ > 0);
    nextButton_->setEnabled(offset_ + kPageSize < total_);
    const int pages = total_ == 0 ? 1 : (total_ + kPageSize - 1) / kPageSize;
    pageLabel_->setText(QStringLiteral("Page %1 of %2").arg(offset_ / kPageSize + 1).arg(pages));
}