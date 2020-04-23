#include "scantools.h"

#include <QDir>
#include <QDebug>
#include <algorithm>
#include <QCryptographicHash>
#include <QFileInfoList>
#include <QDateTime>
#include <QThread>

const size_t BLOCK_SIZE = 1024 * 1024;
const QString FORMAT = "d MMMM yyyy, hh:mm:ss";

scantools::scantools(bool mode) : mode(mode) {
    saved0 = saved1 = result = 0;
    main_state = PREPARED;
    QDir::setCurrent(QDir::homePath());
    scanning_state = SCAN_DIRS;
}

void scantools::start() {
    try {
        if (is_canceled()) throw cancel_exception();
        main_state = SCANNING;
        emit started();
        while (scanning_state != END) {
            switch (scanning_state) {
                case SCAN_DIRS: scan_directories(saved0); break;
                case SORT_SIZE: sort_by_size(); break;
                case CALC_HASH: calculate_hashes(saved0, saved1); break;
                case SORT_HASH: sort_by_hash(saved0); break;
                case GROUP_DUPL: group_duplicates(saved0, saved1); break;
                case SORT_NAME: sort_by_name(saved0); break;
                case SHOW_RES: show_results(saved0, saved1); break;
                default: scanning_state = END;
            }
            saved0 = saved1 = 0;
        }
        result = 0;
        for (size_t i = 0; i < duplicates.size(); i++) {
            result += duplicates[i].size() - 1;
        }
        emit console("FINISHED", true, "green");
        main_state = FINISHED;
        clear();
        emit finished();
    } catch (pause_exception &e) {
        saved0 = e.get(0);
        saved1 = e.get(1);
        emit console("PAUSED", false, "yellow");
        emit paused();
    } catch (cancel_exception &e) {
        clear();
        emit console("CANCELED", false, "orange");
        emit canceled();
    }
}

void scantools::clear() {
    scanning_state = SCAN_DIRS;
    files.clear();
    while (!dirs.empty()) dirs.pop();
    duplicates.clear();
    emit console("", true);
}
void scantools::check(size_t i, size_t j) {
    if (QThread::currentThread()->isInterruptionRequested() || is_canceled()) throw cancel_exception();
    if (is_paused()) throw pause_exception(i, j);
}

void scantools::scan_directories(size_t i0) {
    emit console("scanning directories..", true);
    emit console(".", true);
    if (dirs.empty()) dirs.push(main_directory);
    while (!dirs.empty()) {
        emit console(QString("scanning ").append(dirs.front()).append(" (0%)"), false);
        check();
        QDir d(dirs.front());
        QFileInfoList list = d.entryInfoList(QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Dirs | QDir::Files);
        for (size_t i = i0; i < list.size(); i++) {
            emit console(QString("scanning ").append(dirs.front()).append(" (%1%)").arg((i + 1) * 100 / list.size()), false);
            check(i);
            if (list[i].isDir()) dirs.push(list[i].filePath());
            if (list[i].isFile()) files.emplace_back(list[i]);
        }
        dirs.pop();
    }
    scanning_state = SORT_SIZE;
}
void scantools::sort_by_size() {
    emit console("sorting files by size..", false);
    std::sort(files.begin(), files.end(), [&](const file &f1, const file &f2) {
        check();
        return f1.size < f2.size;
    });
    scanning_state = CALC_HASH;
}
void scantools::calculate_hashes(size_t i0, size_t j0) {
    size_t count = j0;
    emit console("calculating hashes (0) ..", true);
    for (size_t i = i0; i < files.size(); i++) {
        check(i, count);
        if (i > 0 && files[i - 1].size == files[i].size) {
            files[i].first = files[i - 1].first;
        } else {
            files[i].first = i;
        }
        if (files[i].skip) continue;
        if ((i > 0 && files[i - 1].size == files[i].size) || (i + 1 < files.size() && files[i].size == files[i + 1].size)) {
            count++;
            emit console(QString("calculating hashes (%1) ..").arg(count), false);
            QCryptographicHash hash(QCryptographicHash::Md5);
            QFile f(files[i].path);
            if (f.exists() && f.open(QFile::ReadOnly)) {
                hash.addData(&f);
                f.close();
                files[i].hash = hash.result().toHex();
            } else {
                files[i].skip = true;
            }
        }
    }
    scanning_state = SORT_HASH;
}
void scantools::sort_by_hash(size_t i0) {
    emit console("sorting files by hash (0%) ..", true);
    for (size_t i = i0; i < files.size(); i++) {
        emit console(QString("sorting files by hash (%1%) ..").arg((i + 1) * 100 / files.size()), false);
        check(i);
        if (i != files[i].first && (i + 1 == files.size() || files[i].size != files[i + 1].size)) {
            std::sort(files.begin() + files[i].first, files.begin() + i, [&](const file f1, const file f2) {
                check(i);
                return f1.hash < f2.hash;
            });
        }
    }
    emit console("sorting files by hash..", false);
    scanning_state = GROUP_DUPL;
}
void scantools::group_duplicates(size_t i0, size_t j0) {
    emit console("grouping files (0%) ..", true);
    for (size_t i = i0; i < files.size(); i++) {
        if (files[i].skip) qDebug() << QString("skip ").append(files[i].path);
        emit console(QString("grouping files (%1%) ..").arg((i + 1) * 100 / files.size()), false);
        if (files[i].skip || files[i].hash == "" || files[i].duplicated) continue;
        for (size_t j = std::max(j0, i + 1); j < files.size() && files[i].compare(files[j]); j++) {
            check(i, j);
            if (files[j].skip || files[j].duplicated) continue;
            bool eq = true;
            if (mode) {
                QFile f1(files[i].path), f2(files[j].path);
                std::vector<char> buffer1, buffer2;
                if (!f1.exists() || !f1.open(QFile::ReadOnly)) files[i].skip = true;
                if (!f2.exists() || !f2.open(QFile::ReadOnly)) files[j].skip = true;
                if (files[i].skip || files[j].skip) continue;
                while (!f1.atEnd()) {
                    if (f1.read(&buffer1[0], BLOCK_SIZE) != BLOCK_SIZE) files[i].skip = true;
                    if (f2.read(&buffer2[0], BLOCK_SIZE) != BLOCK_SIZE) files[i].skip = true;
                    for (size_t k = 0; k < buffer1.size(); k++) {
                        check(i, j);
                        if (buffer1.at(k) != buffer2.at(k)) {
                            eq = false;
                            break;
                        }
                    }
                    if (!eq) break;
                }
            }
            if (eq) {
                if (!files[i].duplicated) {
                    files[i].duplicated = true;
                    duplicates.push_back(std::vector<file*>());
                    duplicates.back().push_back(&files[i]);
                }
                files[j].duplicated = true;
                duplicates.back().push_back(&files[j]);
            }
            j0 = 0; // it destroys any relations with saving state for next steps
        }
    }
    emit console("grouping files..", false);
    scanning_state = SORT_NAME;
}
void scantools::sort_by_name(size_t i0) {
    emit console("sorting files by name..", true);
    for (size_t i = i0; i < duplicates.size(); i++) {
        emit console(QString("sorting files by name (%1%) ..").arg((i + 1) * 100 / duplicates.size()), false);
        check(i);
        std::sort(duplicates[i].begin(), duplicates[i].end(), [&](const file *f1, const file *f2) {
            check(i);
            return f1->name < f2->name;
        });
    }
    emit console("sorting files by name..", false);
    scanning_state = SHOW_RES;
}
void scantools::show_results(size_t i0, size_t j0) {
    emit console("showing results..", true);
    for (size_t i = i0; i < duplicates.size(); i++) {
        emit console(QString("showing results (%1%) ..").arg((i + 1) * 100 / duplicates.size()), false);
        for (size_t j = j0; j < duplicates[i].size(); j++) {
            check(i, j);
            auto &f = duplicates[i][j];
            emit add_item(f->name, (j > 0) ? "DELETE" : "OK", QString::number(f->size), f->path, f->date.toString(FORMAT), (j > 0) ? &red_brush : nullptr);
            j0 = 0; // it destroys any relations with saving state for next steps
        }
        emit add_item();
    }
    emit update_items();
    emit console("showing results..", false);
    scanning_state = END;
}

void scantools::set_mode(bool mode) {
    this->mode = mode;
    emit console(QString("collision mode ").append((mode) ? "on" : "off"), true, "purple");
}

void scantools::open_directory(QString path) {
    QFileInfo file_info(path);
    if (!file_info.isDir()) {
        emit console(path.append(" is not a directory"), true, "blue");
    } else if (!file_info.isReadable()) {
        emit console(QString("cannot open ").append(path), true, "blue");
    } else {
        QDir::setCurrent(path);
        emit clear_items();
        QFileInfoList list = QDir::current().entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
        std::stable_sort(list.begin(), list.end(), [](const QFileInfo &f1, const QFileInfo &f2) {
            return (f1.isDir() && f2.isFile()) || ((f1.isDir() || f1.isFile()) && (!f2.isDir() && !f2.isFile()));
        });
        if (!QDir::current().isRoot()) {
            emit add_item("..");
        }
        for (QFileInfo f: list) {
            emit add_item(f.fileName(), (f.isDir()) ? "directory" : (f.isFile()) ? "file" : "", (f.isFile()) ? QString::number(f.size()) : "", f.filePath(), f.lastModified().toString(FORMAT));
        }
        emit update_items();
        emit console(QString("open ").append(QDir::currentPath()), true, "blue");
    }
}

void scantools::select_item(QTreeWidgetItem *item, QString (*get_state)(const QTreeWidgetItem &item), void (*change_state)(QTreeWidgetItem &item, QString value)) {
    if (get_state(*item) == "OK") {
        change_state(*item, "DELETE");
        emit color_item(item, &red_brush);
        result++;
    } else if (get_state(*item) == "DELETE") {
        change_state(*item, "OK");
        emit color_item(item, &black_brush);
        result--;
    } else return;
}

size_t scantools::delete_files(QTreeWidgetItemIterator it, QString (*get_state)(const QTreeWidgetItem &item), QString (*get_path)(const QTreeWidgetItem &item)) {
    size_t count = 0;
    emit console("deleting files (0%) ..", true, "red");
    while (*it) {
        if (get_state(*(*it)) == "DELETE") {
            emit console(QString("deleting files (%1%) ..").arg(count * 100 / result), false, "red");
            QFile file(get_path(*(*it)));
            if (!file.exists()) continue;
            if (file.remove()) {
                count++;
            } else {
                emit console(QString("cannot delete ").append(get_path(*(*it))), false, "orange");
            }
        }
        ++it;
    }
    return count;
}
