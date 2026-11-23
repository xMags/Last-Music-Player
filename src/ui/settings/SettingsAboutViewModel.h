#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace lmp::core::db {
class LibraryQueries;
}

namespace lmp::platform {
class IExternalLinks;
}

namespace lmp::ui {

// The About card of Settings (`App.settingsAbout`): the app's name and
// version, a line counting the library, and the button that opens the data
// folder.
//
// GUI thread only; the library count is read there, where the database lives.
class SettingsAboutViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.settingsAbout")

    // "Last Music Player 1.0.0".
    Q_PROPERTY(QString title READ title CONSTANT FINAL)
    // "12 tracks · 3 albums · 2 artists", or empty without a database.
    Q_PROPERTY(QString libraryLine READ libraryLine NOTIFY libraryLineChanged FINAL)

public:
    struct Parts {
        // Null when the database did not open.
        core::db::LibraryQueries* library = nullptr;
        platform::IExternalLinks& links;
        // The data folder (AppPaths::root()).
        QString dataRoot;
    };

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit SettingsAboutViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] QString title() const;
    [[nodiscard]] QString libraryLine() const { return m_libraryLine; }

    // Counts the library again; Settings calls it each time it opens.
    Q_INVOKABLE void refresh();
    // Creates the data folder if needed and opens it in the file manager.
    // False when the system could not open it.
    Q_INVOKABLE bool openDataFolder();

signals:
    void libraryLineChanged();

private:
    core::db::LibraryQueries* m_library;
    platform::IExternalLinks& m_links;
    QString m_dataRoot;
    QString m_libraryLine;
};

} // namespace lmp::ui
