import QtQuick 2.0

Item {
    id: root
    objectName: "MyRoot"
    width: 200
    height: 100

    Row {
        id: row
        objectName: "row"

        Item { objectName: "first"; width: 10; height: 10 }
        Item { objectName: "second"; width: 10; height: 10 }
    }

    Item { objectName: "beside"; width: 10; height: 10 }
}
