import QtQuick

// A row of bars that react to the audio level. Each bar has its own phase so
// the group ripples rather than moving in lockstep. Motion is driven by a
// Timer (event-loop based) so it animates reliably on the layer-shell overlay.
Item {
    id: root
    property real level: 0.0     // 0..1 current audio level
    property bool active: false
    property int barCount: 21
    property real tick: 0.0

    Timer {
        interval: 33; repeat: true; running: root.active
        onTriggered: root.tick += 0.22
    }

    Row {
        anchors.centerIn: parent
        spacing: 3

        Repeater {
            model: root.barCount

            Rectangle {
                id: bar
                width: 3
                radius: 1.5
                anchors.verticalCenter: parent.verticalCenter
                color: "#f2f2f7"

                // Bars near the centre swing higher; a per-index phase ripples.
                readonly property real centreBias:
                    1.0 - Math.abs(index - (root.barCount - 1) / 2)
                          / ((root.barCount - 1) / 2)
                readonly property real wave:
                    0.5 + 0.5 * Math.sin(index * 0.7 + root.tick)

                // Always-alive idle motion (so it clearly "visualizes") plus a
                // strong response to the audio level. No Behavior: the Timer
                // drives smooth per-frame updates directly.
                readonly property real amp:
                    0.16 + root.level * 1.20

                height: root.active
                        ? Math.max(3, root.height
                              * (0.06 + amp * (0.25 + 0.75 * centreBias)
                                            * (0.25 + 0.75 * wave)))
                        : 3
            }
        }
    }
}
