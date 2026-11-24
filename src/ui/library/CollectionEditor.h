#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace lmp::core {
class CollectionActions;
}

namespace lmp::core {
class LinkImportService;
}

namespace lmp::ui {

class Navigator;
class ToastQueue;

// The prompts and confirmations of the user's collections (`App.collections`):
// New playlist (from Library, the sidebar's "+"), New album, Rename playlist
// and Delete playlist or album, wherever they are asked for (a card's menu,
// a sidebar row's menu, a collection page's menu). A request fills the
// prompt or confirmation and asks QML to show it (one dialog of each kind
// lives in the shell); the answer performs the change, says so in a toast,
// opens a new playlist's page, and leaves a deleted collection's page.
//
// GUI thread only.
class CollectionEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.collections")

    Q_PROPERTY(QString promptTitle READ promptTitle NOTIFY promptChanged FINAL)
    Q_PROPERTY(QString promptLabel READ promptLabel NOTIFY promptChanged FINAL)
    Q_PROPERTY(QString promptPlaceholder READ promptPlaceholder NOTIFY promptChanged FINAL)
    Q_PROPERTY(QString promptSubmit READ promptSubmit NOTIFY promptChanged FINAL)
    Q_PROPERTY(QString promptValue READ promptValue NOTIFY promptChanged FINAL)
    // The sentence under the title, and the button's words while it works.
    Q_PROPERTY(QString promptDescription READ promptDescription NOTIFY promptChanged FINAL)
    Q_PROPERTY(QString promptBusyLabel READ promptBusyLabel NOTIFY promptChanged FINAL)
    // An import on its way: the prompt stays open until promptSettled.
    Q_PROPERTY(bool promptBusy READ promptBusy NOTIFY promptChanged FINAL)
    // Why the last answer was refused ("" for the usual words).
    Q_PROPERTY(QString promptError READ promptError NOTIFY promptChanged FINAL)
    Q_PROPERTY(QString confirmTitle READ confirmTitle NOTIFY confirmChanged FINAL)
    Q_PROPERTY(QString confirmText READ confirmText NOTIFY confirmChanged FINAL)
    Q_PROPERTY(QString confirmLabel READ confirmLabel NOTIFY confirmChanged FINAL)
    Q_PROPERTY(int maximumNameLength READ maximumNameLength CONSTANT FINAL)
    // The longest answer the open prompt takes (a name, or a link).
    Q_PROPERTY(int promptMaximumLength READ promptMaximumLength NOTIFY promptChanged FINAL)

public:
    // All three outlive this object.
    CollectionEditor(core::CollectionActions& actions, Navigator& navigator, ToastQueue& toasts,
                     QObject* parent = nullptr);

    [[nodiscard]] QString promptTitle() const { return m_promptTitle; }
    [[nodiscard]] QString promptLabel() const { return m_promptLabel; }
    [[nodiscard]] QString promptPlaceholder() const { return m_promptPlaceholder; }
    [[nodiscard]] QString promptSubmit() const { return m_promptSubmit; }
    [[nodiscard]] QString promptValue() const { return m_promptValue; }
    [[nodiscard]] QString promptDescription() const { return m_promptDescription; }
    [[nodiscard]] QString promptBusyLabel() const { return m_promptBusyLabel; }
    [[nodiscard]] bool promptBusy() const { return m_promptBusy; }
    [[nodiscard]] QString promptError() const { return m_promptError; }
    [[nodiscard]] QString confirmTitle() const { return m_confirmTitle; }
    [[nodiscard]] QString confirmText() const { return m_confirmText; }
    [[nodiscard]] QString confirmLabel() const { return m_confirmLabel; }
    [[nodiscard]] static int maximumNameLength();
    [[nodiscard]] int promptMaximumLength() const;
    static constexpr int MaximumLinkLength = 2048;

    // Importing an album or playlist from a link (API key mode); without an
    // importer nothing is offered.
    void setLinkImporter(core::LinkImportService* importer) { m_importer = importer; }
    // `kind` changes only the prompt's words: "link" (an album or playlist,
    // Account mode), "album" or "playlist" (API key mode).
    Q_INVOKABLE void requestImportLink(const QString& kind);

    Q_INVOKABLE void requestCreatePlaylist();
    Q_INVOKABLE void requestCreateAlbum();
    Q_INVOKABLE void requestRename(const QString& playlistKey, const QString& title);
    // `kind` is "playlist" or "album-collection".
    Q_INVOKABLE void requestDelete(const QString& kind, const QString& key, const QString& title);

    // The prompt's answer; true when the change was made (the dialog closes).
    // An import returns false with promptBusy set and settles later.
    Q_INVOKABLE bool submitPrompt(const QString& value);
    Q_INVOKABLE void confirm();

signals:
    void promptChanged();
    void confirmChanged();
    // Show the name prompt, or the delete confirmation.
    void promptRequested();
    // An import finished: empty on success (the dialog closes), else why not.
    void promptSettled(const QString& error);
    void confirmRequested();

private:
    enum class Pending { None, CreatePlaylist, CreateAlbum, Rename, Delete, ImportLink };

    void leaveDeleted(const QString& kind, const QString& key);

    core::CollectionActions& m_actions;
    Navigator& m_navigator;
    ToastQueue& m_toasts;
    core::LinkImportService* m_importer = nullptr;

    Pending m_pending = Pending::None;
    QString m_kind;
    QString m_key;
    QString m_title;
    QString m_promptTitle;
    QString m_promptLabel;
    QString m_promptPlaceholder;
    QString m_promptSubmit;
    QString m_promptValue;
    QString m_promptDescription;
    QString m_promptBusyLabel;
    QString m_promptError;
    bool m_promptBusy = false;
    QString m_confirmTitle;
    QString m_confirmText;
    QString m_confirmLabel;
};

} // namespace lmp::ui
