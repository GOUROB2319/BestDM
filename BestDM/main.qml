import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true; width: 1120; height: 720; minimumWidth: 900; minimumHeight: 600
    title: "BestDM"
    property bool dark: false
    property color brand: "#087cf0"; property color aqua: "#00c9b0"
    property color ink: dark ? "#f3f7fc" : "#17243a"
    property color muted: dark ? "#a2b0c4" : "#69778b"
    property color surface: dark ? "#182231" : "#ffffff"
    property color border: dark ? "#2d3b50" : "#dfe7f0"
    color: dark ? "#101724" : "#f5f8fc"
    header: ToolBar { height: 68; background: Rectangle { color: root.surface; border.color: root.border }
        RowLayout { anchors.fill: parent; anchors.leftMargin: 26; anchors.rightMargin: 26
            Image { source: "qrc:/assets/Glossy 3D Download App Icon.png"; sourceSize: Qt.size(38,38); Layout.preferredWidth: 38; Layout.preferredHeight: 38 }
            ColumnLayout { spacing: 0; Label { text: "BestDM"; color: root.ink; font.pixelSize: 19; font.weight: Font.DemiBold }; Label { text: "Reliable downloads, beautifully managed"; color: root.muted; font.pixelSize: 11 } }
            Item { Layout.fillWidth: true }
            ToolButton { text: root.dark ? "☀" : "◐"; font.pixelSize: 20; onClicked: root.dark = !root.dark }
        }
    }
    RowLayout { anchors.fill: parent; spacing: 0
        Rectangle { Layout.preferredWidth: 220; Layout.fillHeight: true; color: root.dark ? "#141e2b" : "#edf3fa"; border.color: root.border
            Column { anchors.fill: parent; anchors.margins: 16; spacing: 8
                Button { text: "+  New download"; width: parent.width; height: 42; onClicked: addDialog.open(); background: Rectangle { radius: 10; color: root.brand }; contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.weight: Font.DemiBold } }
                Item { height: 14; width: 1 }
                Repeater { model: ["▣   Downloads", "↻   Active", "✓   Completed", "◷   History"]; delegate: Rectangle { width: 188; height: 42; radius: 9; color: index === 0 ? (root.dark ? "#203a58" : "#d9ebff") : "transparent"; Text { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 13; text: modelData; color: index === 0 ? root.brand : root.muted; font.pixelSize: 14; font.weight: index === 0 ? Font.DemiBold : Font.Normal } } }
                Item { height: 18; width: 1 }; Label { text: "QUEUES"; color: root.muted; font.pixelSize: 11; font.weight: Font.DemiBold }; Label { text: "   General"; color: root.ink; font.pixelSize: 14 }
            }
        }
        Item { Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout { anchors.fill: parent; anchors.margins: 34; spacing: 20
                RowLayout { Layout.fillWidth: true; ColumnLayout { spacing: 4; Label { text: "Downloads"; color: root.ink; font.pixelSize: 28; font.weight: Font.DemiBold }; Label { text: downloadManager.active ? "Your download is safely being written to disk." : "Add a link to begin a secure download."; color: root.muted; font.pixelSize: 14 } }; Item { Layout.fillWidth: true }; Button { text: "Add URL"; onClicked: addDialog.open(); background: Rectangle { radius: 9; color: root.brand }; contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.weight: Font.DemiBold } } }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: downloadManager.url === "" ? 300 : 210; radius: 16; color: root.surface; border.color: root.border
                    ColumnLayout { anchors.fill: parent; anchors.margins: 25; spacing: 12; visible: downloadManager.url !== ""
                        RowLayout { Layout.fillWidth: true; Rectangle { width: 46; height: 46; radius: 12; color: "#ddf8f2"; Text { anchors.centerIn: parent; text: "↓"; font.pixelSize: 27; color: "#009c8d" } }; ColumnLayout { Layout.fillWidth: true; spacing: 3; Label { text: downloadManager.fileName; color: root.ink; font.pixelSize: 16; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }; Label { text: downloadManager.destination; color: root.muted; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true } }; Label { text: downloadManager.state; color: downloadManager.state === "Completed" ? "#009c74" : root.brand; font.weight: Font.DemiBold } }
                        ProgressBar { Layout.fillWidth: true; from: 0; to: downloadManager.total > 0 ? downloadManager.total : 1; value: downloadManager.received; background: Rectangle { implicitHeight: 8; radius: 4; color: root.dark ? "#2c3a4c" : "#e8eef5" }; contentItem: Item { implicitHeight: 8; Rectangle { width: parent.visualPosition * parent.width; height: parent.height; radius: 4; gradient: Gradient { GradientStop { position: 0; color: root.aqua } GradientStop { position: 1; color: root.brand } } } } }
                        RowLayout { Layout.fillWidth: true; Label { text: downloadManager.total > 0 ? (Math.round(downloadManager.received / 104857.6) / 10) + " MB of " + (Math.round(downloadManager.total / 104857.6) / 10) + " MB" : (downloadManager.received > 0 ? (Math.round(downloadManager.received / 104857.6) / 10) + " MB downloaded" : "Waiting for server response"); color: root.muted; font.pixelSize: 12 }; Label { visible: downloadManager.bytesPerSecond > 0; text: (Math.round(downloadManager.bytesPerSecond / 102.4) / 10) + " MB/s" + (downloadManager.etaSeconds >= 0 ? " · " + downloadManager.etaSeconds + "s left" : ""); color: root.muted; font.pixelSize: 12 }; Item { Layout.fillWidth: true }; Button { text: downloadManager.active ? "Pause" : "Resume"; visible: downloadManager.state !== "Completed"; onClicked: downloadManager.active ? downloadManager.pause() : downloadManager.resume() }; Button { text: "Cancel"; visible: downloadManager.state !== "Completed"; onClicked: downloadManager.cancel() } }
                        Label { visible: downloadManager.error !== ""; text: downloadManager.error; color: "#d94b55"; font.pixelSize: 12 }
                    }
                    Column { anchors.centerIn: parent; visible: downloadManager.url === ""; spacing: 12; Text { anchors.horizontalCenter: parent.horizontalCenter; text: "↓"; color: root.brand; font.pixelSize: 54 }; Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Your download list is clear"; color: root.ink; font.pixelSize: 18; font.weight: Font.DemiBold }; Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Paste a link and BestDM will take care of the rest."; color: root.muted; font.pixelSize: 13 } }
                }
                Item { Layout.fillHeight: true }; Label { text: "Secure by default  ·  TLS certificate verification enabled  ·  Partial files stay separate"; color: root.muted; font.pixelSize: 11 }
            }
        }
    }
    Dialog { id: addDialog; modal: true; title: "New download"; width: 550; anchors.centerIn: parent; standardButtons: Dialog.Cancel; background: Rectangle { radius: 15; color: root.surface; border.color: root.border }
        ColumnLayout { width: parent.width; spacing: 12; Label { text: "Download link"; color: root.ink; font.weight: Font.DemiBold }; TextField { id: urlField; Layout.fillWidth: true; placeholderText: "https://example.com/file.zip"; selectByMouse: true }; Label { text: "Save to"; color: root.ink; font.weight: Font.DemiBold }; TextField { id: destinationField; Layout.fillWidth: true; text: downloadManager.defaultDestination(); selectByMouse: true }; Label { text: "BestDM resumes incomplete files using a separate .part file."; color: root.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }; Button { Layout.alignment: Qt.AlignRight; text: "Start download"; enabled: urlField.text.length > 0; onClicked: { downloadManager.start(urlField.text, destinationField.text); addDialog.close(); }; background: Rectangle { radius: 8; color: parent.enabled ? root.brand : root.border }; contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.weight: Font.DemiBold } } }
    }
}
