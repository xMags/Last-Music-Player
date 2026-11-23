#include "ui/settings/LibrarySettingsViewModel.h"

#include <QDir>
#include <QUrl>
#include <QVariantMap>

#include <optional>

#include "core/library/UserDataOperationGate.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/IExternalLinks.h"
#include "platform/IFolderPicker.h"

namespace lmp::ui {

namespace keys = core::settings::keys;

namespace {

const QString kDefaultScanFormats = QStringLiteral(".mp3,.flac");

} // namespace

LibrarySettingsViewModel::LibrarySettingsViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_settings(parts.settings)
    , m_scanner(parts.scanner)
    , m_gate(parts.gate)
    , m_picker(parts.picker)
    , m_links(parts.links)
{
    connect(&m_scanner, &core::LibraryScanner::progress, this, &LibrarySettingsViewModel::onProgress);
    connect(&m_scanner, &core::LibraryScanner::writing, this, [this]() {
        m_writing = true;
        m_progress = 1;
        setStatus(tr("Updating library database..."));
    });
    connect(&m_scanner, &core::LibraryScanner::finished, this, &LibrarySettingsViewModel::onFinished);
    connect(&m_settings, &core::SettingsStore::changed, this, [this](const QString& key) {
        if (key.isEmpty() || key == keys::MusicLibraryPath) {
            emit folderChanged();
        }
        if (key.isEmpty() || key == keys::ScanFormats) {
            emit formatsChanged();
        }
    });
}

QStringList LibrarySettingsViewModel::formatChoices()
{
    return {QStringLiteral(".mp3"), QStringLiteral(".flac"), QStringLiteral(".wav"), QStringLiteral(".m4a"),
            QStringLiteral(".aac"), QStringLiteral(".ogg"), QStringLiteral(".opus"), QStringLiteral(".wma")};
}

QString LibrarySettingsViewModel::folder() const
{
    return m_settings.string(keys::MusicLibraryPath);
}

QVariantList LibrarySettingsViewModel::formats() const
{
    const QStringList chosen =
        core::LibraryScanner::scanExtensions(m_settings.string(keys::ScanFormats, kDefaultScanFormats));
    QVariantList chips;
    for (const QString& extension : formatChoices()) {
        chips.append(QVariantMap{
            {QStringLiteral("label"), extension.mid(1).toUpper()},
            {QStringLiteral("extension"), extension},
            {QStringLiteral("checked"), chosen.contains(extension)},
        });
    }
    return chips;
}

void LibrarySettingsViewModel::changeFolder()
{
    if (m_scanning) {
        return;
    }
    const std::optional<QString> chosen = m_picker.pickFolder(m_ownerWindow, folder());
    if (!chosen || chosen->isEmpty()) {
        return;
    }
    {
        // Saved under the gate, as earlier versions did.
        const std::optional<core::UserDataOperationGate::Lease> lease = m_gate.tryEnter();
        if (!lease) {
            return;
        }
        m_settings.setString(keys::MusicLibraryPath, QDir::toNativeSeparators(*chosen));
    }
    startScan(folder());
}

void LibrarySettingsViewModel::openFolder()
{
    const QString path = folder();
    if (path.isEmpty() || !QDir(path).exists()) {
        return;
    }
    [[maybe_unused]] const bool opened = m_links.open(QUrl::fromLocalFile(path));
}

void LibrarySettingsViewModel::scan()
{
    if (!m_scanning && !folder().isEmpty()) {
        startScan(folder());
    }
}

void LibrarySettingsViewModel::cancelScan()
{
    if (!canCancel()) {
        return;
    }
    m_scanner.cancel();
    setStatus(tr("Cancelling scan..."));
}

void LibrarySettingsViewModel::toggleFormat(const QString& extension)
{
    QStringList chosen =
        core::LibraryScanner::scanExtensions(m_settings.string(keys::ScanFormats, kDefaultScanFormats));
    if (chosen.contains(extension)) {
        chosen.removeAll(extension);
    } else if (formatChoices().contains(extension)) {
        chosen.append(extension);
    }
    // Saved in chip order, as earlier versions wrote it.
    QStringList ordered;
    for (const QString& choice : formatChoices()) {
        if (chosen.contains(choice)) {
            ordered.append(choice);
        }
    }
    m_settings.setString(keys::ScanFormats, ordered.join(QLatin1Char(',')));
}

void LibrarySettingsViewModel::startScan(const QString& path)
{
    if (m_scanner.isRunning()) {
        setStatus(tr("A library scan is already running."));
        return;
    }
    if (!m_scanner.start(path)) {
        setStatus(QDir(path).exists() ? tr("The scan could not start.") : tr("The music folder was not found."));
        return;
    }
    m_scanning = true;
    m_writing = false;
    m_progress = -1;
    setStatus(tr("Scanning your music folder..."));
}

void LibrarySettingsViewModel::setStatus(const QString& status)
{
    m_status = status;
    emit scanChanged();
}

void LibrarySettingsViewModel::onProgress(int done, int total)
{
    if (!m_scanning) {
        return;
    }
    m_progress = total > 0 ? static_cast<qreal>(done) / total : 1;
    setStatus(tr("Scanning %1 / %2 tracks...").arg(done).arg(total));
}

void LibrarySettingsViewModel::onFinished(core::LibraryScanner::Outcome outcome, int trackCount)
{
    if (!m_scanning) {
        return;
    }
    m_scanning = false;
    m_writing = false;
    m_progress = -1;
    switch (outcome) {
    case core::LibraryScanner::Outcome::Completed:
        setStatus(trackCount == 1 ? tr("1 track in your library") : tr("%1 tracks in your library").arg(trackCount));
        return;
    case core::LibraryScanner::Outcome::Cancelled:
        setStatus(tr("Scan cancelled"));
        return;
    case core::LibraryScanner::Outcome::Failed:
        break;
    }
    setStatus(tr("The scan could not be completed."));
}

} // namespace lmp::ui
