#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// Read-only facts about the running application, for QML.
class AppInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit AppInfo(QObject* parent = nullptr);

    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString version() const;
};

} // namespace lmp::ui
