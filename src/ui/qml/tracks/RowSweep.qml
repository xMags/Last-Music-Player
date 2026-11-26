import QtQuick
import LastMusicPlayer

// The band of light that sweeps once across a song row when the song starts
// (motion.css 140-158), 760 ms. Fills its parent; play() starts a sweep.
Rectangle {
    id: band

    function play() {
        sweep.restart()
    }

    width: parent.width
    height: parent.height
    x: -width
    visible: sweep.running
    // linear-gradient(100deg, transparent 20%, accent 22% at 50%, transparent 80%)
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.2; color: "transparent" }
        GradientStop { position: 0.5; color: Theme.mixSrgb(Theme.accentColor, "transparent", 0.22) }
        GradientStop { position: 0.8; color: "transparent" }
    }

    NumberAnimation on x {
        id: sweep

        running: false
        from: -band.width
        to: band.width
        duration: 760
        easing: Motion.water
    }
}
