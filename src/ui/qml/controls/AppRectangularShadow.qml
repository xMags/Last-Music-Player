import QtQuick
import QtQuick.Effects
import LastMusicPlayer

// RectangularShadow that deletes its default material when it goes
// (EffectOwnership explains the leak this closes). Use it wherever
// RectangularShadow would be used.
RectangularShadow {
    id: shadow

    Component.onCompleted: EffectOwnership.adopt(shadow)
}
