#include "Text.h"

namespace scrybe {

bool validPresetName(const QString &name) {
    const QString n = name.trimmed();
    if (n.isEmpty() || n == QLatin1String("format") ||
        n == QLatin1String("markdown") || n == QLatin1String("summary"))
        return false;
    for (const QChar c : n) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || !c.isPrint())
            return false;
    }
    return true;
}

QString unquote(QString s) {
    s = s.trimmed();
    if (s.size() >= 2 &&
        ((s.front() == QLatin1Char('"') && s.back() == QLatin1Char('"')) ||
         (s.front() == QLatin1Char('\'') && s.back() == QLatin1Char('\''))))
        s = s.mid(1, s.size() - 2).trimmed();
    return s;
}

} // namespace scrybe
