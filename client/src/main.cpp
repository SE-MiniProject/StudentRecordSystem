#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <cstdlib>

static QString envValue(const char* key, const QString& fallback) {
    const char* value = std::getenv(key);
    return value && *value ? QString::fromUtf8(value) : fallback;
}

class StudentDialog : public QDialog {
    Q_OBJECT
public:
    explicit StudentDialog(const QJsonObject& student, QWidget* parent = nullptr)
        : QDialog(parent), original_(student) {
        setWindowTitle("Edit Student Details");
        setModal(true);
        auto* form = new QFormLayout(this);

        rollNo_ = new QLineEdit(student.value("roll_no").toString());
        rollNo_->setReadOnly(true);
        rollNo_->setStyleSheet("background:#eeeeee;");
        name_ = new QLineEdit(student.value("name").toString());
        contact_ = new QLineEdit(student.value("contact_details").toString());
        department_ = new QLineEdit(student.value("department").toString());
        semester_ = new QSpinBox();
        semester_->setRange(1, 12);
        semester_->setValue(student.value("semester").toInt(1));

        form->addRow("Roll number (fixed)", rollNo_);
        form->addRow("Name", name_);
        form->addRow("Contact details", contact_);
        form->addRow("Department", department_);
        form->addRow("Semester", semester_);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (name_->text().trimmed().isEmpty() || contact_->text().trimmed().isEmpty() ||
                department_->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, "Validation", "Name, contact details and department are required.");
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    QJsonObject updatePayload() const {
        return QJsonObject{
            {"name", name_->text().trimmed()},
            {"contact_details", contact_->text().trimmed()},
            {"department", department_->text().trimmed()},
            {"semester", semester_->value()}
        };
    }
    qint64 studentId() const { return static_cast<qint64>(original_.value("id").toDouble()); }

private:
    QJsonObject original_;
    QLineEdit *rollNo_{}, *name_{}, *contact_{}, *department_{};
    QSpinBox* semester_{};
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow() {
        apiBase_ = envValue("SRS_API_BASE", "http://127.0.0.1:8080");
        token_ = envValue("SRS_BEARER_TOKEN", "local-admin-token-change-me");
        network_ = new QNetworkAccessManager(this);

        auto* central = new QWidget;
        auto* layout = new QVBoxLayout(central);
        auto* note = new QLabel("Student Record System — Update / Delete stories\n"
                                "Demo token defaults to Administrator. The server enforces roles independently.");
        note->setWordWrap(true);
        layout->addWidget(note);

        table_ = new QTableWidget(0, 6);
        table_->setHorizontalHeaderLabels({"ID", "Roll No.", "Name", "Contact", "Department", "Semester"});
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        layout->addWidget(table_);

        auto* buttons = new QHBoxLayout;
        auto* refresh = new QPushButton("Refresh");
        auto* edit = new QPushButton("Edit selected");
        auto* remove = new QPushButton("Delete selected");
        buttons->addWidget(refresh);
        buttons->addWidget(edit);
        buttons->addWidget(remove);
        layout->addLayout(buttons);
        setCentralWidget(central);
        resize(950, 520);
        connect(refresh, &QPushButton::clicked, this, &MainWindow::loadStudents);
        connect(edit, &QPushButton::clicked, this, &MainWindow::editSelected);
        connect(remove, &QPushButton::clicked, this, &MainWindow::deleteSelected);
        connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editSelected(); });
        loadStudents();
    }

private:
    QUrl endpoint(const QString& path) const { return QUrl(apiBase_ + path); }
    QNetworkRequest request(const QString& path) const {
        QNetworkRequest req(endpoint(path));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Authorization", QByteArray("Bearer ") + token_.toUtf8());
        return req;
    }
    void showReplyError(QNetworkReply* reply, const QString& action) {
        QString message = action + " failed";
        const QByteArray data = reply->readAll();
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(data, &parseError);
        if (doc.isObject()) message += ":\n" + doc.object().value("error").toString("Unexpected server response");
        else if (!data.isEmpty()) message += ":\n" + QString::fromUtf8(data);
        else message += ":\n" + reply->errorString();
        QMessageBox::warning(this, "Request failed", message);
    }
    void loadStudents() {
        auto* reply = network_->get(request("/api/students"));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (reply->error() != QNetworkReply::NoError) { showReplyError(reply, "Loading students"); reply->deleteLater(); return; }
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isArray()) { QMessageBox::warning(this, "Error", "Unexpected student list response."); reply->deleteLater(); return; }
            students_.clear();
            table_->setRowCount(0);
            const auto array = doc.array();
            for (const auto value : array) {
                const auto obj = value.toObject();
                students_.append(obj);
                const int row = table_->rowCount();
                table_->insertRow(row);
                const QStringList values = {
                    QString::number(static_cast<qint64>(obj.value("id").toDouble())),
                    obj.value("roll_no").toString(), obj.value("name").toString(),
                    obj.value("contact_details").toString(), obj.value("department").toString(),
                    QString::number(obj.value("semester").toInt())
                };
                for (int col = 0; col < values.size(); ++col) {
                    auto* item = new QTableWidgetItem(values[col]);
                    if (col == 0) item->setData(Qt::UserRole, obj.value("id").toDouble());
                    table_->setItem(row, col, item);
                }
            }
            reply->deleteLater();
        });
    }
    int selectedIndex() const {
        const auto ranges = table_->selectedRanges();
        return ranges.isEmpty() ? -1 : ranges.first().topRow();
    }
    QJsonObject selectedStudent() const {
        const int row = selectedIndex();
        if (row < 0 || row >= students_.size()) return {};
        return students_[row];
    }
    void editSelected() {
        const auto student = selectedStudent();
        if (student.isEmpty()) { QMessageBox::information(this, "Select a student", "Select a student row first."); return; }
        StudentDialog dialog(student, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const QString path = "/api/students/" + QString::number(dialog.studentId());
        auto* reply = network_->sendCustomRequest(request(path), "PUT",
            QJsonDocument(dialog.updatePayload()).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (reply->error() != QNetworkReply::NoError) showReplyError(reply, "Updating student");
            else QMessageBox::information(this, "Updated", "Student details were saved. The roll number remains unchanged.");
            reply->deleteLater();
            loadStudents();
        });
    }
    void deleteSelected() {
        const auto student = selectedStudent();
        if (student.isEmpty()) { QMessageBox::information(this, "Select a student", "Select a student row first."); return; }
        const QString roll = student.value("roll_no").toString();
        const auto answer = QMessageBox::warning(
            this, "Confirm student deletion",
            "Permanently delete student " + roll + "?\n\nRelated grades and attendance will also be deleted.\nThis action cannot be undone.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return; // No DELETE request is sent before explicit confirmation.
        const QString path = "/api/students/" + QString::number(static_cast<qint64>(student.value("id").toDouble()));
        auto* reply = network_->sendCustomRequest(request(path), "DELETE");
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (reply->error() != QNetworkReply::NoError) showReplyError(reply, "Deleting student");
            else QMessageBox::information(this, "Deleted", "Student and dependent records were deleted.");
            reply->deleteLater();
            loadStudents();
        });
    }

    QString apiBase_, token_;
    QNetworkAccessManager* network_{};
    QTableWidget* table_{};
    QList<QJsonObject> students_;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}

#include "main.moc"
