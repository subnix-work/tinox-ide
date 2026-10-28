// Tests the `widget` namespace of the real bridge through the C ABI. Offscreen.
// Golden files: tests/golden/widget/NAME.in.json is mounted and widget.debug.dump must equal
// NAME.out.json (UPDATE_GOLDEN=1 rewrites them; review the diff).
#include "tinoxkde.h"

#include "driver.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <thread>

#ifndef TKDE_GOLDEN_DIR
#error "TKDE_GOLDEN_DIR must point at tests/golden/widget"
#endif

using Obj = QJsonObject;

static Obj tree(const char *type, const char *key, Obj props = {}, QJsonArray children = {}, Obj place = {})
{
    Obj o{{"type", QString::fromLatin1(type)}, {"key", QString::fromLatin1(key)}};
    if (!props.isEmpty()) {
        o.insert(QStringLiteral("props"), props);
    }
    if (!children.isEmpty()) {
        o.insert(QStringLiteral("children"), children);
    }
    if (!place.isEmpty()) {
        o.insert(QStringLiteral("place"), place);
    }
    return o;
}

static Obj mountTree(const Obj &t)
{
    return query(cmd("widget.mount", Obj{{"tree", t}}));
}

static int64_t nodeId(const Obj &reply, const char *key)
{
    return int64_t(reply.value(QStringLiteral("nodes")).toObject().value(QString::fromLatin1(key)).toDouble());
}

static Obj dumpOf(int64_t mount)
{
    return query(cmd("widget.debug.dump", Obj{{"target", double(mount)}})).value(QStringLiteral("tree")).toObject();
}

static int objectCount()
{
    return query(cmd("core.debug.objects")).value(QStringLiteral("count")).toInt();
}

static Obj act(int64_t target, const char *what, QJsonValue value = QJsonValue())
{
    return query(cmd("widget.debug.act", Obj{{"target", double(target)}, {"act", QString::fromLatin1(what)}, {"value", value}}));
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static void testGolden()
{
    QDir dir(QStringLiteral(TKDE_GOLDEN_DIR));
    const QStringList inputs = dir.entryList({QStringLiteral("*.in.json")});
    EXPECT(!inputs.isEmpty());
    for (const QString &in : inputs) {
        const QString base = in.left(in.size() - int(strlen(".in.json")));
        const Obj input = QJsonDocument::fromJson(readFile(dir.filePath(in))).object();
        Obj reply = mountTree(input);
        if (!reply.value(QStringLiteral("ok")).toBool()) {
            std::fprintf(stderr, "FAIL golden %s: %s\n", qPrintable(base), qPrintable(compact(reply)));
            ++g_failures;
            continue;
        }
        const QByteArray got = QJsonDocument(dumpOf(int64_t(reply.value(QStringLiteral("id")).toDouble()))).toJson(QJsonDocument::Indented);
        const QString outPath = dir.filePath(base + QStringLiteral(".out.json"));
        if (qEnvironmentVariableIsSet("UPDATE_GOLDEN")) {
            QFile f(outPath);
            f.open(QIODevice::WriteOnly);
            f.write(got);
            continue;
        }
        if (got != readFile(outPath)) {
            std::fprintf(stderr, "FAIL golden %s differs from %s\n%s\n", qPrintable(base), qPrintable(outPath), got.constData());
            ++g_failures;
        }
        tkde_release(int64_t(reply.value(QStringLiteral("id")).toDouble()));
    }
}

static void testMinimalUpdate()
{
    Obj m = mountTree(tree("QWidget", "root", {},
                           QJsonArray{tree("QLabel", "title", Obj{{"text", "one"}}), tree("QLineEdit", "input", Obj{{"text", "x"}}),
                                      tree("QPushButton", "go", Obj{{"text", "Go"}})}));
    EXPECT(m.value(QStringLiteral("ok")).toBool());
    const int64_t id = int64_t(m.value(QStringLiteral("id")).toDouble());
    const int64_t label = nodeId(m, "title"), input = nodeId(m, "input"), go = nodeId(m, "go");
    EXPECT(label > 0 && input > 0 && go > 0);
    drainEvents();

    // change one prop: same widgets (same ids), only that text differs, and no event echoes back
    Obj u = query(cmd("widget.update", Obj{{"target", double(id)},
                                           {"tree", tree("QWidget", "root", {},
                                                         QJsonArray{tree("QLabel", "title", Obj{{"text", "two"}}),
                                                                    tree("QLineEdit", "input", Obj{{"text", "x"}}),
                                                                    tree("QPushButton", "go", Obj{{"text", "Go"}})})}}));
    EXPECT(u.value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(nodeId(u, "title"), label);
    EXPECT_EQ(nodeId(u, "input"), input);
    EXPECT_EQ(nodeId(u, "go"), go);
    Obj d = dumpOf(id);
    EXPECT_EQ(d.value(QStringLiteral("children")).toArray().at(0).toObject().value(QStringLiteral("text")).toString(),
              QStringLiteral("two"));
    query(cmd("core.ping"));
    EXPECT(waitFor("widget.textChanged", 200).isEmpty());

    // a prop that disappears returns to its default
    Obj u2 =
        query(cmd("widget.update", Obj{{"target", double(id)},
                                       {"tree", tree("QWidget", "root", {},
                                                     QJsonArray{tree("QLabel", "title"), tree("QLineEdit", "input", Obj{{"text", "x"}}),
                                                                tree("QPushButton", "go", Obj{{"text", "Go"}})})}}));
    EXPECT(u2.value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(dumpOf(id).value(QStringLiteral("children")).toArray().at(0).toObject().value(QStringLiteral("text")).toString(), QString());

    // reorder keeps the widgets, add and remove only touch the difference
    Obj u3 = query(cmd("widget.update", Obj{{"target", double(id)},
                                            {"tree", tree("QWidget", "root", {},
                                                          QJsonArray{tree("QPushButton", "go", Obj{{"text", "Go"}}),
                                                                     tree("QLabel", "extra", Obj{{"text", "new"}}),
                                                                     tree("QLineEdit", "input", Obj{{"text", "x"}})})}}));
    EXPECT(u3.value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(nodeId(u3, "go"), go);
    EXPECT_EQ(nodeId(u3, "input"), input);
    EXPECT(nodeId(u3, "extra") > 0);
    EXPECT(!u3.value(QStringLiteral("nodes")).toObject().contains(QStringLiteral("title")));
    EXPECT_EQ(query(cmd("widget.debug.act", Obj{{"target", double(label)}, {"act", "click"}}))
                  .value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("code"))
                  .toInt(),
              -5); // the removed label is gone
    Obj order = dumpOf(id);
    EXPECT_EQ(order.value(QStringLiteral("layout")).toObject().value(QStringLiteral("count")).toInt(),
              0); // plain QWidget, no layout: free placement
    QStringList keys;
    for (const QJsonValue &c : order.value(QStringLiteral("children")).toArray()) {
        keys << c.toObject().value(QStringLiteral("key")).toString();
    }
    EXPECT_EQ(keys.join(QLatin1Char(',')), QStringLiteral("go,extra,input"));

    // a different type under the same key is a new widget
    Obj u4 =
        query(cmd("widget.update", Obj{{"target", double(id)},
                                       {"tree", tree("QWidget", "root", {},
                                                     QJsonArray{tree("QLabel", "go", Obj{{"text", "now a label"}}), tree("QLabel", "extra"),
                                                                tree("QLineEdit", "input", Obj{{"text", "x"}})})}}));
    EXPECT(u4.value(QStringLiteral("ok")).toBool());
    EXPECT(nodeId(u4, "go") != go);
    EXPECT_EQ(nodeId(u4, "input"), input);

    // the root keeps its identity
    EXPECT_EQ(errorCode(query(cmd("widget.update", Obj{{"target", double(id)}, {"tree", tree("QMainWindow", "root")}}))), -1);
    EXPECT(tkde_release(id) == TKDE_OK);
    query(cmd("core.ping"));
    EXPECT_EQ(errorCode(query(cmd("widget.debug.dump", Obj{{"target", double(id)}}))), -5);
}

static void testOrderedContainers()
{
    // tabs, splitter and layout keep their children in tree order through updates
    auto labels = [](std::initializer_list<const char *> keys, bool tabs = true) {
        QJsonArray a;
        for (const char *k : keys) {
            a.append(tree("QLabel", k, Obj{{"text", QString::fromLatin1(k)}}, {},
                          tabs ? Obj{{"tab", Obj{{"title", QString::fromLatin1(k)}}}} : Obj{}));
        }
        return a;
    };
    Obj m = mountTree(tree("QTabWidget", "tabs", {}, labels({"a", "b", "c"}), {}));
    const int64_t id = int64_t(m.value(QStringLiteral("id")).toDouble());
    auto titles = [&] {
        QStringList l;
        for (const QJsonValue &v : dumpOf(id).value(QStringLiteral("tabs")).toArray()) {
            l << v.toString();
        }
        return l.join(QLatin1Char(','));
    };
    EXPECT_EQ(titles(), QStringLiteral("a,b,c"));
    const int64_t b = nodeId(m, "b");
    EXPECT(query(cmd("widget.update", Obj{{"target", double(id)}, {"tree", tree("QTabWidget", "tabs", {}, labels({"c", "b", "d"}, true))}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT_EQ(titles(), QStringLiteral("c,b,d"));
    EXPECT_EQ(nodeId(query(cmd("widget.update",
                               Obj{{"target", double(id)}, {"tree", tree("QTabWidget", "tabs", {}, labels({"c", "b", "d"}, true))}})),
                     "b"),
              b);
    // changing only a tab's title is a placement change of that one child
    QJsonArray renamed = labels({"c", "b", "d"}, true);
    Obj second = renamed.at(1).toObject();
    second.insert(QStringLiteral("place"), Obj{{"tab", Obj{{"title", "B!"}}}});
    renamed.replace(1, second);
    EXPECT(query(cmd("widget.update", Obj{{"target", double(id)}, {"tree", tree("QTabWidget", "tabs", {}, renamed)}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT_EQ(titles(), QStringLiteral("c,B!,d"));
    tkde_release(id);

    Obj s = mountTree(tree("QSplitter", "sp", {}, labels({"x", "y", "z"}, false)));
    const int64_t sid = int64_t(s.value(QStringLiteral("id")).toDouble());
    EXPECT(query(cmd("widget.update", Obj{{"target", double(sid)}, {"tree", tree("QSplitter", "sp", {}, labels({"z", "x"}, false))}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT_EQ(dumpOf(sid).value(QStringLiteral("order")).toArray(), QJsonArray({QStringLiteral("z"), QStringLiteral("x")}));
    tkde_release(sid);
}

static void testErrorsAreAtomic()
{
    const int before = objectCount();
    auto bad = [&](const Obj &t, const char *why) {
        Obj r = mountTree(t);
        if (errorCode(r) != -1) {
            std::fprintf(stderr, "FAIL expected an error for %s: %s\n", why, qPrintable(compact(r)));
            ++g_failures;
        }
        return r.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
    };
    EXPECT(bad(tree("QLabel", "x", Obj{{"nope", 1}}), "unknown prop").contains(QStringLiteral("unknown prop 'nope'")));
    EXPECT(bad(tree("QLabel", "x", Obj{{"text", 5}}), "wrong kind").contains(QStringLiteral("must be a string")));
    EXPECT(bad(tree("QSplitter", "x", Obj{{"orientation", "diagonal"}}), "bad choice")
               .contains(QStringLiteral("one of horizontal, vertical")));
    EXPECT(bad(tree("QKitchenSink", "x"), "unknown type").contains(QStringLiteral("unknown type")));
    EXPECT(bad(tree("QWidget", "x", {}, QJsonArray{tree("QLabel", "d"), tree("QLabel", "d")}), "duplicate key")
               .contains(QStringLiteral("duplicate")));
    EXPECT(bad(Obj{{"type", "QLabel"}}, "no key").contains(QStringLiteral("key")));
    EXPECT(bad(tree("QWidget", "x", {}, QJsonArray{tree("QLabel", "ok"), tree("QLabel", "fail", Obj{{"wordWrap", "yes"}})}),
               "error deep in the tree")
               .contains(QStringLiteral("'fail'")));
    Obj withLayout = tree("QLabel", "x");
    withLayout.insert(QStringLiteral("layout"), Obj{{"kind", "box"}});
    EXPECT(bad(withLayout, "layout on a label").contains(QStringLiteral("QWidget")));
    EXPECT(bad(tree("QMainWindow", "x", {}, QJsonArray{tree("QDockWidget", "d", {}, {}, Obj{{"area", "middle"}})}), "bad dock area")
               .contains(QStringLiteral("area")));
    EXPECT(bad(tree("Embed", "x"), "embed as root").contains(QStringLiteral("Embed")));
    EXPECT(bad(tree("QWidget", "x", {}, QJsonArray{Obj{{"type", "Embed"}, {"key", "e"}, {"object", 424242}}}), "embed of nothing")
               .contains(QStringLiteral("widget")));
    EXPECT_EQ(objectCount(), before); // nothing was built, nothing registered

    // a failing update leaves the tree as it was
    Obj m = mountTree(tree("QWidget", "root", {}, QJsonArray{tree("QLabel", "l", Obj{{"text", "keep"}})}));
    const int64_t id = int64_t(m.value(QStringLiteral("id")).toDouble());
    EXPECT_EQ(errorCode(query(cmd(
                  "widget.update",
                  Obj{{"target", double(id)},
                      {"tree", tree("QWidget", "root", {}, QJsonArray{tree("QLabel", "l", Obj{{"text", "lost"}, {"bogus", true}})})}}))),
              -1);
    EXPECT_EQ(dumpOf(id).value(QStringLiteral("children")).toArray().at(0).toObject().value(QStringLiteral("text")).toString(),
              QStringLiteral("keep"));
    EXPECT_EQ(errorCode(query(cmd("widget.update", Obj{{"target", 424242.0}, {"tree", tree("QWidget", "root")}}))), -5);
    EXPECT_EQ(errorCode(query(cmd("widget.update", Obj{{"tree", tree("QWidget", "root")}}))), -1);
    tkde_release(id);
}

static void testEvents()
{
    Obj m = mountTree(tree("QWidget", "root", {},
                           QJsonArray{tree("QPushButton", "btn", Obj{{"text", "Go"}, {"checkable", true}}),
                                      tree("QCheckBox", "chk", Obj{{"text", "c"}}), tree("QLineEdit", "edit", Obj{{"text", "a"}}),
                                      tree("QComboBox", "combo", Obj{{"items", QJsonArray{"x", "y", "z"}}}),
                                      tree("QTabWidget", "tabs", Obj{{"tabsClosable", true}},
                                           QJsonArray{tree("QLabel", "t1", {}, {}, Obj{{"tab", Obj{{"title", "1"}}}}),
                                                      tree("QLabel", "t2", {}, {}, Obj{{"tab", Obj{{"title", "2"}}}})}),
                                      tree("QSplitter", "split", {}, QJsonArray{tree("QLabel", "s1"), tree("QLabel", "s2")})}));
    const int64_t id = int64_t(m.value(QStringLiteral("id")).toDouble());
    drainEvents();

    act(nodeId(m, "btn"), "click");
    Obj clicked = waitFor("widget.clicked", 1000);
    EXPECT_EQ(clicked.value(QStringLiteral("key")).toString(), QStringLiteral("btn"));
    EXPECT_EQ(int64_t(clicked.value(QStringLiteral("target")).toDouble()), nodeId(m, "btn"));
    EXPECT(clicked.value(QStringLiteral("checked")).toBool());
    act(nodeId(m, "chk"), "click");
    EXPECT(waitFor("widget.clicked", 1000).value(QStringLiteral("checked")).toBool());

    act(nodeId(m, "edit"), "text", "abc");
    Obj text = waitFor("widget.textChanged", 1000);
    EXPECT_EQ(text.value(QStringLiteral("text")).toString(), QStringLiteral("abc"));
    EXPECT_EQ(text.value(QStringLiteral("key")).toString(), QStringLiteral("edit"));

    act(nodeId(m, "combo"), "index", 2);
    EXPECT_EQ(waitFor("widget.currentChanged", 1000).value(QStringLiteral("index")).toInt(), 2);
    act(nodeId(m, "tabs"), "index", 1);
    Obj tab = waitFor("widget.currentChanged", 1000);
    EXPECT_EQ(tab.value(QStringLiteral("key")).toString(), QStringLiteral("tabs"));
    EXPECT_EQ(tab.value(QStringLiteral("index")).toInt(), 1);
    // tab drag and middle click, like the editor tab bar needs
    act(nodeId(m, "tabs"), "tabMove", QJsonArray{0, 1});
    Obj moved = waitFor("widget.tabMoved", 1000);
    EXPECT_EQ(moved.value(QStringLiteral("from")).toInt(), 0);
    EXPECT_EQ(moved.value(QStringLiteral("to")).toInt(), 1);
    EXPECT_EQ(moved.value(QStringLiteral("key")).toString(), QStringLiteral("tabs"));
    query(cmd("widget.show", Obj{{"target", double(id)}}));
    act(nodeId(m, "tabs"), "middleClick", 1);
    Obj middle = waitFor("widget.closeRequested", 1000);
    EXPECT_EQ(middle.value(QStringLiteral("index")).toInt(), 1);
    EXPECT_EQ(middle.value(QStringLiteral("via")).toString(), QStringLiteral("middle"));
    query(cmd("widget.hide", Obj{{"target", double(id)}})); // a visible window that goes away would end the loop
    act(nodeId(m, "tabs"), "closeTab", 0);
    Obj closing = waitFor("widget.closeRequested", 1000);
    EXPECT_EQ(closing.value(QStringLiteral("index")).toInt(), 0);
    act(nodeId(m, "split"), "split", QJsonArray{30, 70});
    EXPECT_EQ(waitFor("widget.splitterMoved", 1000).value(QStringLiteral("sizes")).toArray().size(), 2);
    EXPECT_EQ(errorCode(act(nodeId(m, "btn"), "text", "x")), -1);
    tkde_release(id);

    // a window asks before it closes; "force" closes it
    Obj w = mountTree(
        tree("QMainWindow", "win", Obj{{"windowTitle", "t"}},
             QJsonArray{tree("QDockWidget", "dock", Obj{{"title", "D"}}, QJsonArray{tree("QLabel", "inner")}, Obj{{"area", "right"}})}));
    const int64_t wid = int64_t(w.value(QStringLiteral("id")).toDouble());
    EXPECT(query(cmd("widget.show", Obj{{"target", double(wid)}})).value(QStringLiteral("ok")).toBool());
    // closing the LAST window ends the loop (docs/bridge-core.md): keep another one open
    Obj keeper = mountTree(tree("QMainWindow", "keeper"));
    query(cmd("widget.show", Obj{{"target", keeper.value(QStringLiteral("id"))}}));
    drainEvents();
    EXPECT(query(cmd("widget.close", Obj{{"target", double(wid)}})).value(QStringLiteral("ok")).toBool());
    Obj asked = waitFor("widget.closeRequested", 1000);
    EXPECT_EQ(asked.value(QStringLiteral("key")).toString(), QStringLiteral("win"));
    EXPECT(!dumpOf(wid).value(QStringLiteral("hidden")).toBool()); // still open
    act(nodeId(w, "dock"), "visible", false);
    Obj dock = waitFor("widget.dockVisibilityChanged", 1000);
    EXPECT_EQ(dock.value(QStringLiteral("key")).toString(), QStringLiteral("dock"));
    EXPECT(!dock.value(QStringLiteral("visible")).toBool());
    EXPECT(query(cmd("widget.close", Obj{{"target", double(wid)}, {"force", true}})).value(QStringLiteral("ok")).toBool());
    EXPECT(dumpOf(wid).value(QStringLiteral("hidden")).toBool());
    tkde_release(wid); // the keeper stays: releasing the last visible window would end the loop
}

static void testEmbed(const QString &dir)
{
    // a widget owned by another module (an editor view) is placed, never deleted by the tree
    Obj doc = query(cmd("editor.createDocument", Obj{{"text", "hello"}}));
    const int64_t docId = idOf(doc);
    const int64_t view = idOf(query(cmd("editor.createView", Obj{{"document", double(docId)}})));
    Obj embed{{"type", "Embed"}, {"key", "editor"}, {"object", double(view)}};
    Obj m = mountTree(tree("QTabWidget", "tabs", {}, QJsonArray{embed}));
    EXPECT(m.value(QStringLiteral("ok")).toBool());
    const int64_t id = int64_t(m.value(QStringLiteral("id")).toDouble());
    EXPECT_EQ(nodeId(m, "editor"), view); // the node IS the embedded object
    EXPECT_EQ(dumpOf(id).value(QStringLiteral("tabs")).toArray().size(), 1);
    EXPECT(query(cmd("editor.getText", Obj{{"target", double(view)}}))
               .value(QStringLiteral("error"))
               .toObject()
               .value(QStringLiteral("code"))
               .toInt() == -1); // a view, not a document: still resolves
    // remove it from the tree: the view lives on
    EXPECT(query(cmd("widget.update", Obj{{"target", double(id)}, {"tree", tree("QTabWidget", "tabs")}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT(query(cmd("editor.setCursor", Obj{{"target", double(view)}, {"position", Obj{{"line", 0}, {"character", 1}}}}))
               .value(QStringLiteral("ok"))
               .toBool());
    // embed again, then release the whole tree: still alive
    EXPECT(query(cmd("widget.update", Obj{{"target", double(id)}, {"tree", tree("QTabWidget", "tabs", {}, QJsonArray{embed})}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT(tkde_release(id) == TKDE_OK);
    query(cmd("core.ping"));
    EXPECT(query(cmd("editor.setCursor", Obj{{"target", double(view)}, {"position", Obj{{"line", 0}, {"character", 2}}}}))
               .value(QStringLiteral("ok"))
               .toBool());
    tkde_release(docId);
    (void)dir;
}

static void testStateRoundTrip(const QString &dir)
{
    const QString file = dir + QStringLiteral("/state.rc");
    auto window = [] {
        return tree(
            "QMainWindow", "main", {},
            QJsonArray{tree("QDockWidget", "dockA", Obj{{"title", "A"}}, QJsonArray{tree("QLabel", "la")}, Obj{{"area", "left"}}),
                       tree("QDockWidget", "dockB", Obj{{"title", "B"}}, QJsonArray{tree("QLabel", "lb")}, Obj{{"area", "bottom"}}),
                       tree("QSplitter", "split", {}, QJsonArray{tree("QLabel", "s1"), tree("QLabel", "s2"), tree("QLabel", "s3")})});
    };
    Obj first = mountTree(window());
    const int64_t a = int64_t(first.value(QStringLiteral("id")).toDouble());
    // user changes: move a dock to the right, resize the splitter
    Obj moved = window();
    QJsonArray kids = moved.value(QStringLiteral("children")).toArray();
    Obj dockA = kids.at(0).toObject();
    dockA.insert(QStringLiteral("place"), Obj{{"area", "right"}});
    kids.replace(0, dockA);
    moved.insert(QStringLiteral("children"), kids);
    EXPECT(query(cmd("widget.update", Obj{{"target", double(a)}, {"tree", moved}})).value(QStringLiteral("ok")).toBool());
    EXPECT(act(nodeId(first, "split"), "split", QJsonArray{50, 20, 30}).value(QStringLiteral("ok")).toBool());
    EXPECT(query(cmd("widget.saveState", Obj{{"target", double(a)}, {"file", file}, {"group", "MainWindow"}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT(QFile::exists(file));
    tkde_release(a);

    Obj second = mountTree(window()); // fresh: dockA on the left, default sizes
    const int64_t b = int64_t(second.value(QStringLiteral("id")).toDouble());
    EXPECT_EQ(dumpOf(b).value(QStringLiteral("children")).toArray().at(0).toObject().value(QStringLiteral("area")).toString(),
              QStringLiteral("left"));
    Obj restored = query(cmd("widget.restoreState", Obj{{"target", double(b)}, {"file", file}, {"group", "MainWindow"}}));
    EXPECT(restored.value(QStringLiteral("ok")).toBool() && restored.value(QStringLiteral("restored")).toBool());
    EXPECT_EQ(dumpOf(b).value(QStringLiteral("children")).toArray().at(0).toObject().value(QStringLiteral("area")).toString(),
              QStringLiteral("right"));
    // nothing saved under another group: restored=false, no error
    Obj none = query(cmd("widget.restoreState", Obj{{"target", double(b)}, {"file", file}, {"group", "Other"}}));
    EXPECT(none.value(QStringLiteral("ok")).toBool() && !none.value(QStringLiteral("restored")).toBool());
    tkde_release(b);
}

static void testLifecycle()
{
    const int before = objectCount();
    for (int i = 0; i < 100; ++i) {
        Obj m = mountTree(
            tree("QMainWindow", "w", {},
                 QJsonArray{tree("QSplitter", "s", {}, QJsonArray{tree("QTreeView", "t"), tree("QPlainTextEdit", "p", Obj{{"text", "x"}})}),
                            tree("QDockWidget", "d", {}, QJsonArray{tree("QComboBox", "c", Obj{{"items", QJsonArray{"1", "2"}}})},
                                 Obj{{"area", "left"}}),
                            tree("QStatusBar", "sb")}));
        EXPECT(tkde_release(int64_t(m.value(QStringLiteral("id")).toDouble())) == TKDE_OK);
    }
    query(cmd("core.ping"));
    for (int i = 0; i < 50 && objectCount() != before; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_EQ(objectCount(), before);
}

static void testLog()
{
    Obj m = mountTree(tree("QPlainTextEdit", "log", Obj{{"readOnly", true}}));
    const int64_t id = int64_t(m.value(QStringLiteral("id")).toDouble());
    EXPECT(query(cmd("widget.append", Obj{{"target", double(id)}, {"text", "line 1"}})).value(QStringLiteral("ok")).toBool());
    EXPECT(query(cmd("widget.append", Obj{{"target", double(id)}, {"text", "line 2"}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(dumpOf(id).value(QStringLiteral("text")).toString(), QStringLiteral("line 1\nline 2"));
    EXPECT_EQ(errorCode(query(cmd("widget.append", Obj{{"target", double(id)}}))), 0); // empty text is allowed
    Obj label = mountTree(tree("QLabel", "l"));
    EXPECT_EQ(errorCode(query(cmd("widget.append", Obj{{"target", label.value(QStringLiteral("id"))}, {"text", "x"}}))), -1);
    tkde_release(id);
    tkde_release(int64_t(label.value(QStringLiteral("id")).toDouble()));
}

static void driver(QString dir)
{
    GcAttach gc;
    EXPECT(!waitFor("core.ready").isEmpty());
    EXPECT(query(cmd("core.capabilities")).value(QStringLiteral("namespaces")).toArray().contains(QStringLiteral("widget")));
    testGolden();
    testMinimalUpdate();
    testOrderedContainers();
    testErrorsAreAtomic();
    testEvents();
    testEmbed(dir);
    testStateRoundTrip(dir);
    testLog();
    testLifecycle();
    tkde_post("{\"cmd\":\"core.quit\"}");
}

int main()
{
    gcInit();
    ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    ::setenv("TKDE_DEBUG_COMMANDS", "1", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);
    QTemporaryDir dir;
    // a private config home: saveState without "file" must never touch the user's files
    ::setenv("XDG_CONFIG_HOME", qPrintable(dir.path()), 1);
    if (tkde_init("widget-test", "Widget test") != 0) {
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
    std::puts("widget ok");
    return 0;
}
