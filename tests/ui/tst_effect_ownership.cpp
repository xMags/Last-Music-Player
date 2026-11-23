#include "ui/effects/EffectOwnership.h"

#include <QPointer>
#include <QQuickItem>
#include <QTest>

using lmp::ui::EffectOwnership;

namespace {

// Stands in for RectangularShadow: an item whose `material` is an item it
// made without an owner.
class ShadowLike : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QQuickItem* material READ material CONSTANT)

public:
    ShadowLike()
        : m_material(new QQuickItem())
    {
    }
    [[nodiscard]] QQuickItem* material() const { return m_material; }

private:
    QQuickItem* m_material;
};

} // namespace

// The material, and child items without an owner, go with the item that
// adopted them; items that already have an owner keep it.
class EffectOwnershipTest : public QObject {
    Q_OBJECT

private slots:
    void theMaterialGoesWithTheShadowEvenIfNeverAttached()
    {
        const EffectOwnership ownership;
        auto* shadow = new ShadowLike();
        QPointer<QQuickItem> material = shadow->material();
        ownership.adopt(shadow);
        QCOMPARE(material->parent(), shadow);
        delete shadow;
        QVERIFY(material.isNull());
    }

    void unownedChildrenGoWithTheItem()
    {
        const EffectOwnership ownership;
        auto* item = new QQuickItem();
        QPointer<QQuickItem> early = new QQuickItem();
        early->setParentItem(item);
        ownership.adopt(item);
        QPointer<QQuickItem> late = new QQuickItem();
        late->setParentItem(item);

        QObject keeper;
        QPointer<QQuickItem> owned = new QQuickItem();
        owned->setParent(&keeper);
        owned->setParentItem(item);

        QCOMPARE(early->parent(), item);
        QCOMPARE(late->parent(), item);
        QCOMPARE(owned->parent(), &keeper);

        delete item;
        QVERIFY(early.isNull());
        QVERIFY(late.isNull());
        QVERIFY(!owned.isNull());
        QVERIFY(owned->parentItem() == nullptr);
    }

    void nothingToAdoptIsHarmless()
    {
        const EffectOwnership ownership;
        ownership.adopt(nullptr);
        QQuickItem item;
        ownership.adopt(&item);
        QVERIFY(item.childItems().isEmpty());
    }
};

QTEST_MAIN(EffectOwnershipTest)
#include "tst_effect_ownership.moc"
