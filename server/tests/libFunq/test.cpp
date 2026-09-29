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
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QObject>
#include <QPushButton>
#include <QShortcut>
#include <QSignalSpy>
#include <QStandardItem>
#include <QStandardItemModel>
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
