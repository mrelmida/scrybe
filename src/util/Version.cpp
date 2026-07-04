#include "Version.h"

#include <QRegularExpression>

namespace scrybe {

QVector<int> parseVersion(QString v) {
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
        v = v.mid(1);
    QVector<int> parts;
    const auto segs = v.split(QLatin1Char('.'));
    for (const QString &s : segs) {
        static const QRegularExpression re(QStringLiteral("^(\\d+)"));
        const auto m = re.match(s);
        parts.append(m.hasMatch() ? m.captured(1).toInt() : 0);
    }
    return parts;
}

bool isNewerVersion(const QString &latest, const QString &current) {
    const QVector<int> a = parseVersion(latest);
    const QVector<int> b = parseVersion(current);
    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const int ai = i < a.size() ? a[i] : 0;
        const int bi = i < b.size() ? b[i] : 0;
        if (ai != bi)
            return ai > bi;
    }
    return false;
}

} // namespace scrybe
