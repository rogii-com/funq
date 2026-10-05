/*
Copyright: SCLE SFE
Contributor: Julien Pagès <j.parkouss@gmail.com>

This software is a computer program whose purpose is to test graphical
applications written with the QT framework (http://qt.digia.com/).

This software is governed by the CeCILL v2.1 license under French law and
abiding by the rules of distribution of free software.  You can  use,
modify and/ or redistribute the software under the terms of the CeCILL
license as circulated by CEA, CNRS and INRIA at the following URL
"http://www.cecill.info".

As a counterpart to the access to the source code and  rights to copy,
modify and redistribute granted by the license, users are provided only
with a limited warranty  and the software's author,  the holder of the
economic rights,  and the successive licensors  have only  limited
liability.

In this respect, the user's attention is drawn to the risks associated
with loading,  using,  modifying and/or developing or reproducing the
software by the user in light of its specific status of free software,
that may mean  that it is complicated to manipulate,  and  that  also
therefore means  that it is reserved for developers  and  experienced
professionals having in-depth computer knowledge. Users are therefore
encouraged to load and test the software's suitability as regards their
requirements in conditions enabling the security of their systems and/or
data to be ensured and,  more generally, to use and operate it in the
same conditions as regards security.

The fact that you are presently reading this means that you have had
knowledge of the CeCILL v2.1 license and that you accept its terms.
*/

#include <QApplication>
#include <QBuffer>
#include <QDialog>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyleHints>
#include <QTabBar>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QtTest/QtTest>

#ifdef QT_QUICK_LIB
#include <QQuickItem>
#include <QQuickView>
#endif

#ifdef QT_QUICKWIDGETS_LIB
#include <QQuickWidget>
#endif

#include "funq.h"
#include "objectpath.h"
#include "player.h"
#include "shortcutresponse.h"

class TestDragNDropWidget : public QWidget {
public:
    explicit TestDragNDropWidget(QWidget * parent = NULL) : QWidget(parent) {
        m_lineEditDrag = new QLineEdit;
        m_lineEditDrop = new QLineEdit;

        QHBoxLayout * layout = new QHBoxLayout;

        layout->addWidget(m_lineEditDrag);
        layout->addWidget(m_lineEditDrop);

        setLayout(layout);

        m_lineEditDrag->setDragEnabled(true);

        m_lineEditDrag->setObjectName("drag");
        m_lineEditDrop->setObjectName("drop");
        setObjectName("testdnd");
    }

    QLineEdit * m_lineEditDrag;
    QLineEdit * m_lineEditDrop;
};

/**
 * Closes the popup a test opened, and remembers whether a command was still
 * waiting for its answer when the popup had to be closed.
 *
 * A menu shown by exec() runs a nested event loop. Were a command to deliver
 * input into such a menu synchronously, the test would hang in that loop; with
 * this guard it fails instead.
 */
struct PopupCloser {
    QTimer timer;
    bool answered;
    bool closedWhileWaiting;

    explicit PopupCloser(int interval = 300)
        : answered(false), closedWhileWaiting(false) {
        timer.setInterval(interval);
        QObject::connect(&timer, &QTimer::timeout, &timer, [this]() {
            if (QWidget * popup = QApplication::activePopupWidget()) {
                closedWhileWaiting = closedWhileWaiting || !answered;
                popup->close();
            }
        });
        timer.start();
    }
};

/**
 * Takes files dropped on a widget and remembers their local paths, the way an
 * import target of the application takes files from the file manager.
 */
struct DropRecorder : public QObject {
    QStringList dropped;

    bool eventFilter(QObject * watched, QEvent * event) override {
        switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove:
            static_cast<QDropEvent *>(event)->acceptProposedAction();
            return true;
        case QEvent::Drop: {
            QDropEvent * drop = static_cast<QDropEvent *>(event);
            foreach (const QUrl & url, drop->mimeData()->urls()) {
                dropped << url.toLocalFile();
            }
            drop->acceptProposedAction();
            return true;
        }
        default:
            return QObject::eventFilter(watched, event);
        }
    }
};

static const char kRowsFormat[] = "application/x-funq-test-rows";

/**
 * Lets the drop a command queued arrive. QApplication hands a drop to the widget
 * that took the last enter, so a test that sends a second drop must not leave the
 * first one in the queue.
 */
static void let_drop_arrive() {
    QCoreApplication::processEvents();
}

static QPoint drop_pos(const QDropEvent * event) {
#if QT_VERSION_MAJOR >= 6
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

/**
 * The rows of a model as a drag of them takes them: the texts of the rows in a
 * format of the model's own. The model remembers which indexes it was asked for
 * and the mime data it handed out, to tell whether the data is deleted.
 */
class DraggableRowsModel : public QStandardItemModel {
public:
    Qt::DropActions dragActions = Qt::CopyAction | Qt::MoveAction;
    bool refusesMimeData = false;
    mutable QModelIndexList mimeIndexes;
    mutable QPointer<QMimeData> lastMime;

    Qt::DropActions supportedDragActions() const override {
        return dragActions;
    }

    QStringList mimeTypes() const override {
        return QStringList() << kRowsFormat;
    }

    QMimeData * mimeData(const QModelIndexList & indexes) const override {
        mimeIndexes = indexes;
        if (refusesMimeData) {
            return NULL;
        }
        QStringList texts;
        foreach (const QModelIndex & index, indexes) {
            texts << data(index).toString();
        }
        QMimeData * mime = new QMimeData();
        mime->setData(kRowsFormat, texts.join('\n').toUtf8());
        lastMime = mime;
        return mime;
    }
};

/**
 * Takes dropped rows the way a drop target of the application does, and writes
 * down what each drag event brought.
 *
 * takes and takesMoves make it refuse the enter or the moves. It accepts with
 * acceptProposedAction(), which gives the action Qt proposes; keepsAction makes
 * it accept with accept(), which leaves the action the event came with.
 */
struct RowsDropLog {
    bool takes = true;
    bool takesMoves = true;
    bool keepsAction = false;
    int enters = 0;
    int moves = 0;
    int leaves = 0;
    int drops = 0;
    QPoint enterPos;
    QPoint dropPos;
    QStringList droppedRows;
    QStringList droppedFormats;
    Qt::DropAction enterAction = Qt::IgnoreAction;
    Qt::DropAction droppedAction = Qt::IgnoreAction;

    void onEnter(QDragEnterEvent * event) {
        ++enters;
        enterPos = drop_pos(event);
        enterAction = event->dropAction();
        decide(event, takes && event->mimeData()->hasFormat(kRowsFormat));
    }

    void onMove(QDragMoveEvent * event) {
        ++moves;
        decide(event, takesMoves);
    }

    void onLeave() {
        ++leaves;
    }

    void onDrop(QDropEvent * event) {
        ++drops;
        dropPos = drop_pos(event);
        droppedAction = event->dropAction();
        droppedFormats = event->mimeData()->formats();
        droppedRows = QString::fromUtf8(event->mimeData()->data(kRowsFormat))
                          .split('\n');
        decide(event, true);
    }

private:
    void decide(QDropEvent * event, bool take) {
        if (!take) {
            event->ignore();
        } else if (keepsAction) {
            event->accept();
        } else {
            event->acceptProposedAction();
        }
    }
};

class RowsDropTarget : public QWidget, public RowsDropLog {
    Q_OBJECT
public:
    explicit RowsDropTarget(QWidget * parent = NULL) : QWidget(parent) {
        setAcceptDrops(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent * event) override { onEnter(event); }
    void dragMoveEvent(QDragMoveEvent * event) override { onMove(event); }
    void dragLeaveEvent(QDragLeaveEvent *) override { onLeave(); }
    void dropEvent(QDropEvent * event) override { onDrop(event); }
};

/**
 * The same, for a widget the application takes drops on through an event filter,
 * as an item view takes them on its viewport.
 */
class RowsDropFilter : public QObject, public RowsDropLog {
public:
    bool eventFilter(QObject * watched, QEvent * event) override {
        switch (event->type()) {
        case QEvent::DragEnter:
            onEnter(static_cast<QDragEnterEvent *>(event));
            return true;
        case QEvent::DragMove:
            onMove(static_cast<QDragMoveEvent *>(event));
            return true;
        case QEvent::DragLeave:
            onLeave();
            return true;
        case QEvent::Drop:
            onDrop(static_cast<QDropEvent *>(event));
            return true;
        default:
            return QObject::eventFilter(watched, event);
        }
    }
};

/**
 * A tree of rows and a widget to drop them on, in one window the commands find by
 * path: Grids with three rows under it, each of two columns.
 */
struct DropScene {
    QWidget host;
    QTreeView tree;
    RowsDropTarget target;
    DraggableRowsModel model;
    QBuffer buffer;
    Player player;

    DropScene() : tree(&host), target(&host), player(&buffer) {
        host.setObjectName("host");
        tree.setObjectName("source");
        target.setObjectName("target");
        model.setColumnCount(2);
        QStandardItem * grids = new QStandardItem("Grids");
        for (int i = 0; i < 3; ++i) {
            grids->appendRow(QList<QStandardItem *>()
                             << new QStandardItem(QString("Grid%1").arg(i))
                             << new QStandardItem(QString("extra%1").arg(i)));
        }
        model.appendRow(grids);
        tree.setModel(&model);
        tree.expandAll();
    }

    qulonglong oid_of(const QString & path) {
        QtJson::JsonObject command;
        command["path"] = path;
        return player.widget_by_path(command)["oid"].toULongLong();
    }

    QModelIndex grids() const {
        return model.index(0, 0);
    }

    static QVariantMap grid(int row, int column = 0) {
        QVariantMap item;
        item["itempath"] = "0-0";
        item["row"] = row;
        item["column"] = column;
        return item;
    }

    QtJson::JsonObject drop_command(const QVariantList & items,
                                    const QString & targetPath = "host::target") {
        QtJson::JsonObject command;
        command["oid"] = oid_of("host::source");
        command["target_oid"] = oid_of(targetPath);
        command["items"] = items;
        return command;
    }
};

/**
 * Remembers the button events an object is sent, in order.
 *
 * Installed on a window, it sees what the platform fed the window together
 * with what QGuiApplication made of it - a double click it generated follows
 * the press it came from - which a widget never sees in full: the press that
 * makes a double click reaches the widget as the double click alone.
 */
struct MouseRecorder : public QObject {
    QList<QEvent::Type> seen;

    bool eventFilter(QObject * watched, QEvent * event) override {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
            seen << event->type();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }
};

/**
 * A widget remembering which of its mouse handlers ran, in order.
 *
 * firstReleaseBusyMs keeps its first release busy that long, the way a product
 * working on a click holds up the input behind it.
 */
class MouseHandlersWidget : public QWidget {
public:
    QList<QEvent::Type> handled;
    int firstReleaseBusyMs = 0;

protected:
    void mousePressEvent(QMouseEvent *) override {
        handled << QEvent::MouseButtonPress;
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        handled << QEvent::MouseButtonRelease;
        if (firstReleaseBusyMs > 0) {
            QTest::qSleep(firstReleaseBusyMs);
            firstReleaseBusyMs = 0;
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *) override {
        handled << QEvent::MouseButtonDblClick;
    }
};

/**
 * A widget writing down the press, the moves made with a button held and the
 * release, with their positions.
 */
class DragRecorderWidget : public QWidget {
public:
    QList<QPoint> pressed;
    QList<QPoint> moved;
    QList<QPoint> released;
    Qt::MouseButtons buttonsOnMove;

protected:
    void mousePressEvent(QMouseEvent * event) override {
        pressed << event->pos();
    }
    void mouseMoveEvent(QMouseEvent * event) override {
        moved << event->pos();
        buttonsOnMove = event->buttons();
    }
    void mouseReleaseEvent(QMouseEvent * event) override {
        released << event->pos();
    }
};

/**
 * A widget writing down the clicks it gets: where, with which modifiers, and
 * how many of them came as a double click.
 */
class ClickRecorderWidget : public QWidget {
public:
    QList<QPoint> pressed;
    QList<Qt::KeyboardModifiers> modifiersOnPress;
    QList<QEvent::Type> handled;

protected:
    void mousePressEvent(QMouseEvent * event) override {
        pressed << event->pos();
        modifiersOnPress << event->modifiers();
        handled << QEvent::MouseButtonPress;
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        handled << QEvent::MouseButtonRelease;
    }
    void mouseDoubleClickEvent(QMouseEvent *) override {
        handled << QEvent::MouseButtonDblClick;
    }
};

class TestSlot : public QWidget {
    Q_OBJECT
public:
    QVariant m_variant;

public slots:
    const QVariant editVariant(const QVariant & variant) {
        m_variant = variant;
        return QVariant(123);
    }
};

class LibFunqTest : public QObject {
    Q_OBJECT
private slots:
    void test_objectPath_objectName_noname() {
        QObject obj;

        QString name = ObjectPath::objectName(&obj);
        QCOMPARE(name, QString("QObject"));
    }
    void test_objectPath_objectName_named() {
        QObject obj;
        obj.setObjectName("NAMEd");

        QString name = ObjectPath::objectName(&obj);
        QCOMPARE(name, QString("NAMEd"));
    }
    void test_objectPath_objectName_noname_with_siblings() {
        QObject parent;
        QObject obj(&parent);
        QObject obj2(&parent);

        QCOMPARE(ObjectPath::objectName(&obj), QString("QObject"));
        QCOMPARE(ObjectPath::objectName(&obj2), QString("QObject-1"));
    }
    void test_objectPath_objectName_named_with_siblings() {
        QObject parent;
        QObject obj(&parent);
        QObject obj2(&parent);

        obj.setObjectName("NAMEd");
        obj2.setObjectName("NAMEd2");

        QCOMPARE(ObjectPath::objectName(&obj), QString("NAMEd"));
        QCOMPARE(ObjectPath::objectName(&obj2), QString("NAMEd2"));
    }
    void test_objectPath_objectName_named_same_with_siblings() {
        QObject parent;
        QObject obj(&parent);
        QObject obj2(&parent);

        obj.setObjectName("NAMEd");
        obj2.setObjectName("NAMEd");

        QCOMPARE(ObjectPath::objectName(&obj), QString("NAMEd"));
        QCOMPARE(ObjectPath::objectName(&obj2), QString("NAMEd-1"));
    }
    void test_objectPath_objectPath_simple() {
        QObject parent;
        QObject obj(&parent);
        QObject obj2(&parent);

        obj.setObjectName("NAMEd");
        obj2.setObjectName("NAMEd");

        QCOMPARE(ObjectPath::objectPath(&obj), QString("QObject::NAMEd"));
        QCOMPARE(ObjectPath::objectPath(&obj2), QString("QObject::NAMEd-1"));
    }
    void test_objectPath_objectPath_simple_with_sep() {
        QObject parent;
        QObject obj(&parent);
        QObject obj2(&parent);

        obj.setObjectName("::NAMEd");
        obj2.setObjectName("NAMEd");

        QCOMPARE(ObjectPath::objectPath(&obj), QString("QObject:::_:NAMEd"));
        QCOMPARE(ObjectPath::objectPath(&obj2), QString("QObject::NAMEd"));
    }
    void test_objectPath_findObject_simple() {
        QMainWindow parent;
        QObject obj(&parent);
        QObject obj2(&parent);

        obj2.setObjectName("NAMEd");

        QCOMPARE(ObjectPath::findObject("QMainWindow::NAMEd"), &obj2);
    }
    void test_objectPath_findObject_with_sep() {
        QMainWindow parent;
        QObject obj(&parent);
        QObject obj2(&parent);

        obj2.setObjectName("::NAMEd");

        QCOMPARE(ObjectPath::findObject("QMainWindow:::_:NAMEd"), &obj2);
    }
    void test_objectPath_findObject_among_many_parentless_namesakes() {
        // окно верхнего уровня нумеруется среди одноимённых: считать номер каждому окну при
        // каждом поиске - квадрат от числа окон, а у приложения их тысячи (меню, попапы)
        QList<QWidget *> twins;
        for (int i = 0; i < 1500; ++i) {
            QWidget * twin = new QWidget();
            twin->setObjectName("Twin");
            twins << twin;
        }
        ObjectPath::registerTopLevelObjects();
        // номера раздаются в порядке, в каком funq увидел окна, поэтому путь берём у окна
        const QString path = ObjectPath::objectPath(twins.at(1200));
        QElapsedTimer timer;
        timer.start();
        QObject * found = NULL;
        for (int i = 0; i < 20; ++i) {
            found = ObjectPath::findObject(path);
        }
        const qint64 elapsed = timer.elapsed();
        QCOMPARE(found, static_cast<QObject *>(twins.at(1200)));
        foreach (int i, QList<int>() << 0 << 1 << 700 << 1499) {
            QCOMPARE(ObjectPath::findObject(ObjectPath::objectPath(twins.at(i))),
                     static_cast<QObject *>(twins.at(i)));
        }
        QVERIFY(ObjectPath::findObject("Twin-1500") == NULL);
        qDeleteAll(twins);
        QVERIFY2(elapsed < 1000, qPrintable(QString("20 lookups took %1 ms").arg(elapsed)));
    }

    void test_objectPath_findObject_keeps_a_name_that_ends_in_digits() {
        // собственное имя вида "tab-2" - это имя, а не номер соседа
        QMainWindow parent;
        QObject tab(&parent);
        tab.setObjectName("tab-2");
        QObject twin(&parent);
        twin.setObjectName("tab");
        QObject twin2(&parent);
        twin2.setObjectName("tab");
        QCOMPARE(ObjectPath::findObject("QMainWindow::tab-2"), &tab);
        QCOMPARE(ObjectPath::findObject("QMainWindow::tab-1"), &twin2);
        QCOMPARE(ObjectPath::findObject("QMainWindow::tab"), &twin);
    }

    void test_objectpath_graphicsItemId() {
        QGraphicsView view;
        QGraphicsScene scene;
        view.setScene(&scene);

        QGraphicsRectItem item;
        QGraphicsRectItem child(&item);

        scene.addItem(&item);

        QCOMPARE(ObjectPath::graphicsItemId(&item), (qulonglong)&item);
        QCOMPARE(ObjectPath::graphicsItemId(&child), (qulonglong)&child);
    }
    void test_objectpath_graphicsItemFromId() {
        QGraphicsView view;
        QGraphicsScene scene;
        view.setScene(&scene);

        QGraphicsRectItem notInScene;
        QGraphicsRectItem parent;
        QGraphicsRectItem item(&parent);
        QGraphicsRectItem item2(&parent);

        scene.addItem(&parent);

        QCOMPARE(ObjectPath::graphicsItemFromId(&view, (qulonglong)&parent),
                 &parent);
        QCOMPARE(ObjectPath::graphicsItemFromId(&view, (qulonglong)&item),
                 &item);
        QCOMPARE(ObjectPath::graphicsItemFromId(&view, (qulonglong)&item2),
                 &item2);
        QCOMPARE(ObjectPath::graphicsItemFromId(&view, (qulonglong)&notInScene),
                 (QGraphicsItem *)NULL);
    }
    /*
     *
     * TESTS for player.cpp
     *
     */
    void test_player_widget_by_path() {
        QMainWindow w;
        QObject o(&w);

        QBuffer buffer;

        Player player(&buffer);

        QtJson::JsonObject command;
        command["path"] = "QMainWindow::QObject";

        QtJson::JsonObject result = player.widget_by_path(command);

        QVERIFY(result["oid"].value<qulonglong>() != 0);
        QCOMPARE(player.registeredObject(result["oid"].value<qulonglong>()),
                 &o);
    }

    void test_player_widget_by_path_wrong_path() {
        QMainWindow w;
        QObject o(&w);

        QBuffer buffer;

        Player player(&buffer);

        QtJson::JsonObject command;
        command["path"] = "QMainWindow::QObject3569";

        QtJson::JsonObject result = player.widget_by_path(command);

        QCOMPARE(result["success"].toBool(), false);
        QCOMPARE(result["errName"].toString(), QString("InvalidWidgetPath"));
    }

    void test_player_object_properties() {
        QMainWindow w;
        QObject o(&w);
        o.setObjectName("toto");

        QBuffer buffer;

        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow::toto";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        command["oid"] = resultPath["oid"];

        QtJson::JsonObject result = player.object_properties(command);

        QCOMPARE(result["objectName"].toString(), QString("toto"));
    }

    void test_player_not_registered_object() {
        QMainWindow w;
        QObject o(&w);

        QBuffer buffer;

        Player player(&buffer);

        QtJson::JsonObject command;
        QtJson::JsonObject result = player.object_properties(command);

        QCOMPARE(result["success"].toBool(), false);
        QCOMPARE(result["errName"].toString(), QString("NotRegisteredObject"));
    }

    void test_player_deleted_object() {
        QMainWindow w;
        QObject * o = new QObject(&w);

        QBuffer buffer;

        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow::QObject";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        command["oid"] = resultPath["oid"];

        delete o;
        QtJson::JsonObject result = player.object_properties(command);

        QCOMPARE(result["success"].toBool(), false);
        QCOMPARE(result["errName"].toString(), QString("NotRegisteredObject"));
    }

    void test_player_active_widget() {
        QMainWindow w;

        w.show();
#if QT_VERSION >= 0x050000
        QVERIFY(QTest::qWaitForWindowExposed(&w));
#else
        QTest::qWaitForWindowShown(&w);
#endif
        QApplication::setActiveWindow(&w);  // required with Xvfb
        QBuffer buffer;

        Player player(&buffer);

        QtJson::JsonObject command;

        QtJson::JsonObject result = player.active_widget(command);

        QVERIFY(result["oid"].value<qulonglong>() != 0);
        QCOMPARE(player.registeredObject(result["oid"].value<qulonglong>()),
                 &w);
    }

    void test_player_object_set_properties() {
        QMainWindow w;
        QObject o(&w);
        o.setObjectName("toto");

        QBuffer buffer;

        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow::toto";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        QtJson::JsonObject properties;
        properties["objectName"] = "titi";
        command["oid"] = resultPath["oid"];
        command["properties"] = properties;

        QtJson::JsonObject result = player.object_set_properties(command);

        qApp->processEvents();

        QCOMPARE(o.objectName(), QString("titi"));
    }

    void test_player_widgets_list() {
        QMainWindow mw;
        QWidget w(&mw);

        QBuffer buffer;

        Player player(&buffer);

        QtJson::JsonObject command;
        QtJson::JsonObject result = player.widgets_list(command);

        QtJson::JsonObject mwResult = result["QMainWindow"].toMap();
        QtJson::JsonObject childrenResult = mwResult["children"].toMap();
        QVERIFY(childrenResult.contains("QWidget"));
        QCOMPARE(mwResult["classes"].toStringList(),
                 QStringList() << "QMainWindow"
                               << "QWidget"
                               << "QObject");
        QCOMPARE(childrenResult["QWidget"].toMap()["classes"].toStringList(),
                 QStringList() << "QWidget"
                               << "QObject");
    }

    void test_player_widgets_list_with_oid() {
        QMainWindow mw;
        QWidget w(&mw);

        QBuffer buffer;

        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        command["oid"] = resultPath["oid"];
        QtJson::JsonObject result = player.widgets_list(command);

        QtJson::JsonObject wResult = result["QWidget"].toMap();
        QVERIFY(wResult["children"].toMap().isEmpty());
        QCOMPARE(wResult["classes"].toStringList(),
                 QStringList() << "QWidget"
                               << "QObject");
    }

    void test_player_widgets_list_names_a_dialog_once() {
        // диалог с родителем - окно: он и среди окон приложения, и среди детей
        // родителя, и полный список отвечал его дважды, со всем содержимым, а
        // защита TooManyWidgets считала его один раз
        QMainWindow mw;
        QDialog dialog(&mw);
        dialog.setObjectName("listedOnce");
        QWidget inside(&dialog);
        inside.setObjectName("insideListedOnce");

        QBuffer buffer;
        Player player(&buffer);

        const QtJson::JsonObject listed = player.widgets_list(QtJson::JsonObject());
        QCOMPARE(count_listed(listed, ObjectPath::objectPath(&dialog)), 1);
        QCOMPARE(count_listed(listed, ObjectPath::objectPath(&inside)), 1);
        // место диалога - под родителем, как и в списке по oid родителя
        QVERIFY(listed["QMainWindow"].toMap()["children"].toMap().contains(
            "listedOnce"));
    }

    void test_player_widget_click() {
        QMainWindow mw;
        QPushButton * btn = new QPushButton("myBtn");
        mw.setCentralWidget(btn);

        QSignalSpy spy(btn, SIGNAL(clicked()));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow::QPushButton";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        command["oid"] = resultPath["oid"];
        player.widget_click(command);

        qApp->processEvents();
        QCOMPARE(spy.count(), 1);
    }

    void test_player_widget_close() {
        QMainWindow mw;
        mw.show();
#if QT_VERSION >= 0x050000
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
#else
        QTest::qWaitForWindowShown(&mw);
#endif

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        command["oid"] = resultPath["oid"];

        QCOMPARE(mw.isVisible(), true);

        player.widget_close(command);

        qApp->processEvents();
        QCOMPARE(mw.isVisible(), false);
    }

    void test_player_call_slot() {
        QMainWindow mw;
        TestSlot testslot;
        testslot.setObjectName("test_slot");
        mw.setCentralWidget(&testslot);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow::test_slot";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        command["oid"] = resultPath["oid"];
        command["slot_name"] = "editVariant";
        command["params"] = 23;

        QVariant result_slot = player.call_slot(command)["result_slot"];
        QCOMPARE(result_slot, QVariant(123));
        QCOMPARE(testslot.m_variant, QVariant(23));
    }

    void test_player_call_slot_queued_runs_after_the_answer() {
        QMainWindow mw;
        QLabel * label = new QLabel("<a href=\"x\">link</a>");
        mw.setCentralWidget(label);
        QSignalSpy spy(label, SIGNAL(linkActivated(const QString &)));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow::QLabel";
        QtJson::JsonObject command;
        command["oid"] = player.widget_by_path(commandPath)["oid"];
        command["slot_name"] = "linkActivated";
        command["params"] = QVariantList() << "x";

        QVERIFY(!player.call_slot_queued(command).contains("errName"));
        QCOMPARE(spy.count(), 0);
        qApp->processEvents();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("x"));
    }

    void test_player_widget_keyclick() {
        QMainWindow mw;
        QLineEdit * line = new QLineEdit();
        mw.setCentralWidget(line);

        QSignalSpy spy(line, SIGNAL(textEdited(const QString &)));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow::QLineEdit";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        command["oid"] = resultPath["oid"];
        command["text"] = "this is a new text";
        player.widget_keyclick(command);

        qApp->processEvents();
        QVERIFY(spy.count() > 0);
        QCOMPARE(line->text(), QString("this is a new text"));
    }

    void test_player_shortcut() {
        QMainWindow mw;
        QShortcut shortcut(Qt::Key_F2, &mw, 0, 0, Qt::ApplicationShortcut);
        mw.show();
#if QT_VERSION >= 0x050000
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
#else
        QTest::qWaitForWindowShown(&mw);
#endif
        QSignalSpy spy(&shortcut, SIGNAL(activated()));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        command["oid"] = resultPath["oid"];
        command["keysequence"] = "F2";

        DelayedResponse * dresponse = player.shortcut(command);

        dresponse->start();

        QEventLoop loop;
        QObject::connect(
            dresponse, SIGNAL(aboutToWriteResponse(const QtJson::JsonObject &)),
            &loop, SLOT(quit()));
        loop.exec();

        QCOMPARE(spy.count(), 1);
    }

    void test_player_shortcut_answers_while_the_dialog_it_opened_is_open() {
        // нажатие шло из шага ответа: сочетание, открывшее диалог с exec(), держало
        // команду до закрытия диалога, а захват клавиатуры оставлял диалог без ввода
        QMainWindow mw;
        QShortcut shortcut(QKeySequence("Ctrl+O"), &mw, 0, 0, Qt::ApplicationShortcut);
        QDialog dialog(&mw);
        QObject::connect(&shortcut, &QShortcut::activated, &dialog,
                         [&dialog]() { dialog.exec(); });
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow";
        QtJson::JsonObject command;
        command["oid"] = player.widget_by_path(commandPath)["oid"];
        command["keysequence"] = "Ctrl+O";

        bool answered = false;
        bool closedBeforeAnswer = false;
        bool dialogWasOpened = false;
        QWidget * grabberInDialog = NULL;
        QtJson::JsonObject result;
        QTimer closer;
        closer.setInterval(1500);
        QObject::connect(&closer, &QTimer::timeout, &closer, [&]() {
            if (QWidget * modal = QApplication::activeModalWidget()) {
                dialogWasOpened = true;
                closedBeforeAnswer = closedBeforeAnswer || !answered;
                grabberInDialog = QWidget::keyboardGrabber();
                modal->close();
            }
        });
        closer.start();

        DelayedResponse * dresponse = player.shortcut(command);
        QObject::connect(dresponse, &DelayedResponse::aboutToWriteResponse,
                         [&](const QtJson::JsonObject & r) {
                             answered = true;
                             result = r;
                         });
        dresponse->start();

        QTRY_VERIFY_WITH_TIMEOUT(answered && dialogWasOpened && !dialog.isVisible(), 8000);
        QVERIFY(!closedBeforeAnswer);
        QVERIFY(grabberInDialog == NULL);
        QCOMPARE(result["opened"].toString(), QString("QDialog"));
    }

    void test_player_shortcut_answers_from_a_loop_it_opened_without_a_window() {
        // нативный диалог Windows не виден ни как модальное окно, ни как попап Qt:
        // сочетание узнаёт о нём по вложенному циклу событий
        QMainWindow mw;
        QShortcut shortcut(QKeySequence("Ctrl+L"), &mw, 0, 0, Qt::ApplicationShortcut);
        bool loopDone = false;
        QObject::connect(&shortcut, &QShortcut::activated, &mw, [&loopDone]() {
            QEventLoop loop;
            QTimer::singleShot(1500, &loop, SLOT(quit()));
            loop.exec();
            loopDone = true;
        });
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow";
        QtJson::JsonObject command;
        command["oid"] = player.widget_by_path(commandPath)["oid"];
        command["keysequence"] = "Ctrl+L";

        bool answered = false;
        bool answeredInsideLoop = false;
        QtJson::JsonObject result;
        DelayedResponse * dresponse = player.shortcut(command);
        QObject::connect(dresponse, &DelayedResponse::aboutToWriteResponse,
                         [&](const QtJson::JsonObject & r) {
                             answered = true;
                             answeredInsideLoop = !loopDone;
                             result = r;
                         });
        dresponse->start();

        QTRY_VERIFY_WITH_TIMEOUT(answered && loopDone, 8000);
        QVERIFY(answeredInsideLoop);
        QVERIFY(result.contains("opened"));
    }

    void test_player_shortcut_15_times() {
        for (int i = 0; i < 15; i++) {
            test_player_shortcut();
        }
    }

    void test_player_tabbar_list() {
        QMainWindow mw;
        QTabBar tb(&mw);

        QStringList tabtexts = QStringList() << "toto"
                                             << "titi"
                                             << "tutu";

        foreach (const QString & txt, tabtexts) { tb.addTab(txt); }

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow::QTabBar";

        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject command;
        command["oid"] = resultPath["oid"];

        QtJson::JsonObject result = player.tabbar_list(command);

        QCOMPARE(tabtexts, result["tabtexts"].toStringList());
    }

    void test_player_headerview_list() {
        QTableWidget table;
        QStringList columns = QStringList() << "C1"
                                            << "C2"
                                            << "C3";
        QStringList rows = QStringList() << "R1"
                                         << "R2";
        table.setColumnCount(columns.count());
        table.setRowCount(rows.count());
        table.setHorizontalHeaderLabels(columns);
        table.setVerticalHeaderLabels(rows);
        for (int i = 0; i < columns.count(); i++) {
            for (int j = 0; j < rows.count(); j++) {
                table.setItem(i, j, new QTableWidgetItem(""));
            }
        }
        table.horizontalHeader()->setObjectName("H");
        table.verticalHeader()->setObjectName("V");

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath, resultPath, command, result;

        commandPath["path"] = "QTableWidget::H";
        resultPath = player.widget_by_path(commandPath);
        command["oid"] = resultPath["oid"];
        result = player.headerview_list(command);
        QCOMPARE(result["headertexts"].toStringList(), columns);

        commandPath["path"] = "QTableWidget::V";
        resultPath = player.widget_by_path(commandPath);
        command["oid"] = resultPath["oid"];
        result = player.headerview_list(command);
        QCOMPARE(result["headertexts"].toStringList(), rows);
    }

    void test_player_headerview_click() {
        QTableWidget table;
        QStringList columns = QStringList() << "C1"
                                            << "C2"
                                            << "C3";
        QStringList rows = QStringList() << "R1"
                                         << "R2";
        table.setColumnCount(columns.count());
        table.setRowCount(rows.count());
        table.setHorizontalHeaderLabels(columns);
        table.setVerticalHeaderLabels(rows);
        for (int i = 0; i < columns.count(); i++) {
            for (int j = 0; j < rows.count(); j++) {
                table.setItem(i, j, new QTableWidgetItem(""));
            }
        }
        table.horizontalHeader()->setObjectName("H");
        table.verticalHeader()->setObjectName("V");

        table.resize(800, 600);

        table.show();
#if QT_VERSION >= 0x050000
        QVERIFY(QTest::qWaitForWindowExposed(&table));
#else
        QTest::qWaitForWindowShown(&table);
#endif

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath, resultPath, command, result;

        for (int i = 0; i < columns.count(); i++) {
            QSignalSpy hspy(table.horizontalHeader(),
                            SIGNAL(sectionClicked(int)));
            commandPath["path"] = "QTableWidget::H";
            resultPath = player.widget_by_path(commandPath);
            command["oid"] = resultPath["oid"];
            command["indexOrName"] = i;
            result = player.headerview_click(command);
            qApp->processEvents();
            QCOMPARE(hspy.count(), 1);
            QCOMPARE(hspy.first().first().toInt(), i);
        }

        for (int i = 0; i < rows.count(); i++) {
            QSignalSpy vspy(table.verticalHeader(),
                            SIGNAL(sectionClicked(int)));
            commandPath["path"] = "QTableWidget::V";
            resultPath = player.widget_by_path(commandPath);
            command["oid"] = resultPath["oid"];
            command["indexOrName"] = i;
            result = player.headerview_click(command);
            qApp->processEvents();
            QCOMPARE(vspy.count(), 1);
            QCOMPARE(vspy.first().first().toInt(), i);
        }
    }

    void test_player_headerview_click_by_name() {
        QTableWidget table;
        QStringList columns = QStringList() << "C1"
                                            << "C2"
                                            << "C3";
        QStringList rows = QStringList() << "R1"
                                         << "R2";
        table.setColumnCount(columns.count());
        table.setRowCount(rows.count());
        table.setHorizontalHeaderLabels(columns);
        table.setVerticalHeaderLabels(rows);
        for (int i = 0; i < columns.count(); i++) {
            for (int j = 0; j < rows.count(); j++) {
                table.setItem(i, j, new QTableWidgetItem(""));
            }
        }
        table.horizontalHeader()->setObjectName("H");
        table.verticalHeader()->setObjectName("V");

        table.resize(800, 600);

        table.show();
#if QT_VERSION >= 0x050000
        QVERIFY(QTest::qWaitForWindowExposed(&table));
#else
        QTest::qWaitForWindowShown(&table);
#endif

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath, resultPath, command, result;

        QSignalSpy hspy(table.horizontalHeader(), SIGNAL(sectionClicked(int)));
        commandPath["path"] = "QTableWidget::H";
        resultPath = player.widget_by_path(commandPath);
        command["oid"] = resultPath["oid"];
        command["indexOrName"] = "C2";
        result = player.headerview_click(command);
        qApp->processEvents();
        QCOMPARE(hspy.count(), 1);
        QCOMPARE(hspy.first().first().toInt(), 1);
    }

    void test_player_model_items() {
        QMainWindow mw;
        QTableView view(&mw);

        QStandardItemModel model(4, 4);
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                QStandardItem * item = new QStandardItem(
                    QString("row %0, column %1").arg(row).arg(column));
                model.setItem(row, column, item);
            }
        }

        view.setModel(&model);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject commandPath;
        commandPath["path"] = "QMainWindow::QTableView";
        QtJson::JsonObject resultPath = player.widget_by_path(commandPath);

        QtJson::JsonObject commandModel;
        commandModel["oid"] = resultPath["oid"];
        QtJson::JsonObject resultModel = player.model(commandModel);

        QtJson::JsonObject command;
        command["oid"] = resultModel["oid"];

        QtJson::JsonObject result = player.model_items(command);

        QList<QVariant> items = result["items"].toList();

        QCOMPARE(items.count(), 4 * 4);
    }

    /**
     * A table of known values behind a view, for the dump tests below.
     */
    static void fill_table(QStandardItemModel & model) {
        for (int row = 0; row < model.rowCount(); ++row) {
            for (int column = 0; column < model.columnCount(); ++column) {
                model.setItem(row, column,
                              new QStandardItem(QString("r%0c%1")
                                                    .arg(row)
                                                    .arg(column)));
            }
        }
    }

    static qulonglong view_oid(Player & player, const QString & path) {
        QtJson::JsonObject command;
        command["path"] = path;
        return player.widget_by_path(command)["oid"].toULongLong();
    }

    /**
     * Runs a delayed response to its answer, the way the server does.
     */
    static QtJson::JsonObject answer_of(DelayedResponse * response) {
        QtJson::JsonObject answer;
        QEventLoop loop;
        QObject::connect(
            response, &DelayedResponse::aboutToWriteResponse, &loop,
            [&answer, &loop](const QtJson::JsonObject & result) {
                answer = result;
                loop.quit();
            });
        response->start();
        loop.exec();
        return answer;
    }

    /**
     * How many nodes of a widgets_list answer carry the given path.
     */
    static int count_listed(const QtJson::JsonObject & nodes,
                            const QString & path) {
        int found = 0;
        foreach (const QVariant & value, nodes) {
            const QVariantMap node = value.toMap();
            if (node["path"].toString() == path) {
                ++found;
            }
            found += count_listed(node["children"].toMap(), path);
        }
        return found;
    }

    void test_player_table_dump() {
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(3, 2);
        fill_table(model);
        model.setHorizontalHeaderLabels(QStringList() << "first"
                                                      << "second");
        view.setModel(&model);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        QtJson::JsonObject result = player.table_dump(command);

        QCOMPARE(result["row_count"].toInt(), 3);
        QCOMPARE(result["column_count"].toInt(), 2);
        QCOMPARE(result["returned_rows"].toInt(), 3);
        QCOMPARE(result["headers"].toStringList(),
                 QStringList() << "first"
                               << "second");
        const QVariantList rows = result["rows"].toList();
        QCOMPARE(rows.count(), 3);
        QCOMPARE(rows.at(0).toList().at(1).toString(), QString("r0c1"));
        QCOMPARE(rows.at(2).toList().at(0).toString(), QString("r2c0"));
    }

    void test_player_table_dump_page() {
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(5, 1);
        fill_table(model);
        view.setModel(&model);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        command["first_row"] = 3;
        command["max_rows"] = 2;
        QtJson::JsonObject result = player.table_dump(command);

        QCOMPARE(result["row_count"].toInt(), 5);
        QCOMPARE(result["returned_rows"].toInt(), 2);
        QCOMPARE(result["first_row"].toInt(), 3);
        const QVariantList rows = result["rows"].toList();
        QCOMPARE(rows.at(0).toList().at(0).toString(), QString("r3c0"));
        QCOMPARE(result["row_indexes"].toList().at(0).toInt(), 3);
    }

    void test_player_table_dump_hidden() {
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(3, 3);
        fill_table(model);
        view.setModel(&model);
        view.hideColumn(1);
        view.hideRow(0);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        QtJson::JsonObject result = player.table_dump(command);

        // скрытое остаётся за бортом, но карта индексов говорит, что это было
        QCOMPARE(result["row_count"].toInt(), 2);
        QCOMPARE(result["column_count"].toInt(), 2);
        QVERIFY(result["rows_hidden"].toBool());
        QVERIFY(result["columns_hidden"].toBool());
        QCOMPARE(result["column_indexes"].toList().at(1).toInt(), 2);
        QCOMPARE(result["row_indexes"].toList().at(0).toInt(), 1);
        const QVariantList rows = result["rows"].toList();
        QCOMPARE(rows.at(0).toList().at(1).toString(), QString("r1c2"));

        command["visible_only"] = false;
        QtJson::JsonObject whole = player.table_dump(command);
        QCOMPARE(whole["row_count"].toInt(), 3);
        QCOMPARE(whole["column_count"].toInt(), 3);
    }

    void test_player_table_dump_role_and_flags() {
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(1, 2);
        fill_table(model);
        model.item(0, 0)->setData("raw", Qt::UserRole + 1);
        model.item(0, 1)->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        view.setModel(&model);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        command["role"] = int(Qt::UserRole) + 1;
        command["with_flags"] = true;
        QtJson::JsonObject result = player.table_dump(command);

        const QVariantList rows = result["rows"].toList();
        QCOMPARE(rows.at(0).toList().at(0).toString(), QString("raw"));
        const QVariantList editable = result["editable"].toList();
        QVERIFY(editable.at(0).toList().at(0).toBool());
        QVERIFY(!editable.at(0).toList().at(1).toBool());
    }

    void test_player_table_dump_refuses_a_huge_answer() {
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(60000, 4);
        view.setModel(&model);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        QtJson::JsonObject result = player.table_dump(command);

        // без границ такой ответ убивал приложение: отказ вместо смерти
        QCOMPARE(result["errName"].toString(), QString("TooManyCells"));
        QVERIFY(result["errDesc"].toString().contains("60000"));

        command["max_rows"] = 2;
        QtJson::JsonObject page = player.table_dump(command);
        QCOMPARE(page["returned_rows"].toInt(), 2);
    }

    void test_player_model_item_set() {
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(2, 2);
        fill_table(model);
        view.setModel(&model);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        command["row"] = 1;
        command["column"] = 0;
        command["value"] = "written";
        QtJson::JsonObject result = player.model_item_set(command);

        QCOMPARE(model.item(1, 0)->text(), QString("written"));
        QCOMPARE(result["display"].toString(), QString("written"));
    }

    void test_player_model_item_type() {
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(1, 1);
        fill_table(model);
        view.setModel(&model);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        command["row"] = 0;
        command["column"] = 0;
        command["text"] = "typed";
        QtJson::JsonObject result = player.model_item_type(command);
        QTest::qWait(200);

        QVERIFY(!result["editor"].toString().isEmpty());
        QCOMPARE(model.item(0, 0)->text(), QString("typed"));
    }

    void test_player_model_item_type_answers_before_a_modal_on_commit() {
        // проверка значения при подтверждении может показать окно с exec(): синхронное
        // подтверждение держало бы команду до его закрытия, и клиент падал по таймауту
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(1, 1);
        fill_table(model);
        view.setModel(&model);
        QMenu menu;
        menu.addAction("invalid value");
        QObject::connect(&model, &QStandardItemModel::itemChanged, &menu,
                         [&menu]() { menu.exec(QCursor::pos()); });
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        mw.activateWindow();
        (void)QTest::qWaitForWindowActive(&mw);

        QBuffer buffer;
        Player player(&buffer);
        PopupCloser closer;

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        command["row"] = 0;
        command["column"] = 0;
        command["text"] = "typed";
        QtJson::JsonObject result = player.model_item_type(command);
        closer.answered = true;

        // подтверждение приходит на следующем витке, и окно проверки открывается уже
        // после ответа
        QTRY_COMPARE(model.item(0, 0)->text(), QString("typed"));
        QVERIFY(!closer.closedWhileWaiting);
        QVERIFY(result.contains("editor_oid"));
        QCOMPARE(result["editor_text"].toString(), QString("typed"));
    }

    void test_player_widget_keyclick_without_a_window_is_an_error() {
        // без oid клавиши идут активному окну, а его может не быть - на агенте CI
        // никто не кликает по приложению
        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["text"] = "x";
        if (qApp->activeWindow()) {
            QSKIP("an active window is left from another test");
        }
        QCOMPARE(player.widget_keyclick(command)["errName"].toString(),
                 QString("NoActiveWindow"));
    }

    void test_player_model_item_type_refuses_read_only() {
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(1, 1);
        fill_table(model);
        model.item(0, 0)->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        view.setModel(&model);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        command["row"] = 0;
        command["column"] = 0;
        command["text"] = "typed";
        QtJson::JsonObject result = player.model_item_type(command);

        QCOMPARE(result["errName"].toString(), QString("ItemNotEditable"));
        QCOMPARE(model.item(0, 0)->text(), QString("r0c0"));
    }

    void test_player_model_select_range() {
        QMainWindow mw;
        QTableView view(&mw);
        QStandardItemModel model(4, 4);
        fill_table(model);
        view.setModel(&model);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTableView");
        command["top"] = 1;
        command["left"] = 1;
        command["bottom"] = 2;
        command["right"] = 3;
        QtJson::JsonObject result = player.model_select_range(command);

        QCOMPARE(result["selected_cells"].toInt(), 6);
        QCOMPARE(view.selectionModel()->selectedIndexes().count(), 6);
    }

    void test_player_model_select_items() {
        QMainWindow mw;
        QTreeView view(&mw);
        QStandardItemModel model;
        QStandardItem * grids = new QStandardItem("Grids");
        for (int i = 0; i < 3; ++i) {
            grids->appendRow(new QStandardItem(QString("Grid%1").arg(i)));
        }
        model.appendRow(grids);
        view.setModel(&model);
        view.expandAll();

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTreeView");
        QVariantMap first;
        first["itempath"] = "0-0";
        first["row"] = 0;
        first["column"] = 0;
        command["items"] = QVariantList() << first;
        QtJson::JsonObject result = player.model_select_items(command);
        QCOMPARE(result["selected_rows"].toInt(), 1);

        QVariantMap third;
        third["itempath"] = "0-0";
        third["row"] = 2;
        third["column"] = 0;
        command["items"] = QVariantList() << third;
        command["mode"] = "select";
        result = player.model_select_items(command);
        QCOMPARE(result["selected_rows"].toInt(), 2);

        command["mode"] = "replace";
        result = player.model_select_items(command);
        QCOMPARE(result["selected_rows"].toInt(), 1);
    }

    void test_player_widget_window() {
        QMainWindow mw;
        QTableView view(&mw);
        mw.setObjectName("host");

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "host::QTableView");
        QtJson::JsonObject result = player.widget_window(command);

        QCOMPARE(result["objectName"].toString(), QString("host"));
    }

    void test_player_widgets_find() {
        QMainWindow mw;
        QTableView first(&mw);
        first.setObjectName("wanted");
        QTableView second(&mw);
        QTabBar bar(&mw);
        Q_UNUSED(second);
        Q_UNUSED(bar);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject byClass;
        byClass["class_name"] = "QTableView";
        byClass["visible_only"] = false;
        QCOMPARE(player.widgets_find(byClass)["items"].toList().count(), 2);

        QtJson::JsonObject byName;
        byName["objectname"] = "wanted";
        byName["visible_only"] = false;
        const QVariantList found = player.widgets_find(byName)["items"].toList();
        QCOMPARE(found.count(), 1);

        QtJson::JsonObject nothing;
        QCOMPARE(player.widgets_find(nothing)["errName"].toString(),
                 QString("MissingFilter"));
    }

    void test_player_model_item_icon_ignores_selection() {
        // отпечаток сравнивает иконки строк; подсветка выделения в него попадать
        // не должна, иначе выделенная строка «отличается» от такой же соседней
        QMainWindow mw;
        QTreeView view(&mw);
        QStandardItemModel model;
        QPixmap red(16, 16);
        red.fill(Qt::red);
        for (int row = 0; row < 2; ++row) {
            model.appendRow(new QStandardItem(QIcon(red), "same"));
        }
        QPixmap blue(16, 16);
        blue.fill(Qt::blue);
        model.appendRow(new QStandardItem(QIcon(blue), "same"));
        view.setModel(&model);
        mw.setCentralWidget(&view);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTreeView");
        command["row"] = 0;
        command["column"] = 0;
        const QString plain = player.model_item_icon(command)["hash"].toString();
        QVERIFY(!plain.isEmpty());

        view.selectionModel()->select(model.index(0, 0),
                                      QItemSelectionModel::ClearAndSelect);
        view.setCurrentIndex(model.index(0, 0));
        qApp->processEvents();
        QCOMPARE(player.model_item_icon(command)["hash"].toString(), plain);

        QtJson::JsonObject twin = command;
        twin["row"] = 1;
        QCOMPARE(player.model_item_icon(twin)["hash"].toString(), plain);

        // а сама иконка в отпечаток попадает: другая картинка - другой отпечаток
        QtJson::JsonObject other = command;
        other["row"] = 2;
        QVERIFY(player.model_item_icon(other)["hash"].toString() != plain);
    }

    void test_player_widgets_find_counts_a_dialog_once() {
        // диалог с родителем - и окно верхнего уровня, и потомок главного
        // окна: обход, зашедший в него дважды, отдавал каждую его таблицу
        // два раза, и в спредшите StarSteer виделись две одноимённые
        QMainWindow mw;
        QDialog dialog(&mw);
        QVBoxLayout * layout = new QVBoxLayout(&dialog);
        QTableView * view = new QTableView(&dialog);
        view->setObjectName("insideDialog");
        QStandardItemModel model(1, 1);
        view->setModel(&model);
        layout->addWidget(view);
        mw.show();
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject byName;
        byName["objectname"] = "insideDialog";
        byName["visible_only"] = true;
        QCOMPARE(player.widgets_find(byName)["items"].toList().count(), 1);

        // и имя, по которому table_dump ищет представление, не двоится
        QtJson::JsonObject dump;
        dump["objectname"] = "insideDialog";
        QCOMPARE(player.table_dump(dump)["row_count"].toInt(), 1);
    }

    void test_player_tabbar_click_and_rects() {
        // вкладки кладём в раскладку окна: клик идёт через платформу, а она
        // ищет виджет по точке. Прямой потомок QMainWindow без раскладки
        // рисуется там, где его никто не найдёт, и клик уходит в пустоту
        QMainWindow mw;
        QWidget * central = new QWidget(&mw);
        QVBoxLayout * layout = new QVBoxLayout(central);
        QTabBar bar(central);
        bar.addTab("one");
        bar.addTab("two");
        bar.addTab("three");
        layout->addWidget(&bar);
        layout->addStretch();
        mw.setCentralWidget(central);
        mw.resize(400, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        mw.activateWindow();
        // активности не требуем: на занятой машине окно может её не получить
        (void)QTest::qWaitForWindowActive(&mw);

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QWidget::QTabBar");
        QtJson::JsonObject listed = player.tabbar_list(command);
        const QVariantList tabs = listed["tabs"].toList();
        QCOMPARE(tabs.count(), 3);
        const QVariantMap second = tabs.at(1).toMap();
        QCOMPARE(second["text"].toString(), QString("two"));
        QVERIFY(second["rect"].toMap()["width"].toDouble() > 0);

        QtJson::JsonObject click;
        click["oid"] = command["oid"];
        click["text"] = "three";
        QtJson::JsonObject clicked = answer_of(player.tabbar_click(click));
        QCOMPARE(clicked["index"].toInt(), 2);
        // ответ описывает уже случившееся переключение
        QVERIFY(clicked["current"].toBool());
        QTest::qWait(200);
        QCOMPARE(bar.currentIndex(), 2);

        // прежняя доставка бьёт прямо в виджет и раскладкой не интересуется
        QtJson::JsonObject back;
        back["oid"] = command["oid"];
        back["index"] = 0;
        back["direct"] = true;
        answer_of(player.tabbar_click(back));
        QTest::qWait(200);
        QCOMPARE(bar.currentIndex(), 0);

        QtJson::JsonObject missing;
        missing["oid"] = command["oid"];
        missing["text"] = "nothing like this";
        QCOMPARE(answer_of(player.tabbar_click(missing))["errName"].toString(),
                 QString("InvalidTab"));
    }

    void test_player_tabbar_click_answers_while_the_menu_it_opened_is_open() {
        // смена вкладки может открыть меню или диалог с exec(): витки цикла,
        // прокрученные внутри команды, держали её, пока окно открыто, и клиент
        // падал по таймауту - как с кнопкой File риббона
        QMainWindow mw;
        QWidget * central = new QWidget(&mw);
        QVBoxLayout * layout = new QVBoxLayout(central);
        QTabBar bar(central);
        bar.addTab("one");
        bar.addTab("two");
        layout->addWidget(&bar);
        layout->addStretch();
        mw.setCentralWidget(central);
        mw.resize(400, 200);
        QMenu menu;
        menu.addAction("unsaved changes");
        QObject::connect(&bar, &QTabBar::currentChanged, &menu,
                         [&menu]() { menu.exec(QCursor::pos()); });
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        mw.activateWindow();
        (void)QTest::qWaitForWindowActive(&mw);

        QBuffer buffer;
        Player player(&buffer);
        // меню закрывает ответ; страховка закрывает его, только если ответа нет
        PopupCloser closer(3000);

        QtJson::JsonObject click;
        click["oid"] = view_oid(player, "QMainWindow::QWidget::QTabBar");
        click["index"] = 1;
        DelayedResponse * response = player.tabbar_click(click);
        bool menuOpenAtAnswer = false;
        QObject::connect(response, &DelayedResponse::aboutToWriteResponse, &menu,
                         [&closer, &menu, &menuOpenAtAnswer]() {
                             closer.answered = true;
                             menuOpenAtAnswer = menu.isVisible();
                             menu.close();
                         });
        const QtJson::JsonObject clicked = answer_of(response);

        QVERIFY(!closer.closedWhileWaiting);
        QVERIFY(menuOpenAtAnswer);
        QCOMPARE(clicked["index"].toInt(), 1);
        QCOMPARE(bar.currentIndex(), 1);
    }

    void test_player_menu_trigger_leaves_a_dialog_open() {
        // действия есть и у диалога (сочетание Ctrl+4 диалога Solo висит на
        // QAction): выбор такого действия диалог не закрывает, а меню - закрывает
        QMainWindow mw;
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        QDialog dialog(&mw);
        QAction trace("Trace", &dialog);
        dialog.addAction(&trace);
        QSignalSpy traced(&trace, SIGNAL(triggered(bool)));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        command["oid"] = player.registerObject(&dialog);
        command["path"] = "Trace";
        QVERIFY(!player.menu_trigger(command).contains("errName"));
        QTRY_COMPARE(traced.count(), 1);
        QVERIFY(dialog.isVisible());

        QMenu menu(&mw);
        menu.addAction("Pick");
        menu.popup(mw.mapToGlobal(QPoint(10, 10)));
        QTRY_VERIFY(menu.isVisible());
        QtJson::JsonObject pick;
        pick["oid"] = player.registerObject(&menu);
        pick["path"] = "Pick";
        player.menu_trigger(pick);
        QTRY_VERIFY(!menu.isVisible());
    }

    void test_player_widget_click_answers_before_a_menu() {
        // кнопка с меню открывает его по нажатию, во вложенном цикле событий:
        // доставленный на месте клик держал бы команду, пока меню открыто, и
        // клиент падал по таймауту - так висела кнопка File риббона StarSteer
        QMainWindow mw;
        QWidget * central = new QWidget(&mw);
        QVBoxLayout * layout = new QVBoxLayout(central);
        QToolButton * button = new QToolButton(central);
        QMenu menu;
        menu.addAction("item");
        bool menuShown = false;
        QObject::connect(&menu, &QMenu::aboutToShow, &menu,
                         [&menuShown]() { menuShown = true; });
        button->setText("menu");
        button->setMenu(&menu);
        button->setPopupMode(QToolButton::InstantPopup);
        layout->addWidget(button);
        layout->addStretch();
        mw.setCentralWidget(central);
        mw.resize(300, 150);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        mw.activateWindow();
        (void)QTest::qWaitForWindowActive(&mw);

        QBuffer buffer;
        Player player(&buffer);
        PopupCloser closer;

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QWidget::QToolButton");
        player.widget_click(command);
        closer.answered = true;

        // ответ ушёл раньше, а меню всё равно открылось - на следующем витке
        QTRY_VERIFY(menuShown);
        QVERIFY(!closer.closedWhileWaiting);
    }

    void test_player_widget_keyclick_answers_before_a_menu() {
        // клавиша, открывающая меню или диалог, не должна держать команду,
        // которая её нажала
        QMainWindow mw;
        QWidget * central = new QWidget(&mw);
        QVBoxLayout * layout = new QVBoxLayout(central);
        QLineEdit * edit = new QLineEdit(central);
        layout->addWidget(edit);
        layout->addStretch();
        mw.setCentralWidget(central);
        mw.resize(300, 150);
        QMenu menu;
        menu.addAction("item");
        bool menuShown = false;
        QObject::connect(&menu, &QMenu::aboutToShow, &menu,
                         [&menuShown]() { menuShown = true; });
        QObject::connect(edit, &QLineEdit::textEdited, &menu, [&menu, edit]() {
            menu.exec(edit->mapToGlobal(QPoint(0, edit->height())));
        });
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        mw.activateWindow();
        (void)QTest::qWaitForWindowActive(&mw);

        QBuffer buffer;
        Player player(&buffer);
        PopupCloser closer;

        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QWidget::QLineEdit");
        command["text"] = "x";
        player.widget_keyclick(command);
        closer.answered = true;

        QTRY_VERIFY(menuShown);
        QVERIFY(!closer.closedWhileWaiting);
        QCOMPARE(edit->text(), QString("x"));
    }

    /**
     * What a window is fed by one double click of a mouse: QGuiApplication
     * makes the double click of the second press itself.
     */
    static QList<QEvent::Type> fed_by_double_click() {
        return QList<QEvent::Type>()
               << QEvent::MouseButtonPress << QEvent::MouseButtonRelease
               << QEvent::MouseButtonPress << QEvent::MouseButtonDblClick
               << QEvent::MouseButtonRelease;
    }

    /**
     * The handlers a widget runs for one double click of a mouse: the press
     * that made the double click reaches it as the double click alone.
     */
    static QList<QEvent::Type> handled_for_double_click() {
        return QList<QEvent::Type>()
               << QEvent::MouseButtonPress << QEvent::MouseButtonRelease
               << QEvent::MouseButtonDblClick << QEvent::MouseButtonRelease;
    }

    void test_player_model_item_doubleclick_is_two_presses() {
        // готовый MouseButtonDblClick платформа не принимает: отладочный Qt
        // StarSteer останавливается на ассерте QTBUG-71263 "Native double
        // clicks are not implemented", и двойной клик по строке дерева до
        // продукта не доходил. Двойной клик - два нажатия подряд, а
        // MouseButtonDblClick из второго делает сам QGuiApplication
        QMainWindow mw;
        QTreeView view(&mw);
        QStandardItemModel model;
        for (int row = 0; row < 3; ++row) {
            model.appendRow(new QStandardItem(QString("row %0").arg(row)));
        }
        view.setModel(&model);
        view.setEditTriggers(QAbstractItemView::NoEditTriggers);
        mw.setCentralWidget(&view);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        mw.activateWindow();
        (void)QTest::qWaitForWindowActive(&mw);
        QSignalSpy doubleClicked(&view, SIGNAL(doubleClicked(QModelIndex)));
        MouseRecorder fed;
        mw.windowHandle()->installEventFilter(&fed);

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = view_oid(player, "QMainWindow::QTreeView");
        command["row"] = 1;
        command["column"] = 0;
        command["itemaction"] = "doubleclick";
        QVERIFY(!player.model_item_action(command).contains("errName"));
        // ответ, как у клика, уходит раньше самого ввода
        QCOMPARE(doubleClicked.count(), 0);

        QTRY_COMPARE(doubleClicked.count(), 1);
        QTest::qWait(100);
        QCOMPARE(doubleClicked.count(), 1);
        QCOMPARE(doubleClicked.at(0).at(0).value<QModelIndex>(),
                 model.index(1, 0));
        QCOMPARE(fed.seen, fed_by_double_click());
    }

    void test_player_widget_click_doubleclick_is_two_presses() {
        QMainWindow mw;
        MouseHandlersWidget * target = new MouseHandlersWidget;
        mw.setCentralWidget(target);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        mw.activateWindow();
        (void)QTest::qWaitForWindowActive(&mw);
        MouseRecorder fed;
        mw.windowHandle()->installEventFilter(&fed);

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        command["mouseAction"] = "doubleclick";
        player.widget_click(command);

        QTRY_COMPARE(target->handled.count(), 4);
        QTest::qWait(100);
        QCOMPARE(target->handled, handled_for_double_click());
        QCOMPARE(fed.seen, fed_by_double_click());
    }

    void test_player_widget_click_doubleclick_right_after_a_click() {
        // клиент кликает строку, чтобы выделить, и тут же дважды - чтобы
        // активировать. Человек так скоро в то же место не нажимает, а Qt
        // склеивал первое нажатие двойного клика с нажатием клика: двойной
        // клик приходил на первом нажатии, а за ним шёл лишний клик
        QMainWindow mw;
        MouseHandlersWidget * target = new MouseHandlersWidget;
        mw.setCentralWidget(target);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        mw.activateWindow();
        (void)QTest::qWaitForWindowActive(&mw);

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        player.widget_click(command);
        QTRY_COMPARE(target->handled.count(), 2);

        command["mouseAction"] = "doubleclick";
        player.widget_click(command);
        QTRY_COMPARE(target->handled.count(), 6);
        QTest::qWait(100);
        QList<QEvent::Type> expected;
        expected << QEvent::MouseButtonPress << QEvent::MouseButtonRelease;
        expected += handled_for_double_click();
        QCOMPARE(target->handled, expected);

        // штампы такого двойного клика обгоняют часы, а клик следом за ним -
        // снова просто клик
        command.remove("mouseAction");
        player.widget_click(command);
        QTRY_COMPARE(target->handled.count(), 8);
        QTest::qWait(100);
        expected << QEvent::MouseButtonPress << QEvent::MouseButtonRelease;
        QCOMPARE(target->handled, expected);
    }

    void test_player_widget_click_doubleclick_survives_a_busy_first_click() {
        // мышь ставит на нажатие время, когда его сделали, а не когда
        // доставили: продукт, занятый первым кликом дольше интервала двойного
        // клика, всё равно получает двойной клик. Штампы при доставке
        // разводили бы нажатия на два одиночных клика
        QMainWindow mw;
        MouseHandlersWidget * target = new MouseHandlersWidget;
        target->firstReleaseBusyMs =
            QGuiApplication::styleHints()->mouseDoubleClickInterval() + 100;
        mw.setCentralWidget(target);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        mw.activateWindow();
        (void)QTest::qWaitForWindowActive(&mw);

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        command["mouseAction"] = "doubleclick";
        player.widget_click(command);

        QTRY_COMPARE(target->handled.count(), 4);
        QTest::qWait(100);
        QCOMPARE(target->handled, handled_for_double_click());
    }

    void test_player_widget_click_doubleclick_direct_goes_to_the_widget() {
        // прямая доставка идёт мимо платформы, и ассерта QTBUG-71263 у неё
        // нет: виджет, как и раньше, получает готовый двойной клик
        QMainWindow mw;
        MouseHandlersWidget * target = new MouseHandlersWidget;
        mw.setCentralWidget(target);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        MouseRecorder fed;
        mw.windowHandle()->installEventFilter(&fed);

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        command["mouseAction"] = "doubleclick";
        command["direct"] = true;
        player.widget_click(command);

        QTRY_COMPARE(target->handled.count(), 4);
        QTest::qWait(100);
        QCOMPARE(target->handled, handled_for_double_click());
        QVERIFY(fed.seen.isEmpty());
    }

    void test_player_widget_drag_walks_the_points_with_the_button_held() {
        QMainWindow mw;
        DragRecorderWidget * target = new DragRecorderWidget;
        mw.setCentralWidget(target);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        QVariantMap from, to;
        from["x"] = 20;
        from["y"] = 30;
        to["x"] = 120;
        to["y"] = 30;
        command["points"] = QVariantList() << from << to;
        command["steps"] = 5;
        command["interval"] = 1;
        QVERIFY(!player.widget_drag(command).contains("errName"));

        // the answer comes first: nothing has reached the widget yet
        QVERIFY(target->pressed.isEmpty());
        QTRY_COMPARE(target->released.count(), 1);
        QCOMPARE(target->pressed, QList<QPoint>() << QPoint(20, 30));
        QCOMPARE(target->released, QList<QPoint>() << QPoint(120, 30));
        QCOMPARE(target->moved.last(), QPoint(120, 30));
        QVERIFY(target->moved.contains(QPoint(80, 30)));
        QVERIFY(target->buttonsOnMove.testFlag(Qt::LeftButton));
    }

    void test_player_widget_click_at_clicks_the_point_with_modifiers() {
        QMainWindow mw;
        ClickRecorderWidget * target = new ClickRecorderWidget;
        mw.setCentralWidget(target);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        command["x"] = 40;
        command["y"] = 25;
        command["modifiers"] = QVariantList() << "shift";
        QVERIFY(!player.widget_click_at(command).contains("errName"));

        QVERIFY(target->pressed.isEmpty());
        QTRY_COMPARE(target->handled.count(), 2);
        QCOMPARE(target->pressed, QList<QPoint>() << QPoint(40, 25));
        QVERIFY(target->modifiersOnPress.first().testFlag(Qt::ShiftModifier));
    }

    void test_player_widget_click_at_double_click_arrives_as_one() {
        QMainWindow mw;
        ClickRecorderWidget * target = new ClickRecorderWidget;
        mw.setCentralWidget(target);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        command["x"] = 10;
        command["y"] = 10;
        command["clicks"] = 2;
        QVERIFY(!player.widget_click_at(command).contains("errName"));

        QTRY_COMPARE(target->handled.count(), 4);
        QCOMPARE(target->handled, handled_for_double_click());
    }

    void test_player_widget_click_at_rejects_three_clicks() {
        QMainWindow mw;
        QWidget * target = new QWidget;
        mw.setCentralWidget(target);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        command["clicks"] = 3;
        QCOMPARE(player.widget_click_at(command)["errName"].toString(),
                 QString("InvalidClicks"));
    }

    void test_player_widget_hover_moves_the_pointer_without_a_button() {
        QMainWindow mw;
        DragRecorderWidget * target = new DragRecorderWidget;
        target->setMouseTracking(true);
        mw.setCentralWidget(target);
        mw.resize(300, 200);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        command["x"] = 60;
        command["y"] = 45;
        QVERIFY(!player.widget_hover(command).contains("errName"));

        QTRY_VERIFY(target->moved.contains(QPoint(60, 45)));
        QCOMPARE(target->buttonsOnMove, Qt::MouseButtons());
        QVERIFY(target->pressed.isEmpty());
    }

    void test_player_shortcut_triggers_the_action_of_an_inactive_window() {
        QMainWindow mw;
        QAction * action = new QAction("Run", &mw);
        action->setShortcut(QKeySequence("Ctrl+Shift+F1"));
        mw.addAction(action);
        QSignalSpy spy(action, SIGNAL(triggered(bool)));
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        QMainWindow other;
        other.show();
        QVERIFY(QTest::qWaitForWindowExposed(&other));
        other.activateWindow();
        QTRY_VERIFY(QApplication::activeWindow() == &other);

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(&mw);
        command["keysequence"] = "Ctrl+Shift+F1";
        DelayedResponse * response = player.shortcut(command);
        response->start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    }

    void test_player_widget_drag_needs_two_points() {
        QMainWindow mw;
        QWidget * target = new QWidget;
        mw.setCentralWidget(target);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));

        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject command;
        command["oid"] = player.registerObject(target);
        QVariantMap one;
        one["x"] = 1;
        one["y"] = 1;
        command["points"] = QVariantList() << one;
        QCOMPARE(player.widget_drag(command)["errName"].toString(),
                 QString("InvalidPoints"));
    }

#if QT_VERSION < 0x050000
    /* TODO: this test crash on ubuntu Using Qt version 5.2.1 in
     * /usr/lib/x86_64-linux-gnu */
    void test_drag_ndrop() {
        TestDragNDropWidget dndwidget;

        dndwidget.show();
#if QT_VERSION >= 0x050000
        QVERIFY(QTest::qWaitForWindowExposed(&dndwidget));
#else
        QTest::qWaitForWindowShown(&dndwidget);
#endif

        dndwidget.m_lineEditDrag->setText(
            "HELLO, I HOPE I WILL BE DRAGGED AND DROPPED !");
        dndwidget.m_lineEditDrag->selectAll();

        qApp->processEvents();

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject command;
        QtJson::JsonObject commandPath;
        commandPath["path"] = "testdnd::drag";

        command["srcoid"] = player.widget_by_path(commandPath)["oid"];

        commandPath["path"] = "testdnd::drop";
        command["destoid"] = player.widget_by_path(commandPath)["oid"];

        DelayedResponse * dresponse = player.drag_n_drop(command);

        QCOMPARE(dndwidget.m_lineEditDrop->text(), QString(""));

        dresponse->start();

        QEventLoop loop;
        QObject::connect(
            dresponse, SIGNAL(aboutToWriteResponse(const QtJson::JsonObject &)),
            &loop, SLOT(quit()));
        loop.exec();

        QCOMPARE(dndwidget.m_lineEditDrop->text(),
                 QString("HELLO, I HOPE I WILL BE DRAGGED AND DROPPED !"));
    }

    void test_drag_ndrop_15_times() {
        for (int i = 0; i < 15; i++) {
            test_drag_ndrop();
        }
    }
#endif

    void test_funq_qt_dialogs_turn_native_file_dialogs_off() {
        // продукт зовёт только QFileDialog: без нативного диалога он становится виджетом,
        // который funq видит и заполняет на любом рабочем столе
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
        qputenv("FUNQ_QT_DIALOGS", "1");
        Funq::applyDialogPolicy();
        QVERIFY(QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs));
        qunsetenv("FUNQ_QT_DIALOGS");
        Funq::applyDialogPolicy();
        QVERIFY(!QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs));
    }

    void test_player_drop_files_delivers_urls_to_the_viewport() {
        QMainWindow mw;
        QListWidget list(&mw);
        mw.setCentralWidget(&list);
        DropRecorder recorder;
        list.viewport()->setAcceptDrops(true);
        list.viewport()->installEventFilter(&recorder);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject path;
        path["path"] = "QMainWindow::QListWidget";
        QtJson::JsonObject command;
        command["oid"] = player.widget_by_path(path)["oid"];
        command["paths"] = QVariantList() << "C:/data/a.las" << "C:/data/b.xlsx";
        const QtJson::JsonObject answer = player.drop_files(command);
        QVERIFY(answer["accepted"].toBool());
        QTRY_COMPARE(recorder.dropped, QStringList() << "C:/data/a.las" << "C:/data/b.xlsx");
    }

    void test_player_drop_files_reports_a_refused_drop() {
        QMainWindow mw;
        QListWidget list(&mw);
        mw.setCentralWidget(&list);
        mw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&mw));
        QBuffer buffer;
        Player player(&buffer);
        QtJson::JsonObject path;
        path["path"] = "QMainWindow::QListWidget";
        QtJson::JsonObject command;
        command["oid"] = player.widget_by_path(path)["oid"];
        command["paths"] = QVariantList() << "C:/data/a.las";
        QVERIFY(!player.drop_files(command)["accepted"].toBool());
    }

    void test_player_drop_model_items_delivers_the_rows_after_the_answer() {
        DropScene scene;
        const QtJson::JsonObject answer = scene.player.drop_model_items(
            scene.drop_command(QVariantList() << DropScene::grid(1)));

        QVERIFY(!answer.contains("errName"));
        QVERIFY(answer["accepted"].toBool());
        QCOMPARE(answer["count"].toInt(), 1);
        QCOMPARE(answer["target_class"].toString(), QString("RowsDropTarget"));
        QVERIFY(answer["formats"].toStringList().contains(kRowsFormat));
        QCOMPARE(scene.target.enters, 1);
        QCOMPARE(scene.target.moves, 1);
        QCOMPARE(scene.target.enterPos, scene.target.rect().center());
        // the drop is queued like a click: the command returns before it arrives
        QCOMPARE(scene.target.drops, 0);
        QVERIFY(!scene.model.lastMime.isNull());
        QTRY_COMPARE(scene.target.drops, 1);
        QCOMPARE(scene.target.droppedRows, QStringList() << "Grid1");
        QCOMPARE(scene.target.droppedFormats, QStringList() << kRowsFormat);
        QCOMPARE(scene.target.dropPos, scene.target.rect().center());
        QVERIFY(scene.model.lastMime.isNull());
    }

    void test_player_drop_model_items_selects_the_rows_unless_told_not_to() {
        DropScene scene;
        QItemSelectionModel * selection = scene.tree.selectionModel();
        selection->select(scene.model.index(2, 0, scene.grids()),
                          QItemSelectionModel::Select | QItemSelectionModel::Rows);

        QtJson::JsonObject command =
            scene.drop_command(QVariantList() << DropScene::grid(0));
        command["select"] = false;
        QVERIFY(scene.player.drop_model_items(command)["accepted"].toBool());
        QCOMPARE(selection->selectedRows().size(), 1);
        QCOMPARE(selection->selectedRows().first().row(), 2);
        let_drop_arrive();

        command.remove("select");
        QVERIFY(scene.player.drop_model_items(command)["accepted"].toBool());
        QCOMPARE(selection->selectedRows().size(), 1);
        QCOMPARE(selection->selectedRows().first().row(), 0);
        QCOMPARE(selection->currentIndex().row(), 0);
        QTRY_COMPARE(scene.target.drops, 2);
    }

    void test_player_drop_model_items_drops_one_index_per_row() {
        DropScene scene;
        scene.model.itemFromIndex(scene.model.index(1, 0, scene.grids()))
            ->setDragEnabled(false);

        // row 2 by its second column, row 0 twice, and a row that cannot be dragged
        const QtJson::JsonObject answer = scene.player.drop_model_items(
            scene.drop_command(QVariantList()
                               << DropScene::grid(0) << DropScene::grid(2, 1)
                               << DropScene::grid(0, 1) << DropScene::grid(1)));

        QVERIFY(answer["accepted"].toBool());
        QCOMPARE(answer["count"].toInt(), 2);
        QCOMPARE(scene.model.mimeIndexes.size(), 2);
        QCOMPARE(scene.model.mimeIndexes.at(0),
                 scene.model.index(0, 0, scene.grids()));
        QCOMPARE(scene.model.mimeIndexes.at(1),
                 scene.model.index(2, 0, scene.grids()));
        // the rows that were asked for are selected, the draggable ones or not
        QCOMPARE(scene.tree.selectionModel()->selectedRows().size(), 3);
        QTRY_COMPARE(scene.target.droppedRows,
                     QStringList() << "Grid0" << "Grid2");
    }

    void test_player_drop_model_items_refuses_rows_that_cannot_be_dragged() {
        DropScene scene;
        scene.model.itemFromIndex(scene.model.index(0, 0, scene.grids()))
            ->setDragEnabled(false);
        QItemSelectionModel * selection = scene.tree.selectionModel();
        selection->select(scene.model.index(2, 0, scene.grids()),
                          QItemSelectionModel::Select | QItemSelectionModel::Rows);

        const QtJson::JsonObject answer = scene.player.drop_model_items(
            scene.drop_command(QVariantList() << DropScene::grid(0)));

        QCOMPARE(answer["errName"].toString(), QString("NotDraggable"));
        QVERIFY(scene.model.mimeIndexes.isEmpty());
        QCOMPARE(scene.target.enters, 0);
        QCOMPARE(selection->selectedRows().size(), 1);
        QCOMPARE(selection->selectedRows().first().row(), 2);
    }

    void test_player_drop_model_items_reports_a_refused_enter_without_an_error() {
        DropScene scene;
        scene.target.takes = false;

        const QtJson::JsonObject answer = scene.player.drop_model_items(
            scene.drop_command(QVariantList() << DropScene::grid(0)));

        QVERIFY(!answer.contains("errName"));
        QVERIFY(!answer["accepted"].toBool());
        QCOMPARE(answer["action"].toString(), QString("ignore"));
        QCOMPARE(scene.target.enters, 1);
        QCOMPARE(scene.target.moves, 0);
        QCOMPARE(scene.target.leaves, 1);
        // nobody is left to delete the data of a drop that is not coming
        QVERIFY(scene.model.lastMime.isNull());
        QTest::qWait(50);
        QCOMPARE(scene.target.drops, 0);
    }

    void test_player_drop_model_items_reports_refused_moves() {
        DropScene scene;
        scene.target.takesMoves = false;

        const QtJson::JsonObject answer = scene.player.drop_model_items(
            scene.drop_command(QVariantList() << DropScene::grid(0)));

        QVERIFY(!answer.contains("errName"));
        QVERIFY(!answer["accepted"].toBool());
        QCOMPARE(scene.target.enters, 1);
        QCOMPARE(scene.target.moves, 1);
        QCOMPARE(scene.target.leaves, 1);
        QVERIFY(scene.model.lastMime.isNull());
        QTest::qWait(50);
        QCOMPARE(scene.target.drops, 0);
    }

    void test_player_drop_model_items_sends_the_requested_number_of_moves() {
        DropScene scene;
        QtJson::JsonObject command =
            scene.drop_command(QVariantList() << DropScene::grid(0));

        command["moves"] = 3;
        QVERIFY(scene.player.drop_model_items(command)["accepted"].toBool());
        QCOMPARE(scene.target.moves, 3);
        let_drop_arrive();

        command["moves"] = 0;
        QVERIFY(scene.player.drop_model_items(command)["accepted"].toBool());
        QCOMPARE(scene.target.moves, 3);
        QTRY_COMPARE(scene.target.drops, 2);

        command["moves"] = -1;
        QCOMPARE(scene.player.drop_model_items(command)["errName"].toString(),
                 QString("InvalidMoves"));
    }

    void test_player_drop_model_items_goes_to_the_nearest_widget_that_accepts_drops() {
        DropScene scene;
        scene.target.setGeometry(0, 0, 200, 100);
        QWidget inner(&scene.target);
        inner.setObjectName("inner");
        inner.setGeometry(30, 20, 50, 40);

        QtJson::JsonObject command = scene.drop_command(
            QVariantList() << DropScene::grid(0), "host::target::inner");
        command["x"] = 5;
        command["y"] = 7;
        QtJson::JsonObject answer = scene.player.drop_model_items(command);

        QVERIFY(answer["accepted"].toBool());
        QCOMPARE(answer["target_class"].toString(), QString("RowsDropTarget"));
        QCOMPARE(scene.target.enterPos, QPoint(35, 27));
        QTRY_COMPARE(scene.target.drops, 1);
        QCOMPARE(scene.target.dropPos, QPoint(35, 27));

        command.remove("x");
        command.remove("y");
        QVERIFY(scene.player.drop_model_items(command)["accepted"].toBool());
        QCOMPARE(scene.target.enterPos, QPoint(30, 20) + inner.rect().center());
        QTRY_COMPARE(scene.target.drops, 2);
    }

    void test_player_drop_model_items_reports_a_target_that_accepts_no_drops() {
        DropScene scene;
        QWidget plain(&scene.host);
        plain.setObjectName("plain");

        QtJson::JsonObject command = scene.drop_command(
            QVariantList() << DropScene::grid(0), "host::plain");
        QtJson::JsonObject answer = scene.player.drop_model_items(command);
        QVERIFY(!answer.contains("errName"));
        QVERIFY(!answer["accepted"].toBool());
        QCOMPARE(answer["target_class"].toString(), QString("QWidget"));
        QCOMPARE(answer["count"].toInt(), 1);
        QVERIFY(scene.model.lastMime.isNull());

        // a disabled widget takes no drag either, though it accepts drops
        scene.target.setEnabled(false);
        answer = scene.player.drop_model_items(
            scene.drop_command(QVariantList() << DropScene::grid(0)));
        QVERIFY(!answer["accepted"].toBool());
        QCOMPARE(scene.target.enters, 0);
    }

    void test_player_drop_model_items_drops_on_the_viewport_of_a_scroll_area() {
        DropScene scene;
        QListWidget list(&scene.host);
        list.setObjectName("list");
        list.setFrameStyle(QFrame::Box | QFrame::Plain);
        list.setLineWidth(6);
        list.setGeometry(0, 120, 200, 150);
        RowsDropFilter filter;
        list.viewport()->setAcceptDrops(true);
        list.viewport()->installEventFilter(&filter);
        scene.host.resize(300, 300);
        scene.host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scene.host));
        QVERIFY(list.viewport()->pos() != QPoint(0, 0));

        QtJson::JsonObject command = scene.drop_command(
            QVariantList() << DropScene::grid(0), "host::list");
        command["x"] = 20;
        command["y"] = 30;
        const QtJson::JsonObject answer = scene.player.drop_model_items(command);

        QVERIFY(answer["accepted"].toBool());
        QCOMPARE(answer["target_class"].toString(), QString("QWidget"));
        QCOMPARE(filter.enterPos,
                 list.viewport()->mapFrom(&list, QPoint(20, 30)));
        QTRY_COMPARE(filter.droppedRows, QStringList() << "Grid0");

        command.remove("x");
        command.remove("y");
        QVERIFY(scene.player.drop_model_items(command)["accepted"].toBool());
        QCOMPARE(filter.enterPos, list.viewport()->rect().center());
        QTRY_COMPARE(filter.drops, 2);
    }

    void test_player_drop_model_items_proposes_the_action_the_view_would() {
        DropScene scene;
        scene.target.keepsAction = true;
        QtJson::JsonObject command =
            scene.drop_command(QVariantList() << DropScene::grid(0));
        const auto action_of = [&scene, &command]() {
            const QString action =
                scene.player.drop_model_items(command)["action"].toString();
            let_drop_arrive();
            return action;
        };

        // a view that sets no default action drags with copy
        QCOMPARE(action_of(), QString("copy"));
        QCOMPARE(scene.target.enterAction, Qt::CopyAction);

        scene.tree.setDefaultDropAction(Qt::MoveAction);
        QCOMPARE(action_of(), QString("move"));

        // a default action the drag does not allow is not used
        scene.tree.setDefaultDropAction(Qt::LinkAction);
        QCOMPARE(action_of(), QString("copy"));

        // a view that only moves its rows inside itself falls back to move
        scene.tree.setDefaultDropAction(Qt::IgnoreAction);
        scene.tree.setDragDropMode(QAbstractItemView::InternalMove);
        QCOMPARE(action_of(), QString("move"));

        command["actions"] = QVariantList() << "link";
        QCOMPARE(action_of(), QString("link"));
        QCOMPARE(scene.target.drops, 5);
    }

    void test_player_drop_model_items_takes_the_actions_from_the_command() {
        DropScene scene;
        scene.target.keepsAction = true;
        QtJson::JsonObject command =
            scene.drop_command(QVariantList() << DropScene::grid(0));

        command["actions"] = QVariantList() << "copy" << "link";
        command["proposed"] = "link";
        QCOMPARE(scene.player.drop_model_items(command)["action"].toString(),
                 QString("link"));
        QCOMPARE(scene.target.enterAction, Qt::LinkAction);

        command["proposed"] = "move";
        QCOMPARE(scene.player.drop_model_items(command)["errName"].toString(),
                 QString("InvalidDropAction"));

        command["proposed"] = "teleport";
        QCOMPARE(scene.player.drop_model_items(command)["errName"].toString(),
                 QString("InvalidDropAction"));

        command.remove("proposed");
        command["actions"] = QVariantList() << "teleport";
        QCOMPARE(scene.player.drop_model_items(command)["errName"].toString(),
                 QString("InvalidDropAction"));
        QTRY_COMPARE(scene.target.drops, 1);
    }

    void test_player_drop_model_items_carries_the_modifiers_into_the_events() {
        DropScene scene;
        QtJson::JsonObject command =
            scene.drop_command(QVariantList() << DropScene::grid(0));

        // Qt proposes copy for a plain drag and move for one with Shift held
        QCOMPARE(scene.player.drop_model_items(command)["action"].toString(),
                 QString("copy"));
        let_drop_arrive();
        command["modifiers"] = QVariantList() << "shift";
        QCOMPARE(scene.player.drop_model_items(command)["action"].toString(),
                 QString("move"));
        QTRY_COMPARE(scene.target.drops, 2);
        QCOMPARE(scene.target.droppedAction, Qt::MoveAction);
    }

    void test_player_drop_model_items_rejects_bad_requests() {
        DropScene scene;
        QtJson::JsonObject command =
            scene.drop_command(QVariantList() << DropScene::grid(7));
        QCOMPARE(scene.player.drop_model_items(command)["errName"].toString(),
                 QString("MissingModelItem"));

        command["items"] = QVariantList();
        QCOMPARE(scene.player.drop_model_items(command)["errName"].toString(),
                 QString("MissingModelItem"));

        scene.model.refusesMimeData = true;
        command["items"] = QVariantList() << DropScene::grid(0);
        QCOMPARE(scene.player.drop_model_items(command)["errName"].toString(),
                 QString("NoMimeData"));
        QCOMPARE(scene.target.enters, 0);
    }

#ifdef QT_QUICK_LIB
    /* QtQuick tests */

    void test_quick_item_by_path() {
        QQuickView view;
        view.setSource(QUrl::fromLocalFile(SOURCE_DIR "sample1.qml"));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QBuffer buffer;
        Player player(&buffer);

        // register the quick view
        QtJson::JsonObject command_get_view;
        command_get_view["path"] = "QQuickView";
        QtJson::JsonObject result_view =
            player.widget_by_path(command_get_view);

        // search for quick_object
        QtJson::JsonObject command;
        command["quick_window_oid"] = result_view["oid"].value<qulonglong>();
        command["path"] = "QQuickItem::QQuickRectangle";

        QtJson::JsonObject result = player.quick_item_find(command);

        QVERIFY(result["oid"].value<qulonglong>() != 0);
        QCOMPARE(
            player.registeredObject(result["oid"].value<qulonglong>()),
            view.contentItem()->childItems().first()->childItems().first());

        // search for object that does not exists
        command["path"] = "tototiti::tutu";
        result = player.quick_item_find(command);

        QCOMPARE(result["success"].toBool(), false);
        QCOMPARE(result["errName"].toString(), QString("InvalidQuickItem"));
    }

    void test_quick_item_find_by_id() {
        QQuickView view;
        view.setSource(QUrl::fromLocalFile(SOURCE_DIR "find_by_id.qml"));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QBuffer buffer;
        Player player(&buffer);

        qulonglong view_id = player.registerObject(&view);

        QtJson::JsonObject command;
        QtJson::JsonObject result;
        qulonglong oid;
        QQuickItem * item;

        // search for root object
        command["quick_window_oid"] = view_id;
        command["qid"] = "root";

        result = player.quick_item_find(command);
        oid = result["oid"].value<qulonglong>();
        QVERIFY(oid != 0);

        item = (QQuickItem *)oid;
        QCOMPARE(item->property("objectName").toString(), QString("MyRoot"));

        // search for rect object
        command["qid"] = "rect";

        result = player.quick_item_find(command);
        oid = result["oid"].value<qulonglong>();
        QVERIFY(oid != 0);

        item = (QQuickItem *)oid;
        QCOMPARE(item->property("objectName").toString(), QString("MyRect"));

        // search for rect object with full id
        command["qid"] = "root.rect";

        result = player.quick_item_find(command);
        oid = result["oid"].value<qulonglong>();
        QVERIFY(oid != 0);

        item = (QQuickItem *)oid;
        QCOMPARE(item->property("objectName").toString(), QString("MyRect"));

        // search for object that does not exists
        command["qid"] = "rootrect";

        result = player.quick_item_find(command);
        QCOMPARE(result["success"].toBool(), false);
        QCOMPARE(result["errName"].toString(), QString("InvalidQuickItem"));
    }

    void test_widgets_list_of_a_quick_item_lists_its_subtree_only() {
        // строка индикаторов - десяток элементов, а сцена панели вокруг неё -
        // сотни со всеми свойствами; читать строку не должно стоить всей сцены
        QQuickView view;
        view.setSource(QUrl::fromLocalFile(SOURCE_DIR "rows.qml"));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QBuffer buffer;
        Player player(&buffer);

        QtJson::JsonObject find;
        find["quick_window_oid"] = player.registerObject(&view);
        find["qid"] = "row";
        const QtJson::JsonObject row = player.quick_item_find(find);
        QVERIFY(row["oid"].value<qulonglong>() != 0);

        QtJson::JsonObject list;
        list["oid"] = row["oid"];
        const QtJson::JsonObject listed = player.widgets_list(list);
        QCOMPARE(QStringList(listed.keys()),
                 QStringList() << "first" << "second");
    }

#ifdef QT_QUICKWIDGETS_LIB
    void test_player_drop_model_items_drops_on_a_quick_widget() {
        DropScene scene;
        QQuickWidget quick(&scene.host);
        quick.setObjectName("quick");
        quick.setSource(QUrl::fromLocalFile(SOURCE_DIR "drop_area.qml"));
        quick.setGeometry(0, 120, 200, 200);
        scene.host.resize(300, 400);
        scene.host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scene.host));
        QCOMPARE(quick.status(), QQuickWidget::Ready);

        const QtJson::JsonObject answer = scene.player.drop_model_items(
            scene.drop_command(QVariantList() << DropScene::grid(1),
                               "host::quick"));

        QVERIFY(!answer.contains("errName"));
        QVERIFY(answer["accepted"].toBool());
        QCOMPARE(answer["target_class"].toString(), QString("QQuickWidget"));
        QTRY_COMPARE(quick.rootObject()->property("dropCount").toInt(), 1);
        QCOMPARE(quick.rootObject()->property("entered").toInt(), 1);
        QCOMPARE(quick.rootObject()->property("droppedText").toString(),
                 QString("Grid1"));
    }
#endif

    void test_player_drop_model_items_reports_a_quick_widget_without_a_drop_area() {
        DropScene scene;
        QQuickWidget quick(&scene.host);
        quick.setObjectName("quick");
        quick.setSource(QUrl::fromLocalFile(SOURCE_DIR "sample1.qml"));
        quick.setGeometry(0, 120, 200, 200);
        scene.host.resize(300, 400);
        scene.host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scene.host));
        QCOMPARE(quick.status(), QQuickWidget::Ready);

        const QtJson::JsonObject answer = scene.player.drop_model_items(
            scene.drop_command(QVariantList() << DropScene::grid(1),
                               "host::quick"));

        QVERIFY(!answer.contains("errName"));
        QVERIFY(!answer["accepted"].toBool());
        QCOMPARE(answer["target_class"].toString(), QString("QQuickWidget"));
        QVERIFY(scene.model.lastMime.isNull());
    }

#if QT_VERSION_MAJOR < 6
    void test_quick_item_click() {
        QQuickView view;
        view.setSource(QUrl::fromLocalFile(SOURCE_DIR "test_click.qml"));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QBuffer buffer;
        Player player(&buffer);

        qulonglong view_id = player.registerObject(&view);

        // search for quick_object
        QtJson::JsonObject command;
        command["quick_window_oid"] = view_id;
        command["path"] = "QQuickItem::QQuickRectangle";

        QtJson::JsonObject result = player.quick_item_find(command);

        QQuickItem * item =
            view.contentItem()->childItems().first()->childItems().first();
        QCOMPARE(item->property("color").toString(), QString("#272822"));

        // click on it
        QtJson::JsonObject command_click;
        command_click["oid"] = result["oid"].value<qulonglong>();
        player.quick_item_click(command_click);

        qApp->processEvents();

        QCOMPARE(item->property("color").toString(), QString("#ffffff"));
    }
#endif
#endif
};

QTEST_MAIN(LibFunqTest)
#include "test.moc"
