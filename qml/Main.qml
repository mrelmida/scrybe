import QtQuick
import QtQuick.Window
import Scrybe

Window {
    id: win

    // Sized to comfortably hold the expanded island + transcript.
    width: 520
    height: 340
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    // Shown/hidden explicitly via requestShow/requestHide (not bound to state),
    // so finish() can hide the overlay to release focus before pasting.
    visible: false

    // React to the controller's show/hide requests. LayerShellQt anchors us
    // to the top/bottom edge; grabbing focus lets Esc/Enter reach us.
    Connections {
        target: controller
        function onRequestShow() { win.show(); win.raise(); root.forceActiveFocus(); }
        function onRequestHide() { win.hide(); }
    }

    Item {
        id: root
        anchors.fill: parent
        focus: true

        // Esc = cancel (discard), Enter = send (finalize + paste).
        Keys.onEscapePressed: controller.cancel()
        Keys.onReturnPressed: controller.send()
        Keys.onEnterPressed:  controller.send()

        // The animated "dynamic island".
        Island {
            id: island
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 12
            state: controller.stateName
        }

        // Live transcription readout below the island. Appears as soon as we
        // start listening (as a growing space) and fills in with partial text.
        TranscriptBox {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: island.bottom
            anchors.topMargin: 10
            width: 460
            text: controller.transcript
            listening: controller.stateName === "listening"
            modelReady: controller.modelReady
            busy: controller.stateName === "transcribing"
                  || controller.stateName === "beautifying"
        }
    }

    // Separate, independent top-level settings window (not transient to the
    // hidden overlay, or it would be suppressed while the overlay is hidden).
    SettingsWindow {
        id: settingsWin
        transientParent: null
        visible: controller.settingsOpen
        onVisibleChanged: if (visible) { raise(); requestActivate(); }
    }
}
