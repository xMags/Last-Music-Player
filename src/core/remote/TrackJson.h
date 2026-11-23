#pragma once

#include "core/model/Track.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QList>
#include <QString>

#include <optional>

// Songs as JSON in both directions: songs the API key server returns, and
// the request bodies of the account service's song and playlist calls. The
// keys and shapes are those earlier versions used.
namespace lmp::core::remote::track_json {

// The source tag a provider gives a failed result; such results are dropped.
inline constexpr QLatin1StringView ProviderErrorTag{"provider-error"};

// One song from a provider answer. Nothing when it has neither a stream nor a
// source address, when its stream address is not safe to play, when its
// source address is not safe to keep, or when its "provider" is the error
// tag. Missing (or mistyped) text takes the earlier defaults: title
// "Provider track", artist "Provider", album the provider name, origin
// "provider". The song is remote, labelled "Music API", genre "Remote", and
// its stream address is its file path; the length comes from "durationMs".
[[nodiscard]] std::optional<Track> trackFromProviderJson(const QJsonObject& item);

// The songs in the "results" array of a provider answer, at most `limit`,
// skipping those trackFromProviderJson drops. Empty when the body is not a
// JSON object with a "results" array.
[[nodiscard]] QList<Track> parseProviderTracks(const QByteArray& payload, qsizetype limit);

// Every song in `results` that trackFromProviderJson keeps, numbered from 1
// in Track::index.
[[nodiscard]] QList<Track> parseProviderTrackArray(const QJsonArray& results);

// A remote song the account service can store: kind "remote", a source
// address that is safe to keep and, when `requireRemoteId`, a server id.
[[nodiscard]] bool isCompatibleAccountRemoteTrack(const Track& track, bool requireRemoteId = true);

// The account service's track object for `track` (see
// account::serializePortableTrack), or empty when the song is not compatible
// or the result would not be valid. The "provider" field is the source tag,
// else the origin unless that is "account".
[[nodiscard]] QByteArray accountTrackJson(const Track& track);

// {"name": <trimmed name>}, or empty when the trimmed name is empty.
[[nodiscard]] QByteArray accountPlaylistJson(const QString& name);

// {"sourceUrl": <trimmed address>}, or empty unless the trimmed text starts
// with "http://" or "https://".
[[nodiscard]] QByteArray accountPlaylistImportJson(const QString& sourceUrl);

// The playlist replacement body (account::serializePlaylistReplacement) for
// the trimmed name and every compatible song, in order.
[[nodiscard]] QByteArray accountPlaylistUpdateJson(const QString& name, const QList<Track>& tracks);

} // namespace lmp::core::remote::track_json
