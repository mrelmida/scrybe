#pragma once

#include <QString>

namespace scrybe {

// Strip a wrapping pair of quotes ("…" or '…') an LLM sometimes adds.
QString unquote(QString s);

} // namespace scrybe
