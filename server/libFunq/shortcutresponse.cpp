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

#include "shortcutresponse.h"

#include "player.h"

#include <QApplication>
#include <QKeyEvent>
#include <QThread>
#include <QtTest>

static void send_keys(QWidget * target, const QKeySequence & binding,
                      bool press) {
    // taken from
    // http://stackoverflow.com/questions/14283764/how-can-i-simulate-emission-of-a-standard-key-sequence
    for (int i = 0; i < static_cast<int>(binding.count()); ++i) {
        uint key = binding[i];
        Qt::KeyboardModifiers modifiers =
            static_cast<Qt::KeyboardModifiers>(key & Qt::KeyboardModifierMask);
        key = key & ~Qt::KeyboardModifierMask;
        if (press) {
            QTest::keyPress(target, static_cast<Qt::Key>(key), modifiers);
        } else {
            QTest::keyRelease(target, static_cast<Qt::Key>(key), modifiers);
        }
    }
}

ShortcutResponse::ShortcutResponse(JsonClient * client,
                                   const QtJson::JsonObject & command)
    : DelayedResponse(client, command),
      m_target(NULL),
      m_delivered(new bool(false)),
      m_loopLevel(0) {
    if (command.contains("oid")) {
        WidgetLocatorContext<QWidget> ctx(static_cast<Player *>(jsonClient()),
                                          command, "oid");
        if (ctx.hasError()) {
            writeResponse(ctx.lastError);
            return;
        }
        m_target = ctx.widget;
    } else {
        m_target = qApp->activeWindow();
    }
    if (m_target) {
        connect(m_target, SIGNAL(destroyed()), this, SLOT(on_target_deleted()));
    }
    m_binding = QKeySequence::fromString(command["keysequence"].toString());
}

void ShortcutResponse::on_target_deleted() {
    m_target = NULL;
}

void ShortcutResponse::execute(int call) {
    if (!m_target) {
        // this can happen when target is deleted for example in
        // step 2, after a press event has been sent. We do not
        // want it to be an error.
        writeResponse(QtJson::JsonObject());
        return;
    }
    if (call == 0) {
        m_target->repaint();
        setInterval(100);
        return;
    }
    if (call == 1) {
        m_modalBefore = QApplication::activeModalWidget();
        m_popupBefore = QApplication::activePopupWidget();
        m_loopLevel = QThread::currentThread()->loopLevel();
        // the target is the context: a widget gone by then takes the keys
        // with it; the response is not, since it may be deleted before a
        // dialog opened by the keys returns
        QPointer<QWidget> target(m_target);
        QSharedPointer<bool> delivered = m_delivered;
        const QKeySequence binding = m_binding;
        QMetaObject::invokeMethod(
            m_target,
            [target, delivered, binding]() {
                target->grabKeyboard();
                send_keys(target, binding, true);
                if (target) {
                    send_keys(target, binding, false);
                }
                if (target && QWidget::keyboardGrabber() == target) {
                    target->releaseKeyboard();
                }
                *delivered = true;
            },
            Qt::QueuedConnection);
        setInterval(20);
        return;
    }
    if (*m_delivered) {
        writeResponse(QtJson::JsonObject());
        return;
    }
    QWidget * opened = NULL;
    QWidget * modal = QApplication::activeModalWidget();
    QWidget * popup = QApplication::activePopupWidget();
    if (modal && modal != m_modalBefore) {
        opened = modal;
    } else if (popup && popup != m_popupBefore) {
        opened = popup;
    }
    // this step runs inside a loop the keys started only when their delivery
    // is held by it; a native dialog shows up this way and in no other
    const bool nested = QThread::currentThread()->loopLevel() > m_loopLevel;
    if (opened || nested) {
        if (QWidget::keyboardGrabber() == m_target) {
            m_target->releaseKeyboard();
        }
        QtJson::JsonObject result;
        result["opened"] =
            opened ? QString::fromLatin1(opened->metaObject()->className())
                   : QString("event loop");
        writeResponse(result);
    }
}
