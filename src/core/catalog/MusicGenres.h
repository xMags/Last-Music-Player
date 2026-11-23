#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

// The short list of genres a song can be given, and the folding of the
// catalogue's genre names into it. The account service folds library songs
// with the same rule, so a catalogue song played before the next library
// load already shows a genre from the list.
namespace lmp::core::catalog {

// The list, in its display order.
[[nodiscard]] const QStringList& musicGenres();

// Lower case, accents dropped, "&" read as " and ", every other run of
// symbols one space, trimmed.
[[nodiscard]] QString normalizeGenreName(QStringView name);

// The genre one name stands for: the first rule whose words appear as whole
// words of the normalized name, most specific rules first. Empty for a name
// that only says where music is from ("world", "international") or that no
// rule knows.
[[nodiscard]] QString genreForName(QStringView name);

// The genre of the first of up to 16 names that stands for one; empty when
// none does.
[[nodiscard]] QString canonicalGenre(const QStringList& names);

} // namespace lmp::core::catalog
