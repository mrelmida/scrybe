import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic

Window {
    id: win
    title: "Scrybe Settings"
    width: 580
    height: 720
    minimumWidth: 480
    color: "#101119"
    flags: Qt.Dialog
    onClosing: controller.setSettingsOpen(false)

    readonly property color accent: "#8a86ff"
    readonly property color card:   "#1a1c28"
    readonly property color stroke: Qt.rgba(1, 1, 1, 0.08)
    readonly property color txt:    "#e8e8f0"
    readonly property color sub:    "#9a9aa8"
    readonly property color field:  "#12131d"

    function indexByKey(list, key) {
        for (var i = 0; i < list.length; i++)
            if (list[i].key === key) return i;
        return 0;
    }

    // ---- reusable styled pieces --------------------------------------------
    component Card: Rectangle {
        Layout.fillWidth: true
        radius: 16
        color: win.card
        border.color: win.stroke
        border.width: 1
        implicitHeight: inner.implicitHeight + 34
        default property alias content: inner.data
        ColumnLayout {
            id: inner
            anchors.fill: parent
            anchors.margins: 17
            spacing: 12
        }
    }
    component Header: Text {
        color: win.accent
        font.pixelSize: 13
        font.bold: true
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1
    }
    component Caption: Text { color: win.sub; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    component Field: Text { color: win.txt; font.pixelSize: 14 }

    component CBox: ComboBox {
        id: cb
        Layout.fillWidth: true
        implicitHeight: 38
        font.pixelSize: 14
        background: Rectangle { radius: 9; color: win.field; border.color: win.stroke; border.width: 1 }
        contentItem: Text {
            leftPadding: 12; rightPadding: 30
            text: cb.displayText; color: win.txt; font: cb.font
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        delegate: ItemDelegate {
            width: cb.width
            contentItem: Text {
                text: cb.textRole ? (Array.isArray(cb.model) ? modelData[cb.textRole] : model[cb.textRole])
                                  : modelData
                color: win.txt; font: cb.font; verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { color: highlighted ? Qt.rgba(1,1,1,0.06) : "transparent" }
            highlighted: cb.highlightedIndex === index
        }
        popup.background: Rectangle { radius: 9; color: "#1e2030"; border.color: win.stroke; border.width: 1 }
    }
    component TxtField: TextField {
        Layout.fillWidth: true
        implicitHeight: 38
        color: win.txt
        font.pixelSize: 14
        leftPadding: 12
        placeholderTextColor: win.sub
        background: Rectangle { radius: 9; color: win.field; border.color: win.stroke; border.width: 1 }
    }
    component Btn: Button {
        id: b
        property bool primary: false
        implicitHeight: 38
        font.pixelSize: 14
        contentItem: Text {
            text: b.text; color: b.primary ? "white" : win.txt; font: b.font
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            leftPadding: 16; rightPadding: 16
        }
        background: Rectangle {
            radius: 9
            color: b.primary ? (b.down ? Qt.darker(win.accent, 1.2) : win.accent)
                             : (b.down ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.05))
            border.color: win.stroke; border.width: b.primary ? 0 : 1
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: win.width
            spacing: 16

            // Title
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 22
                Layout.bottomMargin: 0
                spacing: 12
                Rectangle {
                    width: 34; height: 34; radius: 10
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#7aa2ff" }
                        GradientStop { position: 1; color: "#a06bff" }
                    }
                    Text { anchors.centerIn: parent; text: "🎙"; font.pixelSize: 18 }
                }
                Text { text: "Scrybe Settings"; color: win.txt; font.pixelSize: 20; font.bold: true }
            }

            // ---- Speech recognition ----------------------------------------
            Card {
                Layout.leftMargin: 22; Layout.rightMargin: 22
                Header { text: "Speech recognition" }

                Field { text: "Backend" }
                CBox {
                    id: backendBox
                    model: controller.backendList()
                    Component.onCompleted: currentIndex = model.indexOf(controller.backend)
                    onActivated: controller.setBackend(currentText)
                }
                Caption { text: "‘auto’ picks the best engine for your hardware (NVIDIA → faster-whisper, Intel → OpenVINO)." }

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

            // ---- Formatting ------------------------------------------------
            Card {
                Layout.leftMargin: 22; Layout.rightMargin: 22
                Header { text: "Formatting (LLM)" }

                RowLayout {
                    Layout.fillWidth: true
                    Field { text: "Enable LLM cleanup"; Layout.fillWidth: true }
                    Switch {
                        checked: controller.llmEnabled
                        onToggled: controller.llmEnabled = checked
                    }
                }

                Field { text: "Style" }
                CBox {
                    id: styleBox
                    textRole: "label"; valueRole: "key"
                    property var styles: controller.styleList()
                    model: styles
                    Component.onCompleted: currentIndex = win.indexByKey(styles, controller.beautifyStyle)
                    onActivated: {
                        controller.setBeautifyStyle(currentValue)
                        presetName.text = controller.presetNames().indexOf(currentValue) >= 0 ? currentValue : ""
                        presetBody.text = presetName.text === "" ? "" : controller.presetPrompt(currentValue)
                    }
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
                Caption { text: "Clean formatting · Markdown (lists/headings) · Summarize & shorten · or your own presets below." }
            }

            // ---- Custom presets --------------------------------------------
            Card {
                Layout.leftMargin: 22; Layout.rightMargin: 22
                Header { text: "Custom style presets" }
                Caption { text: "Write an instruction for how to format the text. Saved presets appear in the Style dropdown." }

                Field { text: "Preset name" }
                TxtField { id: presetName; placeholderText: "e.g. Formal email" }

                Field { text: "Instruction" }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    TextArea {
                        id: presetBody
                        color: win.txt; font.pixelSize: 14
                        wrapMode: TextArea.Wrap
                        placeholderText: "e.g. Rewrite the text as a polite, well-structured email with a greeting and sign-off."
                        placeholderTextColor: win.sub
                        background: Rectangle { radius: 9; color: win.field; border.color: win.stroke; border.width: 1 }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Btn {
                        text: "Save preset"; primary: true
                        enabled: presetName.text.trim() !== "" && presetBody.text.trim() !== ""
                        onClicked: {
                            controller.savePreset(presetName.text.trim(), presetBody.text.trim())
                            controller.setBeautifyStyle(presetName.text.trim())
                        }
                    }
                    Btn {
                        text: "Delete"
                        enabled: controller.presetNames().indexOf(presetName.text.trim()) >= 0
                        onClicked: { controller.deletePreset(presetName.text.trim()); presetName.text = ""; presetBody.text = "" }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // ---- Overlay ---------------------------------------------------
            Card {
                Layout.leftMargin: 22; Layout.rightMargin: 22
                Header { text: "Overlay" }
                RowLayout {
                    Layout.fillWidth: true
                    Field { text: "Live preview text"; Layout.fillWidth: true }
                    Switch { checked: controller.previewEnabled; onToggled: controller.previewEnabled = checked }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 22
                Item { Layout.fillWidth: true }
                Btn { text: "Close"; primary: true; onClicked: controller.setSettingsOpen(false) }
            }
        }
    }
}
