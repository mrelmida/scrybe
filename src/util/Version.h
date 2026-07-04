#pragma once

#include <QString>
#include <QVector>

namespace scrybe {

// Parse "1.4.10" → {1,4,10}; ignores a leading 'v' and any trailing suffix
// per segment ("2-rc1" → 2).
QVector<int> parseVersion(QString v);

// Returns true if `latest` is strictly newer than `current`.
bool isNewerVersion(const QString &latest, const QString &current);

} // namespace scrybe
