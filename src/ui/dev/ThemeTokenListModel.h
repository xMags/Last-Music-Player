#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMetaProperty>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

#include "ui/theme/Theme.h"

namespace lmp::ui {

// Every token of one kind on the Theme object (its colours, or its text
// styles), for the design gallery. Reads the values live, so a theme or
// accent switch updates the rows.
class ThemeTokenListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(lmp::ui::Theme* theme READ theme WRITE setTheme NOTIFY themeChanged FINAL)
    Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY kindChanged FINAL)

public:
    enum class Kind { Colors, Fonts };
    Q_ENUM(Kind)

    enum Role {
        NameRole = Qt::UserRole + 1,
        ValueRole,
    };
    Q_ENUM(Role)

    explicit ThemeTokenListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] Theme* theme() const { return m_theme; }
    void setTheme(Theme* theme);
    [[nodiscard]] Kind kind() const { return m_kind; }
    void setKind(Kind kind);

signals:
    void themeChanged();
    void kindChanged();

private:
    void rebuild();
    void refreshValues();

    QPointer<Theme> m_theme;
    Kind m_kind = Kind::Colors;
    QList<QMetaProperty> m_properties;
};

} // namespace lmp::ui
