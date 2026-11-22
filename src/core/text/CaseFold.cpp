#include "core/text/CaseFold.h"

#include <QChar>

namespace lmp::core {

QString foldCaseAscii(QStringView text)
{
    QString folded;
    folded.reserve(text.size());
    for (const QChar character : text) {
        const char16_t unit = character.unicode();
        folded.append(unit >= u'A' && unit <= u'Z' ? QChar(static_cast<char16_t>(unit - u'A' + u'a')) : character);
    }
    return folded;
}

} // namespace lmp::core
