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

#include "player.h"

#include "dragndropresponse.h"
#include "objectpath.h"
#include "delayedresponse.h"
#include "shortcutresponse.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QBuffer>
#include <QComboBox>
#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTableView>
#include <QTime>
#include <QTimer>
#include <QTreeView>
#include <QWidget>
#include <QWindow>

#if QT_VERSION_MAJOR >= 6
#include <QScreen>
#else
#include <QDesktopWidget>
#endif

#ifdef QT_QUICK_LIB
#include <QQuickItem>
#include <QQuickWindow>
#endif

#ifdef QT_QUICKWIDGETS_LIB
#include <QQuickWidget>
#endif

using namespace ObjectPath;

// Qt's invokeMethod takes a fixed number of arguments; four covers the
// signals and slots a test drives, such as toggled(itemId, checked).
static const int gMaxInvokeArguments = 4;
// beyond this, a listing with properties is an answer of tens of megabytes
static const int gMaxWidgetsWithProperties = 3000;

#ifdef QT_QUICK_LIB
/**
 * Returns the window holding the QML scene of an object: the window itself, or
 * the offscreen window of a QQuickWidget.
 *
 * The offscreen window of a QQuickWidget may be destroyed and recreated during
 * the life time of the widget, so it is resolved on every call rather than
 * handed out to clients to keep.
 */
static QQuickWindow * resolveQuickWindow(QObject * object) {
    if (QQuickWindow * window = qobject_cast<QQuickWindow *>(object)) {
        return window;
    }
#ifdef QT_QUICKWIDGETS_LIB
    if (QQuickWidget * widget = qobject_cast<QQuickWidget *>(object)) {
        return widget->quickWindow();
    }
#endif
    return NULL;
}
#endif

/**
 * Posts a click to a widget.
 *
 * A press carries the button among the buttons held down - that is what Qt
 * itself delivers, and what QTest sends. Leaving that field empty makes the
 * press invisible to code that asks which buttons are down: the tools of the
 * cross-section read `buttons()` and silently ignored every synthetic click.
 */
template <class T>
void mouse_click(T * w, const QPoint & pos, Qt::MouseButton button) {
    QPoint global_pos = w->mapToGlobal(pos);
    qApp->postEvent(w,
                    new QMouseEvent(QEvent::MouseButtonPress, pos, global_pos,
                                    button, button, Qt::NoModifier));
    qApp->postEvent(w,
                    new QMouseEvent(QEvent::MouseButtonRelease, pos, global_pos,
                                    button, Qt::NoButton, Qt::NoModifier));
}

template <class T>
void mouse_dclick(T * w, const QPoint & pos) {
    mouse_click(w, pos, Qt::LeftButton);
    qApp->postEvent(w, new QMouseEvent(QEvent::MouseButtonDblClick, pos,
                                       w->mapToGlobal(pos), Qt::LeftButton,
                                       Qt::LeftButton, Qt::NoModifier));
    qApp->postEvent(w,
                    new QMouseEvent(QEvent::MouseButtonRelease, pos,
                                    w->mapToGlobal(pos), Qt::LeftButton,
                                    Qt::NoButton, Qt::NoModifier));
}

#ifdef QT_QUICK_LIB
#ifdef QT_QUICKWIDGETS_LIB
/**
 * Returns the QQuickWidget rendering a window, when the window is the
 * offscreen window of one.
 */
static QQuickWidget * hostingQuickWidget(QQuickWindow * window) {
    foreach (QWidget * widget, QApplication::allWidgets()) {
        QQuickWidget * quickWidget = qobject_cast<QQuickWidget *>(widget);
        if (quickWidget && quickWidget->quickWindow() == window) {
            return quickWidget;
        }
    }
    return NULL;
}
#endif

/**
 * Posts a click into a QML scene.
 *
 * When a QQuickWidget renders the scene, the events go to that widget: this is
 * the way real input reaches the scene, the widget maps the events into its
 * offscreen window, and they carry the real position on screen - a popup that
 * anchors itself to the click relies on it. A scene in a window of its own
 * gets the events directly. A pointer moves in before it presses, which
 * hover-driven items expect.
 */
void quick_mouse_click(QQuickWindow * window, const QPoint & pos,
                       Qt::MouseButton button, bool press, bool release) {
    QObject * receiver = window;
    QPoint global_pos = window->mapToGlobal(pos);
#ifdef QT_QUICKWIDGETS_LIB
    if (QQuickWidget * widget = hostingQuickWidget(window)) {
        receiver = widget;
        global_pos = widget->mapToGlobal(pos);
    }
#endif
    if (press) {
        qApp->postEvent(
            receiver, new QMouseEvent(QEvent::MouseMove, pos, global_pos,
                                      Qt::NoButton, Qt::NoButton,
                                      Qt::NoModifier));
        qApp->postEvent(receiver,
                        new QMouseEvent(QEvent::MouseButtonPress, pos,
                                        global_pos, button, button,
                                        Qt::NoModifier));
    }
    if (release) {
        qApp->postEvent(receiver,
                        new QMouseEvent(QEvent::MouseButtonRelease, pos,
                                        global_pos, button, Qt::NoButton,
                                        Qt::NoModifier));
    }
}
#endif

void activate_focus(QWidget * w) {
    w->activateWindow();
    w->setFocus(Qt::MouseFocusReason);
}

void dump_properties(QObject * object, QtJson::JsonObject & out) {
    const QMetaObject * metaobject = object->metaObject();
    for (int i = 0; i < metaobject->propertyCount(); ++i) {
        QMetaProperty prop = metaobject->property(i);
        QVariant value = object->property(prop.name());
        // first try to serialize and only add property if it is possible
        bool success = false;
        QtJson::serialize(value, success);
        if (success) {
            out[prop.name()] = value;
        }
    }
}

void dump_object(QObject * object, QtJson::JsonObject & out,
                 bool with_properties = false) {
    out["path"] = objectPath(object);
    QStringList classes;
    const QMetaObject * mo = object->metaObject();
    while (mo) {
        // sometimes classes appears twice
        if (!classes.contains(mo->className())) {
            classes << mo->className();
        }
        mo = mo->superClass();
    }
    out["classes"] = classes;
#ifdef QT_QUICK_LIB
    if (QQuickItem * item = qobject_cast<QQuickItem *>(object)) {
        // "path" walks object parents and stops at the offscreen window, so no
        // lookup accepts it back. This one is what quick_item_find takes.
        out["quick_path"] = quickItemPath(item);
        // A QML item has no position on screen of its own. The client turns
        // this rect into screen coordinates with the global position of the
        // widget or the window that renders the scene.
        const QPointF topLeft = item->mapToScene(QPointF(0, 0));
        QtJson::JsonObject sceneRect;
        sceneRect["x"] = topLeft.x();
        sceneRect["y"] = topLeft.y();
        sceneRect["width"] = item->width();
        sceneRect["height"] = item->height();
        out["scene_rect"] = sceneRect;
    }
#endif
    if (with_properties) {
        QtJson::JsonObject properties;
        dump_properties(object, properties);
        out["properties"] = properties;
    }
}

QString item_model_path(QAbstractItemModel * model, const QModelIndex & item) {
    QStringList path;
    QModelIndex parent = model->parent(item);
    while (parent.isValid()) {
        path << (QString::number(parent.row()) + "-" +
                 QString::number(parent.column()));
        parent = model->parent(parent);
    }
    // reverse list
    for (int k = 0, s = path.size(), max = (s / 2); k < max; k++) {
#if QT_VERSION_MAJOR >= 6
        path.swapItemsAt(k, s - (1 + k));
#else
        path.swap(k, s - (1 + k));
#endif
    }
    return path.join("/");
}

QString check_state_name(Qt::CheckState state) {
    switch (state) {
        case Qt::Unchecked:
            return "unchecked";
        case Qt::PartiallyChecked:
            return "partiallyChecked";
        case Qt::Checked:
            return "checked";
    }
    return QString();
}

void dump_item_model_attrs(QAbstractItemModel * model, QtJson::JsonObject & out,
                           const QModelIndex & index,
                           const qulonglong & modelId,
                           bool with_details = false) {
    out["modelid"] = modelId;
    QString path = item_model_path(model, index);
    if (!path.isEmpty()) {
        out["itempath"] = path;
    }
    out["row"] = index.row();
    out["column"] = index.column();
    out["value"] = model->data(index).toString();

    QVariant checkable = model->data(index, Qt::CheckStateRole);
    if (checkable.isValid()) {
        out["check_state"] = check_state_name(
            static_cast<Qt::CheckState>(checkable.toUInt()));
    }

    if (with_details) {
        // what a row shows besides its text: an application marks the active
        // object in bold or greys out a disabled one, and a test wants to
        // assert that without a screenshot
        out["has_children"] = model->hasChildren(index);
        const Qt::ItemFlags flags = model->flags(index);
        out["enabled"] = bool(flags & Qt::ItemIsEnabled);
        out["editable"] = bool(flags & Qt::ItemIsEditable);
        out["has_icon"] = model->data(index, Qt::DecorationRole).isValid();
        const QVariant font = model->data(index, Qt::FontRole);
        if (font.canConvert<QFont>()) {
            const QFont itemFont = font.value<QFont>();
            out["bold"] = itemFont.bold();
            out["italic"] = itemFont.italic();
        }
        const QVariant foreground = model->data(index, Qt::ForegroundRole);
        if (foreground.canConvert<QBrush>()) {
            out["foreground"] = foreground.value<QBrush>().color().name();
        }
        const QVariant tooltip = model->data(index, Qt::ToolTipRole);
        if (tooltip.isValid()) {
            out["tooltip"] = tooltip.toString();
        }
    }
}

void dump_items_model(QAbstractItemModel * model, QtJson::JsonObject & out,
                      const QModelIndex & parent, const qulonglong & modelId,
                      bool recursive = true, bool with_details = false) {
    QtJson::JsonArray items;
    for (int i = 0; i < model->rowCount(parent); ++i) {
        for (int j = 0; j < model->columnCount(parent); ++j) {
            QModelIndex index = model->index(i, j, parent);
            QtJson::JsonObject item;
            dump_item_model_attrs(model, item, index, modelId, with_details);
            if (j == 0 && recursive && model->hasChildren(index)) {
                dump_items_model(model, item, index, modelId, true,
                                 with_details);
            }
            items << item;
        }
    }
    out["items"] = items;
}

QModelIndex get_model_item(QAbstractItemModel * model, const QString & path,
                           int row, int column) {
    QModelIndex parent;
    if (!path.isEmpty()) {
        QStringList parts = path.split("/");
        foreach (const QString & part, parts) {
            QStringList tmp = part.split("-");
            if (tmp.count() != 2) {
                return QModelIndex();
            }
            parent = model->index(tmp.at(0).toInt(), tmp.at(1).toInt(), parent);
            if (!parent.isValid()) {
                return parent;
            }
        }
    }

    return model->index(row, column, parent);
}

void dump_rect(const QRectF & rect, const QString & prefix,
               QtJson::JsonObject & out) {
    QtJson::JsonObject dumped;
    dumped["x"] = rect.x();
    dumped["y"] = rect.y();
    dumped["width"] = rect.width();
    dumped["height"] = rect.height();
    out[prefix] = dumped;
}

/**
 * Describes the items of a graphics scene.
 *
 * Where an item is drawn is what a test has to go on: most scene items are not
 * QObjects, so they answer to no name and carry no property, and a bare
 * identifier tells nothing about which of them is the horizon and which the
 * trajectory. The rectangles are given in the three frames a caller needs -
 * the scene the application computes in, the view it scrolls, and the screen.
 */
void dump_graphics_items(const QList<QGraphicsItem *> & items,
                         QGraphicsView * view, const qulonglong & viewid,
                         QtJson::JsonObject & out) {
    QtJson::JsonArray outitems;
    foreach (QGraphicsItem * item, items) {
        QtJson::JsonObject outitem;
        outitem["gid"] = graphicsItemId(item);
        outitem["viewid"] = viewid;
        QObject * itemObject = dynamic_cast<QObject *>(item);
        outitem["is_qobject"] = itemObject != NULL;
        if (itemObject) {
            const QMetaObject * mo = itemObject->metaObject();
            QStringList classes;
            while (mo) {
                classes << mo->className();
                mo = mo->superClass();
            }
            outitem["classes"] = classes;
            outitem["objectname"] = itemObject->objectName();
        }
        outitem["type"] = item->type();
        outitem["visible"] = item->isVisible();
        outitem["enabled"] = item->isEnabled();
        outitem["selected"] = item->isSelected();
        outitem["z"] = item->zValue();
        const QRectF sceneRect =
            item->mapToScene(item->boundingRect()).boundingRect();
        dump_rect(sceneRect, "scene_rect", outitem);
        if (view) {
            // Scenes here span billions of units, and mapping such a rectangle
            // into the view overflows the int arithmetic QRect is built on -
            // Qt asserts on it. What a caller can click is what the viewport
            // shows, so the rectangle is clipped to that first.
            const QRectF shown =
                view->mapToScene(view->viewport()->rect()).boundingRect();
            const QRectF clipped = sceneRect.intersected(shown);
            if (!clipped.isEmpty()) {
                const QRect viewRect =
                    view->mapFromScene(clipped).boundingRect();
                dump_rect(viewRect, "view_rect", outitem);
                QRect globalRect = viewRect;
                globalRect.moveTopLeft(
                    view->viewport()->mapToGlobal(viewRect.topLeft()));
                dump_rect(globalRect, "global_rect", outitem);
            }
        }
        dump_graphics_items(item->childItems(), view, viewid, outitem);
        outitems << outitem;
    }
    out["items"] = outitems;
}

Player::Player(QIODevice * device, QObject * parent)
    : JsonClient(device, parent) {
}

qulonglong Player::registerObject(QObject * object) {
    if (!object) {
        return 0;
    }
    qulonglong id = (qulonglong)object;
    if (!m_registeredObjects.contains(id)) {
        connect(object, SIGNAL(destroyed(QObject *)), this,
                SLOT(objectDeleted(QObject *)));
        m_registeredObjects[id] = object;
    }
    return id;
}

QObject * Player::registeredObject(const qulonglong & id) {
    return m_registeredObjects[id];
}

void Player::objectDeleted(QObject * object) {
    qulonglong id = (qulonglong)object;
    m_registeredObjects.remove(id);
}

QtJson::JsonObject Player::list_actions(const QtJson::JsonObject &) {
    const QMetaObject * metaObject = this->metaObject();
    QStringList methods;
    for (int i = metaObject->methodOffset(); i < metaObject->methodCount();
         ++i) {
        QMetaMethod method = metaObject->method(i);
        if (method.methodType() == QMetaMethod::Slot) {
            methods << QString::fromLatin1(
                metaObject->method(i).methodSignature());
        }
    }
    QtJson::JsonObject result;
    result["commands"] = methods;
    return result;
}

/**
 * Finds the widgets a caller is after, without sending the rest.
 *
 * Asking for the whole tree to pick a tab bar out of it costs ten thousand nodes
 * on the wire and about ten seconds; the filter belongs on the side that already
 * has the objects. A class matches anywhere in the inheritance chain, so
 * `QTabBar` also answers the application's own subclass of it.
 */
QtJson::JsonObject Player::widgets_find(const QtJson::JsonObject & command) {
    const QString className = command["class_name"].toString();
    const QString objectName = command["objectname"].toString();
    const bool visibleOnly = command["visible_only"].toBool();
    const bool withProperties = command["with_properties"].toBool();
    const int limit =
        command["limit"].isNull() ? 100 : command["limit"].toInt();
    if (className.isEmpty() && objectName.isEmpty()) {
        return createError(
            "MissingFilter",
            QString::fromUtf8("Give class_name or objectname to look for"));
    }
    registerTopLevelObjects();

    QList<QWidget *> queue = QApplication::topLevelWidgets();
    QtJson::JsonArray items;
    while (!queue.isEmpty() && items.size() < limit) {
        QWidget * widget = queue.takeFirst();
        foreach (QObject * child, widget->children()) {
            if (QWidget * childWidget = qobject_cast<QWidget *>(child)) {
                queue << childWidget;
            }
        }
        if (visibleOnly && !widget->isVisible()) {
            continue;
        }
        if (!objectName.isEmpty() &&
            !widget->objectName().contains(objectName, Qt::CaseInsensitive)) {
            continue;
        }
        if (!className.isEmpty()) {
            bool matches = false;
            const QMetaObject * metaObject = widget->metaObject();
            while (metaObject) {
                if (QString::fromLatin1(metaObject->className())
                        .contains(className, Qt::CaseInsensitive)) {
                    matches = true;
                    break;
                }
                metaObject = metaObject->superClass();
            }
            if (!matches) {
                continue;
            }
        }
        QtJson::JsonObject one;
        one["oid"] = registerObject(widget);
        dump_object(widget, one, withProperties);
        items << one;
    }
    QtJson::JsonObject result;
    result["items"] = items;
    return result;
}

QtJson::JsonObject Player::widget_by_path(const QtJson::JsonObject & command) {
    QString path = command["path"].toString();
    QObject * o = findObject(path);
    qulonglong id = registerObject(o);
    if (id == 0) {
        return createError(
            "InvalidWidgetPath",
            QString("Unable to find widget with path `%1`").arg(path));
    }
    QtJson::JsonObject result;
    result["oid"] = id;
    dump_object(o, result);
    return result;
}

QtJson::JsonObject Player::quick_item_find(const QtJson::JsonObject & command) {
    QtJson::JsonObject result;
#ifdef QT_QUICK_LIB
    QuickWindowLocatorContext ctx(this, command, "quick_window_oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QQuickItem * item = NULL;
    qulonglong id;
    QString qid = command["qid"].toString();
    if (!qid.isEmpty()) {
        item = ObjectPath::findQuickItemById(ctx.window->contentItem(), qid);
        id = registerObject(item);
        if (id == 0) {
            return createError(
                "InvalidQuickItem",
                QString("Unable to find quick item with id `%1`").arg(qid));
        }
    } else {
        QString path = command["path"].toString();
        item = ObjectPath::findQuickItem(ctx.window, path);
        id = registerObject(item);
        if (id == 0) {
            return createError(
                "InvalidQuickItem",
                QString("Unable to find quick item with path `%1`").arg(path));
        }
    }
    result["oid"] = id;
    result["quick_window_oid"] = command["quick_window_oid"].toString();
    dump_object(item, result);
#else
    Q_UNUSED(command);
    result = createQtQuickOnlyError();
#endif
    return result;
}

QtJson::JsonObject Player::quick_item_at(const QtJson::JsonObject & command) {
    QtJson::JsonObject result;
#ifdef QT_QUICK_LIB
    QuickWindowLocatorContext ctx(this, command, "quick_window_oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    const QPointF scenePos(command["x"].toDouble(), command["y"].toDouble());
    const bool with_properties = command["with_properties"].toBool();
    QtJson::JsonArray items;
    foreach (QQuickItem * item,
             ObjectPath::quickItemsAt(ctx.window->contentItem(), scenePos)) {
        QtJson::JsonObject dumped;
        dumped["oid"] = registerObject(item);
        dump_object(item, dumped, with_properties);
        items << dumped;
    }
    result["items"] = items;
#else
    Q_UNUSED(command);
    result = createQtQuickOnlyError();
#endif
    return result;
}

QtJson::JsonObject Player::active_widget(const QtJson::JsonObject & command) {
    QObject * active = NULL;
    QString type = command["type"].toString();
    if (type == "modal") {
        active = QApplication::activeModalWidget();
        if (!active) {
            active = QApplication::modalWindow();
        }
    } else if (type == "popup" || type == "popup_window") {
        if (type == "popup") {
            active = QApplication::activePopupWidget();
        }
        if (!active) {
            // A QML Menu is a window of its own, not a widget: the style
            // decides, and a popup window is what Qt Quick Controls use by
            // default. activePopupWidget() never reports one.
            QWindow * focus = QGuiApplication::focusWindow();
            if (focus && focus->isVisible() &&
                (focus->flags() & Qt::WindowType_Mask) == Qt::Popup) {
                active = focus;
            } else {
                foreach (QWindow * window, QGuiApplication::topLevelWindows()) {
                    if (window->isVisible() &&
                        (window->flags() & Qt::WindowType_Mask) == Qt::Popup) {
                        // the last one opened is the innermost submenu
                        active = window;
                    }
                }
            }
        }
    } else if (type == "focus") {
        active = QApplication::focusWidget();
        if (!active) {
            active = QApplication::focusWindow();
        }
    } else {
        active = QApplication::activeWindow();
        if (!active) {
            QWindowList lst = QGuiApplication::topLevelWindows();
            if (!lst.isEmpty()) {
                active = lst.first();
            }
        }
    }
    if (!active) {
        return createError(
            "NoActiveWindow",
            QString::fromUtf8("There is no active widget (%1)").arg(type));
    }
    qulonglong id = registerObject(active);
    QtJson::JsonObject result;
    result["oid"] = id;
    dump_object(active, result);
    return result;
}

ObjectLocatorContext::ObjectLocatorContext(Player * player,
                                           const QtJson::JsonObject & command,
                                           const QString & oidKey) {
    id = command[oidKey].value<qulonglong>();
    obj = player->registeredObject(id);
    if (!obj) {
        lastError = player->createError(
            "NotRegisteredObject",
            QString::fromUtf8(
                "The object (id:%1) is not registered or has been destroyed")
                .arg(id));
    }
}

#ifdef QT_QUICK_LIB
QuickWindowLocatorContext::QuickWindowLocatorContext(
    Player * player, const QtJson::JsonObject & command, const QString & objKey)
    : ObjectLocatorContext(player, command, objKey) {
    if (!hasError()) {
        window = resolveQuickWindow(obj);
        if (!window) {
            lastError = player->createError(
                "NotAQuickWindow",
                QString::fromUtf8("Object (id:%1) is neither a QQuickWindow "
                                  "nor a QQuickWidget")
                    .arg(id));
        }
    }
}

QuickItemLocatorContext::QuickItemLocatorContext(
    Player * player, const QtJson::JsonObject & command, const QString & objKey)
    : ObjectLocatorContext(player, command, objKey) {
    if (!hasError()) {
        item = qobject_cast<QQuickItem *>(obj);
        if (!item) {
            lastError = player->createError(
                "NotAWidget",
                QString::fromUtf8("Object (id:%1) is not a QQuickItem")
                    .arg(id));
        } else {
            window = item->window();
            if (!window) {
                lastError = player->createError(
                    "NoWindowForQuickItem",
                    "No QQuickWindow associated to the item.");
            }
        }
    }
}
#endif

QtJson::JsonObject Player::object_properties(
    const QtJson::JsonObject & command) {
    ObjectLocatorContext ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QtJson::JsonObject result;
    dump_properties(ctx.obj, result);
    return result;
}

QtJson::JsonObject Player::object_set_properties(
    const QtJson::JsonObject & command) {
    ObjectLocatorContext ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QVariantMap properties = command["properties"].value<QVariantMap>();
    _object_set_properties(ctx.obj, properties);
    QtJson::JsonObject result;
    return result;
}

void Player::_object_set_properties(QObject * object,
                                    const QVariantMap & properties) {
    for (QtJson::JsonObject::const_iterator iter = properties.begin();
         iter != properties.end(); ++iter) {
        object->setProperty(iter.key().toStdString().c_str(), iter.value());
    }
}

#ifdef QT_QUICK_LIB
void recursive_list_quick_item(QQuickItem * item, QtJson::JsonObject & out,
                               bool with_properties) {
    QtJson::JsonObject resultItems, resultItem;
    dump_object(item, resultItem, with_properties);
    foreach (QQuickItem * child, item->childItems()) {
        recursive_list_quick_item(child, resultItems, with_properties);
    }
    resultItem["children"] = resultItems;
    out[quickObjectName(item)] = resultItem;
}
#endif

/**
 * Lists the QML scene a widget renders, if it renders one.
 *
 * The scene of a QQuickWidget hangs off an offscreen window rather than off
 * the widget, so walking children() never reaches it and the widget looks like
 * a leaf. Listing the children of the content item matches what
 * findQuickItem() expects a path to start with.
 */
void list_quick_scene(QObject * object, QtJson::JsonObject & out,
                      bool with_properties) {
#ifdef QT_QUICK_LIB
    QQuickWindow * window = resolveQuickWindow(object);
    QQuickItem * content = window ? window->contentItem() : NULL;
    if (content) {
        foreach (QQuickItem * child, content->childItems()) {
            recursive_list_quick_item(child, out, with_properties);
        }
    }
#else
    Q_UNUSED(object);
    Q_UNUSED(out);
    Q_UNUSED(with_properties);
#endif
}

void recursive_list_widget(QWidget * widget, QtJson::JsonObject & out,
                           bool with_properties) {
    QtJson::JsonObject resultWidgets, resultWidget;
    dump_object(widget, resultWidget, with_properties);
    foreach (QObject * obj, widget->children()) {
        QWidget * subWidget = qobject_cast<QWidget *>(obj);
        if (subWidget) {
            recursive_list_widget(subWidget, resultWidgets, with_properties);
        }
    }
    list_quick_scene(widget, resultWidgets, with_properties);
    resultWidget["children"] = resultWidgets;
    out[objectName(widget)] = resultWidget;
}

/**
 * Counts the widgets of the application.
 *
 * Used to refuse a listing that would answer with every property of every
 * widget: in an application of ten thousand widgets that answer is tens of
 * megabytes built up recursively, and it takes the application down without
 * so much as an assert - twice reproduced on StarSteer. Reading the same
 * properties one widget at a time is fine, so the size of the single answer
 * is what does it.
 */
static int countWidgets() {
    int seen = 0;
    QList<QWidget *> queue = QApplication::topLevelWidgets();
    while (!queue.isEmpty()) {
        QWidget * widget = queue.takeFirst();
        ++seen;
        foreach (QObject * child, widget->children()) {
            if (QWidget * childWidget = qobject_cast<QWidget *>(child)) {
                queue << childWidget;
            }
        }
    }
    return seen;
}

QtJson::JsonObject Player::widgets_list(const QtJson::JsonObject & command) {
    bool with_properties = command["with_properties"].toBool();
    QtJson::JsonObject result;
    if (command.contains("oid")) {
        ObjectLocatorContext ctx(this, command, "oid");
        if (ctx.hasError()) {
            return ctx.lastError;
        }
        foreach (QObject * obj, ctx.obj->children()) {
            QWidget * subWidget = qobject_cast<QWidget *>(obj);
            if (subWidget) {
                recursive_list_widget(subWidget, result, with_properties);
            }
        }
        // the QML scene of the object asked about, which is not among its
        // children: without this, listing a QQuickWidget or a popup window by
        // its oid answers nothing while listing its parent shows the scene
        list_quick_scene(ctx.obj, result, with_properties);
    } else {
        if (with_properties && !command["force"].toBool()) {
            const int count = countWidgets();
            if (count > gMaxWidgetsWithProperties) {
                return createError(
                    "TooManyWidgets",
                    QString::fromUtf8(
                        "Listing all %1 widgets with their properties is what "
                        "brings the application down; ask for a subtree by "
                        "`oid`, drop `with_properties`, or pass `force` to "
                        "insist")
                        .arg(count));
            }
        }
        registerTopLevelObjects();
        QList<QWidget *> widgets = QApplication::topLevelWidgets();
        if (!widgets.isEmpty()) {
            foreach (QWidget * widget, widgets) {
                recursive_list_widget(widget, result, with_properties);
            }
        } else {
            // no qwidgets, this is probably a qtquick app - anyway, check for
            // windows
            foreach (QWindow * window, QApplication::topLevelWindows()) {
                QtJson::JsonObject resultWindow;
                dump_object(window, resultWindow, with_properties);
                result[resultWindow["path"].toString()] = resultWindow;
            }
        }
    }
    return result;
}

QtJson::JsonObject Player::windows_list(const QtJson::JsonObject & command) {
    const bool with_properties = command["with_properties"].toBool();
    const bool with_scene = command["with_scene"].toBool();
    const bool only_popups = command["only_popups"].toBool();
    registerTopLevelObjects();

    // Every top-level widget has a window of its own behind it, and every
    // QQuickWidget an offscreen one; both are already reachable as widgets, so
    // reporting them again would bury the windows that are only windows - a
    // QML menu, for one.
    QSet<QWindow *> backing;
    foreach (QWidget * widget, QApplication::topLevelWidgets()) {
        if (widget->windowHandle()) {
            backing.insert(widget->windowHandle());
        }
    }

    QtJson::JsonObject result;
    foreach (QWindow * window, QGuiApplication::topLevelWindows()) {
        if (backing.contains(window)) {
            continue;
        }
#if defined(QT_QUICK_LIB) && defined(QT_QUICKWIDGETS_LIB)
        if (QQuickWindow * quickWindow = qobject_cast<QQuickWindow *>(window)) {
            if (hostingQuickWidget(quickWindow)) {
                continue;
            }
        }
#endif
        const bool isPopup =
            (window->flags() & Qt::WindowType_Mask) == Qt::Popup;
        if (only_popups && !isPopup) {
            continue;
        }
        QtJson::JsonObject dumped;
        dumped["oid"] = registerObject(window);
        dump_object(window, dumped, with_properties);
        dumped["visible"] = window->isVisible();
        dumped["active"] = window->isActive();
        dumped["is_popup"] = isPopup;
        const QRect geometry = window->geometry();
        dumped["x"] = geometry.x();
        dumped["y"] = geometry.y();
        dumped["width"] = geometry.width();
        dumped["height"] = geometry.height();
        if (window->transientParent()) {
            dumped["transient_parent"] = objectPath(window->transientParent());
        }
        if (with_scene) {
            QtJson::JsonObject scene;
            list_quick_scene(window, scene, with_properties);
            dumped["children"] = scene;
        }
        result[dumped["path"].toString()] = dumped;
    }
    return result;
}

QtJson::JsonObject Player::quit(const QtJson::JsonObject &) {
    if (qApp) {
        qApp->exit();
    }
    QtJson::JsonObject result;
    return result;
}

QtJson::JsonObject Player::action_trigger(const QtJson::JsonObject & command) {
    WidgetLocatorContext<QAction> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    bool blocking = command["blocking"].toBool();
    if (blocking) {
        // block until QAction::trigger() returns
        ctx.widget->trigger();
    } else {
        // trigger the action, but return immediately
        QTimer::singleShot(0, ctx.widget, SLOT(trigger()));
    }
    QtJson::JsonObject result;
    return result;
}

QtJson::JsonObject Player::widget_click(const QtJson::JsonObject & command) {
    WidgetLocatorContext<QWidget> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QString action = command["mouseAction"].toString();
    QPoint pos = ctx.widget->rect().center();
    if (action == "doubleclick") {
        mouse_dclick(ctx.widget, pos);
    } else if (action == "rightclick") {
        mouse_click(ctx.widget, pos, Qt::RightButton);
    } else if (action == "middleclick") {
        mouse_click(ctx.widget, pos, Qt::MiddleButton);
    } else {
        mouse_click(ctx.widget, pos, Qt::LeftButton);
    }
    QtJson::JsonObject result;
    return result;
}

QtJson::JsonObject Player::quick_item_click(
    const QtJson::JsonObject & command) {
#ifdef QT_QUICK_LIB
    QuickItemLocatorContext ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }

    QPointF relativeCenter(ctx.item->width() / 2.0, ctx.item->height() / 2.0);

    QPoint sPos = ctx.item->mapToScene(relativeCenter).toPoint();

    // a QML scene builds its own context menu from a right click, so the
    // button is part of what a test asks for
    Qt::MouseButton button = Qt::LeftButton;
    const QString buttonName = command["button"].toString();
    if (buttonName == "right") {
        button = Qt::RightButton;
    } else if (buttonName == "middle") {
        button = Qt::MiddleButton;
    } else if (!buttonName.isEmpty() && buttonName != "left") {
        return createError(
            "InvalidButton",
            QString::fromUtf8("Unknown mouse button `%1`").arg(buttonName));
    }
    // An application may show a menu from the press itself, in a nested event
    // loop; the release that follows would then land in that menu and pick
    // whatever entry is under the cursor. Pressing and releasing separately is
    // what such a case needs.
    const QString action = command["mouseAction"].toString();
    bool press = true, release = true;
    if (action == "press") {
        release = false;
    } else if (action == "release") {
        press = false;
    } else if (!action.isEmpty() && action != "click") {
        return createError(
            "InvalidAction",
            QString::fromUtf8("Unknown action `%1`; it must be click, press or "
                              "release").arg(action));
    }
    quick_mouse_click(ctx.window, sPos, button, press, release);
    QtJson::JsonObject result;
    return result;
#else
    Q_UNUSED(command);
    return createQtQuickOnlyError();
#endif
}

QtJson::JsonObject Player::widget_move(const QtJson::JsonObject & command) {
  WidgetLocatorContext<QWidget> ctx(this, command, "oid");
  if (ctx.hasError()) {
      return ctx.lastError;
  }

  QPoint pos = ctx.widget->pos();
  if (!command["x"].isNull()) {
    pos.setX(command["x"].toInt());
  }
  if (!command["y"].isNull()) {
    pos.setY(command["y"].toInt());
  }
  ctx.widget->move(pos);

  QtJson::JsonObject result;
  result["x"] = ctx.widget->x();
  result["y"] = ctx.widget->y();
  return result;
}

QtJson::JsonObject Player::widget_resize(const QtJson::JsonObject & command) {
  WidgetLocatorContext<QWidget> ctx(this, command, "oid");
  if (ctx.hasError()) {
      return ctx.lastError;
  }

  QSize size = ctx.widget->size();
  if (!command["width"].isNull()) {
    size.setWidth(command["width"].toInt());
  }
  if (!command["height"].isNull()) {
    size.setHeight(command["height"].toInt());
  }
  ctx.widget->resize(size);

  QtJson::JsonObject result;
  result["width"] = ctx.widget->width();
  result["height"] = ctx.widget->height();
  return result;
}

QtJson::JsonObject Player::widget_close(const QtJson::JsonObject & command) {
    WidgetLocatorContext<QWidget> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }

    QTimer::singleShot(0, ctx.widget, SLOT(close()));

    QtJson::JsonObject result;
    return result;
}

QtJson::JsonObject Player::widget_map_position(
    const QtJson::JsonObject & command) {
    WidgetLocatorContext<QWidget> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QWidget * parent = 0;
    if (!command["parent_oid"].isNull()) {
        WidgetLocatorContext<QWidget> parentCtx(this, command, "parent_oid");
        if (parentCtx.hasError()) {
            return ctx.lastError;
        } else {
            parent = parentCtx.widget;
        }
    }
    QString direction = command["direction"].toString();
    QPoint pos;
    pos.setX(command["x"].toInt());
    pos.setY(command["y"].toInt());

    if (direction == "from") {
        if (parent) {
            pos = ctx.widget->mapFrom(parent, pos);
        } else {
            pos = ctx.widget->mapFromGlobal(pos);
        }
    } else if (direction == "to") {
        if (parent) {
            pos = ctx.widget->mapTo(parent, pos);
        } else {
            pos = ctx.widget->mapToGlobal(pos);
        }
    } else {
        return createError(
            "InvalidDirection",
            QString::fromUtf8("The direction '%1' is invalid").arg(direction));
    }

    QtJson::JsonObject result;
    result["x"] = pos.x();
    result["y"] = pos.y();
    return result;
}

QtJson::JsonObject Player::model(const QtJson::JsonObject & command) {
    ObjectLocatorContext ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }

    QAbstractItemModel * model = 0;
    if (QAbstractItemView * view = qobject_cast<QAbstractItemView *>(ctx.obj)) {
        model = view->model();
    } else if (QComboBox * cbx = qobject_cast<QComboBox *>(ctx.obj)) {
        model = cbx->model();
    }

    qulonglong modelId = registerObject(model);
    if (modelId != 0) {
        QtJson::JsonObject result;
        result["oid"] = modelId;
        dump_object(model, result);
        return result;
    } else {
        return createError(
            "MissingModel",
            QString("Unable to find model for object with id `%1`")
                .arg(ctx.id));
    }
}

QtJson::JsonObject Player::model_items(const QtJson::JsonObject & command) {
    ObjectLocatorContext ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }

    QAbstractItemModel * model = qobject_cast<QAbstractItemModel *>(ctx.obj);
    if (!model) {
        return createError(
            "NotAModel",
            QString("Object with id `%1` is not a QAbstractItemModel")
                .arg(ctx.id));
    }

    QtJson::JsonObject result;
    bool recursive = !(ctx.obj->inherits("QAbstractTableModel") ||
                       ctx.obj->inherits("QAbstractListModel"));
    dump_items_model(model, result, QModelIndex(), ctx.id, recursive,
                     command["with_details"].toBool());
    return result;
}

QtJson::JsonObject Player::model_item_action(
    const QtJson::JsonObject & command) {
    WidgetLocatorContext<QAbstractItemView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QAbstractItemModel * model = ctx.widget->model();
    if (!model) {
        return createError(
            "MissingModel",
            QString::fromUtf8("The view (id:%1) has no associated model")
                .arg(ctx.id));
    }
    QModelIndex index =
        get_model_item(model, command["itempath"].toString(),
                       command["row"].toInt(), command["column"].toInt());
    if (!index.isValid()) {
        return createError(
            "MissingModelItem",
            QString::fromUtf8("Unable to find an item identified by %1")
                .arg(command["itempath"].toString()));
    }
    ctx.widget->scrollTo(index);  // item visible
    QString itemaction = command["itemaction"].toString();

    QPoint cursorPosition;

    if (itemaction == "click" || itemaction == "doubleclick" ||
        itemaction == "contextmenu") {
        QString origin = command["origin"].toString();
        int offsetX = command["offset_x"].toInt();
        int offsetY = command["offset_y"].toInt();
        QRect visualRect = ctx.widget->visualRect(index);
        cursorPosition = visualRect.center();
        if (origin == "left") {
            cursorPosition.setX(visualRect.x());
        } else if (origin == "right") {
            cursorPosition.setX(visualRect.width() - 1);
        }
        int newX = cursorPosition.x() + offsetX;
        int newY = cursorPosition.y() + offsetY;

        /* The new coordinates have to be within the bounds */
        if (newX < visualRect.x())
            newX = visualRect.x() + 2;
        else if (newX > visualRect.x() + visualRect.width())
            newX = visualRect.x() + visualRect.width() - 2;

        if (newY < visualRect.y())
            newY = visualRect.y() + 2;
        else if (newY > visualRect.y() + visualRect.height())
            newY = visualRect.y() + visualRect.height() - 2;

        cursorPosition.setX(newX);
        cursorPosition.setY(newY);
    }

    QString resultingCheckState;
    if (itemaction == "select") {
        _model_item_action(itemaction, ctx.widget, index);
    } else if (itemaction == "edit") {
        _model_item_action(itemaction, ctx.widget, index);
    } else if (itemaction == "scrollto") {
        // the scrollTo() above is the whole action: bring the item into view
        // and leave the selection alone, unlike "select"
    } else if (itemaction == "expand" || itemaction == "collapse") {
        QTreeView * tree = qobject_cast<QTreeView *>(ctx.widget);
        if (!tree) {
            return createError(
                "NotATreeView",
                QString::fromUtf8("The view (id:%1) is not a QTreeView, it "
                                  "cannot %2 an item")
                    .arg(ctx.id)
                    .arg(itemaction));
        }
        tree->setExpanded(index, itemaction == "expand");
    } else if (itemaction == "check" || itemaction == "uncheck" ||
               itemaction == "toggle") {
        // the same setData() the check box delegate performs on a click
        const QVariant current = model->data(index, Qt::CheckStateRole);
        if (!current.isValid()) {
            return createError(
                "NotCheckable",
                QString::fromUtf8("The item %1 has no check state")
                    .arg(command["itempath"].toString()));
        }
        Qt::CheckState state = Qt::Checked;
        if (itemaction == "uncheck" ||
            (itemaction == "toggle" &&
             static_cast<Qt::CheckState>(current.toUInt()) == Qt::Checked)) {
            state = Qt::Unchecked;
        }
        if (!model->setData(index, state, Qt::CheckStateRole)) {
            return createError(
                "CheckStateRefused",
                QString::fromUtf8("The model refused the check state of item %1")
                    .arg(command["itempath"].toString()));
        }
        resultingCheckState = check_state_name(static_cast<Qt::CheckState>(
            model->data(index, Qt::CheckStateRole).toUInt()));
    } else if (itemaction == "contextmenu") {
        // A right click of its own opens no context menu: the platform, not
        // the mouse event, is what makes Qt deliver a context menu event, so a
        // synthesized click never reaches contextMenuEvent(). Post that event
        // instead. It is posted, not sent: a menu shown with exec() runs a
        // nested event loop, and sending would block this command until the
        // menu closes.
        ctx.widget->setCurrentIndex(index);
        QWidget * viewport = ctx.widget->viewport();
        qApp->postEvent(viewport, new QContextMenuEvent(
                                      QContextMenuEvent::Mouse, cursorPosition,
                                      viewport->mapToGlobal(cursorPosition)));
    } else if (itemaction == "click") {
        mouse_click(ctx.widget->viewport(), cursorPosition, Qt::LeftButton);
    } else if (itemaction == "rightclick") {
        mouse_click(ctx.widget->viewport(), cursorPosition, Qt::RightButton);
    } else if (itemaction == "middleclick") {
        mouse_click(ctx.widget->viewport(), cursorPosition, Qt::MiddleButton);
    } else if (itemaction == "doubleclick") {
        mouse_dclick(ctx.widget->viewport(), cursorPosition);
    } else {
        return createError(
            "MissingItemAction",
            QString::fromUtf8("itemaction %1 unknown").arg(itemaction));
    }
    QtJson::JsonObject result;
    if (!resultingCheckState.isEmpty()) {
        result["check_state"] = resultingCheckState;
    }
    return result;
}

/**
 * Returns the text of an action as it reads in the menu: without the shortcut
 * hint a menu paints on the right, and without the ampersands marking the
 * accelerator letter.
 */
static QString menu_action_text(const QAction * action) {
    QString text = action->text();
    const int tab = text.indexOf(QLatin1Char('\t'));
    if (tab >= 0) {
        text.truncate(tab);
    }
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

static QAction * find_menu_action(QWidget * menu, const QString & text) {
    foreach (QAction * action, menu->actions()) {
        if (menu_action_text(action) == text) {
            return action;
        }
    }
    foreach (QAction * action, menu->actions()) {
        if (menu_action_text(action).compare(text, Qt::CaseInsensitive) == 0) {
            return action;
        }
    }
    return NULL;
}

/**
 * Locates the menu a menu command addresses: the one given by oid, or the
 * popup currently open when no oid is given - which is what a context menu is.
 */
QWidget * Player::locate_menu(const QtJson::JsonObject & command,
                              QtJson::JsonObject & error) {
    QWidget * menu = NULL;
    if (command.contains("oid")) {
        ObjectLocatorContext ctx(this, command, "oid");
        if (ctx.hasError()) {
            error = ctx.lastError;
            return NULL;
        }
        menu = qobject_cast<QWidget *>(ctx.obj);
        if (!menu) {
            error = createError(
                "NotAWidget",
                QString::fromUtf8("Object (id:%1) is not a widget with actions")
                    .arg(ctx.id));
            return NULL;
        }
    } else {
        menu = QApplication::activePopupWidget();
        if (!menu) {
            error = createError("NoActivePopup",
                                "There is no popup menu open");
            return NULL;
        }
    }
    return menu;
}

QtJson::JsonObject Player::menu_actions(const QtJson::JsonObject & command) {
    QtJson::JsonObject error;
    QWidget * menu = locate_menu(command, error);
    if (!menu) {
        return error;
    }

    QMenu * asMenu = qobject_cast<QMenu *>(menu);
    QtJson::JsonArray actions;
    foreach (QAction * action, menu->actions()) {
        QtJson::JsonObject item;
        item["oid"] = registerObject(action);
        item["text"] = menu_action_text(action);
        item["raw_text"] = action->text();
        item["object_name"] = action->objectName();
        item["enabled"] = action->isEnabled();
        item["visible"] = action->isVisible();
        item["checkable"] = action->isCheckable();
        item["checked"] = action->isChecked();
        item["separator"] = action->isSeparator();
        item["has_submenu"] = action->menu() != NULL;
        if (action->menu()) {
            // a submenu can be read without opening it, so a test can assert
            // what is inside without walking the menu with the pointer
            item["submenu_oid"] = registerObject(action->menu());
        }
        item["has_icon"] = !action->icon().isNull();
        if (!action->shortcut().isEmpty()) {
            item["shortcut"] = action->shortcut().toString();
        }
        if (asMenu) {
            // where the row is painted, for a screenshot of one entry
            const QRect rect = asMenu->actionGeometry(action);
            const QPoint global = asMenu->mapToGlobal(rect.topLeft());
            item["x"] = rect.x();
            item["y"] = rect.y();
            item["width"] = rect.width();
            item["height"] = rect.height();
            item["global_x"] = global.x();
            item["global_y"] = global.y();
        }
        actions << item;
    }

    QtJson::JsonObject result;
    result["menu_oid"] = registerObject(menu);
    result["path"] = objectPath(menu);
    result["actions"] = actions;
    return result;
}

QtJson::JsonObject Player::menu_trigger(const QtJson::JsonObject & command) {
    QtJson::JsonObject error;
    QWidget * menu = locate_menu(command, error);
    if (!menu) {
        return error;
    }

    QStringList parts;
    const QVariant path = command["path"];
    if (path.userType() == QMetaType::QVariantList) {
        foreach (const QVariant & part, path.toList()) {
            parts << part.toString();
        }
    } else {
        parts = path.toString().split("->");
    }
    for (int i = 0; i < parts.size(); ++i) {
        parts[i] = parts.at(i).trimmed();
    }
    if (parts.isEmpty() || parts.first().isEmpty()) {
        return createError("MissingActionPath",
                           "No action to trigger was given");
    }

    QWidget * current = menu;
    QAction * action = NULL;
    for (int i = 0; i < parts.size(); ++i) {
        action = find_menu_action(current, parts.at(i));
        if (!action) {
            QStringList available;
            foreach (QAction * candidate, current->actions()) {
                if (!candidate->isSeparator()) {
                    available << menu_action_text(candidate);
                }
            }
            return createError(
                "MissingAction",
                QString::fromUtf8("No action named `%1` in `%2`; it has: %3")
                    .arg(parts.at(i))
                    .arg(objectPath(current))
                    .arg(available.join(", ")));
        }
        if (i + 1 < parts.size()) {
            if (!action->menu()) {
                return createError(
                    "NotASubmenu",
                    QString::fromUtf8("The action `%1` has no submenu")
                        .arg(parts.at(i)));
            }
            current = action->menu();
        }
    }

    if (!action->isEnabled()) {
        return createError(
            "ActionDisabled",
            QString::fromUtf8("The action `%1` is disabled")
                .arg(parts.last()));
    }

    // A menu closes itself when the user picks an entry, and code triggered by
    // the entry often opens a dialog - which would come up behind a menu left
    // open. Closing first keeps the application in the state a real pick
    // leaves it in.
    if (command["close"].isNull() || command["close"].toBool()) {
        menu->close();
    }

    if (command["blocking"].toBool()) {
        action->trigger();
    } else {
        QTimer::singleShot(0, action, SLOT(trigger()));
    }

    QtJson::JsonObject result;
    result["oid"] = registerObject(action);
    result["text"] = menu_action_text(action);
    return result;
}

QtJson::JsonObject Player::object_property_object(
    const QtJson::JsonObject & command) {
    ObjectLocatorContext ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    const QString name = command["property"].toString();
    const QVariant value = ctx.obj->property(name.toLatin1().constData());
    if (!value.isValid()) {
        return createError("MissingProperty",
                           QString::fromUtf8("`%1` has no property `%2`")
                               .arg(ctx.obj->metaObject()->className())
                               .arg(name));
    }
    QObject * object = value.value<QObject *>();
    if (!object) {
        return createError(
            "NotAnObjectProperty",
            QString::fromUtf8("The property `%1` of `%2` holds no object")
                .arg(name)
                .arg(ctx.obj->metaObject()->className()));
    }

    QtJson::JsonObject result;
    result["oid"] = registerObject(object);
    dump_object(object, result, command["with_properties"].toBool());
    return result;
}

QtJson::JsonObject Player::object_methods(const QtJson::JsonObject & command) {
    ObjectLocatorContext ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    const bool inherited = command["inherited"].toBool();
    const QMetaObject * metaObject = ctx.obj->metaObject();
    const int firstMethod = inherited ? 0 : metaObject->methodOffset();
    const int firstProperty = inherited ? 0 : metaObject->propertyOffset();

    QtJson::JsonArray methods;
    for (int i = firstMethod; i < metaObject->methodCount(); ++i) {
        const QMetaMethod method = metaObject->method(i);
        QtJson::JsonObject item;
        item["name"] = QString::fromLatin1(method.name());
        item["signature"] = QString::fromLatin1(method.methodSignature());
        switch (method.methodType()) {
            case QMetaMethod::Signal:
                item["type"] = "signal";
                break;
            case QMetaMethod::Slot:
                item["type"] = "slot";
                break;
            case QMetaMethod::Method:
                item["type"] = "method";
                break;
            case QMetaMethod::Constructor:
                item["type"] = "constructor";
                break;
        }
        QStringList parameters;
        for (int p = 0; p < method.parameterCount(); ++p) {
            parameters << QString::fromLatin1(method.parameterTypeName(p));
        }
        item["parameters"] = parameters;
        item["return_type"] = QString::fromLatin1(method.typeName());
        item["class"] = QString::fromLatin1(method.enclosingMetaObject()->className());
        methods << item;
    }

    QtJson::JsonArray properties;
    for (int i = firstProperty; i < metaObject->propertyCount(); ++i) {
        const QMetaProperty property = metaObject->property(i);
        QtJson::JsonObject item;
        item["name"] = QString::fromLatin1(property.name());
        item["type"] = QString::fromLatin1(property.typeName());
        item["writable"] = property.isWritable();
        item["readable"] = property.isReadable();
        if (property.hasNotifySignal()) {
            item["notify"] =
                QString::fromLatin1(property.notifySignal().methodSignature());
        }
        properties << item;
    }

    QtJson::JsonObject result;
    dump_object(ctx.obj, result);
    result["methods"] = methods;
    result["properties_meta"] = properties;
    return result;
}

QtJson::JsonObject Player::widget_context_menu(
    const QtJson::JsonObject & command) {
    WidgetLocatorContext<QWidget> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QPoint pos = ctx.widget->rect().center();
    if (!command["x"].isNull()) {
        pos.setX(command["x"].toInt());
    }
    if (!command["y"].isNull()) {
        pos.setY(command["y"].toInt());
    }
    // posted, not sent: a menu shown with exec() runs a nested event loop and
    // sending would block this command until the menu closes
    qApp->postEvent(ctx.widget,
                    new QContextMenuEvent(QContextMenuEvent::Mouse, pos,
                                          ctx.widget->mapToGlobal(pos)));
    QtJson::JsonObject result;
    result["x"] = pos.x();
    result["y"] = pos.y();
    return result;
}

QtJson::JsonObject Player::model_item_rect(const QtJson::JsonObject & command) {
    WidgetLocatorContext<QAbstractItemView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QAbstractItemModel * model = ctx.widget->model();
    if (!model) {
        return createError(
            "MissingModel",
            QString::fromUtf8("The view (id:%1) has no associated model")
                .arg(ctx.id));
    }
    QModelIndex index =
        get_model_item(model, command["itempath"].toString(),
                       command["row"].toInt(), command["column"].toInt());
    if (!index.isValid()) {
        return createError(
            "MissingModelItem",
            QString::fromUtf8("Unable to find an item identified by %1")
                .arg(command["itempath"].toString()));
    }
    if (command["scroll"].toBool()) {
        ctx.widget->scrollTo(index);
    }

    // the rect a real pointer needs: where the row is painted, on the screen
    QWidget * viewport = ctx.widget->viewport();
    const QRect rect = ctx.widget->visualRect(index);
    const QPoint global = viewport->mapToGlobal(rect.topLeft());
    QtJson::JsonObject result;
    result["x"] = rect.x();
    result["y"] = rect.y();
    result["width"] = rect.width();
    result["height"] = rect.height();
    result["global_x"] = global.x();
    result["global_y"] = global.y();
    result["visible"] = !rect.isEmpty() && viewport->rect().intersects(rect);
    if (QTreeView * tree = qobject_cast<QTreeView *>(ctx.widget)) {
        result["expanded"] = tree->isExpanded(index);
    }
    if (QItemSelectionModel * selection = ctx.widget->selectionModel()) {
        result["selected"] = selection->isSelected(index);
    }
    return result;
}

void Player::_model_item_action(const QString & action,
                                QAbstractItemView * widget,
                                const QModelIndex & index) {
    if (action == "select") {
        widget->setCurrentIndex(index);
    } else if (action == "edit") {
        widget->setCurrentIndex(index);
        widget->edit(index);
    }
}

QtJson::JsonObject Player::model_gitem_action(
    const QtJson::JsonObject & command) {
    WidgetLocatorContext<QGraphicsView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    qulonglong gid = command["gid"].value<qulonglong>();
    QGraphicsItem * item = graphicsItemFromId(ctx.widget, gid);
    if (!item) {
        return createError(
            "MissingGItem",
            QString::fromUtf8("The view (id:%1) has no associated item %2")
                .arg(ctx.id)
                .arg(gid));
    }
    if (command["ensure_visible"].isNull() ||
        command["ensure_visible"].toBool()) {
        ctx.widget->ensureVisible(item);
    }
    QString itemaction = command["itemaction"].toString();

    // The centre of the bounding rectangle is not on the item when the item is
    // a long polyline - a horizon crossing the section has its centre in empty
    // space - so a caller may name the point itself, in the item's own
    // coordinates or in the scene's.
    QPointF scenePos;
    if (!command["x"].isNull() && !command["y"].isNull()) {
        const QPointF given(command["x"].toDouble(), command["y"].toDouble());
        scenePos = command["scene"].toBool() ? given : item->mapToScene(given);
    } else {
        scenePos = item->mapToScene(item->boundingRect().center());
    }
    QPoint viewPos = ctx.widget->mapFromScene(scenePos);
    if (itemaction == "click" || itemaction == "rightclick" ||
        itemaction == "middleclick") {
        if (ctx.widget->scene() && ctx.widget->scene()->mouseGrabberItem()) {
            ctx.widget->scene()->mouseGrabberItem()->ungrabMouse();
        }
        if (itemaction == "rightclick") {
            mouse_click(ctx.widget->viewport(), viewPos, Qt::RightButton);
        } else if (itemaction == "middleclick") {
            mouse_click(ctx.widget->viewport(), viewPos, Qt::MiddleButton);
        } else {
            mouse_click(ctx.widget->viewport(), viewPos, Qt::LeftButton);
        }
    } else if (itemaction == "doubleclick") {
        if (ctx.widget->scene() && ctx.widget->scene()->mouseGrabberItem()) {
            ctx.widget->scene()->mouseGrabberItem()->ungrabMouse();
        }
        mouse_dclick(ctx.widget->viewport(), viewPos);
    } else {
        return createError(
            "MissingItemAction",
            QString::fromUtf8("itemaction %1 unknown").arg(itemaction));
    }
    QtJson::JsonObject result;
    return result;
}

/**
 * Tells whether a widget can be grabbed at all.
 *
 * A widget of no size makes the render-to-texture path assert, and one that
 * was never shown answers an empty image that reaches the caller as empty
 * data and fails far from here. Both are worth an error that says so.
 */
static QString grabRefusalReason(QWidget * widget) {
    if (!widget->isVisible()) {
        return QString::fromUtf8("it is not visible");
    }
    if (widget->size().isEmpty()) {
        return QString::fromUtf8("its size is %1x%2")
            .arg(widget->width())
            .arg(widget->height());
    }
    return QString();
}

static QtJson::JsonObject encodePixmap(const QPixmap & pixmap,
                                       const QString & wanted) {
    QString format = wanted.isEmpty() ? QString("PNG") : wanted;
    QBuffer buffer;
    pixmap.save(&buffer, "PNG");
    QtJson::JsonObject result;
    result["format"] = format;
    result["data"] = buffer.data().toBase64();
    return result;
}

QtJson::JsonObject Player::grab(const QtJson::JsonObject & command) {
    QPixmap pixmap;
    if (command.contains("oid")) {
        // grab a single widget
        WidgetLocatorContext<QWidget> ctx(this, command, "oid");
        if (ctx.hasError()) {
            return ctx.lastError;
        }
        const QString refusal = grabRefusalReason(ctx.widget);
        if (!refusal.isEmpty()) {
            return createError(
                "WidgetNotGrabbable",
                QString::fromUtf8("The widget (id:%1) cannot be grabbed: %2")
                    .arg(ctx.id)
                    .arg(refusal));
        }
#if QT_VERSION_MAJOR >= 6
        pixmap = ctx.widget->grab();
#else
        pixmap = QPixmap::grabWidget(ctx.widget);
#endif
    } else {
        // grab the whole screen
#if QT_VERSION_MAJOR >= 6
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            pixmap = screen->grabWindow();
        }
#else
        pixmap = QPixmap::grabWindow(QApplication::desktop()->winId());
#endif
    }
    return encodePixmap(pixmap, command["format"].toString());
}

namespace {
/**
 * Answers with a grab taken once the widget stopped changing.
 *
 * A scene is repainted a frame or two after whatever caused it - a layout
 * pass, a deferred update, a debounced map tile - and a grab taken in
 * between reads a framebuffer that was just cleared to the background. There
 * is no signal to wait for that every widget has, so the picture itself is
 * the signal: the same image twice in a row means the drawing has stopped.
 */
class SettledGrabResponse : public DelayedResponse {
public:
    SettledGrabResponse(Player * player, const QtJson::JsonObject & command,
                        QWidget * widget, int quiet, int stable, int deadline,
                        const QtJson::JsonObject & failure = QtJson::JsonObject())
        : DelayedResponse(player, command, quiet, deadline + 2 * quiet + 1000),
          m_player(player),
          m_widget(widget),
          m_format(command["format"].toString()),
          m_failure(failure),
          m_stable(stable < 2 ? 2 : stable),
          m_deadline(deadline),
          m_repeats(1) {
        m_elapsed.start();
    }

protected:
    void execute(int) override {
        if (!m_failure.isEmpty()) {
            writeResponse(m_failure);
            return;
        }
        if (m_widget.isNull()) {
            writeResponse(m_player->createError(
                "WidgetNotGrabbable",
                QString::fromUtf8("The widget is gone")));
            return;
        }
        const QString refusal = grabRefusalReason(m_widget);
        if (!refusal.isEmpty()) {
            writeResponse(m_player->createError(
                "WidgetNotGrabbable",
                QString::fromUtf8("The widget cannot be grabbed: %1")
                    .arg(refusal)));
            return;
        }
        const QPixmap pixmap = m_widget->grab();
        const QImage shot = pixmap.toImage();
        m_repeats = (!m_previous.isNull() && shot == m_previous) ? m_repeats + 1
                                                                 : 1;
        m_previous = shot;

        const bool settled = m_repeats >= m_stable;
        if (!settled && m_elapsed.elapsed() < m_deadline) {
            return;
        }
        QtJson::JsonObject result = encodePixmap(pixmap, m_format);
        result["settled"] = settled;
        result["waited_ms"] = static_cast<int>(m_elapsed.elapsed());
        writeResponse(result);
    }

private:
    Player * m_player;
    QPointer<QWidget> m_widget;
    QString m_format;
    QtJson::JsonObject m_failure;
    QImage m_previous;
    QElapsedTimer m_elapsed;
    int m_stable;
    int m_deadline;
    int m_repeats;
};
}  // namespace

DelayedResponse * Player::grab_settled(const QtJson::JsonObject & command) {
    WidgetLocatorContext<QWidget> ctx(this, command, "oid");
    if (ctx.hasError()) {
        // the slot signature fixes the shape of the answer, so the error
        // travels as a response that answers on its first turn
        return new SettledGrabResponse(this, command, NULL, 0, 2, 0,
                                       ctx.lastError);
    }
    const int quiet = command["quiet_ms"].isNull()
                          ? 200
                          : command["quiet_ms"].toInt();
    const int stable = command["stable_frames"].isNull()
                           ? 2
                           : command["stable_frames"].toInt();
    const int deadline = command["timeout_ms"].isNull()
                             ? 5000
                             : command["timeout_ms"].toInt();
    return new SettledGrabResponse(this, command, ctx.widget, quiet, stable,
                                   deadline);
}

QtJson::JsonObject Player::widget_keyclick(const QtJson::JsonObject & command) {
    QWidget * widget;
    if (command.contains("oid")) {
        WidgetLocatorContext<QWidget> ctx(this, command, "oid");
        if (ctx.hasError()) {
            return ctx.lastError;
        }
        widget = ctx.widget;
    } else {
        widget = qApp->activeWindow();
    }
    QString text = command["text"].toString();
    for (int i = 0; i < text.count(); ++i) {
        QChar ch = text[i];
        int key = (int)ch.toLatin1();
        qApp->postEvent(
            widget,
            new QKeyEvent(QKeyEvent::KeyPress, key, Qt::NoModifier, ch));
        qApp->postEvent(
            widget,
            new QKeyEvent(QKeyEvent::KeyRelease, key, Qt::NoModifier, ch));
    }
    QtJson::JsonObject result;
    return result;
}

DelayedResponse * Player::shortcut(const QtJson::JsonObject & command) {
    return new ShortcutResponse(this, command);
}

QtJson::JsonObject Player::tabbar_list(const QtJson::JsonObject & command) {
    WidgetLocatorContext<QTabBar> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QStringList texts;
    for (int i = 0; i < ctx.widget->count(); ++i) {
        texts << ctx.widget->tabText(i);
    }
    QtJson::JsonObject result;
    result["tabtexts"] = texts;
    return result;
}

QtJson::JsonObject Player::headerview_list(const QtJson::JsonObject & command) {
    WidgetLocatorContext<QHeaderView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QAbstractItemModel * model = ctx.widget->model();
    if (!model) {
        return createError(
            "MissingModel",
            QString::fromUtf8("The header view (id:%1) has no associated model")
                .arg(ctx.id));
    }
    QStringList texts;
    int nbItems = ctx.widget->orientation() == Qt::Vertical
        ? model->rowCount()
        : model->columnCount();
    for (int i = 0; i < nbItems; i++) {
        texts << model->headerData(i, ctx.widget->orientation()).toString();
    }
    QtJson::JsonObject result;
    result["headertexts"] = texts;
    return result;
}

QtJson::JsonObject Player::headerview_click(
    const QtJson::JsonObject & command) {
    WidgetLocatorContext<QHeaderView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    int logicalIndex;
    QVariant indexOrName = command["indexOrName"];
    if (indexOrName.type() == QVariant::String) {
        QString name = indexOrName.toString();
        QAbstractItemModel * model = ctx.widget->model();
        if (!model) {
            return createError(
                "MissingModel",
                QString::fromUtf8(
                    "The header view (id:%1) has no associated model")
                    .arg(ctx.id));
        }
        bool found = false;
        int nbItems = ctx.widget->orientation() == Qt::Horizontal
            ? model->rowCount()
            : model->columnCount();
        for (int i = 0; i < nbItems; i++) {
            if (name ==
                model->headerData(i, ctx.widget->orientation()).toString()) {
                logicalIndex = i;
                found = true;
                break;
            }
        }
        if (!found) {
            return createError(
                "MissingHeaderViewText",
                QString::fromUtf8(
                    "The header view (id:%1) has no text column `%2`")
                    .arg(ctx.id)
                    .arg(name));
        }
    } else {
        logicalIndex = ctx.widget->logicalIndex(command["indexOrName"].toInt());
    }

    int pos = ctx.widget->sectionPosition(logicalIndex);
    if (pos == -1) {
        return createError(
            "InvalidHeaderViewIndex",
            QString::fromUtf8(
                "The header view (id:%1) has no index %2 or it is hidden")
                .arg(ctx.id)
                .arg(logicalIndex));
    }
    QPoint mousePos;
    if (ctx.widget->orientation() == Qt::Horizontal) {
        mousePos.setY(ctx.widget->height() / 2);
        mousePos.setX(pos + ctx.widget->offset() + 5);
    } else {
        mousePos.setX(ctx.widget->width() / 2);
        mousePos.setY(pos + ctx.widget->offset() + 5);
    }
    mouse_click(ctx.widget->viewport(), mousePos, Qt::LeftButton);
    QtJson::JsonObject result;
    return result;
}

QtJson::JsonObject Player::headerview_path_from_view(
    const QtJson::JsonObject & command) {
    WidgetLocatorContext<QAbstractItemView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }

    QHeaderView * header = NULL;
    QTableView * tview = qobject_cast<QTableView *>(ctx.widget);
    if (tview) {
        if (command["orientation"] == "vertical") {
            header = tview->verticalHeader();
        } else {
            header = tview->horizontalHeader();
        }
    } else {
        QTreeView * trview = qobject_cast<QTreeView *>(ctx.widget);
        if (trview) {
            header = trview->header();
        }
    }

    if (!header) {
        return createError(
            "InvalidHeaderView",
            QString::fromUtf8("No header view found for the view (id:%1)")
                .arg(ctx.id));
    }
    QtJson::JsonObject result;
    result["headerpath"] = objectPath(header);
    return result;
}

QtJson::JsonObject Player::graphicsitems(const QtJson::JsonObject & command) {
    WidgetLocatorContext<QGraphicsView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QList<QGraphicsItem *> topLevelItems;
    foreach (QGraphicsItem * item, ctx.widget->items()) {
        if (!item->parentItem()) {
            topLevelItems << item;
        }
    }
    QtJson::JsonObject result;
    // Where a caller may click: a scene here is far larger than the viewport,
    // and a point outside this rectangle maps to a position off the widget.
    dump_rect(ctx.widget->mapToScene(ctx.widget->viewport()->rect())
                  .boundingRect(),
              "visible_scene_rect", result);
    dump_rect(QRectF(ctx.widget->viewport()->rect()), "viewport_rect", result);
    dump_graphics_items(topLevelItems, ctx.widget, ctx.id, result);
    return result;
}

/**
 * The items under a point, topmost first.
 *
 * A scene item has no name to look up, so a point is the only way to say which
 * one is meant; the product usually knows the scene coordinates of what it
 * drew, hence the choice of frame.
 */
QtJson::JsonObject Player::gitems_at(const QtJson::JsonObject & command) {
    WidgetLocatorContext<QGraphicsView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    const bool inScene = command["scene"].toBool();
    QList<QGraphicsItem *> items;
    if (inScene) {
        if (!ctx.widget->scene()) {
            return createError("MissingScene",
                               QString::fromUtf8("The view (id:%1) has no scene")
                                   .arg(ctx.id));
        }
        items = ctx.widget->scene()->items(
            QPointF(command["x"].toDouble(), command["y"].toDouble()));
    } else {
        items = ctx.widget->items(
            QPoint(command["x"].toInt(), command["y"].toInt()));
    }
    QtJson::JsonObject result;
    QtJson::JsonArray outitems;
    foreach (QGraphicsItem * item, items) {
        QtJson::JsonObject one;
        dump_graphics_items(QList<QGraphicsItem *>() << item, ctx.widget, ctx.id,
                            one);
        outitems << one["items"].toList().value(0);
    }
    result["items"] = outitems;
    return result;
}

QtJson::JsonObject Player::gitem_properties(
    const QtJson::JsonObject & command) {
    WidgetLocatorContext<QGraphicsView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    qulonglong gid = command["gid"].value<qulonglong>();
    QGraphicsItem * item = graphicsItemFromId(ctx.widget, gid);
    if (!item) {
        return createError(
            "MissingGItem",
            QString::fromUtf8("QGraphicsitem %1 is not in view %2")
                .arg(gid)
                .arg(ctx.id));
    }
    QObject * object = dynamic_cast<QObject *>(item);
    if (!object) {
        return createError(
            "GItemNotQObject",
            QString::fromUtf8(
                "QGraphicsitem %1 in view %2 does not inherit from QObject")
                .arg(gid)
                .arg(ctx.id));
    }
    QtJson::JsonObject result;
    dump_properties(object, result);
    return result;
}

DelayedResponse * Player::drag_n_drop(const QtJson::JsonObject & command) {
    return new DragNDropResponse(this, command);
}

/**
 * Reads the numbers of a geometry value out of what JSON can carry.
 *
 * A point, a size or a rectangle arrives either as a list of numbers or as a
 * map with the usual keys.
 */
static bool geometry_numbers(const QVariant & value, const QStringList & keys,
                             QList<qreal> & numbers) {
    if (value.userType() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        if (list.size() != keys.size()) {
            return false;
        }
        foreach (const QVariant & number, list) {
            if (!number.canConvert<qreal>()) {
                return false;
            }
            numbers << number.toReal();
        }
        return true;
    }
    if (value.userType() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        foreach (const QString & key, keys) {
            if (!map.contains(key) || !map.value(key).canConvert<qreal>()) {
                return false;
            }
            numbers << map.value(key).toReal();
        }
        return true;
    }
    return false;
}

/**
 * Builds a point, a size or a rectangle from a list or a map.
 *
 * QVariant converts between none of these and what JSON carries, so a method
 * taking a QPointF - or a QVariant holding one, the shape QML-facing code uses
 * - would read an empty point out of the list it was given.
 */
static bool build_geometry(const QVariant & value, int typeId, QVariant & out) {
    QList<qreal> n;
    switch (typeId) {
        case QMetaType::QPoint:
        case QMetaType::QPointF:
            if (!geometry_numbers(value, QStringList() << "x" << "y", n)) {
                return false;
            }
            out = typeId == QMetaType::QPoint
                      ? QVariant(QPoint(qRound(n.at(0)), qRound(n.at(1))))
                      : QVariant(QPointF(n.at(0), n.at(1)));
            return true;
        case QMetaType::QSize:
        case QMetaType::QSizeF:
            if (!geometry_numbers(value, QStringList() << "width" << "height",
                                  n)) {
                return false;
            }
            out = typeId == QMetaType::QSize
                      ? QVariant(QSize(qRound(n.at(0)), qRound(n.at(1))))
                      : QVariant(QSizeF(n.at(0), n.at(1)));
            return true;
        case QMetaType::QRect:
        case QMetaType::QRectF:
            if (!geometry_numbers(value,
                                  QStringList() << "x" << "y" << "width"
                                                << "height",
                                  n)) {
                return false;
            }
            out = typeId == QMetaType::QRect
                      ? QVariant(QRect(qRound(n.at(0)), qRound(n.at(1)),
                                       qRound(n.at(2)), qRound(n.at(3))))
                      : QVariant(QRectF(n.at(0), n.at(1), n.at(2), n.at(3)));
            return true;
        default:
            return false;
    }
}

QtJson::JsonObject Player::call_slot(const QtJson::JsonObject & command) {
    // any QObject will do: a QML item exposes its methods the same way a
    // widget exposes its slots, and forceActiveFocus() or selectAll() on a
    // text field are what a test needs there
    ObjectLocatorContext ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QString slot_name = command["slot_name"].toString();
    QVariant params = command["params"];
    QVariant result_slot;

    // The historical contract is a test hook shaped `QVariant f(QVariant)`.
    // Real methods rarely look like that: clear() takes nothing, a QML signal
    // such as activated(int) or toggled(itemId, checked) takes one or more
    // typed arguments. The shape is read from the meta object and matched
    // against what the caller gave: the hook shape wins when the caller gave
    // no list, then the method whose arity matches. Invoking a signal emits
    // it, which is how a QML control is driven without its popup.
    QVariantList givenArguments;
    if (params.userType() == QMetaType::QVariantList) {
        foreach (const QVariant & value, params.toList()) {
            givenArguments << value;
        }
    } else if (params.isValid() && !params.isNull()) {
        givenArguments << params;
    }
    if (givenArguments.size() > gMaxInvokeArguments) {
        return createError(
            "NoMethodInvoked",
            QString::fromUtf8("At most %1 arguments can be passed to %2")
                .arg(gMaxInvokeArguments)
                .arg(slot_name));
    }

    // A method declared as taking a QVariant - what QML-facing code does -
    // accepts anything, so nothing says that a pair of numbers meant a point.
    // `param_types` lets the caller name the type each argument must hold.
    const QVariantList wantedTypes = command["param_types"].toList();
    for (int i = 0; i < givenArguments.size() && i < wantedTypes.size(); ++i) {
        const QByteArray name = wantedTypes.at(i).toString().toLatin1();
        const QMetaType type = QMetaType::fromName(name.constData());
        if (!type.isValid()) {
            return createError("NoMethodInvoked",
                               QString::fromUtf8("Unknown type `%1`")
                                   .arg(QString::fromLatin1(name)));
        }
        QVariant built;
        if (build_geometry(givenArguments.at(i), type.id(), built)) {
            givenArguments[i] = built;
        } else if (!givenArguments[i].convert(type)) {
            return createError(
                "NoMethodInvoked",
                QString::fromUtf8("Argument %1 of %2 does not convert to %3")
                    .arg(i)
                    .arg(slot_name)
                    .arg(QString::fromLatin1(name)));
        }
    }

    const QMetaObject * metaObject = ctx.obj->metaObject();
    const QByteArray wanted = slot_name.toLatin1();
    QMetaMethod chosen;
    int rank = 0;  // 3: QVariant hook, 2: no argument, 1: arity matches
    QStringList signatures;
    for (int i = 0; i < metaObject->methodCount(); ++i) {
        const QMetaMethod method = metaObject->method(i);
        if (method.name() != wanted) {
            continue;
        }
        signatures << QString::fromLatin1(method.methodSignature());
        int methodRank = 0;
        if (method.parameterCount() == givenArguments.size()) {
            methodRank = method.parameterCount() == 0 ? 2 : 1;
        }
        if (params.userType() != QMetaType::QVariantList &&
            method.parameterCount() == 1 &&
            method.parameterType(0) == QMetaType::QVariant) {
            methodRank = 3;
        }
        if (methodRank > rank) {
            chosen = method;
            rank = methodRank;
        }
    }
    if (rank == 0) {
        return createError(
            "NoMethodInvoked",
            QString::fromUtf8("No method %1 on %2 takes %3 argument(s); it has: %4")
                .arg(slot_name)
                .arg(metaObject->className())
                .arg(givenArguments.size())
                .arg(signatures.isEmpty() ? QString("no such method")
                                          : signatures.join(", ")));
    }

    const bool returnsVariant = chosen.returnType() == QMetaType::QVariant;
    QVariantList arguments;
    if (rank == 3) {
        // an invalid QVariant is what a caller that gave nothing meant
        arguments << (givenArguments.isEmpty() ? params : givenArguments.at(0));
    } else if (rank >= 1) {
        arguments = givenArguments;
        for (int i = 0; i < arguments.size(); ++i) {
            const int parameterType = chosen.parameterType(i);
            if (parameterType == QMetaType::QVariant) {
                continue;
            }
            QVariant built;
            if (build_geometry(arguments.at(i), parameterType, built)) {
                arguments[i] = built;
                continue;
            }
#if QT_VERSION_MAJOR >= 6
            const QMetaType targetType(parameterType);
            const bool converted = arguments[i].convert(targetType);
            const char * typeName = targetType.name();
#else
            const bool converted = arguments[i].convert(parameterType);
            const char * typeName = QMetaType::typeName(parameterType);
#endif
            if (!converted) {
                return createError(
                    "NoMethodInvoked",
                    QString::fromUtf8("Argument %1 of %2 must be a %3, and the "
                                      "given value does not convert to it")
                        .arg(i)
                        .arg(slot_name)
                        .arg(typeName));
            }
        }
    }

    QGenericArgument generic[gMaxInvokeArguments];
    for (int i = 0; i < arguments.size(); ++i) {
        const int parameterType =
            rank == 3 ? int(QMetaType::QVariant) : chosen.parameterType(i);
#if QT_VERSION_MAJOR >= 6
        const char * typeName = QMetaType(parameterType).name();
#else
        const char * typeName = QMetaType::typeName(parameterType);
#endif
        // A parameter declared as QVariant wants the address of the variant,
        // not of the value inside it. The two coincide while the value fits in
        // the variant's own storage, which is why passing the contents used to
        // work: a QPointF fits, a QRectF does not and the callee then read a
        // variant out of the payload.
        const void * data =
            parameterType == QMetaType::QVariant
                ? static_cast<const void *>(&arguments.at(i))
                : arguments.at(i).constData();
        generic[i] = QGenericArgument(typeName, data);
    }

    bool invokedMeth;
    if (returnsVariant) {
        invokedMeth = chosen.invoke(
            ctx.obj, Qt::DirectConnection,
            QGenericReturnArgument("QVariant", &result_slot), generic[0],
            generic[1], generic[2], generic[3]);
    } else {
        invokedMeth = chosen.invoke(ctx.obj, Qt::DirectConnection, generic[0],
                                    generic[1], generic[2], generic[3]);
    }
    if (!invokedMeth) {
        return createError("NoMethodInvoked",
                           QString::fromUtf8("The method %1 could not be called")
                               .arg(slot_name));
    }

    QtJson::JsonObject result;
    result["result_slot"] = result_slot;
    return result;
}

QtJson::JsonObject Player::widget_activate_focus(
    const QtJson::JsonObject & command) {
    WidgetLocatorContext<QWidget> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    activate_focus(ctx.widget);

    QtJson::JsonObject result;
    return result;
}

QtJson::JsonObject Player::grab_graphics_view(
    const QtJson::JsonObject & command) {
    WidgetLocatorContext<QGraphicsView> ctx(this, command, "oid");
    if (ctx.hasError()) {
        return ctx.lastError;
    }
    QString format = command["format"].toString();
    if (format.isEmpty()) {
        format = "PNG";
    }
    QPixmap pixmap(ctx.widget->scene()->width(), ctx.widget->scene()->height());
    QPainter q_painter(&pixmap);

    ctx.widget->scene()->render(&q_painter);
    QBuffer buffer;
    pixmap.save(&buffer, format.toStdString().c_str());

    QtJson::JsonObject result;
    result["format"] = format;
    result["data"] = buffer.data().toBase64();

    return result;
}
