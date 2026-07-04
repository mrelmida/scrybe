#include "Text.h"

namespace scrybe {

QString unquote(QString s) {
    s = s.trimmed();
    if (s.size() >= 2 &&
        ((s.front() == QLatin1Char('"') && s.back() == QLatin1Char('"')) ||
         (s.front() == QLatin1Char('\'') && s.back() == QLatin1Char('\''))))
        s = s.mid(1, s.size() - 2).trimmed();
    return s;
}

} // namespace scrybe
