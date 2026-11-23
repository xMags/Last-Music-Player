#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace lmp::core::account {
class HistoryImportService;
} // namespace lmp::core::account

namespace lmp::ui {

// The one-time "import listening history" question (`App.historyImport`)
// and its outcome, as a line for a toast.
//
// GUI thread only.
class HistoryImportViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.historyImport")

    Q_PROPERTY(bool offering READ offering NOTIFY offeringChanged FINAL)

public:
    // `service` outlives this object.
    explicit HistoryImportViewModel(core::account::HistoryImportService& service, QObject* parent = nullptr);

    [[nodiscard]] bool offering() const;

    Q_INVOKABLE void accept();
    Q_INVOKABLE void decline();

    // "3 compatible tracks were queued ... 2 local or unresolved entries
    // were left on this PC." as the earlier versions worded it.
    [[nodiscard]] static QString summary(int imported, int skipped);

signals:
    void offeringChanged();
    // A line to show as a toast.
    void message(const QString& text);

private:
    core::account::HistoryImportService& m_service;
};

} // namespace lmp::ui
