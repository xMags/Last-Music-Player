#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <chrono>

namespace lmp::ui {

// The short messages shown above the player bar, newest first (row 0). At
// most three are visible; showing a fourth drops the oldest. Each dismisses
// itself after `lifetime`, or earlier when its action is triggered or it is
// dismissed by id. Lives on the GUI thread; its timers are its children and
// stop with it.
class ToastQueue : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        ToastIdRole = Qt::UserRole + 1,
        TextRole,
        ErrorRole,
        ActionLabelRole,
    };
    Q_ENUM(Role)

    static constexpr int kMaxVisible = 3;
    static constexpr std::chrono::milliseconds kDefaultLifetime{4000};

    explicit ToastQueue(QObject* parent = nullptr);
    ToastQueue(std::chrono::milliseconds lifetime, QObject* parent);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return static_cast<int>(m_toasts.size()); }

    // Shows a toast and returns its id. `actionId` is reported by
    // actionTriggered when the user presses the action button.
    Q_INVOKABLE int show(const QString& text, bool error = false,
                         const QString& actionLabel = {}, const QString& actionId = {});
    Q_INVOKABLE void dismiss(int toastId);
    // The user pressed the toast's action: reports it, then dismisses the toast.
    Q_INVOKABLE void triggerAction(int toastId);

signals:
    void countChanged();
    void actionTriggered(const QString& actionId);

private:
    struct Toast {
        int id = 0;
        QString text;
        bool error = false;
        QString actionLabel;
        QString actionId;
        QTimer* timer = nullptr; // child of the queue
    };

    [[nodiscard]] int rowOf(int toastId) const;
    void removeRow(int row);

    std::chrono::milliseconds m_lifetime;
    QList<Toast> m_toasts;
    int m_nextId = 1;
};

} // namespace lmp::ui
