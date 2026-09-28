#pragma once

#include <QEvent>
#include <QPointF>
#include <QString>
#include <QtGui/qtguiglobal.h>

QT_BEGIN_NAMESPACE

class QWindow;

/*
 * Input as the platform delivers it.
 *
 * These are exported by QtGui and declared only in QtTest's headers, and they
 * are how QTest feeds a window: the event goes through the platform pipeline
 * instead of straight to a widget. That is the whole difference. Posting to a
 * widget skips hit testing, z-order and grabs, and it never updates the state
 * QGuiApplication keeps about the input device - so an application asking
 * qApp->mouseButtons() sees no button held during a synthetic press, and a
 * field that only trusts what a user typed ignores synthetic keys.
 */
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow * window,
                                      const QPointF & local,
                                      const QPointF & global,
                                      Qt::MouseButtons state,
                                      Qt::MouseButton button,
                                      QEvent::Type type,
                                      Qt::KeyboardModifiers mods,
                                      int timestamp);

Q_GUI_EXPORT void qt_handleKeyEvent(QWindow * window,
                                    QEvent::Type type,
                                    int key,
                                    Qt::KeyboardModifiers mods,
                                    const QString & text,
                                    bool autorep,
                                    ushort count);

Q_GUI_EXPORT void qt_handleWheelEvent(QWindow * window,
                                      const QPointF & local,
                                      const QPointF & global,
                                      QPoint pixelDelta,
                                      QPoint angleDelta,
                                      Qt::KeyboardModifiers mods,
                                      Qt::ScrollPhase phase);

QT_END_NAMESPACE
