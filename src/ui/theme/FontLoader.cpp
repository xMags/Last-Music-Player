#include "ui/theme/FontLoader.h"

#include <QFontDatabase>
#include <QLoggingCategory>

namespace lmp::ui {

namespace {

Q_LOGGING_CATEGORY(lcFonts, "lmp.ui.fonts")

// Resource paths of the bundled font files (see src/ui/CMakeLists.txt).
const QStringList& bundledFontPaths()
{
    static const QStringList paths{
        QStringLiteral(":/qt/qml/LastMusicPlayer/fonts/MrDeHaviland-Regular.ttf"),
    };
    return paths;
}

} // namespace

QStringList FontLoader::loadBundledFonts()
{
    QStringList families;
    for (const QString& path : bundledFontPaths()) {
        const int id = QFontDatabase::addApplicationFont(path);
        if (id < 0) {
            qCWarning(lcFonts) << "Could not load bundled font" << path;
            continue;
        }
        families.append(QFontDatabase::applicationFontFamilies(id));
    }
    return families;
}

QString FontLoader::firstAvailableFamily(const QStringList& stack)
{
    const QStringList installed = QFontDatabase::families();
    for (const QString& family : stack) {
        if (installed.contains(family, Qt::CaseInsensitive)) {
            return family;
        }
    }
    return stack.isEmpty() ? QString() : stack.last();
}

} // namespace lmp::ui
