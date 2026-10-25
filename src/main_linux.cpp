#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QAbstractItemView>
#include <QStringList>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QStatusBar>
#include <QVariant>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <map>
#include <string>

#include "fields.h"
#include "save.h"
#include "steam_save_discovery.h"

namespace {
QString qs(const std::wstring& s) { return QString::fromStdWString(s); }
QString valueText(const Field& f, int64_t v) {
    if (f.type == FieldType::Flag8 || f.type == FieldType::Bits8 || f.type == FieldType::Bits32)
        return v ? QStringLiteral("Yes") : QStringLiteral("No");
    if (f.type == FieldType::Hex32) return QStringLiteral("0x%1").arg(static_cast<quint32>(v), 8, 16, QLatin1Char('0')).toUpper();
    if (f.names) {
        for (const auto& e : *f.names)
            if (e.value == v) return qs(e.name);
        return QStringLiteral("Unknown (%1)").arg(v);
    }
    return QString::number(v);
}
bool isToggle(const Field& f) {
    return f.type == FieldType::Flag8 || f.type == FieldType::Bits8 || f.type == FieldType::Bits32;
}
}

class EditorWindow final : public QMainWindow {
public:
    EditorWindow() {
        setWindowTitle(QStringLiteral("RE6 Save Editor"));
        resize(1100, 720);
        pages_ = BuildPages();

        auto* central = new QWidget(this);
        auto* root = new QVBoxLayout(central);
        auto* actions = new QHBoxLayout;
        auto* open = new QPushButton(QStringLiteral("Open…"), central);
        saveButton_ = new QPushButton(QStringLiteral("Save"), central);
        saveAsButton_ = new QPushButton(QStringLiteral("Save As…"), central);
        auto* unlock = new QPushButton(QStringLiteral("Unlock Everything"), central);
        actions->addWidget(open);
        actions->addWidget(saveButton_);
        actions->addWidget(saveAsButton_);
        actions->addSpacing(16);
        actions->addWidget(unlock);
        actions->addStretch();
        root->addLayout(actions);

        auto* split = new QSplitter(central);
        tree_ = new QTreeWidget(split);
        tree_->setHeaderHidden(true);
        tree_->setMinimumWidth(220);
        table_ = new QTableWidget(split);
        table_->setColumnCount(3);
        table_->setHorizontalHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value (double-click to edit)"), QStringLiteral("Offset")});
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        split->addWidget(tree_);
        split->addWidget(table_);
        split->setStretchFactor(1, 1);
        root->addWidget(split);
        setCentralWidget(central);
        statusBar()->showMessage(QStringLiteral("No file loaded. Use Open… to select savedata.bin."));

        buildTree();
        connect(open, &QPushButton::clicked, this, [this] { openFile(); });
        connect(saveButton_, &QPushButton::clicked, this, [this] { saveFile(false); });
        connect(saveAsButton_, &QPushButton::clicked, this, [this] { saveFile(true); });
        connect(unlock, &QPushButton::clicked, this, [this] {
            if (!save_.Loaded()) return;
            UnlockEverything(save_);
            showPage(currentPage_);
            updateStatus();
        });
        connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
            if (!item) return;
            int index = item->data(0, Qt::UserRole).toInt();
            if (index >= 0 && index < static_cast<int>(pages_.size())) {
                currentPage_ = index;
                showPage(index);
            } else if (item->childCount() > 0) {
                item->setExpanded(true);
                tree_->setCurrentItem(item->child(0));
            }
        });
        connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { editRow(row); });
        saveButton_->setEnabled(false);
        saveAsButton_->setEnabled(false);
    }

private:
    std::vector<Page> pages_;
    SaveFile save_;
    QTreeWidget* tree_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* saveAsButton_ = nullptr;
    int currentPage_ = 0;

    void buildTree() {
        std::map<QString, QTreeWidgetItem*> nodes;
        for (int i = 0; i < static_cast<int>(pages_.size()); ++i) {
            const QString path = qs(pages_[i].path);
            const QStringList parts = path.split('/');
            QString prefix;
            QTreeWidgetItem* parent = nullptr;
            for (int p = 0; p < parts.size(); ++p) {
                prefix = prefix.isEmpty() ? parts[p] : prefix + '/' + parts[p];
                auto it = nodes.find(prefix);
                if (it == nodes.end()) {
                    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
                    item->setText(0, parts[p]);
                    item->setData(0, Qt::UserRole, p == parts.size() - 1 ? i : -1);
                    nodes.emplace(prefix, item);
                    parent = item;
                } else parent = it->second;
            }
        }
        tree_->expandToDepth(0);
        if (tree_->topLevelItemCount()) tree_->setCurrentItem(tree_->topLevelItem(0));
    }

    void showPage(int index) {
        table_->setRowCount(0);
        if (index < 0 || index >= static_cast<int>(pages_.size())) return;
        const auto& fields = pages_[index].fields;
        table_->setRowCount(static_cast<int>(fields.size()));
        for (int row = 0; row < static_cast<int>(fields.size()); ++row) {
            const Field& f = fields[row];
            auto* name = new QTableWidgetItem(qs(f.name));
            auto* value = new QTableWidgetItem(save_.Loaded() ? valueText(f, save_.Get(f)) : QString());
            auto* offset = new QTableWidgetItem(QStringLiteral("0x%1").arg(f.offset, 5, 16, QLatin1Char('0')).toUpper());
            name->setFlags(name->flags() & ~Qt::ItemIsEditable);
            value->setFlags(value->flags() & ~Qt::ItemIsEditable);
            offset->setFlags(offset->flags() & ~Qt::ItemIsEditable);
            if (f.readOnly) name->setToolTip(QStringLiteral("Read-only"));
            table_->setItem(row, 0, name);
            table_->setItem(row, 1, value);
            table_->setItem(row, 2, offset);
        }
    }

    void editRow(int row) {
        if (!save_.Loaded() || currentPage_ < 0 || row < 0) return;
        const Field& f = pages_[currentPage_].fields[row];
        if (f.readOnly) return;
        const int64_t current = save_.Get(f);
        int64_t next = current;
        if (isToggle(f)) {
            next = current ? 0 : 1;
        } else if (f.names) {
            QStringList labels;
            int selected = 0, n = 0;
            for (const auto& e : *f.names) {
                labels << qs(e.name);
                if (e.value == current) selected = n;
                ++n;
            }
            bool ok = false;
            QString choice = QInputDialog::getItem(this, qs(f.name), QStringLiteral("Value"), labels, selected, false, &ok);
            if (!ok) return;
            for (const auto& e : *f.names) if (qs(e.name) == choice) { next = e.value; break; }
        } else {
            bool ok = false;
            QString entered = QInputDialog::getText(this, qs(f.name),
                QStringLiteral("Enter a numeric value (0x prefix for hexadecimal)"),
                QLineEdit::Normal, QString::number(current), &ok).trimmed();
            if (!ok || entered.isEmpty()) return;
            bool valid = false;
            if (f.type == FieldType::I16) {
                next = entered.toLongLong(&valid, 0);
            } else {
                QString digits = entered;
                int base = 10;
                if (digits.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
                    digits = digits.mid(2);
                    base = 16;
                }
                qulonglong raw = digits.toULongLong(&valid, base);
                if (valid && raw <= 0xFFFFFFFFull) next = static_cast<int64_t>(raw);
                else valid = false;
            }
            if (!valid) {
                QMessageBox::warning(this, QStringLiteral("Invalid value"), QStringLiteral("Enter a valid numeric value."));
                return;
            }
        }
        int64_t lo, hi;
        FieldRange(f, lo, hi);
        if (next < lo || next > hi) {
            QMessageBox::warning(this, QStringLiteral("Invalid value"),
                                 QStringLiteral("Value must be between %1 and %2.").arg(lo).arg(hi));
            return;
        }
        save_.Set(f, next);
        showPage(currentPage_);
        updateStatus();
    }

    void openFile() {
        if (save_.dirty) {
            auto answer = QMessageBox::question(this, QStringLiteral("Unsaved changes"),
                QStringLiteral("Save changes before opening another file?"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (answer == QMessageBox::Cancel) return;
            if (answer == QMessageBox::Save && !saveFile(false)) return;
        }
        const QStringList detectedSaves = FindResidentEvil6SaveCandidates(
            QDir::homePath(), qEnvironmentVariable("XDG_DATA_HOME"),
            qEnvironmentVariable("STEAM_DIR"));
        const bool hasDetectedSave = !detectedSaves.isEmpty();
        const QString initialDirectory = hasDetectedSave
            ? QFileInfo(detectedSaves.front()).absolutePath()
            : QDir::homePath();

        QFileDialog dialog(this,
            hasDetectedSave
                ? QStringLiteral("Open Resident Evil 6 save — detected path pre-filled")
                : QStringLiteral("Open Resident Evil 6 save"),
            initialDirectory,
            QStringLiteral("RE6 save (savedata*.bin);;All files (*)"));
        dialog.setFileMode(QFileDialog::ExistingFile);
        if (hasDetectedSave) {
            // Pre-fill the detected filename, but leave confirmation to the user.
            dialog.selectFile(QFileInfo(detectedSaves.front()).fileName());
            statusBar()->showMessage(
                QStringLiteral("Found %1 possible Steam save(s); the most recently modified is preselected. Confirm it or choose another file.")
                    .arg(detectedSaves.size()));
        } else {
            statusBar()->showMessage(
                QStringLiteral("No Steam save was detected automatically. Choose savedata.bin manually."));
        }
        if (dialog.exec() != QDialog::Accepted) return;
        const QString file = dialog.selectedFiles().value(0);
        if (file.isEmpty()) return;
        std::wstring error;
        if (!save_.Load(file.toStdWString(), error)) {
            QMessageBox::critical(this, QStringLiteral("Could not open save"), qs(error));
            return;
        }
        showPage(currentPage_);
        saveButton_->setEnabled(true);
        saveAsButton_->setEnabled(true);
        updateStatus();
    }

    bool saveFile(bool saveAs) {
        if (!save_.Loaded()) return false;
        QString file = saveAs ? QString() : qs(save_.path);
        if (file.isEmpty()) file = QFileDialog::getSaveFileName(this, QStringLiteral("Save Resident Evil 6 save"),
                                                                 qs(save_.path), QStringLiteral("RE6 save (*.bin);;All files (*)"));
        if (file.isEmpty()) return false;
        std::wstring error;
        if (!save_.Save(file.toStdWString(), error)) {
            QMessageBox::critical(this, QStringLiteral("Could not save"), qs(error));
            return false;
        }
        updateStatus();
        statusBar()->showMessage(QStringLiteral("Saved. Checksum updated; backup is saved beside the original as .bak."), 8000);
        return true;
    }

    void updateStatus() {
        if (!save_.Loaded()) return;
        setWindowTitle((save_.dirty ? QStringLiteral("* ") : QString()) +
                       QFileInfo(qs(save_.path)).fileName() + QStringLiteral(" - RE6 Save Editor"));
        statusBar()->showMessage(qs(save_.path) + (save_.dirty ? QStringLiteral("  |  Unsaved changes") : QStringLiteral("  |  Loaded")));
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RE6 Save Editor"));
    EditorWindow window;
    window.show();
    return app.exec();
}
