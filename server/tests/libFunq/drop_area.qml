import QtQuick 2.0

Item {
    id: root
    width: 200
    height: 200
    property int entered: 0
    property int dropCount: 0
    property string droppedText: ""
    property var droppedFormats: []

    DropArea {
        anchors.fill: parent
        onEntered: root.entered += 1
        onDropped: (drop) => {
            root.dropCount += 1
            root.droppedFormats = drop.formats
            root.droppedText = drop.getDataAsString("application/x-funq-test-rows")
            drop.accept()
        }
    }
}
