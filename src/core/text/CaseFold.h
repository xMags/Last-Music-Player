#pragma once

#include <QString>
#include <QStringView>

namespace lmp::core {

// Lower-cases A to Z only and leaves every other unit as it is, so the result
// always has the input's length. Earlier versions folded text with towlower
// in the C locale and with SQLite's lower(), both of which change only ASCII
// letters; keys they stored and the matches they made depend on that, so the
// ported rules fold the same way.
[[nodiscard]] QString foldCaseAscii(QStringView text);

} // namespace lmp::core
