#pragma once

#include <QDirIterator>
#include <QList>
#include <memory>

struct UploadFolderEntry {
    QString relativePath;
    bool directory;
};
using UploadFolderPlan = QList<UploadFolderEntry>;

inline QDirIterator uploadFolderEntries(const QString& path, bool sftp) {
    if (sftp) {
        return QDirIterator(path, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                            QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    }
    return QDirIterator(path, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
}
