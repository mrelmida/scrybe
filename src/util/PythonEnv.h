#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

namespace scrybe {

inline QString pythonExecutable() {
    const QString venvPython =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath(QStringLiteral("scrybe/venv/bin/python"));
    if (QFileInfo::exists(venvPython))
        return venvPython;

    const QString systemPython = QStandardPaths::findExecutable(QStringLiteral("python3"));
    return systemPython.isEmpty() ? QStringLiteral("python3") : systemPython;
}

} // namespace scrybe
