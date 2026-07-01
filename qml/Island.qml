import QtQuick
import QtQuick.Effects

// The pill-shaped, animated container that expands when active.
Item {
    id: root
    property string state: "idle"   // idle | listening | transcribing | beautifying | pasting

    // Collapsed vs expanded geometry, animated via Behaviors.
    property bool expanded: state === "listening" || state === "transcribing"
                            || state === "beautifying"

    width: expanded ? 360 : 180
    height: expanded ? 64 : 40
    opacity: state === "idle" ? 0.0 : 1.0
    scale: state === "idle" ? 0.85 : 1.0

    Behavior on width  { NumberAnimation { duration: 320; easing.type: Easing.OutBack } }
    Behavior on height { NumberAnimation { duration: 320; easing.type: Easing.OutBack } }
    Behavior on opacity { NumberAnimation { duration: 220 } }
    Behavior on scale  { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: height / 2
        color: "#101014"
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1

        // Soft drop shadow for the floating look.
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.55)
            shadowBlur: 0.6
            shadowVerticalOffset: 6
        }

        Row {
            anchors.centerIn: parent
            spacing: 14

            // Status dot: red pulse while listening.
            Rectangle {
                id: dot
                width: 10; height: 10; radius: 5
                anchors.verticalCenter: parent.verticalCenter
                color: root.state === "listening" ? "#ff453a" : "#8e8e93"

                SequentialAnimation on opacity {
                    running: root.state === "listening"
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 600; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
                }
            }

            // Audio visualizer (live while listening).
            Visualizer {
                anchors.verticalCenter: parent.verticalCenter
                width: 200
                height: 40
                visible: root.expanded
                active: root.state === "listening"
                level: controller.level
            }
        }
    }
}
