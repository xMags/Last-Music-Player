#include "ui/downloads/OfflineArtistPhotos.h"

#include "core/catalog/CatalogService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <utility>
#include <vector>

namespace lmp::ui {

namespace {

constexpr int kSaveDelayMs = 2000;

} // namespace

OfflineArtistPhotos::OfflineArtistPhotos(core::catalog::CatalogService& catalog, QString file, QObject* parent)
    : QObject(parent)
    , m_catalog(catalog)
    , m_file(std::move(file))
{
    m_saveDelay.setSingleShot(true);
    m_saveDelay.setInterval(kSaveDelayMs);
    connect(&m_saveDelay, &QTimer::timeout, this, &OfflineArtistPhotos::save);
    load();
}

OfflineArtistPhotos::~OfflineArtistPhotos()
{
    if (m_saveDelay.isActive()) {
        save();
    }
}

QString OfflineArtistPhotos::keyFor(const QString& name)
{
    return name.trimmed().toLower();
}

QString OfflineArtistPhotos::known(const QString& name) const
{
    return m_photos.value(keyFor(name)).url;
}

void OfflineArtistPhotos::lookUp(const QStringList& names)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QString& name : names) {
        const QString key = keyFor(name);
        if (key.isEmpty() || m_waiting.contains(key)) {
            continue;
        }
        if (const auto remembered = m_photos.constFind(key); remembered != m_photos.constEnd()) {
            if (!remembered->url.isEmpty() || remembered->at.secsTo(now) < MissRetrySeconds) {
                continue;
            }
        }
        m_waiting.insert(key);
        m_queue.push_back(name.trimmed());
    }
    pump();
}

void OfflineArtistPhotos::pump()
{
    while (m_running < LookupsAtOnce && !m_queue.empty()) {
        const QString name = m_queue.front();
        m_queue.pop_front();
        ++m_running;
        m_catalog.resolveArtist(name).then(
            this, [this, name](const core::Result<std::optional<core::catalog::CatalogItem>>& answer) {
                --m_running;
                const QString key = keyFor(name);
                m_waiting.remove(key);
                // A failed lookup is asked again next time; an answer is kept.
                if (answer) {
                    const QString url = answer.value() ? answer.value()->artworkUrl : QString();
                    m_photos.insert(key, {.url = url, .at = QDateTime::currentDateTimeUtc()});
                    m_saveDelay.start();
                    if (!url.isEmpty()) {
                        emit photoFound(name, url);
                    }
                }
                pump();
            });
    }
}

void OfflineArtistPhotos::load()
{
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly)) {
        return; // none yet
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return; // unreadable: looked up again
    }
    const QJsonObject photos = document.object();
    for (auto it = photos.constBegin(); it != photos.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const QString url = entry.value(QLatin1StringView("url")).toString();
        const qint64 at = entry.value(QLatin1StringView("at")).toInteger(-1);
        if (at < 0) {
            continue;
        }
        m_photos.insert(keyFor(it.key()), {.url = url, .at = QDateTime::fromMSecsSinceEpoch(at, QTimeZone::UTC)});
    }
}

void OfflineArtistPhotos::save()
{
    // The newest first out past the bound.
    std::vector<std::pair<QString, Remembered>> entries;
    entries.reserve(static_cast<std::size_t>(m_photos.size()));
    for (auto it = m_photos.cbegin(); it != m_photos.cend(); ++it) {
        entries.emplace_back(it.key(), it.value());
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.second.at > b.second.at; });
    if (entries.size() > static_cast<std::size_t>(MaxRemembered)) {
        entries.resize(static_cast<std::size_t>(MaxRemembered));
    }
    QJsonObject photos;
    for (const auto& [key, remembered] : entries) {
        photos.insert(key, QJsonObject{{QStringLiteral("url"), remembered.url},
                                       {QStringLiteral("at"), remembered.at.toMSecsSinceEpoch()}});
    }
    if (!QDir().mkpath(QFileInfo(m_file).absolutePath())) {
        return; // looked up again next time
    }
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(photos).toJson(QJsonDocument::Compact));
    (void)file.commit();
}

} // namespace lmp::ui
