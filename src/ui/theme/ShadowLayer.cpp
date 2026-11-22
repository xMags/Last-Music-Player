#include "ui/theme/ShadowLayer.h"

namespace lmp::ui {

QVariantList shadowLayers(std::initializer_list<ShadowLayer> layers)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(layers.size()));
    for (const ShadowLayer& layer : layers) {
        list.append(QVariant::fromValue(layer));
    }
    return list;
}

} // namespace lmp::ui
