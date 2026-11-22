#include "ui/toasts/ToastQueue.h"

namespace lmp::ui {

ToastQueue::ToastQueue(QObject* parent)
    : ToastQueue(kDefaultLifetime, parent)
{
}

ToastQueue::ToastQueue(std::chrono::milliseconds lifetime, QObject* parent)
    : QAbstractListModel(parent)
    , m_lifetime(lifetime)
{
}

int ToastQueue::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant ToastQueue::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Toast& toast = m_toasts.at(index.row());
    switch (role) {
    case ToastIdRole:
        return toast.id;
    case TextRole:
        return toast.text;
    case ErrorRole:
        return toast.error;
    case ActionLabelRole:
        return toast.actionLabel;
    default:
        return {};
    }
}

QHash<int, QByteArray> ToastQueue::roleNames() const
{
    return {
        {ToastIdRole, "toastId"},
        {TextRole, "text"},
        {ErrorRole, "error"},
        {ActionLabelRole, "actionLabel"},
    };
}

int ToastQueue::show(const QString& text, bool error, const QString& actionLabel,
                     const QString& actionId)
{
    while (count() >= kMaxVisible) {
        removeRow(count() - 1);
    }

    const int id = m_nextId++;
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(m_lifetime);
    connect(timer, &QTimer::timeout, this, [this, id]() { dismiss(id); });

    beginInsertRows({}, 0, 0);
    m_toasts.prepend(Toast{
        .id = id,
        .text = text,
        .error = error,
        .actionLabel = actionLabel,
        .actionId = actionId,
        .timer = timer,
    });
    endInsertRows();
    emit countChanged();
    timer->start();
    return id;
}

void ToastQueue::dismiss(int toastId)
{
    const int row = rowOf(toastId);
    if (row >= 0) {
        removeRow(row);
    }
}

void ToastQueue::triggerAction(int toastId)
{
    const int row = rowOf(toastId);
    if (row < 0) {
        return;
    }
    const QString actionId = m_toasts.at(row).actionId;
    removeRow(row);
    if (!actionId.isEmpty()) {
        emit actionTriggered(actionId);
    }
}

int ToastQueue::rowOf(int toastId) const
{
    for (int row = 0; row < count(); ++row) {
        if (m_toasts.at(row).id == toastId) {
            return row;
        }
    }
    return -1;
}

void ToastQueue::removeRow(int row)
{
    QTimer* timer = m_toasts.at(row).timer;
    beginRemoveRows({}, row, row);
    m_toasts.removeAt(row);
    endRemoveRows();
    timer->stop();
    timer->deleteLater();
    emit countChanged();
}

} // namespace lmp::ui
