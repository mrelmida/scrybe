#pragma once

#include <QString>

namespace scrybe {

// Strip a wrapping pair of quotes ("…" or '…') an LLM sometimes adds.
QString unquote(QString s);

// Custom styles must be a single QSettings key and must not shadow built-ins.
bool validPresetName(const QString &name);

// Quote one literal shell argument, including apostrophes and substitutions.
QString shellQuote(QString s);

} // namespace scrybe
