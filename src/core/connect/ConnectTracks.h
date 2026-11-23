#pragma once

#include "core/model/Track.h"

#include <QJsonObject>

// Songs as the device link carries them: the account service's song object.
namespace lmp::core::connect {

// The object another device can play: the account's song object for a song
// the account can store; for another remote song with a safe address, the
// same fields with an id made from it; for a file on this PC only what
// identifies it to a person (title, artist, album, length) with
// "unavailable" set, since a path never leaves this PC.
[[nodiscard]] QJsonObject connectTrackJson(const Track& track);

// A song from another device. One the account can play becomes an account
// song; anything else is shown only (unavailable), with the title, artist,
// album, artwork and length it carries.
[[nodiscard]] Track trackFromConnect(const QJsonObject& object);

// The "id" of a song object, or empty.
[[nodiscard]] QString connectTrackId(const QJsonObject& object);

} // namespace lmp::core::connect
