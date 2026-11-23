#pragma once

#include <QString>

// The normalised form of a collection link, the one the account service files
// a saved or imported playlist under: a playlist is the saved copy of an
// album or playlist when its link has the same key. Share links rarely match
// byte for byte, so the key has a lower-case host, no tracking parameters,
// the other parameters sorted, and no fragment, scheme or trailing slash.
namespace lmp::core::catalog {

// Empty for anything but an http(s) address.
[[nodiscard]] QString canonicalSourceKey(const QString& raw);

} // namespace lmp::core::catalog
