#include <QColor>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSemaphore>
#include <QSize>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>
#include <QMetaObject>

#include <memory>
#include <vector>

Q_IMPORT_QML_PLUGIN(LastMusicPlayerPlugin)

namespace {

// Records the size every picture is asked for at.
class RecordingProvider final : public QQuickImageProvider {
public:
    RecordingProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        Q_UNUSED(id)
        {
            const QMutexLocker locker(&m_lock);
            m_sizes.push_back(requestedSize);
        }
        const QSize side = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(2000, 2000);
        QImage image(side, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(40, 80, 120));
        if (size != nullptr) {
            *size = image.size();
        }
        return image;
    }

    [[nodiscard]] std::vector<QSize> sizes() const
    {
        const QMutexLocker locker(&m_lock);
        return m_sizes;
    }

private:
    mutable QMutex m_lock;
    std::vector<QSize> m_sizes;
};

// Holds every picture back until the test lets it through; "missing" never
// has one.
class GatedProvider final : public QQuickImageProvider {
public:
    GatedProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        if (id == QStringLiteral("missing")) {
            return {};
        }
        m_gate.acquire();
        QImage image(requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(8, 8),
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(40, 80, 120));
        if (size != nullptr) {
            *size = image.size();
        }
        return image;
    }

    void open() { m_gate.release(); }

private:
    QSemaphore m_gate;
};

constexpr char TileQml[] = R"(
import QtQuick
import LastMusicPlayer
ArtTile {
    width: 0
    height: 0
}
)";

// The missing-picture tile's glyph, once the tile's loader has made it;
// null while the loader is inactive.
[[nodiscard]] QQuickItem* missingGlyph(QObject* missingTile)
{
    auto* item = missingTile->property("item").value<QQuickItem*>();
    return item != nullptr ? item->findChild<QQuickItem*>(QStringLiteral("coverMissingGlyph")) : nullptr;
}

// Whether any text is drawn under `item`.
[[nodiscard]] bool drawsText(const QQuickItem* item)
{
    for (const QQuickItem* child : item->findChildren<QQuickItem*>()) {
        if (QByteArrayView(child->metaObject()->className()).startsWith("QQuickText")) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool allSized(const std::vector<QSize>& sizes)
{
    for (const QSize& size : sizes) {
        if (!size.isValid() || size.isEmpty()) {
            return false;
        }
    }
    return true;
}

} // namespace

// A cover tile decodes its picture at its own size, never at full size.
class ArtTileTest : public QObject {
    Q_OBJECT

private slots:
    void picturesAreOnlyAskedForAtTheTilesSize()
    {
        QQmlEngine engine;
        auto* provider = new RecordingProvider(); // the engine owns it
        engine.addImageProvider(QStringLiteral("recording"), provider);
        QQuickWindow window; // never shown
        QQmlComponent component(&engine);
        component.setData(TileQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* tile = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(tile);
        tile->setProperty("source", QUrl(QStringLiteral("image://recording/cover")));

        // Without a size or a window there is nothing to decode at.
        QTest::qWait(100);
        QVERIFY(provider->sizes().empty());

        tile->setParentItem(window.contentItem());
        QTest::qWait(100);
        QVERIFY(provider->sizes().empty());

        tile->setSize(QSizeF(100, 100));
        QTRY_COMPARE(provider->sizes().size(), std::size_t{1});
        const QSize decoded = provider->sizes().front();
        QVERIFY(!decoded.isEmpty());
        QTRY_VERIFY(tile->property("loaded").toBool());

        // Torn down: losing the size or the window loads nothing again.
        tile->setSize(QSizeF(0, 0));
        tile->setParentItem(nullptr);
        QTest::qWait(100);
        QCOMPARE(provider->sizes().size(), std::size_t{1});

        // A new picture comes at the size the tile last had.
        tile->setParentItem(window.contentItem());
        tile->setSize(QSizeF(100, 100));
        tile->setProperty("source", QUrl(QStringLiteral("image://recording/other")));
        QTRY_COMPARE(provider->sizes().size(), std::size_t{2});
        QCOMPARE(provider->sizes().back(), decoded);
        QVERIFY(allSized(provider->sizes()));
    }

    // Loading shows the skeleton block, a loaded picture neither placeholder,
    // and no picture (none given, or one that fails) the gradient tile with
    // its glyph and no text.
    void placeholdersFollowThePicture()
    {
        QQmlEngine engine;
        auto* provider = new GatedProvider(); // the engine owns it
        engine.addImageProvider(QStringLiteral("gated"), provider);
        QQuickWindow window; // never shown
        QQmlComponent component(&engine);
        component.setData(TileQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* tile = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(tile);
        tile->setParentItem(window.contentItem());
        tile->setSize(QSizeF(100, 100));
        auto* skeleton = tile->findChild<QQuickItem*>(QStringLiteral("coverSkeleton"));
        auto* missing = tile->findChild<QObject*>(QStringLiteral("coverMissingTile"));
        QVERIFY(skeleton);
        QVERIFY(missing);
        const auto missingShown = [missing]() { return missing->property("active").toBool(); };

        // No picture at all: the gradient with the note, no text.
        QVERIFY(missingShown());
        QVERIFY(!skeleton->isVisible());
        QQuickItem* glyph = missingGlyph(missing);
        QVERIFY(glyph);
        QVERIFY(glyph->isVisible());
        QVERIFY(glyph->property("source").toUrl().toString().startsWith(QStringLiteral("image://icon/music-note/")));
        QVERIFY(!drawsText(missing->property("item").value<QQuickItem*>()));

        // A round tile shows the person.
        tile->setProperty("radius", 50);
        QVERIFY(missingGlyph(missing)->property("source").toUrl().toString().startsWith(
            QStringLiteral("image://icon/person/")));
        tile->setProperty("radius", 8);

        // A bare tile shows only the gradient.
        tile->setProperty("bare", true);
        QVERIFY(!missingGlyph(missing)->isVisible());
        tile->setProperty("bare", false);

        // On its way.
        tile->setProperty("source", QUrl(QStringLiteral("image://gated/cover")));
        QTRY_VERIFY(skeleton->isVisible());
        QVERIFY(!missingShown());

        // Loaded.
        provider->open();
        QTRY_VERIFY(tile->property("loaded").toBool());
        QTRY_VERIFY(!skeleton->isVisible());
        QVERIFY(!missingShown());

        // A picture that cannot load.
        tile->setProperty("source", QUrl(QStringLiteral("image://gated/missing")));
        QTRY_VERIFY(missingShown());
        QVERIFY(!skeleton->isVisible());
        QVERIFY(!tile->property("loaded").toBool());
        QTRY_VERIFY(missingGlyph(missing) != nullptr && missingGlyph(missing)->isVisible());
    }

    // Another picture fades in over the old one; no picture (the owner clears
    // the address while it loads another thing) drops the old one at once and
    // shows the skeleton.
    void theOldPictureStaysOnlyForANewOne()
    {
        QQmlEngine engine;
        auto* provider = new GatedProvider(); // the engine owns it
        engine.addImageProvider(QStringLiteral("gated"), provider);
        QQuickWindow window; // never shown
        QQmlComponent component(&engine);
        component.setData(TileQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* tile = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(tile);
        tile->setParentItem(window.contentItem());
        tile->setSize(QSizeF(100, 100));
        auto* previous = tile->findChild<QObject*>(QStringLiteral("previousPicture"));
        auto* skeleton = tile->findChild<QQuickItem*>(QStringLiteral("coverSkeleton"));
        QVERIFY(previous);
        QVERIFY(skeleton);
        const QUrl first(QStringLiteral("image://gated/first"));
        tile->setProperty("source", first);
        provider->open();
        QTRY_VERIFY(tile->property("covered").toBool());

        // The same tile, another picture: the old one waits under it.
        tile->setProperty("source", QUrl(QStringLiteral("image://gated/second")));
        QCOMPARE(previous->property("source").toUrl(), first);
        provider->open();
        QTRY_VERIFY(tile->property("covered").toBool());
        QTRY_VERIFY(previous->property("source").toUrl().isEmpty());

        // Cleared while the owner loads another thing: gone at once.
        tile->setProperty("pending", true);
        tile->setProperty("source", QUrl());
        QVERIFY(previous->property("source").toUrl().isEmpty());
        QVERIFY(!tile->property("covered").toBool());
        QTRY_VERIFY(skeleton->isVisible());
    }

    // While the owner is still loading, a tile with no picture yet shows the
    // skeleton, not the gradient tile; once loading ends with no picture, the
    // gradient tile shows.
    void aPendingTileWithoutAPictureShowsTheSkeleton()
    {
        QQmlEngine engine;
        QQuickWindow window; // never shown
        QQmlComponent component(&engine);
        component.setData(TileQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* tile = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(tile);
        tile->setParentItem(window.contentItem());
        tile->setSize(QSizeF(100, 100));
        auto* skeleton = tile->findChild<QQuickItem*>(QStringLiteral("coverSkeleton"));
        auto* missing = tile->findChild<QObject*>(QStringLiteral("coverMissingTile"));
        QVERIFY(skeleton);
        QVERIFY(missing);

        tile->setProperty("pending", true);
        QTRY_VERIFY(skeleton->isVisible());
        QVERIFY(!missing->property("active").toBool());
        QVERIFY(!tile->property("pictureMissing").toBool());

        tile->setProperty("pending", false);
        QTRY_VERIFY(missing->property("active").toBool());
        QVERIFY(!skeleton->isVisible());
    }
};

QTEST_MAIN(ArtTileTest)
#include "tst_art_tile.moc"
