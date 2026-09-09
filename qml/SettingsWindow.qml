import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic

Window {
    id: win
    title: "Scrybe Settings"
    width: 880
    height: 640
    minimumWidth: 720
    minimumHeight: 520
    color: win.bg
    flags: Qt.Dialog
    onClosing: { micPane.testing = false; controller.stopMicPreview(); controller.setSettingsOpen(false) }

    // ---- themes ------------------------------------------------------------
    // Flat glassmorphism: near-flat bases with translucent, hairline-edged
    // panels that read as frosted glass. "oled" is pure black for OLED panels.
    readonly property var themes: ({
        "oled": {
            name: "OLED Black", isLight: false,
            bg: "#000000", bg2: "#060608", sidebar: "#050506",
            surface: Qt.rgba(1,1,1,0.05), surface2: Qt.rgba(1,1,1,0.09),
            stroke: Qt.rgba(1,1,1,0.10), field: Qt.rgba(1,1,1,0.04),
            txt: "#f3f4f7", sub: "#8a8d99",
            accent: "#2dd4bf", accent2: "#38bdf8",
            good: "#34d399", warn: "#fbbf24", danger: "#fb7185"
        },
        "graphite": {
            name: "Graphite", isLight: false,
            bg: "#0b0d11", bg2: "#111420", sidebar: "#080a0e",
            surface: Qt.rgba(1,1,1,0.05), surface2: Qt.rgba(1,1,1,0.10),
            stroke: Qt.rgba(1,1,1,0.09), field: Qt.rgba(1,1,1,0.04),
            txt: "#eef0f5", sub: "#98a0b0",
            accent: "#5b8cff", accent2: "#22d3ee",
            good: "#4ade80", warn: "#fbbf24", danger: "#fb7185"
        },
        "light": {
            name: "Daylight", isLight: true,
            bg: "#e9ebf0", bg2: "#f4f5f8", sidebar: "#e2e5ea",
            surface: Qt.rgba(1,1,1,0.72), surface2: Qt.rgba(1,1,1,0.95),
            stroke: Qt.rgba(0,0,0,0.10), field: Qt.rgba(1,1,1,0.85),
            txt: "#15181f", sub: "#5b6270",
            accent: "#2563eb", accent2: "#0ea5e9",
            good: "#16a34a", warn: "#d97706", danger: "#e11d48"
        }
    })
    readonly property var themeOrder: ["oled", "graphite", "light"]
    readonly property var pal: themes[controller.theme] ? themes[controller.theme] : themes["oled"]

    // ---- palette (theme-driven) -------------------------------------------
    readonly property bool  isLight: pal.isLight
    readonly property color bg:      pal.bg
    readonly property color bg2:     pal.bg2
    readonly property color accent:  pal.accent
    readonly property color accent2: pal.accent2
    readonly property color card:    pal.surface
    readonly property color cardHi:  pal.surface2
    readonly property color cardTop: isLight ? Qt.rgba(1,1,1,0.92) : Qt.rgba(1,1,1,0.08)
    readonly property color stroke:  pal.stroke
    readonly property color txt:     pal.txt
    readonly property color sub:     pal.sub
    readonly property color field:   pal.field
    readonly property color good:    pal.good
    readonly property color warn:    pal.warn
    readonly property color danger:  pal.danger
    // Contrast overlays that flip for the light theme.
    readonly property color hover: isLight ? Qt.rgba(0,0,0,0.05) : Qt.rgba(1,1,1,0.05)
    readonly property color press: isLight ? Qt.rgba(0,0,0,0.10) : Qt.rgba(1,1,1,0.10)
    readonly property color track: isLight ? Qt.rgba(0,0,0,0.16) : Qt.rgba(1,1,1,0.14)

    // Feather-style stroked glyphs (inner SVG markup for a 0..24 viewBox).
    readonly property string gMic: "<rect x='9' y='2' width='6' height='11' rx='3'/><path d='M5 10v1a7 7 0 0 0 14 0v-1'/><line x1='12' y1='19' x2='12' y2='22'/><line x1='8' y1='22' x2='16' y2='22'/>"
    readonly property string gCheck: "<polyline points='20 6 9 17 4 12'/>"
    readonly property string gDownload: "<path d='M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4'/><polyline points='7 10 12 15 17 10'/><line x1='12' y1='15' x2='12' y2='3'/>"
    readonly property string gArrowUp: "<line x1='12' y1='19' x2='12' y2='5'/><polyline points='5 12 12 5 19 12'/>"
    readonly property string gSliders: "<line x1='4' y1='21' x2='4' y2='14'/><line x1='4' y1='10' x2='4' y2='3'/><line x1='12' y1='21' x2='12' y2='12'/><line x1='12' y1='8' x2='12' y2='3'/><line x1='20' y1='21' x2='20' y2='16'/><line x1='20' y1='12' x2='20' y2='3'/><line x1='1' y1='14' x2='7' y2='14'/><line x1='9' y1='8' x2='15' y2='8'/><line x1='17' y1='16' x2='23' y2='16'/>"

    property int pane: 0
    readonly property var nav: [
        { glyph: "<circle cx='12' cy='12' r='9'/><circle cx='8.5' cy='10' r='1'/><circle cx='12' cy='8' r='1'/><circle cx='15.5' cy='10' r='1'/><circle cx='10' cy='15' r='1'/>", label: "Appearance" },
        { glyph: gMic, label: "Speech" },
        { glyph: gSliders, label: "Microphone" },
        { glyph: "<polyline points='4 7 4 4 20 4 20 7'/><line x1='9' y1='20' x2='15' y2='20'/><line x1='12' y1='4' x2='12' y2='20'/>", label: "Formatting" },
        { glyph: "<path d='M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/>", label: "Presets" },
        { glyph: "<rect x='3' y='4' width='18' height='16' rx='2'/><line x1='3' y1='9' x2='21' y2='9'/>", label: "Overlay" },
        { glyph: "<rect x='6' y='6' width='12' height='12' rx='1'/><rect x='9.5' y='9.5' width='5' height='5'/><line x1='9' y1='2' x2='9' y2='4'/><line x1='15' y1='2' x2='15' y2='4'/><line x1='9' y1='20' x2='9' y2='22'/><line x1='15' y1='20' x2='15' y2='22'/><line x1='2' y1='9' x2='4' y2='9'/><line x1='2' y1='15' x2='4' y2='15'/><line x1='20' y1='9' x2='22' y2='9'/><line x1='20' y1='15' x2='22' y2='15'/>", label: "Hardware" },
        { glyph: gDownload, label: "Updates" }
    ]

    function indexByKey(list, key) {
        for (var i = 0; i < list.length; i++)
            if (list[i].key === key) return i;
        return 0;
    }

    // A QML color → "%23rrggbb" for embedding in an inline SVG data URI (the
    // '#' must be percent-encoded, and any alpha byte dropped).
    function svgColor(c) {
        var s = ("" + c).replace("#", "");
        if (s.length === 8) s = s.substring(2);   // strip #aarrggbb alpha
        return "%23" + s;
    }

    // Subtle ambient depth behind the (translucent) content panels.
    Rectangle {
        anchors.fill: parent
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: win.bg2 }
            GradientStop { position: 1.0; color: win.bg }
        }
    }

    // ---- reusable styled pieces -------------------------------------------
    // Monochrome stroked (Feather-style) line icon, tinted to any theme color.
    // The colour is baked straight into the SVG stroke, so it always contrasts.
    component Ico: Image {
        property string glyph: ""
        property color color: win.txt
        property int size: 18
        width: size; height: size
        sourceSize: Qt.size(size * 2, size * 2)
        smooth: true
        fillMode: Image.PreserveAspectFit
        source: "data:image/svg+xml;utf8,"
            + "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='"
            + win.svgColor(color)
            + "' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
            + glyph + "</svg>"
    }
    component Card: Rectangle {
        Layout.fillWidth: true
        radius: 16
        // Faint top-to-body sheen over the base = frosted-glass panel.
        gradient: Gradient {
            GradientStop { position: 0.0; color: win.cardTop }
            GradientStop { position: 0.22; color: win.card }
            GradientStop { position: 1.0; color: win.card }
        }
        border.color: win.stroke
        border.width: 1
        implicitHeight: inner.implicitHeight + 36
        default property alias content: inner.data
        ColumnLayout {
            id: inner
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
        }
    }
    component Header: Text {
        color: win.accent; font.pixelSize: 12; font.bold: true
        font.capitalization: Font.AllUppercase; font.letterSpacing: 1.4
    }
    component Caption: Text {
        color: win.sub; font.pixelSize: 12; Layout.fillWidth: true
        wrapMode: Text.WordWrap; lineHeight: 1.15
    }
    component Field: Text { color: win.txt; font.pixelSize: 14; font.bold: true }

    component CBox: ComboBox {
        id: cb
        Layout.fillWidth: true
        implicitHeight: 40
        font.pixelSize: 14
        background: Rectangle { radius: 10; color: win.field; border.color: cb.activeFocus ? win.accent : win.stroke; border.width: 1 }
        contentItem: Text {
            leftPadding: 14; rightPadding: 32
            text: cb.displayText; color: win.txt; font: cb.font
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        indicator: Text {
            x: cb.width - 24; y: (cb.height - height) / 2
            text: "⌄"; color: win.sub; font.pixelSize: 16
        }
        // Qt 6.11 no longer injects modelData/index into ComboBox delegates as
        // context properties — they must be declared `required` (see the Basic
        // style's own delegate). `model[role]` covers object rows; plain string
        // lists expose the value under the implicit "modelData" role.
        delegate: ItemDelegate {
            id: dlg
            required property var model
            required property int index
            width: cb.width
            contentItem: Text {
                text: cb.textRole ? dlg.model[cb.textRole] : dlg.model.modelData
                color: win.txt; font: cb.font; verticalAlignment: Text.AlignVCenter
                leftPadding: 6
            }
            background: Rectangle { color: dlg.highlighted ? win.hover : "transparent"; radius: 6 }
            highlighted: cb.highlightedIndex === dlg.index
        }
        popup.background: Rectangle { radius: 10; color: win.cardHi; border.color: win.stroke; border.width: 1 }
    }
    component TxtField: TextField {
        Layout.fillWidth: true
        implicitHeight: 40
        color: win.txt; font.pixelSize: 14
        leftPadding: 14
        placeholderTextColor: win.sub
        background: Rectangle { radius: 10; color: win.field; border.color: parent && parent.activeFocus ? win.accent : win.stroke; border.width: 1 }
    }
    component Btn: Button {
        id: b
        property bool primary: false
        property bool danger: false
        implicitHeight: 40
        font.pixelSize: 14; font.bold: primary
        opacity: enabled ? 1 : 0.4
        contentItem: Text {
            text: b.text
            color: b.primary ? "white" : (b.danger ? win.danger : win.txt)
            font: b.font
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            leftPadding: 18; rightPadding: 18
        }
        background: Rectangle {
            radius: 10
            color: b.primary ? (b.down ? Qt.darker(win.accent, 1.2) : win.accent)
                             : (b.down ? win.press : win.hover)
            border.color: b.primary ? "transparent" : win.stroke
            border.width: b.primary ? 0 : 1
        }
    }
    component Toggle: Switch {
        id: sw
        indicator: Rectangle {
            implicitWidth: 46; implicitHeight: 26; radius: 13
            x: sw.width - width; y: (sw.height - height) / 2
            color: sw.checked ? win.accent : win.track
            Behavior on color { ColorAnimation { duration: 120 } }
            Rectangle {
                width: 20; height: 20; radius: 10; color: "white"
                y: 3; x: sw.checked ? parent.width - width - 3 : 3
                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
        }
    }
    component Sld: Slider {
        id: sl
        Layout.fillWidth: true
        implicitHeight: 28
        background: Rectangle {
            x: sl.leftPadding; y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: sl.availableWidth; height: 6; radius: 3
            color: win.track
            Rectangle {
                width: sl.visualPosition * parent.width; height: parent.height; radius: 3
                color: win.accent
            }
        }
        handle: Rectangle {
            x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
            y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: 18; height: 18; radius: 9
            color: "white"
            border.color: win.stroke; border.width: 1
        }
    }
    component Meter: Rectangle {
        property real level: 0
        Layout.fillWidth: true
        implicitHeight: 10; radius: 5
        color: win.track
        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, parent.level))
            height: parent.height; radius: 5
            color: parent.level > 0.85 ? win.warn : win.accent
            Behavior on width { NumberAnimation { duration: 60 } }
        }
    }
    component Badge: Rectangle {
        property string label: ""
        property color tone: win.accent
        implicitHeight: 22; implicitWidth: bt.implicitWidth + 18
        radius: 11
        color: Qt.rgba(tone.r, tone.g, tone.b, 0.16)
        Text { id: bt; anchors.centerIn: parent; text: label; color: tone; font.pixelSize: 11; font.bold: true }
    }

    // ---- layout: sidebar + content ----------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar
        Rectangle {
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            color: win.pal.sidebar
            border.color: win.stroke; border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 14
                    spacing: 11
                    Rectangle {
                        width: 38; height: 38; radius: 11
                        gradient: Gradient {
                            GradientStop { position: 0; color: win.accent }
                            GradientStop { position: 1; color: win.accent2 }
                        }
                        Ico { anchors.centerIn: parent; glyph: win.gMic; size: 20; color: "white" }
                    }
                    ColumnLayout {
                        spacing: 0
                        Text { text: "Scrybe"; color: win.txt; font.pixelSize: 18; font.bold: true }
                        Text { text: "v" + controller.updater.currentVersion; color: win.sub; font.pixelSize: 11 }
                    }
                }

                Repeater {
                    model: win.nav
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 42
                        radius: 10
                        color: win.pane === index ? Qt.rgba(win.accent.r, win.accent.g, win.accent.b, win.isLight ? 0.14 : 0.18)
                                                  : (ma.containsMouse ? win.hover : "transparent")
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            spacing: 11
                            Ico { glyph: modelData.glyph; size: 18
                                  color: win.pane === index ? win.accent : win.sub }
                            Text {
                                text: modelData.label
                                color: win.pane === index ? win.txt : win.sub
                                font.pixelSize: 14; font.bold: win.pane === index
                            }
                            Item { Layout.fillWidth: true }
                            Rectangle {
                                visible: win.pane === index
                                width: 3; height: 18; radius: 2; color: win.accent
                                Layout.rightMargin: 4
                            }
                        }
                        MouseArea {
                            id: ma; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.pane = index
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // update hint chip in sidebar
                Rectangle {
                    visible: controller.updater.updateAvailable
                    Layout.fillWidth: true
                    implicitHeight: 40; radius: 10
                    color: Qt.rgba(win.good.r, win.good.g, win.good.b, 0.14)
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 8
                        Ico { glyph: win.gArrowUp; size: 15; color: win.good }
                        Text { text: "Update ready"; color: win.good; font.pixelSize: 12; font.bold: true }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.pane = 7 }
                }
            }
        }

        // Content
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true

            StackLayout {
                width: parent.width
                currentIndex: win.pane

                // ====================================== 0 · APPEARANCE ======
                ColumnLayout {
                    width: parent.width
                    spacing: 16
                    Item { Layout.preferredHeight: 6 }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Theme" }
                        Caption { text: "Flat glassmorphism. OLED Black is pure black — darkest for OLED displays. Your choice is saved instantly." }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 14
                            Repeater {
                                model: win.themeOrder
                                delegate: ColumnLayout {
                                    property var th: win.themes[modelData]
                                    property bool sel: controller.theme === modelData
                                    spacing: 9
                                    Layout.fillWidth: true

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 100
                                        radius: 14
                                        color: th.bg
                                        border.color: sel ? win.accent : win.stroke
                                        border.width: sel ? 2 : 1

                                        // mini mock of the settings look
                                        Rectangle {
                                            id: mockSide
                                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 12 }
                                            width: 34; radius: 8; color: th.sidebar
                                            Column {
                                                anchors.centerIn: parent; spacing: 5
                                                Repeater {
                                                    model: 3
                                                    Rectangle { width: 16; height: 4; radius: 2
                                                        color: index === 0 ? th.accent : th.surface2 }
                                                }
                                            }
                                        }
                                        Column {
                                            anchors { left: mockSide.right; right: parent.right; verticalCenter: parent.verticalCenter
                                                      leftMargin: 8; rightMargin: 12 }
                                            spacing: 7
                                            Rectangle {
                                                width: parent.width; height: 26; radius: 6
                                                color: th.surface; border.color: th.stroke; border.width: 1
                                                Rectangle { width: 42; height: 6; radius: 3; color: th.accent
                                                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter } }
                                            }
                                            Rectangle {
                                                width: parent.width; height: 20; radius: 6
                                                color: th.surface; border.color: th.stroke; border.width: 1
                                            }
                                        }
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: controller.theme = modelData
                                        }
                                    }
                                    RowLayout {
                                        spacing: 7
                                        Text { text: sel ? "●" : "○"
                                               color: sel ? win.accent : win.sub; font.pixelSize: 12 }
                                        Text { text: th.name; color: win.txt; font.pixelSize: 13
                                               font.bold: sel }
                                    }
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.preferredHeight: 8 }
                }

                // ========================================== 1 · SPEECH ======
                ColumnLayout {
                    width: parent.width
                    spacing: 16
                    Item { Layout.preferredHeight: 6 }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Speech recognition" }

                        Field { text: "Backend" }
                        CBox {
                            id: backendBox
                            model: controller.backendList()
                            Component.onCompleted: currentIndex = model.indexOf(controller.backend)
                            onActivated: controller.setBackend(currentText)
                        }
                        Caption { text: "‘auto’ picks the best engine for your hardware — NVIDIA → faster-whisper, Intel → OpenVINO, otherwise CPU. See the Hardware tab." }

                        Field { text: "Model" }
                        CBox {
                            id: modelBox
                            textRole: "label"; valueRole: "key"
                            model: controller.modelList()
                            Component.onCompleted: currentIndex = win.indexByKey(model, controller.model)
                            onActivated: controller.setModel(currentValue)
                        }

                        Field { text: "Language" }
                        TxtField {
                            text: controller.language
                            placeholderText: "auto (or a code like en, de, tr)"
                            onEditingFinished: controller.setLanguage(text.trim() === "" ? "auto" : text.trim())
                        }
                    }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Voice activity" }

                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Field { text: "Skip silence" }
                                Caption { text: "Skip recordings with no detected voice activity to reduce empty transcriptions. Energy detection can miss quiet or continuous speech and mistake changing noise for speech. Turn this off if words are skipped." }
                            }
                            Toggle { checked: controller.vadEnabled; onToggled: controller.vadEnabled = checked }
                        }

                        Field {
                            text: "Auto-send after silence  ·  "
                                  + (controller.autoSendSecs > 0 ? controller.autoSendSecs.toFixed(1) + "s" : "off")
                        }
                        Sld {
                            from: 0; to: 5; stepSize: 0.5
                            value: controller.autoSendSecs
                            onMoved: controller.setAutoSendSecs(value)
                        }
                        Caption { text: "Hands-free dictation: once you've spoken and then stayed quiet this long, Scrybe sends automatically — no Enter needed. Set to off for manual send only." }
                    }
                    Item { Layout.fillHeight: true; Layout.preferredHeight: 8 }
                }

                // ======================================= 2 · MICROPHONE ====
                ColumnLayout {
                    id: micPane
                    width: parent.width
                    spacing: 16
                    property bool testing: false
                    function refreshDevices() {
                        var cur = micBox.currentValue !== undefined ? micBox.currentValue
                                                                    : controller.micDevice
                        micBox.model = controller.micDeviceList()
                        micBox.currentIndex = win.indexByKey(micBox.model, cur)
                    }
                    Connections {
                        target: win
                        function onPaneChanged() {
                            if (win.pane === 2) {
                                micPane.refreshDevices()
                            } else if (micPane.testing) {
                                micPane.testing = false
                                controller.stopMicPreview()
                            }
                        }
                    }
                    Connections {
                        // A dictation session (hotkey) takes the mic over; the
                        // preview does not resume when it ends.
                        target: controller
                        function onStateChanged() {
                            if (controller.stateName !== "idle")
                                micPane.testing = false
                        }
                    }
                    Item { Layout.preferredHeight: 6 }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Microphone" }

                        Field { text: "Input device" }
                        CBox {
                            id: micBox
                            textRole: "label"; valueRole: "key"
                            model: controller.micDeviceList()
                            Component.onCompleted: currentIndex = win.indexByKey(model, controller.micDevice)
                            onActivated: controller.setMicDevice(currentValue)
                        }
                        Caption { text: "Which input Scrybe records from. Plugged in a new mic? Revisit this pane to refresh the list." }

                        Field { text: "Level meter scale  ·  ×" + controller.micGain.toFixed(1) }
                        Sld {
                            id: gainSlider
                            from: 1; to: 25; stepSize: 0.5
                            value: controller.micGain
                            onMoved: controller.setMicGain(value)
                        }
                        Caption { text: "Changes how strongly the level meter and overlay react. Recorded volume, transcription, and voice detection stay the same. Adjust microphone input volume in your system sound settings." }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 12
                            Btn {
                                text: micPane.testing ? "Stop test" : "Test microphone"
                                primary: !micPane.testing
                                onClicked: {
                                    micPane.testing = !micPane.testing
                                    if (micPane.testing) controller.startMicPreview()
                                    else controller.stopMicPreview()
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 4
                                Meter { level: controller.level }
                                Caption { text: micPane.testing ? "Speak — the bar should move with your voice." : "Press Test microphone and speak to check your setup." }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.preferredHeight: 8 }
                }

                // ======================================= 3 · FORMATTING ====
                ColumnLayout {
                    id: llmPane
                    width: parent.width
                    spacing: 16
                    property var probe: ({ checking: false, available: false, models: [] })
                    function refreshProbe() { llmPane.probe = controller.llmProbeInfo() }
                    Component.onCompleted: { controller.probeLlm(false); refreshProbe() }
                    Connections {
                        target: controller
                        function onLlmProbeChanged() { llmPane.refreshProbe() }
                    }
                    Item { Layout.preferredHeight: 6 }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Ollama connection" }

                        Field { text: "Endpoint" }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            TxtField {
                                id: endpointField
                                text: controller.llmEndpoint
                                placeholderText: "http://localhost:11434"
                                onEditingFinished: controller.setLlmEndpoint(text)
                            }
                            Btn {
                                text: llmPane.probe.checking ? "Checking…" : "Test"
                                enabled: !llmPane.probe.checking
                                onClicked: { controller.setLlmEndpoint(endpointField.text); controller.probeLlm(true) }
                            }
                        }
                        RowLayout {
                            spacing: 8
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: llmPane.probe.checking ? win.warn : (llmPane.probe.available ? win.good : win.danger)
                            }
                            Caption {
                                text: llmPane.probe.checking ? "Checking…"
                                      : llmPane.probe.available
                                          ? ("Connected — " + llmPane.probe.models.length + " model"
                                             + (llmPane.probe.models.length === 1 ? "" : "s") + " available")
                                          : "Unreachable — is Ollama running?"
                            }
                        }

                        Field { text: "Model" }
                        TxtField {
                            id: llmModelField
                            text: controller.llmModel
                            placeholderText: "e.g. qwen2.5:1.5b"
                            onEditingFinished: controller.setLlmModel(text)
                        }
                        Flow {
                            Layout.fillWidth: true
                            visible: llmPane.probe.models.length > 0
                            spacing: 8
                            Repeater {
                                model: llmPane.probe.models
                                delegate: Rectangle {
                                    required property string modelData
                                    property bool sel: modelData === controller.llmModel
                                    implicitHeight: 26; implicitWidth: chipText.implicitWidth + 20
                                    radius: 13
                                    color: sel ? Qt.rgba(win.accent.r, win.accent.g, win.accent.b, 0.18) : win.hover
                                    border.color: sel ? win.accent : win.stroke; border.width: 1
                                    Text {
                                        id: chipText
                                        anchors.centerIn: parent
                                        text: modelData; font.pixelSize: 12
                                        color: sel ? win.accent : win.txt
                                    }
                                    MouseArea {
                                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: { llmModelField.text = modelData; controller.setLlmModel(modelData) }
                                    }
                                }
                            }
                        }
                        Caption { text: "The Ollama model used to clean up dictated text. Pull new ones with `ollama pull <name>`, then Test to refresh this list." }
                    }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Formatting (LLM)" }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Field { text: "Enable LLM cleanup" }
                                Caption { text: "Runs the transcript through a local Ollama model before pasting." }
                            }
                            Toggle { checked: controller.llmEnabled; onToggled: controller.llmEnabled = checked }
                        }

                        Field { text: "Style" }
                        CBox {
                            id: styleBox
                            textRole: "label"; valueRole: "key"
                            property var styles: controller.styleList()
                            model: styles
                            Component.onCompleted: currentIndex = win.indexByKey(styles, controller.beautifyStyle)
                            onActivated: controller.setBeautifyStyle(currentValue)
                            Connections {
                                target: controller
                                function onPresetsChanged() {
                                    var cur = styleBox.currentValue
                                    styleBox.styles = controller.styleList()
                                    styleBox.model = styleBox.styles
                                    styleBox.currentIndex = win.indexByKey(styleBox.styles, cur)
                                }
                            }
                        }
                        Caption { text: "Clean formatting · Markdown (lists/headings) · Summarize & shorten · or your own presets (Presets tab)." }
                    }
                    Item { Layout.fillHeight: true; Layout.preferredHeight: 8 }
                }

                // ========================================== 4 · PRESETS ====
                ColumnLayout {
                    id: presetPane
                    width: parent.width
                    spacing: 16
                    property var names: controller.presetNames()
                    property bool generating: false
                    property bool testing: false
                    property string errorText: ""
                    property int editorRevision: 0
                    property int sampleRevision: 0
                    property int draftRevision: -1
                    property string draftDescription: ""
                    property int previewRevision: -1
                    property int previewSampleRevision: -1
                    onEditorRevisionChanged: if (testOutput) testOutput.text = ""
                    function load(name) {
                        ++presetPane.editorRevision
                        presetName.text = name
                        presetBody.text = controller.presetPrompt(name)
                        tempSlider.value = controller.presetTemp(name)
                        presetPane.errorText = ""
                    }
                    Connections {
                        target: controller
                        function onPresetsChanged() { presetPane.names = controller.presetNames() }
                        function onPresetDraftReady(text) {
                            presetPane.generating = false
                            if (presetPane.draftRevision !== presetPane.editorRevision
                                    || presetPane.draftDescription !== draftDesc.text.trim()) return
                            presetBody.text = text
                            presetPane.errorText = ""
                        }
                        function onPresetDraftFailed(message) {
                            presetPane.generating = false
                            if (presetPane.draftRevision !== presetPane.editorRevision
                                    || presetPane.draftDescription !== draftDesc.text.trim()) return
                            presetPane.errorText = message
                        }
                        function onPresetTestReady(text) {
                            presetPane.testing = false
                            if (presetPane.previewRevision !== presetPane.editorRevision
                                    || presetPane.previewSampleRevision !== presetPane.sampleRevision) return
                            testOutput.text = text
                            presetPane.errorText = ""
                        }
                        function onPresetTestFailed(message) {
                            presetPane.testing = false
                            if (presetPane.previewRevision !== presetPane.editorRevision
                                    || presetPane.previewSampleRevision !== presetPane.sampleRevision) return
                            presetPane.errorText = message
                        }
                    }
                    Item { Layout.preferredHeight: 6 }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Custom style presets" }
                        Caption { text: "Write (or generate) an instruction for how to format the text. Saved presets appear in the Style dropdown." }

                        Flow {
                            Layout.fillWidth: true
                            visible: presetPane.names.length > 0
                            spacing: 8
                            Repeater {
                                model: presetPane.names
                                delegate: Rectangle {
                                    required property string modelData
                                    property bool sel: modelData === presetName.text
                                    implicitHeight: 26; implicitWidth: pchip.implicitWidth + 20
                                    radius: 13
                                    color: sel ? Qt.rgba(win.accent.r, win.accent.g, win.accent.b, 0.18) : win.hover
                                    border.color: sel ? win.accent : win.stroke; border.width: 1
                                    Text {
                                        id: pchip
                                        anchors.centerIn: parent
                                        text: parent.modelData; font.pixelSize: 12
                                        color: parent.sel ? win.accent : win.txt
                                    }
                                    MouseArea {
                                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: presetPane.load(parent.modelData)
                                    }
                                }
                            }
                        }

                        Field { text: "Preset name" }
                        TxtField {
                            id: presetName; placeholderText: "e.g. Formal email"
                            onTextChanged: ++presetPane.editorRevision
                        }

                        Field { text: "Instruction" }
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 130
                            TextArea {
                                id: presetBody
                                onTextChanged: ++presetPane.editorRevision
                                color: win.txt; font.pixelSize: 14
                                wrapMode: TextArea.Wrap
                                placeholderText: "e.g. Rewrite the text as a polite, well-structured email with a greeting and sign-off."
                                placeholderTextColor: win.sub
                                background: Rectangle { radius: 10; color: win.field; border.color: win.stroke; border.width: 1 }
                            }
                        }

                        Field {
                            text: "Creativity  ·  " + tempSlider.value.toFixed(2)
                        }
                        Sld {
                            id: tempSlider
                            onValueChanged: ++presetPane.editorRevision
                            from: 0; to: 1; stepSize: 0.05
                            value: 0.3
                        }
                        Caption { text: "Temperature for this preset. Low = predictable, faithful edits; high = freer rewording. 0.1–0.3 suits most formatting styles." }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Btn {
                                text: "Save preset"; primary: true
                                enabled: presetName.text.trim() !== "" && presetBody.text.trim() !== ""
                                onClicked: {
                                    if (controller.savePreset(presetName.text.trim(), presetBody.text.trim(), tempSlider.value)) {
                                        controller.setBeautifyStyle(presetName.text.trim())
                                        presetPane.errorText = ""
                                    } else {
                                        presetPane.errorText = "Choose a name without slashes or control characters. The names format, markdown, and summary are reserved."
                                    }
                                }
                            }
                            Btn {
                                text: "New"
                                onClicked: { ++presetPane.editorRevision; presetName.text = ""; presetBody.text = ""; tempSlider.value = 0.3 }
                            }
                            Btn {
                                text: "Delete"; danger: true
                                enabled: presetPane.names.indexOf(presetName.text.trim()) >= 0
                                onClicked: { controller.deletePreset(presetName.text.trim()); presetName.text = ""; presetBody.text = "" }
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "AI assistant" }
                        Caption { text: "Uses your Ollama model (Formatting tab) to write the instruction for you, and to preview what a preset does before you save it." }

                        Field { text: "Generate from a description" }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            TxtField {
                                id: draftDesc
                                placeholderText: "e.g. terse commit messages in imperative mood"
                            }
                            Btn {
                                text: presetPane.generating ? "Generating…" : "Generate"
                                primary: true
                                enabled: !presetPane.generating && draftDesc.text.trim() !== ""
                                onClicked: {
                                    presetPane.generating = true
                                    presetPane.draftRevision = presetPane.editorRevision
                                    presetPane.draftDescription = draftDesc.text.trim()
                                    presetPane.errorText = ""
                                    controller.generatePreset(draftDesc.text.trim())
                                }
                            }
                        }
                        Caption { text: "The draft lands in the Instruction box above — review, tweak, then save." }

                        Field { text: "Try it on sample text" }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            TxtField {
                                id: sampleField
                                onTextChanged: {
                                    ++presetPane.sampleRevision
                                    testOutput.text = ""
                                }
                                placeholderText: "e.g. um so basically we need to fix the login bug before friday"
                            }
                            Btn {
                                text: presetPane.testing ? "Testing…" : "Test"
                                enabled: !presetPane.testing && presetBody.text.trim() !== "" && sampleField.text.trim() !== ""
                                onClicked: {
                                    presetPane.testing = true
                                    presetPane.previewRevision = presetPane.editorRevision
                                    presetPane.previewSampleRevision = presetPane.sampleRevision
                                    presetPane.errorText = ""
                                    testOutput.text = ""
                                    controller.testPreset(presetBody.text.trim(), tempSlider.value, sampleField.text.trim())
                                }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            visible: testOutput.text !== ""
                            radius: 10
                            color: win.field
                            border.color: win.stroke; border.width: 1
                            implicitHeight: testOutput.implicitHeight + 24
                            TextEdit {
                                id: testOutput
                                anchors.fill: parent; anchors.margins: 12
                                readOnly: true; selectByMouse: true
                                wrapMode: TextEdit.Wrap
                                color: win.txt; font.pixelSize: 13
                                selectionColor: win.accent
                            }
                        }
                        Caption {
                            visible: presetPane.errorText !== ""
                            text: presetPane.errorText
                            color: win.danger
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.preferredHeight: 8 }
                }

                // ========================================== 5 · OVERLAY ====
                ColumnLayout {
                    width: parent.width
                    spacing: 16
                    Item { Layout.preferredHeight: 6 }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Overlay" }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Field { text: "Live preview text" }
                                Caption { text: "Show the transcription filling in while you speak." }
                            }
                            Toggle { checked: controller.previewEnabled; onToggled: controller.previewEnabled = checked }
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.preferredHeight: 8 }
                }

                // ========================================= 6 · HARDWARE ====
                ColumnLayout {
                    id: hwPane
                    width: parent.width
                    spacing: 16
                    property var hw: ({})
                    property var backends: []
                    // Availability probes run asynchronously; the list re-reads
                    // itself when each probe finishes (backendProbesChanged).
                    function refresh(force) {
                        hw = controller.hardwareInfo()
                        backends = controller.backendInfo()
                        controller.probeBackends(force === true)
                    }
                    Component.onCompleted: refresh(false)
                    Connections {
                        target: win
                        function onPaneChanged() { if (win.pane === 6) hwPane.refresh(false) }
                    }
                    Connections {
                        target: controller
                        function onBackendProbesChanged() { hwPane.backends = controller.backendInfo() }
                    }
                    Item { Layout.preferredHeight: 6 }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Detected hardware" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 12
                            Ico { glyph: "<rect x='2' y='3' width='20' height='14' rx='2'/><line x1='8' y1='21' x2='16' y2='21'/><line x1='12' y1='17' x2='12' y2='21'/>"
                                  size: 24; color: win.accent }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 3
                                Field { text: hwPane.hw.gpus ? hwPane.hw.gpus : "…" }
                                Caption { text: "Active backend (auto-resolved): <b>" + (hwPane.hw.resolved ? hwPane.hw.resolved : "…") + "</b>" }
                            }
                            Btn { text: "Rescan"; onClicked: hwPane.refresh(true) }
                        }
                    }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Speech backends" }
                        Caption { text: "Install only what your hardware needs. On non-Intel machines OpenVINO is skipped automatically." }

                        Repeater {
                            model: hwPane.backends
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                radius: 12
                                color: win.cardHi
                                border.color: win.stroke; border.width: 1
                                implicitHeight: row.implicitHeight + 26
                                RowLayout {
                                    id: row
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 12
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 5
                                        RowLayout {
                                            spacing: 8
                                            Text { text: modelData.label; color: win.txt; font.pixelSize: 14; font.bold: true }
                                            Badge { visible: modelData.recommended; label: "RECOMMENDED"; tone: win.accent2 }
                                            Badge { visible: modelData.available; label: "READY"; tone: win.good }
                                        }
                                        Caption { text: modelData.note }
                                    }
                                    Btn {
                                        text: modelData.available ? "Reinstall" : "Install"
                                        primary: !modelData.available && modelData.recommended
                                        visible: modelData.installable || !modelData.available
                                        onClicked: controller.installBackend(modelData.key)
                                    }
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.preferredHeight: 8 }
                }

                // ========================================== 7 · UPDATES ====
                ColumnLayout {
                    width: parent.width
                    spacing: 16
                    Item { Layout.preferredHeight: 6 }

                    Card {
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        Header { text: "Software updates" }

                        RowLayout {
                            Layout.fillWidth: true; spacing: 14
                            Rectangle {
                                width: 44; height: 44; radius: 12
                                color: controller.updater.updateAvailable
                                       ? Qt.rgba(win.good.r, win.good.g, win.good.b, 0.16)
                                       : win.hover
                                Ico {
                                    anchors.centerIn: parent; size: 22
                                    glyph: controller.updater.updateAvailable ? win.gArrowUp : win.gCheck
                                    color: controller.updater.updateAvailable ? win.good : win.sub
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 3
                                Field { text: "Installed version " + controller.updater.currentVersion }
                                Caption { text: controller.updater.status }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 10
                            Btn {
                                text: controller.updater.checking ? "Checking…" : "Check now"
                                enabled: !controller.updater.checking
                                onClicked: controller.updater.check(false)
                            }
                            Btn {
                                text: "Install update " + controller.updater.latestVersion
                                primary: true
                                visible: controller.updater.updateAvailable
                                onClicked: controller.updater.installUpdate()
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Caption {
                            text: "Scrybe checks GitHub for a newer release on launch. Installing pulls the latest source and rebuilds in a terminal."
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.preferredHeight: 8 }
                }
            }
        }
    }
}
