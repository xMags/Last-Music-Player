#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "core/library/LibraryScanner.h"

namespace lmp::core {
class SettingsStore;
class UserDataOperationGate;
} // namespace lmp::core

namespace lmp::platform {
class IExternalLinks;
class IFolderPicker;
} // namespace lmp::platform

namespace lmp::ui {

// The Library card of Settings (`App.librarySettings`), from the current app
// (MainWindow.xaml 1786-1852, MainWindow.LibraryScan.cpp): the music folder
// with Change and Open, Scan now with its progress and Cancel, and the file
// format chips that fill ScanFormats.
//
// GUI thread only.
class LibrarySettingsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.librarySettings")

    // The saved music folder; empty when none was chosen.
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged FINAL)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged FINAL)
    // While scanning, what the scan is doing; after it, how it ended.
    Q_PROPERTY(QString scanStatus READ scanStatus NOTIFY scanChanged FINAL)
    // Cancel applies while files are being read.
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY scanChanged FINAL)
    // 0 to 1 while files are read; -1 while the count is not known.
    Q_PROPERTY(qreal scanProgress READ scanProgress NOTIFY scanChanged FINAL)
    // One map per chip, in the earlier order: label, extension, checked.
    Q_PROPERTY(QVariantList formats READ formats NOTIFY formatsChanged FINAL)

public:
    struct Parts {
        core::SettingsStore& settings;
        core::LibraryScanner& scanner;
        core::UserDataOperationGate& gate;
        platform::IFolderPicker& picker;
        platform::IExternalLinks& links;
    };

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit LibrarySettingsViewModel(Parts parts, QObject* parent = nullptr);

    // The main window, for the picker to be modal to.
    void setOwnerWindow(quintptr window) { m_ownerWindow = window; }

    [[nodiscard]] QString folder() const;
    [[nodiscard]] bool scanning() const { return m_scanning; }
    [[nodiscard]] QString scanStatus() const { return m_status; }
    [[nodiscard]] bool canCancel() const { return m_scanning && !m_writing; }
    [[nodiscard]] qreal scanProgress() const { return m_progress; }
    [[nodiscard]] QVariantList formats() const;

    // Asks for a folder; a chosen one is saved and scanned at once.
    Q_INVOKABLE void changeFolder();
    Q_INVOKABLE void openFolder();
    // Scans the saved folder again.
    Q_INVOKABLE void scan();
    Q_INVOKABLE void cancelScan();
    // Turns a format chip on or off and saves ScanFormats.
    Q_INVOKABLE void toggleFormat(const QString& extension);

    // The chips' extensions, in the earlier order.
    [[nodiscard]] static QStringList formatChoices();

signals:
    void folderChanged();
    void scanChanged();
    void formatsChanged();

private:
    void startScan(const QString& folder);
    void setStatus(const QString& status);
    void onProgress(int done, int total);
    void onFinished(core::LibraryScanner::Outcome outcome, int trackCount);

    core::SettingsStore& m_settings;
    core::LibraryScanner& m_scanner;
    core::UserDataOperationGate& m_gate;
    platform::IFolderPicker& m_picker;
    platform::IExternalLinks& m_links;
    quintptr m_ownerWindow = 0;
    bool m_scanning = false;
    bool m_writing = false;
    qreal m_progress = -1;
    QString m_status;
};

} // namespace lmp::ui
