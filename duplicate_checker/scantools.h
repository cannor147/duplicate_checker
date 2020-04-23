#ifndef SCANTOOLS_H
#define SCANTOOLS_H

#include <QFileInfo>
#include <QDebug>
#include <QString>
#include <queue>
#include <vector>
#include <QTreeWidget>
#include <QDateTime>
#include <QDir>

static QColor red = QColor(255, 0, 0);
static QColor black = QColor(0, 0, 0);
static QBrush red_brush = QBrush(red);
static QBrush black_brush = QBrush(black);

struct file {
    QString name;
    QString path;
    long long size;
    QDateTime date;

    bool skip;
    bool duplicated;
    size_t first;
    QString hash;

    file(QFileInfo &file_info) {
        name = file_info.fileName();
        path = file_info.filePath();
        size = file_info.size();
        date = file_info.lastModified();
        skip = !file_info.isReadable();
        duplicated = false;
        hash = "";
    }
    bool compare(file &another) {
        return size == another.size && hash == another.hash;
    }
};

class cancel_exception : public std::exception {
    const char* what () const throw () { return "Cancel"; }
};

class pause_exception : public std::exception {
private:
    size_t data[2];
public:
    pause_exception(size_t i = 0, size_t j = 0) {
        data[0] = i; data[1] = j;
    }
    size_t get(size_t i) {
        return data[i];
    }
    const char* what () const throw () { return "Pause"; }
};

class unknown_exception : public std::exception {
    const char* what () const throw () { return "Unknown error"; }
};

class scantools : public QObject {
    Q_OBJECT

signals:
    void started();
    void paused();
    void canceled();
    void finished();
    void console(QString text, bool save, QString color = "");
    void add_item(QString s1 = "", QString s2 = "", QString s3 = "", QString s4 = "", QString s5 = "", QBrush *brush = nullptr);
    void color_item(QTreeWidgetItem *item, QBrush *brush);
    void clear_items();
    void update_items();

public slots:
    void start();

private:
    /* we can use it in any part of code */
    bool mode;
    size_t result;
    QString main_directory;
    enum {SCAN_DIRS, SORT_SIZE, CALC_HASH, SORT_HASH, GROUP_DUPL, SORT_NAME, SHOW_RES, END} scanning_state;
    enum {PREPARED, SCANNING, PAUSED, CANCELED, FINISHED} main_state;

    /* we can use it only in while-switch block in start */
    size_t saved0, saved1;
    std::vector<file> files;
    std::queue<QString> dirs;
    std::vector<std::vector<file*>> duplicates;

    /* parts of scanning */
    void scan_directories(size_t);
    void sort_by_size();
    void calculate_hashes(size_t, size_t);
    void sort_by_hash(size_t);
    void group_duplicates(size_t, size_t);
    void sort_by_name(size_t);
    void show_results(size_t i0, size_t j0);

    /* service */
    void check(size_t i = 0, size_t j = 0);
    void clear();

public:
    /* standard methods */
    scantools(bool mode = false);
    ~scantools() = default;

    /* changing methods */
    void pause() { this->main_state = PAUSED; }
    void cancel() { this->main_state = CANCELED; }
    void set_mode(bool mode);
    void open_directory(QString path = QDir::currentPath());
    void select_item(QTreeWidgetItem *item, QString (*get_state)(const QTreeWidgetItem &item), void (*change_state)(QTreeWidgetItem &item, QString value));
    size_t delete_files(QTreeWidgetItemIterator it, QString (*get_state)(const QTreeWidgetItem &item), QString (*get_path)(const QTreeWidgetItem &item));

    /* describing methods */
    bool is_mode() { return mode; }
    bool is_prepared() { return main_state == PREPARED; }
    bool is_scanning() { return main_state == SCANNING; }
    bool is_paused() { return main_state == PAUSED; }
    bool is_canceled() { return main_state == CANCELED; }
    bool is_finished() { return main_state == FINISHED; }
    size_t number_for_deleting() { return (is_finished()) ? result : 0; }
};

#endif // SCANTOOLS_H
