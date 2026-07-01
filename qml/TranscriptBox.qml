import QtQuick
import QtQuick.Effects

// Translucent, rounded box showing the live/finished transcription. It grows
// smoothly as text arrives, and shows a placeholder + key hints while listening.
Item {
    id: root
    property string text: ""
    property bool busy: false
    property bool listening: false
    property bool modelReady: true

    readonly property bool showing: listening || busy || text.length > 0

    implicitHeight: content.implicitHeight + 24
    height: implicitHeight
    visible: opacity > 0.01
    opacity: showing ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 180 } }
    Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: Qt.rgba(0.06, 0.06, 0.08, 0.74)
        border.color: Qt.rgba(1, 1, 1, 0.09)
        border.width: 1

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.45)
            shadowBlur: 0.5
            shadowVerticalOffset: 4
        }

        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 14
            spacing: 8

            Text {
                id: label
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 6
                elide: Text.ElideRight
                font.pixelSize: 16
                color: root.text.length > 0 ? "#f2f2f7" : "#9a9aa2"
                text: {
                    if (root.text.length > 0) return root.text;
                    if (root.busy) return "Transcribing…";
                    if (root.listening)
                        return root.modelReady ? "Listening… speak now"
                                               : "Loading speech model…";
                    return "";
                }
            }

            // Key hints, shown while the bubble is interactive.
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 14
                visible: root.listening
                Text { text: "⏎  send";  color: "#8e8e93"; font.pixelSize: 12 }
                Text { text: "esc  cancel"; color: "#8e8e93"; font.pixelSize: 12 }
            }
        }
    }
}
