// Tests the `action` namespace of the real bridge through the C ABI. Offscreen.
// Key presses are simulated with QTest on the UI thread, so shortcuts go the same way a user's do.
#include "tinoxkde.h"

#include "driver.h"

#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QMainWindow>
#include <QTemporaryDir>
#include <QTest>

#include <thread>

using Obj = QJsonObject;

static Obj act(const char *id, Obj fields = {})
{
    fields.insert(QStringLiteral("id"), QString::fromLatin1(id));
    return fields;
}

static Obj descriptor(const char *id, Obj fields = {})
{
    return act(id, fields);
}

static bool define(QJsonArray actions)
{
    return query(cmd("action.define", Obj{{"actions", actions}})).value(QStringLiteral("ok")).toBool();
}

static Obj findAction(const char *id)
{
    for (const QJsonValue &a : query(cmd("action.list")).value(QStringLiteral("actions")).toArray()) {
        if (a.toObject().value(QStringLiteral("id")).toString() == QLatin1String(id)) {
            return a.toObject();
        }
    }
    return Obj();
}

/// Runs `fn` on the UI thread and waits for it.
template <class F>
static void onUi(F fn)
{
    QMetaObject::invokeMethod(qApp, fn, Qt::BlockingQueuedConnection);
}

static void press(int64_t window, Qt::Key key, Qt::KeyboardModifiers mods)
{
    onUi([=] {
        QObject *o = nullptr;
        // the window is found by its object name: the widget tree names nodes after their keys
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (w->isVisible() && qobject_cast<QMainWindow *>(w)) {
                o = w;
            }
        }
        (void)window;
        if (o) {
            QTest::keyClick(qobject_cast<QWidget *>(o), key, mods);
        }
    });
}

static QJsonArray strings(std::initializer_list<const char *> l)
{
    QJsonArray a;
    for (const char *s : l) {
        a.append(QString::fromLatin1(s));
    }
    return a;
}

static void testDefineAndList()
{
    EXPECT(define(QJsonArray{
        descriptor("build.run", Obj{{"text", "&Run"}, {"icon", "media-playback-start"}, {"shortcut", "F5"}, {"toolTip", "Build and run"}}),
        descriptor("view.wrap", Obj{{"text", "Wrap lines"}, {"checkable", true}, {"checked", true}}),
        descriptor("theme.light", Obj{{"text", "Light"}, {"group", "theme"}, {"checked", true}}),
        descriptor("theme.dark", Obj{{"text", "Dark"}, {"group", "theme"}}), descriptor("file_open", Obj{{"standard", true}}),
        descriptor("file_quit", Obj{{"standard", true}}), descriptor("file_save", Obj{{"standard", true}, {"enabled", false}})}));
    Obj run = findAction("build.run");
    EXPECT_EQ(run.value(QStringLiteral("text")).toString(), QStringLiteral("&Run"));
    EXPECT_EQ(run.value(QStringLiteral("label")).toString(), QStringLiteral("Run")); // for the command palette
    EXPECT_EQ(run.value(QStringLiteral("shortcut")).toString(), QStringLiteral("F5"));
    EXPECT_EQ(run.value(QStringLiteral("defaultShortcut")).toString(), QStringLiteral("F5"));
    EXPECT_EQ(run.value(QStringLiteral("toolTip")).toString(), QStringLiteral("Build and run"));
    EXPECT(!run.value(QStringLiteral("standard")).toBool());
    // KDE standard actions come with the platform's text and shortcut, not ours
    Obj open = findAction("file_open");
    EXPECT(open.value(QStringLiteral("standard")).toBool());
    EXPECT_EQ(open.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+O"));
    EXPECT(!open.value(QStringLiteral("label")).toString().isEmpty() &&
           !open.value(QStringLiteral("label")).toString().contains(QLatin1Char('&'))); // translated by the platform
    EXPECT_EQ(findAction("file_quit").value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Q"));
    EXPECT(!findAction("file_save").value(QStringLiteral("enabled")).toBool());
    EXPECT(findAction("view.wrap").value(QStringLiteral("checked")).toBool());
    EXPECT_EQ(findAction("theme.dark").value(QStringLiteral("group")).toString(), QStringLiteral("theme"));

    // defining again updates; unmentioned fields stay
    EXPECT(define(QJsonArray{descriptor("build.run", Obj{{"text", "&Run project"}})}));
    EXPECT_EQ(findAction("build.run").value(QStringLiteral("text")).toString(), QStringLiteral("&Run project"));
    EXPECT_EQ(findAction("build.run").value(QStringLiteral("shortcut")).toString(), QStringLiteral("F5"));

    // errors reject the whole batch
    const int count = query(cmd("action.list")).value(QStringLiteral("actions")).toArray().size();
    auto bad = [&](QJsonArray a, const char *expect) {
        Obj r = query(cmd("action.define", Obj{{"actions", a}}));
        EXPECT_EQ(errorCode(r), -1);
        EXPECT(
            r.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().contains(QString::fromLatin1(expect)));
    };
    bad(QJsonArray{descriptor("ok.one"), descriptor("bad", Obj{{"shortcut", "Ctrl+Banana"}})}, "not a key sequence");
    bad(QJsonArray{descriptor("ok.two"), descriptor("bad", Obj{{"nope", 1}})}, "unknown field 'nope'");
    bad(QJsonArray{descriptor("not_a_standard", Obj{{"standard", true}})}, "standard action name");
    bad(QJsonArray{descriptor("twice"), descriptor("twice")}, "twice");
    bad(QJsonArray{descriptor("x", Obj{{"checkable", "yes"}})}, "boolean");
    bad(QJsonArray{Obj{{"text", "no id"}}}, "id");
    EXPECT_EQ(query(cmd("action.list")).value(QStringLiteral("actions")).toArray().size(), count);
}

static void testTriggerAndState()
{
    drainEvents();
    EXPECT(query(cmd("action.trigger", act("build.run"))).value(QStringLiteral("ok")).toBool());
    Obj ev = waitFor("action.triggered", 1000);
    EXPECT_EQ(ev.value(QStringLiteral("id")).toString(), QStringLiteral("build.run"));
    EXPECT(!ev.value(QStringLiteral("checked")).toBool());

    // a checkable one toggles and reports the new state; state pushed from outside is silent
    EXPECT(query(cmd("action.trigger", act("view.wrap"))).value(QStringLiteral("ok")).toBool());
    EXPECT(!waitFor("action.triggered", 1000).value(QStringLiteral("checked")).toBool());
    EXPECT(query(cmd("action.set", act("view.wrap", Obj{{"checked", true}}))).value(QStringLiteral("ok")).toBool());
    query(cmd("core.ping"));
    EXPECT(waitFor("action.triggered", 200).isEmpty());
    EXPECT(findAction("view.wrap").value(QStringLiteral("checked")).toBool());

    // radio group: checking one clears the other
    query(cmd("action.trigger", act("theme.dark")));
    EXPECT(findAction("theme.dark").value(QStringLiteral("checked")).toBool());
    EXPECT(!findAction("theme.light").value(QStringLiteral("checked")).toBool());

    // enable/disable/visible/text from the logic
    EXPECT(query(cmd("action.set", act("build.run", Obj{{"enabled", false}, {"text", "Stop"}, {"icon", "media-playback-stop"}})))
               .value(QStringLiteral("ok"))
               .toBool());
    Obj run = findAction("build.run");
    EXPECT(!run.value(QStringLiteral("enabled")).toBool());
    EXPECT_EQ(run.value(QStringLiteral("text")).toString(), QStringLiteral("Stop"));
    EXPECT_EQ(errorCode(query(cmd("action.trigger", act("build.run")))), -7); // disabled
    EXPECT(
        query(cmd("action.set", act("build.run", Obj{{"enabled", true}, {"text", "&Run project"}}))).value(QStringLiteral("ok")).toBool());

    EXPECT_EQ(errorCode(query(cmd("action.trigger", act("nope")))), -5);
    EXPECT_EQ(errorCode(query(cmd("action.trigger"))), -1);
    EXPECT_EQ(errorCode(query(cmd("action.set", act("build.run", Obj{{"shortcut", "F6"}})))), -1); // shortcuts have their own command
    EXPECT_EQ(errorCode(query(cmd("action.set", act("build.run", Obj{{"enabled", "no"}})))), -1);
}

static void testMenusToolbarsAndKeys(const QString &dir)
{
    Obj tree{{"type", "QMainWindow"},
             {"key", "win"},
             {"props", Obj{{"windowTitle", "Actions"}}},
             {"children", QJsonArray{Obj{{"type", "QToolBar"}, {"key", "tb"}},
                                     Obj{{"type", "QLabel"}, {"key", "content"}, {"props", Obj{{"text", "x"}}}}}}};
    Obj m = query(cmd("widget.mount", Obj{{"tree", tree}}));
    EXPECT(m.value(QStringLiteral("ok")).toBool());
    const int64_t win = int64_t(m.value(QStringLiteral("id")).toDouble());
    const int64_t tb = int64_t(m.value(QStringLiteral("nodes")).toObject().value(QStringLiteral("tb")).toDouble());

    Obj menus{
        {"menus",
         QJsonArray{
             Obj{{"id", "file"},
                 {"title", "&File"},
                 {"items",
                  QJsonArray{"file_open", "-", Obj{{"menu", Obj{{"title", "&Theme"}, {"items", QJsonArray{"theme.light", "theme.dark"}}}}},
                             "-", "file_quit"}}},
             Obj{{"id", "build"}, {"title", "&Build"}, {"items", QJsonArray{"build.run", Obj{{"section", "Options"}}, "view.wrap"}}}}}};
    Obj r = query(cmd("action.setMenuBar", Obj{{"target", double(win)}, {"menus", menus.value(QStringLiteral("menus"))}}));
    EXPECT(r.value(QStringLiteral("ok")).toBool());
    Obj dump = query(cmd("action.debug.menus", Obj{{"target", double(win)}}));
    QJsonArray top = dump.value(QStringLiteral("menus")).toArray();
    EXPECT_EQ(top.size(), 2);
    EXPECT_EQ(top.at(0).toObject().value(QStringLiteral("menu")).toString(), QStringLiteral("&File"));
    EXPECT_EQ(top.at(0).toObject().value(QStringLiteral("items")).toArray().at(0).toString(), QStringLiteral("file_open"));
    EXPECT_EQ(top.at(0).toObject().value(QStringLiteral("items")).toArray().at(1).toString(), QStringLiteral("-"));
    EXPECT_EQ(
        top.at(0).toObject().value(QStringLiteral("items")).toArray().at(2).toObject().value(QStringLiteral("items")).toArray().size(), 2);
    EXPECT_EQ(top.at(1).toObject().value(QStringLiteral("items")).toArray().at(1).toString(), QStringLiteral("--Options--"));

    // a bad item rejects the whole menu bar and leaves the old one
    EXPECT_EQ(errorCode(query(
                  cmd("action.setMenuBar",
                      Obj{{"target", double(win)}, {"menus", QJsonArray{Obj{{"title", "&X"}, {"items", QJsonArray{"does.not.exist"}}}}}}))),
              -1);
    EXPECT_EQ(query(cmd("action.debug.menus", Obj{{"target", double(win)}})).value(QStringLiteral("menus")).toArray().size(), 2);
    EXPECT_EQ(errorCode(query(cmd("action.setMenuBar", Obj{{"target", double(win)}, {"menus", QJsonArray{Obj{{"items", QJsonArray{}}}}}}))),
              -1); // no title

    // toolbar
    EXPECT(query(cmd("action.setToolBar", Obj{{"target", double(tb)}, {"items", strings({"file_open", "-", "build.run"})}}))
               .value(QStringLiteral("ok"))
               .toBool());
    QJsonArray bars = query(cmd("action.debug.menus", Obj{{"target", double(win)}})).value(QStringLiteral("toolBars")).toArray();
    EXPECT_EQ(bars.at(0).toObject().value(QStringLiteral("items")).toArray().size(), 3);
    EXPECT(query(cmd("action.setToolBar", Obj{{"target", double(tb)}, {"items", strings({"build.run"})}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT_EQ(query(cmd("action.debug.menus", Obj{{"target", double(win)}}))
                  .value(QStringLiteral("toolBars"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("items"))
                  .toArray()
                  .size(),
              1);
    EXPECT_EQ(errorCode(query(cmd("action.setToolBar", Obj{{"target", double(win)}, {"items", QJsonArray{}}}))), -1); // not a toolbar

    // shortcuts through real key presses
    EXPECT(query(cmd("widget.show", Obj{{"target", double(win)}})).value(QStringLiteral("ok")).toBool());
    onUi([] {
        QWidget *w = nullptr;
        for (QWidget *t : QApplication::topLevelWidgets()) {
            if (t->isVisible() && qobject_cast<QMainWindow *>(t)) {
                w = t;
            }
        }
        if (w) {
            w->activateWindow();
            QTest::qWaitForWindowActive(w, 2000);
        }
    });
    drainEvents();
    press(win, Qt::Key_F5, Qt::NoModifier);
    Obj byKey = waitFor("action.triggered", 2000);
    EXPECT_EQ(byKey.value(QStringLiteral("id")).toString(), QStringLiteral("build.run"));
    press(win, Qt::Key_O, Qt::ControlModifier);
    EXPECT_EQ(waitFor("action.triggered", 2000).value(QStringLiteral("id")).toString(), QStringLiteral("file_open"));

    // remapping: the new key works, the old one does not; the change is persisted
    const QString rc = dir + QStringLiteral("/shortcuts.rc");
    EXPECT(query(cmd("action.configure", Obj{{"file", rc}, {"group", "Shortcuts"}})).value(QStringLiteral("ok")).toBool());
    Obj remap = query(cmd("action.setShortcut", Obj{{"id", "build.run"}, {"shortcuts", strings({"Ctrl+R"})}}));
    EXPECT(remap.value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(waitFor("action.shortcutChanged", 1000).value(QStringLiteral("id")).toString(), QStringLiteral("build.run"));
    drainEvents();
    press(win, Qt::Key_F5, Qt::NoModifier);
    query(cmd("core.ping"));
    EXPECT(waitFor("action.triggered", 300).isEmpty());
    press(win, Qt::Key_R, Qt::ControlModifier);
    EXPECT_EQ(waitFor("action.triggered", 2000).value(QStringLiteral("id")).toString(), QStringLiteral("build.run"));
    EXPECT_EQ(findAction("build.run").value(QStringLiteral("defaultShortcut")).toString(), QStringLiteral("F5"));
    QFile f(rc);
    EXPECT(f.open(QIODevice::ReadOnly) && f.readAll().contains("Ctrl+R"));

    // a taken shortcut is refused, naming the owner; nothing changes
    Obj conflict = query(cmd("action.setShortcut", Obj{{"id", "view.wrap"}, {"shortcuts", strings({"Ctrl+R"})}}));
    EXPECT_EQ(errorCode(conflict), -1);
    EXPECT(conflict.value(QStringLiteral("error"))
               .toObject()
               .value(QStringLiteral("message"))
               .toString()
               .contains(QStringLiteral("build.run")));
    EXPECT_EQ(errorCode(query(cmd("action.setShortcut", Obj{{"id", "view.wrap"}, {"shortcuts", strings({"Ctrl+Nonsense"})}}))), -1);

    // "restart": the action is defined again and the user's shortcut wins over the default
    EXPECT(query(cmd("action.remove", act("build.run"))).value(QStringLiteral("ok")).toBool());
    EXPECT(findAction("build.run").isEmpty());
    EXPECT(define(QJsonArray{descriptor("build.run", Obj{{"text", "&Run"}, {"shortcut", "F5"}})}));
    EXPECT_EQ(findAction("build.run").value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+R"));

    // reset to the default
    EXPECT(query(cmd("action.setShortcut", act("build.run"))).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(findAction("build.run").value(QStringLiteral("shortcut")).toString(), QStringLiteral("F5"));

    // a context menu from actions
    Obj ctx = query(cmd("widget.mount", Obj{{"tree", Obj{{"type", "QLabel"}, {"key", "ctx"}}}}));
    const int64_t ctxId = int64_t(ctx.value(QStringLiteral("id")).toDouble());
    EXPECT(query(cmd("action.setContextMenu", Obj{{"target", double(ctxId)}, {"items", strings({"build.run", "-", "view.wrap"})}}))
               .value(QStringLiteral("ok"))
               .toBool());
    Obj cd = query(cmd("action.debug.menus", Obj{{"target", double(ctxId)}}));
    EXPECT_EQ(cd.value(QStringLiteral("items")).toArray().size(), 3);
    EXPECT_EQ(cd.value(QStringLiteral("policy")).toInt(), int(Qt::ActionsContextMenu));
    tkde_release(ctxId);
    // keep the window open: closing the last one ends the loop
}

static void testLoadUi()
{
    Obj m = query(cmd("widget.mount", Obj{{"tree", Obj{{"type", "QMainWindow"}, {"key", "rcwin"}}}}));
    const int64_t id = int64_t(m.value(QStringLiteral("id")).toDouble());
    const QString xml = QStringLiteral("<!DOCTYPE gui SYSTEM \"kpartgui.dtd\"><gui name=\"tinoxide\" version=\"1\">"
                                       "<MenuBar><Menu name=\"file\"><text>&amp;File</text><Action name=\"file_open\"/><Separator/><Action "
                                       "name=\"file_quit\"/></Menu></MenuBar>"
                                       "<ToolBar name=\"mainToolBar\"><text>Main</text><Action name=\"file_open\"/></ToolBar></gui>");
    EXPECT_EQ(errorCode(query(cmd("action.loadUi", Obj{{"target", double(id)}}))), -1); // neither file nor xml
    EXPECT(query(cmd("action.loadUi", Obj{{"target", double(id)}, {"xml", xml}})).value(QStringLiteral("ok")).toBool());
    Obj d = query(cmd("action.debug.menus", Obj{{"target", double(id)}}));
    QJsonArray menus = d.value(QStringLiteral("menus")).toArray();
    EXPECT_EQ(menus.size(), 1);
    EXPECT_EQ(menus.at(0).toObject().value(QStringLiteral("items")).toArray().at(0).toString(), QStringLiteral("file_open"));
    EXPECT_EQ(menus.at(0).toObject().value(QStringLiteral("items")).toArray().at(2).toString(), QStringLiteral("file_quit"));
    EXPECT_EQ(errorCode(query(cmd("action.loadUi", Obj{{"target", double(id)}, {"xml", xml}}))), -1); // once per window
    tkde_release(id);
}

static void testShortcutsDialog()
{
    EXPECT(query(cmd("action.showShortcutsDialog")).value(QStringLiteral("ok")).toBool());
    bool shown = false;
    onUi([&] {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (w->inherits("KShortcutsDialog") && w->isVisible()) {
                shown = true;
                w->close();
            }
        }
    });
    EXPECT(shown);
    EXPECT_EQ(errorCode(query(cmd("action.showShortcutsDialog", Obj{{"parent", 424242.0}}))), -5);
}

static void driver(QString dir)
{
    GcAttach gc;
    EXPECT(!waitFor("core.ready").isEmpty());
    EXPECT(query(cmd("core.capabilities")).value(QStringLiteral("namespaces")).toArray().contains(QStringLiteral("action")));
    testDefineAndList();
    testTriggerAndState();
    testMenusToolbarsAndKeys(dir);
    testLoadUi();
    testShortcutsDialog();
    tkde_post("{\"cmd\":\"core.quit\"}");
}

int main(int argc, char **)
{
    (void)argc;
    gcInit();
    ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    ::setenv("TKDE_DEBUG_COMMANDS", "1", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);
    QTemporaryDir dir;
    // private config and data homes: shortcuts must never be written to the user's files
    ::setenv("XDG_CONFIG_HOME", qPrintable(dir.path() + QStringLiteral("/config")), 1);
    ::setenv("XDG_DATA_HOME", qPrintable(dir.path() + QStringLiteral("/data")), 1);
    if (tkde_init("action-test", "Action test") != 0) {
        std::fprintf(stderr, "init failed: %s\n", tkde_last_error());
        return 1;
    }
    std::thread logic(driver, dir.path());
    tkde_exec();
    logic.join();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures.load());
        return 1;
    }
    std::puts("action ok");
    return 0;
}
