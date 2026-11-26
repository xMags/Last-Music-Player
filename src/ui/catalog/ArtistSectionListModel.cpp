#include "ui/catalog/ArtistSectionListModel.h"

namespace lmp::ui {

ArtistSectionListModel::ArtistSectionListModel(QObject* parent)
    : KeyedListModel(parent)
{
}

int ArtistSectionListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant ArtistSectionListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Section& section = m_sections.at(index.row());
    switch (role) {
    case KeyRole:
        return section.key;
    case TitleRole:
        return section.title;
    case PillRole:
        return section.pill;
    case LinkRole:
        return section.link;
    default:
        return {};
    }
}

QHash<int, QByteArray> ArtistSectionListModel::roleNames() const
{
    return {{KeyRole, "key"}, {TitleRole, "title"}, {PillRole, "pill"}, {LinkRole, "link"}};
}

void ArtistSectionListModel::setSections(const QList<Section>& sections)
{
    if (sections == m_sections) {
        return;
    }
    const int before = count();
    replaceRows(m_sections, sections, [](const Section& section) { return section.key; });
    if (count() != before) {
        emit countChanged();
    }
}

} // namespace lmp::ui
