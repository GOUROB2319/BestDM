import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 620
    title: "BestDM"

    SystemPalette { id: systemPalette }
    property bool dark: downloadManager.theme === "Dark" ||
                        (downloadManager.theme === "System" && systemPalette.window.hsvValue < 0.5)
    property string currentFilter: "All"
    property string searchText: ""
    property string pendingClipboardUrl: ""
    property string pendingAuthenticationId: ""
    property string pendingAuthenticationOrigin: ""
    property string pendingAuthenticationHost: ""
    property string pendingAuthenticationRealm: ""
    property string authenticationDialogError: ""
    property var pendingAuthenticationRequests: []
    property string pendingCompletionActionError: ""
    property string pendingCancelId: ""
    property var pendingCancelIds: []
    property string pendingRemoveId: ""
    property var pendingRemoveIds: []
    property bool pendingClearHistory: false
    property bool urlDropActive: false
    property string pendingDestinationId: ""
    property var selectedDownloadIds: ({})
    property var pendingValidUrls: []
    property var pendingAllValidUrls: []
    property var pendingInvalidUrls: []
    property var pendingDuplicates: []
    property var pendingCollisionUrls: []
    property string pendingCollisionFileName: ""
    property bool pendingCollisionIncludesDuplicates: false
    property int pendingCollisionConnections: 8
    property var pendingFileConflicts: []
    property int pendingConnectionCount: 8
    property var selectedDetails: ({})
    property double clockSeconds: Math.floor(Date.now() / 1000)
    property color brand: "#087cf0"
    property color aqua: "#00bda8"
    property color ink: dark ? "#f3f7fc" : "#17243a"
    property color muted: dark ? "#9aa9bd" : "#69778b"
    property color surface: dark ? "#182231" : "#ffffff"
    property color border: dark ? "#2d3b50" : "#dfe7f0"
    Component.onCompleted: {
        downloadManager.registerMainWindow(root)
        downloadProxy.setQueueFilter(downloadManager.selectedQueueName)
    }
    onClosing: function(closeEvent) {
        if (downloadManager.minimizeToTray && downloadManager.trayAvailable) {
            closeEvent.accepted = false
            root.hide()
        }
    }
    onWindowStateChanged: {
        if (windowState === Qt.WindowMinimized && downloadManager.minimizeToTray && downloadManager.trayAvailable)
            Qt.callLater(function() { root.hide() })
    }
    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: root.clockSeconds = Math.floor(Date.now() / 1000)
    }
    Shortcut {
        sequence: StandardKey.New
        onActivated: addDialog.open()
    }
    Shortcut {
        sequence: StandardKey.Find
        onActivated: {
            searchField.forceActiveFocus()
            searchField.selectAll()
        }
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: settingsDialog.open()
    }
    onCurrentFilterChanged: downloadProxy.setFilter(currentFilter)
    Connections {
        target: downloadManager
        function onClipboardUrlDetected(url) {
            root.pendingClipboardUrl = url
            clipboardPrompt.open()
        }
        function onHttpAuthenticationRequested(id, origin, host, realm) {
            downloadManager.activateMainWindow()
            var requests = root.pendingAuthenticationRequests.slice()
            requests.push({ downloadId: id, origin: origin, host: host, realm: realm })
            root.pendingAuthenticationRequests = requests
            root.showNextAuthenticationPrompt()
        }
        function onProtocolUrlReceived(url) {
            urlsField.text = urlsField.text.trim().length > 0 ? urlsField.text + "\n" + url : url
            addDialog.open()
            Qt.callLater(function() { urlsField.forceActiveFocus() })
        }
        function onPowerActionPromptRequested() { shutdownConfirmationDialog.open() }
        function onPowerActionPromptDismissed() { shutdownConfirmationDialog.close() }
        function onPostDownloadActionFailed(message) {
            root.pendingCompletionActionError = message
            if (root.visible) completionActionErrorDialog.open()
        }
        function onCountsChanged() {
            if (downloadProxy.categoryFilter.length > 0
                    && downloadManager.categoryNames.indexOf(downloadProxy.categoryFilter) < 0)
                downloadProxy.setCategoryFilter("All categories")
            var selection = Object.assign({}, root.selectedDownloadIds)
            var changed = false
            for (var id in selection) {
                if (!downloadManager.hasDownload(id)) {
                    delete selection[id]
                    changed = true
                }
            }
            if (changed) root.selectedDownloadIds = selection
        }
        function onQueueSelectionChanged() {
            downloadProxy.setQueueFilter(downloadManager.selectedQueueName)
        }
    }
    color: dark ? "#101724" : "#f5f8fc"

    function filterCount(filter) {
        return downloadManager.queueCount(downloadManager.selectedQueueName, filter)
    }
    function filterTitle(filter) {
        if (filter === "All") return "All downloads"
        return filter
    }
    function formatBytes(value) {
        if (value < 1024) return value + " B"
        if (value < 1048576) return (value / 1024).toFixed(1) + " KB"
        if (value < 1073741824) return (value / 1048576).toFixed(1) + " MB"
        return (value / 1073741824).toFixed(2) + " GB"
    }
    function queueBatch(addresses, fileName, includeDuplicates, connectionCount, collisionReviewed, overwriteExisting) {
        var urlText = addresses.join("\n")
        if (downloadManager.collisionPolicy === "Ask" && collisionReviewed !== true) {
            var conflicts = downloadManager.existingOutputConflicts(urlText, destinationField.text, fileName)
            if (conflicts.length > 0) {
                root.pendingCollisionUrls = addresses.slice()
                root.pendingCollisionFileName = fileName
                root.pendingCollisionIncludesDuplicates = includeDuplicates === true
                root.pendingCollisionConnections = connectionCount
                root.pendingFileConflicts = conflicts
                fileCollisionDialog.open()
                return
            }
        }
        if (downloadManager.addUrls(urlText, destinationField.text, fileName,
                                    includeDuplicates === true, connectionCount,
                                    overwriteExisting === true, collisionReviewed === true)) {
            urlsField.clear()
            reviewUrlsDialog.close()
            addDialog.close()
            fileCollisionDialog.close()
        }
    }
    function resolveFileCollision(overwriteExisting, acceptBatch) {
        var addresses = root.pendingCollisionUrls.slice()
        var fileName = root.pendingCollisionFileName
        var includeDuplicates = root.pendingCollisionIncludesDuplicates
        var connectionCount = root.pendingCollisionConnections
        root.pendingCollisionUrls = []
        root.pendingFileConflicts = []
        root.pendingCollisionFileName = ""
        fileCollisionDialog.close()
        if (acceptBatch !== true) return
        root.queueBatch(addresses, fileName, includeDuplicates, connectionCount, true, overwriteExisting)
    }
    function saveScheduleSettings() {
        var selectedDays = []
        for (var i = 0; i < scheduleDayRepeater.count; ++i) {
            var dayBox = scheduleDayRepeater.itemAt(i)
            if (dayBox && dayBox.checked) selectedDays.push(dayBox.modelData)
        }
        downloadManager.setScheduleSettings(scheduleEnabledBox.checked, selectedDays,
                                            scheduleStartField.text, scheduleEndField.text)
    }
    function selectedIds() {
        return Object.keys(root.selectedDownloadIds)
    }
    function clearSelection() {
        root.selectedDownloadIds = ({})
    }
    function cancelSelection(deletePartial) {
        if (root.pendingCancelIds.length > 0)
            downloadManager.cancelDownloads(root.pendingCancelIds, deletePartial)
        else if (root.pendingCancelId.length > 0)
            downloadManager.cancelDownload(root.pendingCancelId, deletePartial)
        root.pendingCancelIds = []
        root.pendingCancelId = ""
        root.clearSelection()
        cancelDialog.close()
    }
    function removeSelection(deleteFiles) {
        if (root.pendingRemoveIds.length > 0)
            downloadManager.removeDownloads(root.pendingRemoveIds, deleteFiles)
        else if (root.pendingRemoveId.length > 0)
            downloadManager.removeDownload(root.pendingRemoveId, deleteFiles)
        root.pendingRemoveIds = []
        root.pendingRemoveId = ""
        root.pendingClearHistory = false
        root.clearSelection()
        removeDialog.close()
    }
    function requestClearHistory() {
        root.pendingRemoveIds = downloadManager.historyIds(downloadManager.selectedQueueName)
        root.pendingRemoveId = ""
        root.pendingClearHistory = true
        removeDialog.open()
    }
    function showDetails(id) {
        root.selectedDetails = downloadManager.details(id)
        detailsDialog.open()
    }
    function showNextAuthenticationPrompt() {
        if (authenticationDialog.visible || pendingAuthenticationRequests.length === 0) return
        var requests = pendingAuthenticationRequests.slice()
        var next = requests.shift()
        pendingAuthenticationRequests = requests
        pendingAuthenticationId = next.downloadId
        pendingAuthenticationOrigin = next.origin
        pendingAuthenticationHost = next.host
        pendingAuthenticationRealm = next.realm
        authenticationDialogError = ""
        authenticationUsernameField.clear()
        authenticationPasswordField.clear()
        authenticationDialog.open()
        Qt.callLater(function() { authenticationUsernameField.forceActiveFocus() })
    }
    function finishAuthenticationPrompt() {
        pendingAuthenticationId = ""
        pendingAuthenticationOrigin = ""
        pendingAuthenticationHost = ""
        pendingAuthenticationRealm = ""
        authenticationUsernameField.clear()
        authenticationPasswordField.clear()
        authenticationDialog.close()
        Qt.callLater(function() { root.showNextAuthenticationPrompt() })
    }
    function submitAuthentication() {
        if (downloadManager.provideHttpCredentials(root.pendingAuthenticationId,
                                                  root.pendingAuthenticationOrigin,
                                                  authenticationUsernameField.text,
                                                  authenticationPasswordField.text)) {
            root.finishAuthenticationPrompt()
        } else {
            root.authenticationDialogError = "This download is no longer waiting for credentials."
        }
    }
    function formatTimestamp(value) {
        return value > 0 ? new Date(value * 1000).toLocaleString() : "—"
    }

    header: ToolBar {
        height: 68
        background: Rectangle { color: root.surface; border.color: root.border }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 13
            Image {
                source: "qrc:/assets/Glossy 3D Download App Icon.png"
                sourceSize: Qt.size(40, 40)
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
            }
            ColumnLayout {
                spacing: 0
                Label { text: "BestDM"; color: root.ink; font.pixelSize: 19; font.weight: Font.DemiBold }
                Label { text: "Downloads, under control"; color: root.muted; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: downloadManager.queueCount(downloadManager.selectedQueueName, "Running") + " active  ·  "
                    + downloadManager.queueCount(downloadManager.selectedQueueName, "Queued") + " queued"
                color: root.muted
                font.pixelSize: 12
            }
            ToolButton {
                text: "Settings"
                ToolTip.visible: hovered
                ToolTip.text: "Settings (Ctrl+,)"
                onClicked: settingsDialog.open()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 218
            Layout.fillHeight: true
            color: root.dark ? "#141e2b" : "#edf3fa"
            border.color: root.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 7
                Button {
                    text: "+   New downloads"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 43
                    ToolTip.visible: hovered
                    ToolTip.text: "New download (Ctrl+N)"
                    onClicked: addDialog.open()
                    background: Rectangle { radius: 9; color: root.brand }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.weight: Font.DemiBold }
                }
                Item { Layout.preferredHeight: 13 }
                Repeater {
                    model: ["All", "Active", "Queued", "Paused", "Completed", "Failed", "History"]
                    delegate: Button {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        text: root.filterTitle(modelData) + "    " + root.filterCount(modelData)
                        onClicked: root.currentFilter = modelData
                        background: Rectangle {
                            radius: 8
                            color: root.currentFilter === modelData ? (root.dark ? "#203a58" : "#d9ebff") : "transparent"
                        }
                        contentItem: Text {
                            text: parent.text
                            color: root.currentFilter === modelData ? root.brand : root.muted
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 8
                            font.pixelSize: 13
                            font.weight: root.currentFilter === modelData ? Font.DemiBold : Font.Normal
                        }
                    }
                }
                Label { text: "QUEUE"; color: root.muted; font.pixelSize: 10; font.weight: Font.DemiBold; Layout.topMargin: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: queuePicker
                        Layout.fillWidth: true
                        model: downloadManager.queueNames
                        currentIndex: Math.max(0, model.indexOf(downloadManager.selectedQueueName))
                        onActivated: downloadManager.selectQueue(currentText)
                    }
                    ToolButton {
                        text: "+"
                        ToolTip.visible: hovered
                        ToolTip.text: "Create queue"
                        onClicked: createQueueDialog.open()
                    }
                }
                Item { Layout.fillHeight: true }
                Label { text: "CONCURRENCY"; color: root.muted; font.pixelSize: 10; font.weight: Font.DemiBold }
                Label { text: downloadManager.maximumConcurrent + " simultaneous downloads"; color: root.ink; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: "TLS certificate checks enabled"; color: root.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 28
            spacing: 17
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 3
                    Label { text: root.currentFilter === "All" ? "Downloads" : root.currentFilter; color: root.ink; font.pixelSize: 27; font.weight: Font.DemiBold }
                    Label { text: "Add links, track progress, and resume safely."; color: root.muted; font.pixelSize: 13 }
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: "Clear"
                    visible: root.currentFilter === "History"
                        && downloadManager.historyIds(downloadManager.selectedQueueName).length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Clear download history"
                    onClicked: root.requestClearHistory()
                }
                TextField {
                    id: searchField
                    Layout.preferredWidth: root.currentFilter === "History" ? 145 : 235
                    placeholderText: "Search downloads (Ctrl+F)"
                    onTextChanged: {
                        root.searchText = text.toLowerCase().trim()
                        downloadProxy.setSearch(root.searchText)
                    }
                }
                Button {
                    text: "Retry failed (" + downloadManager.queueCount(downloadManager.selectedQueueName, "Failed") + ")"
                    visible: downloadManager.queueCount(downloadManager.selectedQueueName, "Failed") > 0
                    onClicked: downloadManager.retryFailedInQueue(downloadManager.selectedQueueName)
                }
                Button {
                    text: "Add URLs"
                    onClicked: addDialog.open()
                    background: Rectangle { radius: 8; color: root.brand }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.weight: Font.DemiBold; leftPadding: 12; rightPadding: 12 }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.selectedIds().length > 0
                Label { text: root.selectedIds().length + " selected"; color: root.ink; font.weight: Font.DemiBold }
                Button { text: "Pause"; onClicked: { downloadManager.pauseDownloads(root.selectedIds()); root.clearSelection() } }
                Button { text: "Resume"; onClicked: { downloadManager.resumeDownloads(root.selectedIds()); root.clearSelection() } }
                Button { text: "Cancel"; onClicked: { root.pendingCancelIds = root.selectedIds(); root.pendingCancelId = ""; cancelDialog.open() } }
                Button { text: "Move to queue"; onClicked: moveToQueueMenu.open() }
                Menu {
                    id: moveToQueueMenu
                    Repeater {
                        model: downloadManager.queueNames
                        delegate: MenuItem {
                            required property string modelData
                            text: modelData
                            onTriggered: {
                                downloadManager.moveDownloadsToQueue(root.selectedIds(), modelData)
                                root.clearSelection()
                            }
                        }
                    }
                }
                Button { text: "Remove"; onClicked: { root.pendingRemoveIds = root.selectedIds(); root.pendingRemoveId = ""; removeDialog.open() } }
                Button { text: "Clear selection"; onClicked: root.clearSelection() }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 13
                color: root.surface
                border.color: root.border
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 13
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Layout.preferredHeight: 38
                        Label { text: "FILE"; color: root.muted; font.pixelSize: 10; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        ComboBox {
                            Layout.preferredWidth: 145
                            model: downloadManager.categoryNames
                            currentIndex: Math.max(0, model.indexOf(downloadProxy.categoryFilter || "All categories"))
                            displayText: "Category: " + currentText
                            onActivated: downloadProxy.setCategoryFilter(currentText)
                        }
                        Label { text: "STATUS"; color: root.muted; font.pixelSize: 10; font.weight: Font.DemiBold; Layout.preferredWidth: 110 }
                        Label { text: "TRANSFER"; color: root.muted; font.pixelSize: 10; font.weight: Font.DemiBold; Layout.preferredWidth: 105 }
                        ComboBox {
                            Layout.preferredWidth: 125
                            model: ["Date", "Name", "State", "Size", "Progress"]
                            currentIndex: Math.max(0, model.indexOf(downloadProxy.sorting))
                            displayText: "Sort: " + currentText
                            onActivated: downloadProxy.setSorting(currentText)
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    ListView {
                        id: downloadList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: downloadProxy
                        spacing: 7
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: row
                            required property string downloadId
                            required property string url
                            required property string fileName
                            required property string category
                            required property string destination
                            required property string downloadState
                            required property double received
                            required property double total
                            required property double bytesPerSecond
                            required property double averageBytesPerSecond
                            required property double percent
                            required property double etaSeconds
                            required property string error
                            required property int retryCount
                            required property double nextRetryAt
                            width: downloadList.width
                            height: error.length > 0 ? 151 : 131
                            radius: 10
                            color: root.dark ? "#1b2838" : "#fbfdff"
                            border.color: root.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Rectangle {
                                        Layout.preferredWidth: 35
                                        Layout.preferredHeight: 35
                                        radius: 9
                                        color: root.dark ? "#173d4a" : "#ddf8f2"
                                        Text { anchors.centerIn: parent; text: "↓"; color: "#009c8d"; font.pixelSize: 21 }
                                    }
                                    CheckBox {
                                        Layout.preferredWidth: 25
                                        Layout.minimumWidth: 25
                                        checked: !!root.selectedDownloadIds[row.downloadId]
                                        Accessible.name: "Select " + row.fileName
                                        onToggled: {
                                            var next = Object.assign({}, root.selectedDownloadIds)
                                            if (checked) next[row.downloadId] = true
                                            else delete next[row.downloadId]
                                            root.selectedDownloadIds = next
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Label { text: row.fileName; color: root.ink; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Label { text: row.category + "  ·  " + row.destination; color: root.muted; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                    }
                                    Label {
                                        id: statusLabel
                                        text: row.downloadState === "Retrying"
                                            ? "Retry " + row.retryCount + "/" + downloadManager.maximumRetries + " · " + Math.max(0, row.nextRetryAt - root.clockSeconds) + "s"
                                            : row.downloadState
                                        color: row.downloadState === "Completed" ? "#009c74" : (row.downloadState === "Failed" ? "#d94b55" : root.brand)
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        Layout.preferredWidth: 100
                                        elide: Text.ElideRight
                                    }
                                    Button {
                                        text: row.downloadState === "Downloading" || row.downloadState === "Verifying" ? "Pause" : "Resume"
                                        visible: row.downloadState === "Downloading" || row.downloadState === "Verifying" || row.downloadState === "Paused" || row.downloadState === "Failed" || row.downloadState === "Queued" || (row.downloadState === "Canceled" && row.received > 0)
                                        enabled: row.downloadState !== "Queued"
                                        onClicked: row.downloadState === "Downloading" || row.downloadState === "Verifying" ? downloadManager.pauseDownload(row.downloadId) : downloadManager.resumeDownload(row.downloadId)
                                    }
                                    Button {
                                        text: "Cancel"
                                        visible: row.downloadState === "Probing" || row.downloadState === "Downloading" || row.downloadState === "Verifying" || row.downloadState === "Queued" || row.downloadState === "Waiting for schedule" || row.downloadState === "Paused" || row.downloadState === "Failed" || row.downloadState === "Retrying"
                                        onClicked: { root.pendingCancelId = row.downloadId; root.pendingCancelIds = []; cancelDialog.open() }
                                    }
                                    Button {
                                        text: "Remove"
                                        visible: true
                                        onClicked: { root.pendingRemoveId = row.downloadId; root.pendingRemoveIds = []; removeDialog.open() }
                                    }
                                    ToolButton {
                                        text: "⋮"
                                        onClicked: taskMenu.open()
                                        Menu {
                                            id: taskMenu
                                            MenuItem { text: "Open file"; enabled: row.downloadState === "Completed"; onTriggered: downloadManager.openFile(row.downloadId) }
                                            MenuItem { text: "Open destination folder"; onTriggered: downloadManager.openFolder(row.downloadId) }
                                            MenuItem {
                                                text: "Change destination folder…"
                                                enabled: downloadManager.canChangeDestination(row.downloadId)
                                                onTriggered: {
                                                    root.pendingDestinationId = row.downloadId
                                                    taskDestinationPicker.open()
                                                }
                                            }
                                            MenuSeparator {}
                                            MenuItem { text: "Copy URL"; onTriggered: downloadManager.copyUrl(row.downloadId) }
                                            MenuItem { text: "Copy destination"; onTriggered: downloadManager.copyDestination(row.downloadId) }
                                            MenuItem { text: "Details"; onTriggered: root.showDetails(row.downloadId) }
                                        }
                                    }
                                }
                                ProgressBar {
                                    id: taskProgress
                                    Layout.fillWidth: true
                                    from: 0
                                    to: row.total > 0 ? row.total : 1
                                    value: row.total > 0 ? row.received : 0
                                    indeterminate: row.total <= 0 && row.downloadState === "Downloading"
                                    visible: (row.total > 0 && row.downloadState !== "Failed" && row.downloadState !== "Canceled")
                                        || (row.total <= 0 && row.downloadState === "Downloading")
                                    background: Rectangle { implicitHeight: 6; radius: 3; color: root.dark ? "#2c3a4c" : "#e8eef5" }
                                    contentItem: Item {
                                        implicitHeight: 6
                                        Rectangle {
                                            width: taskProgress.visualPosition * parent.width
                                            height: parent.height
                                            radius: 3
                                            gradient: Gradient { GradientStop { position: 0; color: root.aqua } GradientStop { position: 1; color: root.brand } }
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: row.total > 0 ? root.formatBytes(row.received) + " of " + root.formatBytes(row.total) : root.formatBytes(row.received) + " transferred · size unknown"
                                        color: root.muted
                                        font.pixelSize: 10
                                    }
                                    Label { text: row.bytesPerSecond > 0 ? root.formatBytes(row.bytesPerSecond) + "/s" : ""; color: root.muted; font.pixelSize: 10 }
                                    Label { text: row.averageBytesPerSecond > 0 ? "avg " + root.formatBytes(row.averageBytesPerSecond) + "/s" : ""; color: root.muted; font.pixelSize: 10 }
                                    Label { text: row.percent >= 0 ? row.percent.toFixed(1) + "%" : ""; color: root.muted; font.pixelSize: 10 }
                                    Label { text: row.etaSeconds >= 0 ? row.etaSeconds + "s remaining" : ""; color: root.muted; font.pixelSize: 10 }
                                    Item { Layout.fillWidth: true }
                                }
                                Label { visible: row.error.length > 0; text: row.error; color: "#d94b55"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                        }
                        footer: Item {
                            width: downloadList.width
                            height: downloadManager.count === 0 ? 270 : 0
                            Column {
                                anchors.centerIn: parent
                                spacing: 9
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "No downloads yet"; color: root.ink; font.pixelSize: 18; font.weight: Font.DemiBold }
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Add one or more HTTP or HTTPS links to get started."; color: root.muted; font.pixelSize: 12 }
                                Button { anchors.horizontalCenter: parent.horizontalCenter; text: "Add URLs"; onClicked: addDialog.open() }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        z: 2
                        visible: downloadManager.count > 0
                            && downloadManager.matchingCount(root.currentFilter, root.searchText,
                                                              downloadManager.selectedQueueName) === 0
                        text: "No downloads match this filter or search."
                        color: root.muted
                        font.pixelSize: 13
                    }
                }
            }
            Label { text: downloadManager.lastError; visible: text.length > 0; color: "#d94b55"; font.pixelSize: 11 }
            Label { text: "Transfers stream to partial files. Existing files are never overwritten."; color: root.muted; font.pixelSize: 10 }
        }
    }

    Dialog {
        id: addDialog
        modal: true
        title: "Add downloads"
        width: 580
        anchors.centerIn: parent
        standardButtons: Dialog.Cancel
        onOpened: connectionsPerDownloadField.value = downloadManager.connectionsPerDownload
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: "Paste one HTTP or HTTPS link per line"; color: root.ink; font.weight: Font.DemiBold }
            TextArea { id: urlsField; Layout.fillWidth: true; Layout.preferredHeight: 115; placeholderText: "https://example.com/file.zip"; wrapMode: TextEdit.Wrap; selectByMouse: true }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Save to"; color: root.ink; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Button { text: "Import .txt"; onClicked: urlListFileDialog.open() }
                Button { text: "Paste clipboard"; onClicked: urlsField.paste() }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: destinationField; Layout.fillWidth: true; text: downloadManager.defaultDestination(); selectByMouse: true }
                Button { text: "Browse…"; onClicked: destinationPicker.open() }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Connections per file"; color: root.ink; Layout.fillWidth: true }
                SpinBox { id: connectionsPerDownloadField; from: 1; to: 16; value: downloadManager.connectionsPerDownload }
            }
            TextField {
                id: fileNameField
                Layout.fillWidth: true
                visible: urlsField.text.trim().length > 0 && urlsField.text.trim().indexOf("\n") < 0 && urlsField.text.trim().indexOf("\r") < 0
                placeholderText: "Filename (suggested from the link)"
                text: visible ? downloadManager.suggestedFileName(urlsField.text.trim()) : ""
                selectByMouse: true
            }
            Label { text: "Up to " + downloadManager.maximumBatchSize + " links per batch. Invalid links are reviewed before valid links start."; color: root.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: downloadManager.lastError; visible: text.length > 0; color: "#d94b55"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "Add to queue"
                    enabled: urlsField.text.trim().length > 0
                    onClicked: {
                        const inspection = downloadManager.inspectUrls(urlsField.text)
                        if (inspection.error.length > 0) return
                        root.pendingValidUrls = inspection.validUrls
                        root.pendingAllValidUrls = inspection.allValidUrls
                        root.pendingInvalidUrls = inspection.invalidUrls
                        root.pendingDuplicates = inspection.duplicates
                        root.pendingConnectionCount = connectionsPerDownloadField.value
                        if (root.pendingInvalidUrls.length > 0 || root.pendingDuplicates.length > 0) reviewUrlsDialog.open()
                        else root.queueBatch(root.pendingValidUrls, fileNameField.visible ? fileNameField.text : "", false, root.pendingConnectionCount)
                    }
                    background: Rectangle { radius: 8; color: parent.enabled ? root.brand : root.border }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.weight: Font.DemiBold; leftPadding: 12; rightPadding: 12 }
                }
            }
        }
    }

    Dialog {
        id: clipboardPrompt
        modal: true
        title: "Link copied"
        width: 500
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label { text: "A web link was copied. Would you like to review it in the Add dialog?"; color: root.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: root.pendingClipboardUrl; color: root.muted; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; selectByMouse: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "Ignore"; onClicked: { root.pendingClipboardUrl = ""; clipboardPrompt.close() } }
                Button {
                    text: "Review link"
                    onClicked: {
                        urlsField.text = root.pendingClipboardUrl
                        root.pendingClipboardUrl = ""
                        clipboardPrompt.close()
                        addDialog.open()
                        urlsField.forceActiveFocus()
                    }
                }
            }
        }
    }

    Dialog {
        id: fileCollisionDialog
        modal: true
        title: "A file with this name already exists"
        width: 500
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label {
                text: "Existing file(s): " + root.pendingFileConflicts.join(", ")
                color: root.ink
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
            Label {
                text: "Choose how to handle the conflict. Replacement occurs only after the new download completes successfully."
                color: root.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Button { text: "Keep existing and rename new"; Layout.fillWidth: true; onClicked: root.resolveFileCollision(false, true) }
            Button { text: "Replace after download"; Layout.fillWidth: true; highlighted: true; onClicked: root.resolveFileCollision(true, true) }
            Button { text: "Cancel batch"; Layout.fillWidth: true; onClicked: root.resolveFileCollision(false, false) }
        }
    }

    Dialog {
        id: authenticationDialog
        modal: true
        title: "Server sign-in required"
        width: 460
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label {
                text: "A server requires credentials for " + root.pendingAuthenticationHost
                color: root.ink
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: root.pendingAuthenticationRealm
                textFormat: Text.PlainText
                visible: text.length > 0
                color: root.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextField {
                id: authenticationUsernameField
                Layout.fillWidth: true
                placeholderText: "Username"
                maximumLength: 512
                selectByMouse: true
                onAccepted: authenticationPasswordField.forceActiveFocus()
            }
            TextField {
                id: authenticationPasswordField
                Layout.fillWidth: true
                placeholderText: "Password"
                maximumLength: 4096
                echoMode: TextInput.Password
                selectByMouse: true
                onAccepted: root.submitAuthentication()
            }
            Label {
                text: "These credentials stay in memory for this session and are not saved in the download database."
                color: root.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: root.authenticationDialogError
                visible: text.length > 0
                color: "#d94b55"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: root.finishAuthenticationPrompt() }
                Button {
                    id: submitAuthenticationButton
                    text: "Continue"
                    enabled: authenticationUsernameField.text.trim().length > 0
                    onClicked: root.submitAuthentication()
                }
            }
        }
    }

    FolderDialog {
        id: destinationPicker
        title: "Choose download folder"
        onAccepted: {
            const chosen = selectedFolder.toLocalFile()
            if (chosen.length > 0) destinationField.text = chosen
        }
    }

    FileDialog {
        id: urlListFileDialog
        title: "Import URLs from a text file"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Text files (*.txt)"]
        onAccepted: {
            const imported = downloadManager.readUrlListFile(selectedFile)
            if (imported.length > 0) urlsField.text = imported
        }
    }

    FileDialog {
        id: postDownloadProgramPicker
        title: "Choose a post-download program"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Applications (*.exe)"]
        onAccepted: downloadManager.setPostDownloadProgram(selectedFile.toLocalFile())
    }

    FileDialog {
        id: diagnosticExportDialog
        title: "Export redacted diagnostics"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Text files (*.txt)"]
        defaultSuffix: "txt"
        onAccepted: diagnosticLogger.exportToFile(selectedFile)
    }

    FolderDialog {
        id: defaultDestinationPicker
        title: "Choose the default download folder"
        onAccepted: {
            const chosen = selectedFolder.toLocalFile()
            if (chosen.length > 0) downloadManager.setDefaultDownloadDirectory(chosen)
        }
    }

    FolderDialog {
        id: taskDestinationPicker
        title: "Choose destination for this download"
        onAccepted: {
            const chosen = selectedFolder.toLocalFile()
            if (chosen.length > 0)
                downloadManager.setDownloadDestination(root.pendingDestinationId, chosen)
            root.pendingDestinationId = ""
        }
        onRejected: root.pendingDestinationId = ""
    }

    Dialog {
        id: reviewUrlsDialog
        modal: true
        title: "Review invalid links"
        width: 560
        anchors.centerIn: parent
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label {
                text: root.pendingValidUrls.length + " unique link(s) can be added. Review invalid or duplicate links below."
                color: root.ink
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                readOnly: true
                wrapMode: TextEdit.Wrap
                text: root.pendingInvalidUrls.concat(root.pendingDuplicates)
                    .map(function(item) { return item.url + "\n" + item.reason }).join("\n\n")
                selectByMouse: true
            }
            TextField {
                id: reviewFilenameField
                Layout.fillWidth: true
                visible: root.pendingValidUrls.length === 1
                placeholderText: "Filename (suggested from the valid link)"
                text: visible ? downloadManager.suggestedFileName(root.pendingValidUrls[0]) : ""
                selectByMouse: true
            }
            Label {
                text: downloadManager.lastError
                visible: text.length > 0
                color: "#d94b55"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "Back to edit"; onClicked: reviewUrlsDialog.close() }
                Button {
                    text: "Add unique links"
                    enabled: root.pendingValidUrls.length > 0
                    onClicked: root.queueBatch(root.pendingValidUrls, reviewFilenameField.visible ? reviewFilenameField.text : "", false, root.pendingConnectionCount)
                    background: Rectangle { radius: 8; color: parent.enabled ? root.brand : root.border }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.weight: Font.DemiBold; leftPadding: 12; rightPadding: 12 }
                }
                Button {
                    text: "Add all " + root.pendingAllValidUrls.length + " links"
                    visible: root.pendingDuplicates.length > 0
                    enabled: root.pendingAllValidUrls.length > 0
                    onClicked: root.queueBatch(root.pendingAllValidUrls, "", true, root.pendingConnectionCount)
                    background: Rectangle { radius: 8; color: parent.enabled ? root.brand : root.border }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.weight: Font.DemiBold; leftPadding: 12; rightPadding: 12 }
                }
            }
        }
    }

    Dialog {
        id: settingsDialog
        modal: true
        title: "Settings"
        width: 500
        height: 720
        anchors.centerIn: parent
        standardButtons: Dialog.Close
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ScrollView {
            anchors.fill: parent
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
            width: parent.width
            spacing: 11
            Label { text: "Appearance"; color: root.ink; font.weight: Font.DemiBold }
            CheckBox {
                id: protocolRegistrationBox
                visible: downloadManager.protocolRegistrationAvailable
                text: "Register bestdm:// links with BestDM"
                checked: downloadManager.protocolRegistered
                onToggled: {
                    if (!downloadManager.setProtocolRegistered(checked))
                        checked = downloadManager.protocolRegistered
                }
            }
            Label {
                visible: downloadManager.protocolRegistrationAvailable
                text: "Opening a BestDM link fills the Add URLs dialog. Review it and click Add before a download starts."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: downloadManager.lastError.length > 0
                text: downloadManager.lastError
                color: "#d94b55"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ComboBox {
                Layout.fillWidth: true
                model: ["System", "Light", "Dark"]
                currentIndex: Math.max(0, model.indexOf(downloadManager.theme))
                onActivated: downloadManager.setTheme(currentText)
            }
            Label { text: "After a download completes"; color: root.ink; font.weight: Font.DemiBold }
            ComboBox {
                id: completionActionCombo
                Layout.fillWidth: true
                model: ["None", "Open file", "Open folder", "Run selected program (wait up to 2 min)"]
                currentIndex: downloadManager.postDownloadAction === "Run program" ? 3
                    : Math.max(0, model.indexOf(downloadManager.postDownloadAction))
                onActivated: downloadManager.setPostDownloadAction(index === 3 ? "Run program" : currentText)
                delegate: ItemDelegate {
                    required property int index
                    required property string modelData
                    width: parent.width
                    text: modelData
                    enabled: index !== 3 || downloadManager.postDownloadProgram.length > 0
                    highlighted: completionActionCombo.highlightedIndex === index
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    text: downloadManager.postDownloadProgram
                    placeholderText: "Choose an executable program"
                    readOnly: true
                    selectByMouse: true
                }
                Button { text: "Browse…"; onClicked: postDownloadProgramPicker.open() }
            }
            Label {
                text: "Choose a program to enable Run selected program. BestDM passes the completed file path as one argument, without a shell, and stops it after two minutes."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            CheckBox {
                visible: downloadManager.defenderScanAvailable
                text: "Scan each completed file with Microsoft Defender"
                checked: downloadManager.defenderScanEnabled
                onToggled: downloadManager.setDefenderScanEnabled(checked)
            }
            Label {
                visible: !downloadManager.defenderScanAvailable
                text: "Microsoft Defender command-line scanning is unavailable on this system."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: downloadManager.defenderScanEnabled
                text: "BestDM scans locally before opening the file, folder, or selected program. Defender may require BestDM to run as administrator; a scan error prevents that action."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: "After the queue finishes"
                color: root.ink
                font.weight: Font.DemiBold
            }
            ComboBox {
                visible: downloadManager.powerActionAvailable
                Layout.fillWidth: true
                model: ["None", "Shutdown", "Sleep", "Hibernate"]
                currentIndex: Math.max(0, model.indexOf(downloadManager.queuePowerAction))
                onActivated: downloadManager.setQueuePowerAction(currentText)
            }
            Label {
                visible: !downloadManager.powerActionAvailable
                text: "Windows queue power actions are unavailable on this platform or system."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            CheckBox {
                text: "Show download completion and failure notifications"
                checked: downloadManager.notificationsEnabled
                onToggled: downloadManager.setNotificationsEnabled(checked)
            }
            CheckBox {
                text: "Minimize or close BestDM to the system tray"
                enabled: downloadManager.trayAvailable
                checked: downloadManager.minimizeToTray
                onToggled: downloadManager.setMinimizeToTray(checked)
            }
            Label {
                visible: !downloadManager.trayAvailable
                text: "System tray is unavailable; BestDM will close normally."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label { text: "Default download folder"; color: root.ink; font.weight: Font.DemiBold }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    text: downloadManager.defaultDownloadDirectory
                    readOnly: true
                    selectByMouse: true
                }
                Button { text: "Browse…"; onClicked: defaultDestinationPicker.open() }
            }
            Label { text: "Maximum simultaneous downloads"; color: root.ink; font.weight: Font.DemiBold }
            SpinBox {
                from: 1
                to: 16
                value: downloadManager.maximumConcurrent
                onValueModified: downloadManager.setMaximumConcurrent(value)
            }
            Label { text: "Maximum URLs per batch (1–500)"; color: root.ink; font.weight: Font.DemiBold }
            SpinBox {
                from: 1
                to: 500
                value: downloadManager.maximumBatchSize
                onValueModified: downloadManager.setMaximumBatchSize(value)
            }
            Label { text: "When a filename already exists"; color: root.ink; font.weight: Font.DemiBold }
            ComboBox {
                Layout.fillWidth: true
                model: ["Rename", "Ask", "Overwrite"]
                currentIndex: Math.max(0, model.indexOf(downloadManager.collisionPolicy))
                onActivated: downloadManager.setCollisionPolicy(currentText)
            }
            Label {
                text: downloadManager.collisionPolicy === "Overwrite"
                    ? "An unmanaged file is replaced atomically only after the new download completes successfully. BestDM task and history paths stay reserved."
                    : downloadManager.collisionPolicy === "Ask"
                        ? "For conflicts known before queueing, choose to keep and rename the new download, replace after success, or cancel. A conflicting server filename later gets a safe suffix. BestDM task and history paths stay reserved."
                        : "BestDM keeps existing files and adds a numbered suffix to the new download."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label { text: "Default connections per file"; color: root.ink; font.weight: Font.DemiBold }
            SpinBox {
                from: 1
                to: 16
                value: downloadManager.connectionsPerDownload
                onValueModified: downloadManager.setConnectionsPerDownload(value)
            }
            Label {
                text: "Multiple connections are used only when the server confirms byte ranges, reports a validator, and the file size is known; otherwise BestDM uses one stream."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label { text: "Automatic retry attempts"; color: root.ink; font.weight: Font.DemiBold }
            SpinBox {
                from: 0
                to: 10
                value: downloadManager.maximumRetries
                onValueModified: downloadManager.setMaximumRetries(value)
            }
            Label { text: "Global bandwidth limit (KiB/s; 0 = no limit)"; color: root.ink; font.weight: Font.DemiBold }
            SpinBox {
                from: 0
                to: 1048576
                stepSize: 64
                value: Math.round(downloadManager.speedLimitBytesPerSecond / 1024)
                onValueModified: downloadManager.setSpeedLimitKiBPerSecond(value)
            }
            Label {
                text: downloadManager.speedLimitBytesPerSecond > 0
                    ? "Shared across all active downloads: " + root.formatBytes(downloadManager.speedLimitBytesPerSecond) + "/s"
                    : "No global bandwidth limit"
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label { text: "Category rules for new downloads"; color: root.ink; font.weight: Font.DemiBold }
            CheckBox {
                text: "Create a subfolder for each category"
                checked: downloadManager.automaticCategoryFolders
                onToggled: downloadManager.setAutomaticCategoryFolders(checked)
            }
            CheckBox {
                text: "Offer to add HTTP/HTTPS links copied to clipboard"
                checked: downloadManager.clipboardMonitoringEnabled
                onToggled: downloadManager.setClipboardMonitoringEnabled(checked)
            }
            Label {
                text: "When enabled, BestDM checks clipboard text locally and asks before opening the Add dialog. It never queues clipboard links automatically."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: "Uses the category label as a folder inside the destination chosen for each batch."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label { text: "One rule per line: ext: zip = Archives or host: example.com = Vendor. First matching rule wins."; color: root.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            TextArea {
                id: categoryRulesEditor
                Layout.fillWidth: true
                Layout.preferredHeight: 125
                text: downloadManager.categoryRulesText
                placeholderText: "ext: pdf = Documents\nhost: example.com = Vendor"
                wrapMode: TextEdit.NoWrap
                selectByMouse: true
                background: Rectangle { radius: 7; color: root.dark ? "#111b28" : "#f8fafc"; border.color: root.border }
            }
            Label { text: downloadManager.lastError; visible: text.length > 0; color: "#d94b55"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Button {
                text: "Save category rules"
                onClicked: downloadManager.setCategoryRules(categoryRulesEditor.text)
            }
            Label { text: "Download schedule"; color: root.ink; font.weight: Font.DemiBold }
            CheckBox { id: scheduleEnabledBox; text: "Only download during selected times"; checked: downloadManager.scheduleEnabled }
            Label { text: "Times use your computer’s local time. End time is exclusive; overnight windows continue into the following day. Equal start and end times allow the full selected day."; color: root.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    id: scheduleDayRepeater
                    model: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
                    CheckBox {
                        required property string modelData
                        text: modelData
                        checked: downloadManager.scheduleDays.indexOf(modelData) >= 0
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "From"; color: root.ink }
                TextField { id: scheduleStartField; Layout.fillWidth: true; text: downloadManager.scheduleStartTime; placeholderText: "09:00"; validator: RegularExpressionValidator { regularExpression: /^([01][0-9]|2[0-3]):[0-5][0-9]$/ } }
                Label { text: "Until"; color: root.ink }
                TextField { id: scheduleEndField; Layout.fillWidth: true; text: downloadManager.scheduleEndTime; placeholderText: "17:00"; validator: RegularExpressionValidator { regularExpression: /^([01][0-9]|2[0-3]):[0-5][0-9]$/ } }
            }
            Button { text: "Save schedule"; onClicked: root.saveScheduleSettings() }
            Label { text: "Network proxy"; color: root.ink; font.weight: Font.DemiBold }
            ComboBox {
                id: proxyModeBox
                Layout.fillWidth: true
                model: ["Direct", "System", "Manual"]
                currentIndex: Math.max(0, model.indexOf(downloadManager.proxyMode))
            }
            ComboBox {
                id: proxyTypeBox
                Layout.fillWidth: true
                visible: proxyModeBox.currentText === "Manual"
                model: ["HTTP", "SOCKS5"]
                currentIndex: Math.max(0, model.indexOf(downloadManager.proxyType))
            }
            RowLayout {
                Layout.fillWidth: true
                visible: proxyModeBox.currentText === "Manual"
                TextField {
                    id: proxyHostField
                    Layout.fillWidth: true
                    text: downloadManager.proxyHost
                    placeholderText: "Proxy host"
                    selectByMouse: true
                }
                SpinBox {
                    id: proxyPortBox
                    from: 1
                    to: 65535
                    value: downloadManager.proxyPort
                }
            }
            TextField {
                id: proxyUsernameField
                Layout.fillWidth: true
                visible: proxyModeBox.currentText === "Manual"
                text: downloadManager.proxyUsername
                placeholderText: "Proxy username (optional)"
                selectByMouse: true
            }
            TextField {
                id: proxyPasswordField
                Layout.fillWidth: true
                visible: proxyModeBox.currentText === "Manual"
                enabled: downloadManager.secureProxyStorageAvailable
                placeholderText: downloadManager.proxyPasswordSaved ? "Saved password (leave blank to keep)" : "Proxy password (optional)"
                echoMode: TextInput.Password
                selectByMouse: true
            }
            CheckBox {
                id: clearProxyPassword
                visible: proxyModeBox.currentText === "Manual" && downloadManager.secureProxyStorageAvailable
                enabled: downloadManager.proxyPasswordSaved && proxyPasswordField.text.length === 0
                text: "Clear saved proxy password"
            }
            Label {
                text: clearProxyPassword.checked
                    ? "Qt may keep credentials already used in this session until BestDM restarts."
                    : (downloadManager.secureProxyStorageAvailable
                        ? (downloadManager.proxyPasswordSaved ? "Manual proxy password is stored in Windows Credential Manager. System proxy credentials are not imported." : "Manual proxy passwords are stored in Windows Credential Manager. System proxy credentials are not imported.")
                    : "This platform has no configured secure proxy credential store; password authentication is unavailable."
                    )
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Button {
                text: "Save proxy settings"
                onClicked: {
                    if (downloadManager.setProxySettings(proxyModeBox.currentText, proxyTypeBox.currentText,
                            proxyHostField.text, proxyPortBox.value, proxyUsernameField.text,
                            proxyPasswordField.text, clearProxyPassword.checked)) {
                        proxyPasswordField.clear()
                        clearProxyPassword.checked = false
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: proxyTestUrlField
                    Layout.fillWidth: true
                    text: "https://example.com/"
                    placeholderText: "HTTP/HTTPS test URL"
                    selectByMouse: true
                }
                Button {
                    text: downloadManager.proxyTestRunning ? "Testing…" : "Test"
                    enabled: !downloadManager.proxyTestRunning
                    onClicked: downloadManager.testProxyConnection(proxyTestUrlField.text)
                }
            }
            Label {
                text: downloadManager.proxyTestMessage
                visible: text.length > 0
                color: downloadManager.proxyTestMessage.indexOf("succeeded") >= 0 ? root.aqua : root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label { text: "Diagnostics"; color: root.ink; font.weight: Font.DemiBold }
            Label {
                text: "Exports redacted application logs only. Download URLs, query values, credentials, and the home path are removed. No download database or files are included."
                color: root.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Button {
                text: "Export redacted logs…"
                Layout.fillWidth: true
                onClicked: diagnosticExportDialog.open()
            }
            Label {
                text: diagnosticLogger.exportStatus
                visible: text.length > 0
                color: text.indexOf("Exported") === 0 ? root.aqua : "#d94b55"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label { text: downloadManager.lastError; visible: text.length > 0; color: "#d94b55"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: "HTTP and HTTPS downloads are supported. FTP/SFTP and media tools are not available yet."; color: root.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
    }

    Dialog {
        id: createQueueDialog
        modal: true
        title: "Create download queue"
        width: 400
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label {
                text: "Create a named queue for organizing downloads. All queues share the configured global concurrency limit."
                color: root.ink
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextField {
                id: queueNameInput
                Layout.fillWidth: true
                placeholderText: "Queue name"
                maximumLength: 40
                selectByMouse: true
                onAccepted: {
                    if (downloadManager.createQueue(text)) {
                        clear()
                        createQueueDialog.close()
                    }
                }
            }
            Label {
                text: downloadManager.lastError
                visible: text.length > 0
                color: "#d94b55"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: createQueueDialog.close() }
                Button {
                    text: "Create queue"
                    enabled: queueNameInput.text.trim().length > 0
                    onClicked: {
                        if (downloadManager.createQueue(queueNameInput.text)) {
                            queueNameInput.clear()
                            createQueueDialog.close()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: removeDialog
        modal: true
        title: root.pendingClearHistory ? "Clear download history" : "Remove download"
        width: 460
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        onClosed: root.pendingClearHistory = false
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 14
            Label {
                Layout.fillWidth: true
                text: root.pendingClearHistory
                    ? "Clear " + root.pendingRemoveIds.length + " history entries. Choose whether to keep downloaded and partial files or delete them too."
                    : (root.pendingRemoveIds.length > 1
                        ? "Remove " + root.pendingRemoveIds.length + " selected tasks. You can keep their files or delete completed and partial files too."
                        : "Remove this task from BestDM. You can keep its files or delete its completed and partial files too.")
                color: root.ink
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: root.pendingClearHistory ? "Remove from history"
                        : (root.pendingRemoveIds.length > 1 ? "Remove selected from list" : "Remove from list")
                    onClicked: root.removeSelection(false)
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: root.pendingClearHistory ? "Clear history and delete files"
                        : (root.pendingRemoveIds.length > 1 ? "Remove selected and delete files" : "Remove and delete files")
                    onClicked: root.removeSelection(true)
                }
                Button { text: "Back"; onClicked: { root.pendingClearHistory = false; removeDialog.close() } }
            }
        }
    }

    Dialog {
        id: cancelDialog
        modal: true
        title: "Cancel download"
        width: 440
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 14
            Label {
                Layout.fillWidth: true
                text: root.pendingCancelIds.length > 1
                    ? "Cancel " + root.pendingCancelIds.length + " selected downloads. Choose whether to keep or delete their partial data."
                    : "Keep the partial data to resume when the server allows it. BestDM safely restarts if resume is unsupported. Or delete the partial data now."
                color: root.ink
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Button { text: root.pendingCancelIds.length > 1 ? "Keep partials" : "Keep partial"; onClicked: root.cancelSelection(false) }
                Item { Layout.fillWidth: true }
                Button { text: root.pendingCancelIds.length > 1 ? "Delete partials" : "Delete partial"; onClicked: root.cancelSelection(true) }
                Button { text: "Back"; onClicked: cancelDialog.close() }
            }
        }
    }

    Dialog {
        id: detailsDialog
        modal: true
        title: "Download details"
        width: 620
        anchors.centerIn: parent
        standardButtons: Dialog.Close
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label { text: "File"; color: root.muted; font.pixelSize: 11 }
            Label { text: root.selectedDetails.fileName || ""; color: root.ink; font.weight: Font.DemiBold; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label { text: "Source URL (sensitive query values redacted)"; color: root.muted; font.pixelSize: 11 }
            Label { text: root.selectedDetails.url || ""; color: root.ink; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; selectByMouse: true }
            Label { text: "Resolved URL after redirects"; color: root.muted; font.pixelSize: 11; visible: !!root.selectedDetails.resolvedUrl && root.selectedDetails.resolvedUrl !== root.selectedDetails.url }
            Label { text: root.selectedDetails.resolvedUrl || ""; color: root.ink; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; selectByMouse: true; visible: !!root.selectedDetails.resolvedUrl && root.selectedDetails.resolvedUrl !== root.selectedDetails.url }
            Label { text: "Destination"; color: root.muted; font.pixelSize: 11 }
            Label { text: root.selectedDetails.filePath || ""; color: root.ink; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; selectByMouse: true }
            Label { text: "Category"; color: root.muted; font.pixelSize: 11 }
            Label { text: root.selectedDetails.category || "Uncategorized"; color: root.ink }
            Label { text: "Queue"; color: root.muted; font.pixelSize: 11 }
            Label { text: root.selectedDetails.queueName || "Default"; color: root.ink }
            GridLayout {
                columns: 2
                columnSpacing: 18
                rowSpacing: 6
                Label { text: "State"; color: root.muted }
                Label { text: root.selectedDetails.state || ""; color: root.ink }
                Label { text: "Size"; color: root.muted }
                Label { text: root.selectedDetails.total > 0 ? root.formatBytes(root.selectedDetails.received) + " of " + root.formatBytes(root.selectedDetails.total) : root.formatBytes(root.selectedDetails.received) + " (total unknown)"; color: root.ink }
                Label { text: "Progress"; color: root.muted }
                Label { text: root.selectedDetails.percent >= 0 ? root.selectedDetails.percent.toFixed(1) + "%" : "Unknown"; color: root.ink }
                Label { text: "Average speed"; color: root.muted }
                Label { text: root.selectedDetails.averageBytesPerSecond > 0 ? root.formatBytes(root.selectedDetails.averageBytesPerSecond) + "/s" : "Not enough transfer time"; color: root.ink }
                Label { text: "Content type"; color: root.muted }
                Label { text: root.selectedDetails.contentType || "Unknown"; color: root.ink; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                Label { text: "Byte range resume"; color: root.muted }
                Label { text: root.selectedDetails.rangeSupported ? "Supported by the server" : "Not confirmed"; color: root.ink }
                Label { text: "Retries"; color: root.muted }
                Label { text: root.selectedDetails.retryCount || 0; color: root.ink }
                Label { text: "Requested connections"; color: root.muted }
                Label { text: root.selectedDetails.connectionCount || 1; color: root.ink }
                Label { text: "Next retry"; color: root.muted; visible: root.selectedDetails.nextRetryAt > 0 }
                Label { text: root.formatTimestamp(root.selectedDetails.nextRetryAt); color: root.ink; visible: root.selectedDetails.nextRetryAt > 0 }
                Label { text: "Added"; color: root.muted }
                Label { text: root.formatTimestamp(root.selectedDetails.createdAt); color: root.ink }
                Label { text: "Completed"; color: root.muted }
                Label { text: root.formatTimestamp(root.selectedDetails.completedAt); color: root.ink }
            }
            Label {
                text: root.selectedDetails.error || ""
                visible: text.length > 0
                color: "#d94b55"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    Dialog {
        id: shutdownConfirmationDialog
        modal: true
        title: "Downloads finished"
        width: 460
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: downloadManager.shutdownCountdownSeconds > 0
                    ? "Windows will " + (downloadManager.queuePowerAction === "Shutdown" ? "shut down" : downloadManager.queuePowerAction.toLowerCase()) + " in "
                        + downloadManager.shutdownCountdownSeconds + " seconds. Save any open work."
                    : "All downloads have finished. Start a 60-second countdown before Windows "
                        + (downloadManager.queuePowerAction === "Shutdown" ? "shuts down" : downloadManager.queuePowerAction === "Sleep" ? "sleeps" : "hibernates") + "?"
                color: root.ink
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: { downloadManager.cancelPowerAction(); shutdownConfirmationDialog.close() } }
                Button {
                    visible: downloadManager.shutdownCountdownSeconds === 0
                    text: "Start 60-second countdown"
                    onClicked: downloadManager.startPowerActionCountdown()
                }
            }
        }
    }

    Dialog {
        id: completionActionErrorDialog
        modal: true
        title: "Microsoft Defender scan failed"
        width: 480
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { radius: 13; color: root.surface; border.color: root.border }
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: root.pendingCompletionActionError + " The download itself completed, but its configured post-download action was not run. Review the scan error; you can open the file manually from its task menu."
                color: root.ink
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "Close"; onClicked: completionActionErrorDialog.close() }
            }
        }
    }

    DropArea {
        anchors.fill: parent
        z: 50
        onEntered: function(drag) { root.urlDropActive = drag.hasUrls }
        onExited: root.urlDropActive = false
        onDropped: function(drop) {
            root.urlDropActive = false
            if (!drop.urls || drop.urls.length === 0) return
            var droppedUrls = []
            for (var i = 0; i < drop.urls.length; ++i)
                droppedUrls.push(String(drop.urls[i]))
            urlsField.text = droppedUrls.join("\n")
            addDialog.open()
            Qt.callLater(function() { urlsField.forceActiveFocus() })
            drop.acceptProposedAction()
        }
    }
    Rectangle {
        anchors.fill: parent
        z: 51
        visible: root.urlDropActive
        color: root.dark ? "#d91b2838" : "#d9087cf0"
        border.color: "#087cf0"
        border.width: 3
        Label {
            anchors.centerIn: parent
            text: "Drop links to review them in BestDM"
            color: "white"
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
    }
}
