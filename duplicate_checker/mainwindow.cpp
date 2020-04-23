#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCommonStyle>
#include <QDesktopWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QDebug>
#include <QDateTime>
#include <QMessageBox>

#include <queue>
#include <vector>
#include <future>

const size_t COLUMNS = 5;

main_window::main_window(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    QCommonStyle style;
    setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), qApp->desktop()->availableGeometry()));

    ui->actionAbout->setIcon(style.standardIcon(QCommonStyle::SP_DialogHelpButton));
    ui->actionCancel->setIcon(style.standardIcon(QCommonStyle::SP_DialogCancelButton));
    ui->actionChoose->setIcon(style.standardIcon(QCommonStyle::SP_DialogOpenButton));
    ui->actionExit->setIcon(style.standardIcon(QCommonStyle::SP_DialogCloseButton));
    ui->actionPause->setIcon(style.standardIcon(QCommonStyle::SP_MediaPause));
    ui->actionRefresh->setIcon(style.standardIcon(QCommonStyle::SP_BrowserReload));
    ui->actionScan->setIcon(style.standardIcon(QCommonStyle::SP_MediaPlay));

    ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    ui->treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->treeWidget->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->treeWidget->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    ui->treeWidget->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    ui->treeWidget->header()->setSectionResizeMode(4, QHeaderView::Stretch);

    connect(ui->actionAbout, &QAction::triggered, this, &main_window::about_slot);
    connect(ui->actionAgain, &QAction::triggered, this, &main_window::again_slot);
    connect(ui->actionCancel, &QAction::triggered, this, &main_window::cancel_slot);
    connect(ui->actionChoose, &QAction::triggered, this, &main_window::choose_slot);
    connect(ui->actionCollision, &QAction::triggered, this, &main_window::collision_slot);
    connect(ui->actionDelete, &QAction::triggered, this, &main_window::delete_slot);
    connect(ui->actionExit, &QAction::triggered, this, &main_window::exit_slot);
    connect(ui->actionPause, &QAction::triggered, this, &main_window::pause_slot);
    connect(ui->actionRefresh, &QAction::triggered, this, &main_window::refresh_slot);
    connect(ui->actionScan, &QAction::triggered, this, &main_window::scan_slot);
    connect(ui->treeWidget, &QTreeWidget::itemActivated, this, &main_window::open_slot);

    thread = new QThread();
    st.moveToThread(thread);
    connect(thread, &QThread::started, &st, &scantools::start);

    connect(&st, &scantools::started, this, &main_window::started_slot);
    connect(&st, &scantools::paused, this, &main_window::paused_slot);
    connect(&st, &scantools::canceled, this, &main_window::finished_slot);
    connect(&st, &scantools::finished, this, &main_window::finished_slot);
    connect(&st, &scantools::console, this, &main_window::console_slot);
    connect(&st, &scantools::add_item, this, &main_window::add_item);
    connect(&st, &scantools::color_item, this, &main_window::color_item);
    connect(&st, &scantools::clear_items, this, &main_window::clear_items);
    connect(&st, &scantools::update_items, this, &main_window::update_items);
    st.open_directory();
}

main_window::~main_window() {
    delete thread;
}

void main_window::about_slot() {
    QMessageBox::aboutQt(this);
}

void main_window::again_slot() {
    setItemsEnabled(true, ui->actionChoose, ui->actionRefresh, ui->actionScan, ui->actionCollision);
    setItemsEnabled(false, ui->actionPause, ui->actionCancel);
    setItemsVisible(false, ui->actionAgain, ui->actionDelete);
    disconnect(ui->treeWidget, &QTreeWidget::itemActivated, this, &main_window::select_slot);
    connect(ui->treeWidget, &QTreeWidget::itemActivated, this, &main_window::open_slot);
    ui->treeWidget->setEnabled(false);
    st.open_directory();
    ui->treeWidget->setEnabled(true);
}

void main_window::cancel_slot() {
    st.cancel();
    thread->start();
}

void main_window::choose_slot() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Directory for Scanning",
                QString(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    QDir::setCurrent(dir);
    st.open_directory();
}

void main_window::collision_slot() {
    st.set_mode(!st.is_mode());
    ui->actionCollision->setToolTip(QString("Collision mode ").append((st.is_mode()) ? "on" : "off"));

    if (st.is_mode()) {
        st.set_mode(false);
        console_slot("collision mode off", true, "purple");
        ui->actionCollision->setToolTip("Collision mode on");
    } else {
        st.set_mode(true);
        console_slot("collision mode on", true, "purple");
        ui->actionCollision->setToolTip("Collision mode off");
    }
}

void main_window::delete_slot() {
    size_t n = st.number_for_deleting();
    if (n == 0) {
        short_dialog(QString("No files for deleting"));
        return;
    }
    auto res = long_dialog("Deleting files!", QString("Do you really want to delete %1 ").arg(n).append((n == 1) ? "file" : "files").append("?"));
    if (res == QMessageBox::Ok) {
        QTreeWidgetItemIterator it(ui->treeWidget);
        size_t deleted = st.delete_files(it, [](const QTreeWidgetItem &item){return item.text(1);}, [](const QTreeWidgetItem &item){return item.text(3);});
        if (deleted == 1) {
            console_slot("file was deleted", false, "red");
            short_dialog(QString("%1 file was deleted").arg(deleted));
        } else {
            console_slot("files were deleted", false, "red");
            short_dialog(QString("%1 files were deleted").arg(deleted));
        }
        again_slot();
    }
}

void main_window::exit_slot() {
    if (st.is_scanning() || st.is_paused()) {
        auto res = long_dialog("Exit", "Do you really want to exit? Files are still scanning");
        if (res == QMessageBox::Ok) {
            thread->exit();
            QWidget::close();
        }
    } else {
        QWidget::close();
    }
}

void main_window::open_slot(QTreeWidgetItem *item, int column) {
    QString path = QDir::currentPath() + "/" + item->text(0);
    st.open_directory(path);
}

void main_window::pause_slot() {
    st.pause();
    thread->quit();
}

void main_window::refresh_slot() {
    st.open_directory();
}

void main_window::scan_slot() {
    thread->start();
    ui->treeWidget->clear();
}

void main_window::select_slot(QTreeWidgetItem *item, int column) {
    st.select_item(item, [](const QTreeWidgetItem &item){return item.text(1);}, [](QTreeWidgetItem &item, QString value){return item.setText(1, value);});
}

void main_window::started_slot() {
    setText(ui->actionScan, "Resume");
    setItemsEnabled(true, ui->actionPause, ui->actionCancel);
    setItemsEnabled(false, ui->actionChoose, ui->actionRefresh, ui->actionScan, ui->actionCollision);
    ui->treeWidget->setDisabled(true);
}
void main_window::paused_slot() {
    setItemsEnabled(true, ui->actionScan, ui->actionCancel);
    setItemsEnabled(false, ui->actionPause);
}
void main_window::finished_slot() {
    thread->exit();
    setText(ui->actionScan, "Scan");
    setItemsEnabled(false, ui->actionScan, ui->actionPause, ui->actionCancel);
    setItemsVisible(true, ui->actionAgain, ui->actionDelete);

    disconnect(ui->treeWidget, &QTreeWidget::itemActivated, this, &main_window::open_slot);
    connect(ui->treeWidget, &QTreeWidget::itemActivated, this, &main_window::select_slot);
    ui->treeWidget->setEnabled(true);
}

void main_window::error(QString &text) {
    QMessageBox messageBox;
    messageBox.critical(nullptr, "Error", text);
    messageBox.setFixedSize(500, 200);
}
void main_window::console_slot(QString text, bool save, QString color) {
    if (color != "") text = QString("<font color=\"").append(color).append("\">").append(text).append("</font>");
    if (save) {
        cs.add_line(text);
    } else {
        cs.change_line(text);
    }
    ui->label->setText(cs.get_text());
}

void main_window::add_item(QString s1, QString s2, QString s3, QString s4, QString s5, QBrush *brush) {
    QTreeWidgetItem *item = new QTreeWidgetItem(ui->treeWidget);
    item->setText(0, s1);
    item->setText(1, s2);
    item->setText(2, s3);
    item->setText(3, s4);
    item->setText(4, s5);
    ui->treeWidget->addTopLevelItem(item);
    if (brush != nullptr) color_item(item, brush);
}

void main_window::color_item(QTreeWidgetItem *item, QBrush *brush) {
    for (size_t k = 0; k < COLUMNS; k++) {
        item->setForeground(k, *brush);
    }
}

void main_window::clear_items() {
    ui->treeWidget->clear();
    QCoreApplication::processEvents();
}

void main_window::update_items() {
    QCoreApplication::processEvents();
}

int main_window::short_dialog(QString const &text) {
    QMessageBox message_box;
    message_box.setText(text);
    message_box.setStandardButtons(QMessageBox::Ok);
    return message_box.exec();
}
int main_window::long_dialog(QString const &text, QString const &information) {
    QMessageBox message_box;
    message_box.setText(text);
    message_box.setInformativeText(information);
    message_box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    message_box.setIcon(QMessageBox::Information);
    message_box.setDefaultButton(QMessageBox::Ok);
    return message_box.exec();
}
