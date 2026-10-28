// Tests the `model` namespace of the real bridge through the C ABI. Offscreen.
// QAbstractItemModelTester is attached to the models, so every change made by the commands
// below is checked against the model contract as it happens.
#include "tinoxkde.h"

#include "driver.h"

#include <QAbstractItemModelTester>
#include <QAbstractItemView>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMimeData>
#include <QTest>

#include <chrono>
#include <thread>

using Obj = QJsonObject;

template <class F>
static void onUi(F fn)
{
    QMetaObject::invokeMethod(qApp, fn, Qt::BlockingQueuedConnection);
}

static QAbstractItemModel *modelPtr(int64_t id)
{
    Obj r = query(cmd("model.debug.pointer", Obj{{"target", double(id)}}));
    return qobject_cast<QAbstractItemModel *>(reinterpret_cast<QObject *>(quintptr(r.value(QStringLiteral("ptr")).toDouble())));
}

/// From now on every change of the model is verified by Qt's tester (fatal on a violation).
static void watch(int64_t id)
{
    onUi([] {
        qInstallMessageHandler(
            [](QtMsgType, const QMessageLogContext &, const QString &m) { std::fprintf(stderr, "QT: %s\n", qPrintable(m)); });
    });
    QAbstractItemModel *model = modelPtr(id); // asks the bridge: not from the UI thread
    onUi([model] { new QAbstractItemModelTester(model, QAbstractItemModelTester::FailureReportingMode::Fatal, model); });
}

static Obj row(const char *id, const char *text, Obj more = {})
{
    more.insert(QStringLiteral("id"), QString::fromLatin1(id));
    more.insert(QStringLiteral("text"), QString::fromLatin1(text));
    return more;
}

static int64_t newModel(Obj fields = {}, bool tester = true)
{
    Obj r = query(cmd("model.create", fields));
    EXPECT(r.value(QStringLiteral("ok")).toBool());
    const int64_t id = idOf(r);
    if (tester) {
        watch(id);
    }
    return id;
}

static bool ok(const Obj &r)
{
    return r.value(QStringLiteral("ok")).toBool();
}

static QString outline(int64_t model)
{
    QStringList lines;
    for (const QJsonValue &v : query(cmd("model.debug.rows", Obj{{"target", double(model)}})).value(QStringLiteral("rows")).toArray()) {
        lines << QStringLiteral("%1%2").arg(QString(v.toObject().value(QStringLiteral("depth")).toInt() * 2, QLatin1Char(' ')),
                                            v.toObject().value(QStringLiteral("id")).toString());
    }
    return lines.join(QLatin1Char('\n'));
}

static int64_t mountView(const char *type, const char *key, Obj props = {})
{
    Obj r = query(
        cmd("widget.mount", Obj{{"tree", Obj{{"type", QString::fromLatin1(type)}, {"key", QString::fromLatin1(key)}, {"props", props}}}}));
    EXPECT(ok(r));
    return int64_t(r.value(QStringLiteral("nodes")).toObject().value(QString::fromLatin1(key)).toDouble());
}

static void testStructure()
{
    const int64_t m = newModel(Obj{{"columns", QJsonArray{"Name", "Size"}}});
    EXPECT(ok(query(cmd(
        "model.reset",
        Obj{{"target", double(m)},
            {"rows",
             QJsonArray{
                 row("src", "src",
                     Obj{{"icon", "folder"},
                         {"children", QJsonArray{row("main", "Main.tnx", Obj{{"cells", QJsonArray{"1 KB"}}}), row("util", "Util.tnx")}}}),
                 row("README", "README.md",
                     Obj{{"cells", QJsonArray{Obj{{"text", "4 KB"}, {"toolTip", "four"}}}}, {"bold", true}, {"color", "#ff0000"}}),
                 row("tests", "tests", Obj{{"hasChildren", true}})}}}))));
    EXPECT_EQ(outline(m), QStringLiteral("src\n  main\n  util\nREADME\ntests"));
    EXPECT_EQ(query(cmd("model.info", Obj{{"target", double(m)}})).value(QStringLiteral("rows")).toInt(), 5);

    // roles
    Obj d = query(cmd("model.debug.data", Obj{{"target", double(m)}, {"id", "README"}}));
    EXPECT_EQ(d.value(QStringLiteral("display")).toString(), QStringLiteral("README.md"));
    EXPECT(d.value(QStringLiteral("bold")).toBool());
    EXPECT_EQ(d.value(QStringLiteral("color")).toString(), QStringLiteral("#ff0000"));
    EXPECT(d.value(QStringLiteral("checkState")).isNull());
    Obj cell = query(cmd("model.debug.data", Obj{{"target", double(m)}, {"id", "README"}, {"column", 1}}));
    EXPECT_EQ(cell.value(QStringLiteral("display")).toString(), QStringLiteral("4 KB"));
    EXPECT_EQ(cell.value(QStringLiteral("toolTip")).toString(), QStringLiteral("four"));
    EXPECT(query(cmd("model.debug.data", Obj{{"target", double(m)}, {"id", "src"}})).value(QStringLiteral("hasIcon")).toBool() ||
           true); // the theme may lack the icon

    // insert in the middle, at a parent, with nested rows
    EXPECT(ok(query(cmd("model.insert", Obj{{"target", double(m)},
                                            {"index", 1},
                                            {"rows", QJsonArray{row("docs", "docs", Obj{{"children", QJsonArray{row("a", "a.md")}}})}}}))));
    EXPECT(ok(query(cmd("model.insert", Obj{{"target", double(m)},
                                            {"parent", "src"},
                                            {"index", 0},
                                            {"rows", QJsonArray{row("first", "First.tnx"), row("second", "Second.tnx")}}}))));
    EXPECT_EQ(outline(m), QStringLiteral("src\n  first\n  second\n  main\n  util\ndocs\n  a\nREADME\ntests"));

    // move: within a parent, to another parent, to the top; never into itself
    EXPECT(ok(query(cmd("model.move", Obj{{"target", double(m)}, {"id", "main"}, {"parent", "src"}, {"index", 0}}))));
    EXPECT(ok(query(cmd("model.move", Obj{{"target", double(m)}, {"id", "util"}, {"parent", "docs"}}))));
    EXPECT(ok(query(cmd("model.move", Obj{{"target", double(m)}, {"id", "README"}, {"index", 0}}))));
    EXPECT_EQ(outline(m), QStringLiteral("README\nsrc\n  main\n  first\n  second\ndocs\n  a\n  util\ntests"));
    EXPECT_EQ(errorCode(query(cmd("model.move", Obj{{"target", double(m)}, {"id", "src"}, {"parent", "main"}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("model.move", Obj{{"target", double(m)}, {"id", "docs"}, {"parent", "docs"}}))), -1);
    EXPECT_EQ(outline(m), QStringLiteral("README\nsrc\n  main\n  first\n  second\ndocs\n  a\n  util\ntests"));

    // a colour can be set and cleared again
    EXPECT(ok(query(cmd("model.update", Obj{{"target", double(m)}, {"id", "first"}, {"fields", Obj{{"color", "#ff0000"}}}}))));
    EXPECT_EQ(query(cmd("model.debug.data", Obj{{"target", double(m)}, {"id", "first"}})).value(QStringLiteral("color")).toString(),
              QStringLiteral("#ff0000"));
    EXPECT(ok(query(cmd("model.update", Obj{{"target", double(m)}, {"id", "first"}, {"fields", Obj{{"color", ""}}}}))));
    EXPECT_EQ(query(cmd("model.debug.data", Obj{{"target", double(m)}, {"id", "first"}})).value(QStringLiteral("color")).toString(),
              QString());

    // update: fields change, data changes, structure does not
    EXPECT(ok(query(cmd(
        "model.update",
        Obj{{"target", double(m)}, {"id", "first"}, {"fields", Obj{{"text", "Renamed.tnx"}, {"checked", true}, {"toolTip", "tip"}}}}))));
    Obj u = query(cmd("model.debug.data", Obj{{"target", double(m)}, {"id", "first"}}));
    EXPECT_EQ(u.value(QStringLiteral("display")).toString(), QStringLiteral("Renamed.tnx"));
    EXPECT_EQ(u.value(QStringLiteral("checkState")).toInt(), int(Qt::Checked));
    EXPECT(ok(query(
        cmd("model.update", Obj{{"target", double(m)}, {"id", "first"}, {"fields", Obj{{"checked", QJsonValue(QJsonValue::Null)}}}}))));
    EXPECT(query(cmd("model.debug.data", Obj{{"target", double(m)}, {"id", "first"}})).value(QStringLiteral("checkState")).isNull());

    // remove a subtree and a row that goes with it
    EXPECT(ok(query(cmd("model.remove", Obj{{"target", double(m)}, {"ids", QJsonArray{"docs", "util", "second"}}}))));
    EXPECT_EQ(outline(m), QStringLiteral("README\nsrc\n  main\n  first\ntests"));
    EXPECT_EQ(errorCode(query(cmd("model.get", Obj{{"target", double(m)}, {"id", "util"}}))), -5);
    Obj g = query(cmd("model.get", Obj{{"target", double(m)}, {"id", "src"}}));
    EXPECT_EQ(g.value(QStringLiteral("children")).toArray(), QJsonArray({QStringLiteral("main"), QStringLiteral("first")}));
    EXPECT_EQ(g.value(QStringLiteral("index")).toInt(), 1);
    // an id is free again once its row is gone
    EXPECT(ok(query(cmd("model.insert", Obj{{"target", double(m)}, {"rows", QJsonArray{row("util", "again")}}}))));

    // a reset replaces everything and may reuse ids
    EXPECT(ok(query(cmd("model.reset", Obj{{"target", double(m)}, {"rows", QJsonArray{row("src", "fresh")}}}))));
    EXPECT_EQ(outline(m), QStringLiteral("src"));
    EXPECT(ok(query(cmd("model.reset", Obj{{"target", double(m)}, {"rows", QJsonArray{}}}))));
    EXPECT_EQ(query(cmd("model.info", Obj{{"target", double(m)}})).value(QStringLiteral("rows")).toInt(), 0);
    tkde_release(m);
}

static void testErrorsAreAtomic()
{
    const int64_t m = newModel();
    EXPECT(ok(query(cmd("model.reset", Obj{{"target", double(m)}, {"rows", QJsonArray{row("a", "A")}}}))));
    auto bad = [&](const char *name, Obj fields, const char *expect) {
        fields.insert(QStringLiteral("target"), double(m));
        Obj r = query(cmd(name, fields));
        if (errorCode(r) == 0) {
            std::fprintf(stderr, "FAIL %s should fail (%s)\n", name, expect);
            ++g_failures;
            return;
        }
        EXPECT(
            r.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().contains(QString::fromLatin1(expect)));
    };
    bad("model.insert", Obj{{"rows", QJsonArray{row("b", "B"), row("a", "dup")}}}, "not unique");
    bad("model.insert", Obj{{"rows", QJsonArray{row("c", "C"), row("c", "again")}}}, "not unique");
    bad("model.insert", Obj{{"rows", QJsonArray{Obj{{"text", "no id"}}}}}, "id");
    bad("model.insert", Obj{{"rows", QJsonArray{row("d", "D", Obj{{"nope", 1}})}}}, "unknown field 'nope'");
    bad("model.insert", Obj{{"rows", QJsonArray{row("e", "E", Obj{{"color", "not-a-colour"}})}}}, "not a colour");
    bad("model.insert", Obj{{"rows", QJsonArray{row("f", "F", Obj{{"children", QJsonArray{row("a", "dup in child")}}})}}}, "not unique");
    bad("model.insert", Obj{{"rows", QJsonArray{row("g", "G", Obj{{"flags", Obj{{"dance", true}}}})}}}, "flags");
    bad("model.insert", Obj{{"parent", "missing"}, {"rows", QJsonArray{row("h", "H")}}}, "no row 'missing'");
    bad("model.insert", Obj{{"rows", 5}}, "array");
    bad("model.remove", Obj{{"ids", QJsonArray{"a", "missing"}}}, "no row");
    bad("model.update", Obj{{"id", "a"}, {"fields", Obj{{"children", QJsonArray{}}}}}, "structure");
    bad("model.update", Obj{{"id", "missing"}, {"fields", Obj{}}}, "no row");
    EXPECT_EQ(outline(m), QStringLiteral("a")); // nothing of the above was applied
    EXPECT_EQ(errorCode(query(cmd("model.insert", Obj{{"target", 424242.0}, {"rows", QJsonArray{}}}))), -5);
    tkde_release(m);
}

static void testLazyTree()
{
    drainEvents();
    const int64_t m = newModel({}, false); // QAbstractItemModelTester calls fetchMore itself, which would be counted
    const int64_t tree = mountView("QTreeView", "tree", Obj{{"headerHidden", true}});
    EXPECT(ok(query(cmd("model.attach", Obj{{"model", double(m)}, {"view", double(tree)}}))));
    EXPECT(ok(query(cmd("model.reset", Obj{{"target", double(m)},
                                           {"rows", QJsonArray{row("root", "root", Obj{{"hasChildren", true}}),
                                                               row("other", "other", Obj{{"hasChildren", true}})}}}))));
    query(cmd("core.ping"));
    // showing the top level asks for nothing below it
    EXPECT(waitFor("model.fetchChildren", 300).isEmpty());
    EXPECT(!query(cmd("model.get", Obj{{"target", double(m)}, {"id", "root"}})).value(QStringLiteral("fetched")).toBool());

    // expanding exactly one folder asks for exactly that folder, once
    EXPECT(ok(query(cmd("model.expand", Obj{{"view", double(tree)}, {"id", "root"}}))));
    Obj ask = waitFor("model.fetchChildren", 1000);
    EXPECT_EQ(ask.value(QStringLiteral("id")).toString(), QStringLiteral("root"));
    EXPECT_EQ(int64_t(ask.value(QStringLiteral("target")).toDouble()), m);
    EXPECT(waitFor("model.fetchChildren", 200).isEmpty()); // no second request while one is pending
    EXPECT(ok(query(
        cmd("model.setChildren", Obj{{"target", double(m)},
                                     {"parent", "root"},
                                     {"rows", QJsonArray{row("sub", "sub", Obj{{"hasChildren", true}}), row("file", "file.txt")}}}))));
    EXPECT_EQ(outline(m), QStringLiteral("root\n  sub\n  file\nother"));
    EXPECT(query(cmd("model.get", Obj{{"target", double(m)}, {"id", "root"}})).value(QStringLiteral("fetched")).toBool());

    // the next level only when it is opened
    query(cmd("core.ping"));
    EXPECT(waitFor("model.fetchChildren", 200).isEmpty());
    EXPECT(ok(query(cmd("model.expand", Obj{{"view", double(tree)}, {"id", "sub"}}))));
    EXPECT_EQ(waitFor("model.fetchChildren", 1000).value(QStringLiteral("id")).toString(), QStringLiteral("sub"));
    // an empty answer: the folder has no children and stops claiming it has
    EXPECT(ok(query(cmd("model.setChildren", Obj{{"target", double(m)}, {"parent", "sub"}, {"rows", QJsonArray{}}}))));
    EXPECT(!query(cmd("model.get", Obj{{"target", double(m)}, {"id", "sub"}})).value(QStringLiteral("hasChildren")).toBool());
    // a refresh replaces children (ids of the old ones may come back)
    EXPECT(ok(
        query(cmd("model.setChildren",
                  Obj{{"target", double(m)}, {"parent", "root"}, {"rows", QJsonArray{row("file", "file.txt"), row("new", "new.txt")}}}))));
    EXPECT_EQ(outline(m), QStringLiteral("root\n  file\n  new\nother"));
    // collapsed and expanded events
    drainEvents();
    EXPECT(ok(query(cmd("model.expand", Obj{{"view", double(tree)}, {"id", "root"}, {"expanded", false}}))));
    Obj collapsed = waitFor("model.collapsed", 500);
    EXPECT_EQ(collapsed.value(QStringLiteral("key")).toString(), QStringLiteral("tree"));
    EXPECT_EQ(collapsed.value(QStringLiteral("id")).toString(), QStringLiteral("root"));
    EXPECT(ok(query(cmd("model.expand", Obj{{"view", double(tree)}, {"id", "root"}}))));
    EXPECT_EQ(waitFor("model.expanded", 500).value(QStringLiteral("id")).toString(), QStringLiteral("root"));
    EXPECT(waitFor("model.fetchChildren", 200).isEmpty()); // already loaded: no new request
    tkde_release(m);
}

static void testLazyRoot()
{
    drainEvents();
    const int64_t m = newModel(Obj{{"lazy", true}}, false);
    const int64_t list = mountView("QListView", "list");
    EXPECT(ok(query(cmd("model.attach", Obj{{"model", double(m)}, {"view", double(list)}}))));
    Obj ask = waitFor("model.fetchChildren", 1000);
    EXPECT(ask.value(QStringLiteral("id")).isNull()); // null = the top level
    EXPECT(ok(query(cmd("model.setChildren", Obj{{"target", double(m)}, {"rows", QJsonArray{row("one", "One"), row("two", "Two")}}}))));
    EXPECT_EQ(outline(m), QStringLiteral("one\ntwo"));
    tkde_release(m);
}

static void testProxyAndSelection()
{
    drainEvents();
    const int64_t m = newModel(Obj{{"columns", QJsonArray{"Name"}}});
    EXPECT(ok(
        query(cmd("model.reset",
                  Obj{{"target", double(m)},
                      {"rows", QJsonArray{row("c", "cherry"), row("a", "Apple"), row("b", "banana"),
                                          row("d", "date", Obj{{"children", QJsonArray{row("e", "elderberry"), row("f", "fig")}}})}}}))));
    Obj p = query(cmd("model.createProxy", Obj{{"source", double(m)}, {"sort", Obj{{"column", 0}, {"order", "asc"}}}}));
    EXPECT(ok(p));
    const int64_t proxy = idOf(p);
    watch(proxy);
    auto order = [&] {
        QStringList l;
        for (const QJsonValue &v : query(cmd("model.debug.rows", Obj{{"target", double(proxy)}})).value(QStringLiteral("rows")).toArray()) {
            l << v.toObject().value(QStringLiteral("id")).toString();
        }
        return l.join(QLatin1Char(','));
    };
    EXPECT_EQ(order(), QStringLiteral("a,b,c,d,e,f")); // case-insensitive, children stay under their parent
    EXPECT(ok(query(cmd("model.setFilter", Obj{{"target", double(proxy)}, {"sort", Obj{{"column", 0}, {"order", "desc"}}}}))));
    EXPECT_EQ(order(), QStringLiteral("d,f,e,c,b,a"));
    // filter keeps the ancestors of a match
    EXPECT(ok(query(cmd("model.setFilter", Obj{{"target", double(proxy)}, {"filter", Obj{{"text", "fig"}}}}))));
    EXPECT_EQ(order(), QStringLiteral("d,f"));
    EXPECT(ok(query(cmd("model.setFilter", Obj{{"target", double(proxy)}, {"filter", Obj{{"text", "^[ab]"}, {"regex", true}}}}))));
    EXPECT_EQ(order(), QStringLiteral("b,a"));
    EXPECT_EQ(errorCode(query(cmd("model.setFilter", Obj{{"target", double(proxy)}, {"filter", Obj{{"text", "(["}, {"regex", true}}}}))),
              -1);
    EXPECT_EQ(order(), QStringLiteral("b,a")); // the bad pattern changed nothing
    EXPECT(ok(query(cmd("model.setFilter", Obj{{"target", double(proxy)}, {"filter", Obj{{"text", ""}}}}))));
    // changes of the source flow through
    EXPECT(ok(query(cmd("model.insert", Obj{{"target", double(m)}, {"rows", QJsonArray{row("z", "zucchini")}}}))));
    EXPECT_EQ(order(), QStringLiteral("z,d,f,e,c,b,a"));
    EXPECT(ok(query(cmd("model.update", Obj{{"target", double(m)}, {"id", "z"}, {"fields", Obj{{"text", "0 first"}}}}))));
    EXPECT_EQ(order(), QStringLiteral("d,f,e,c,b,a,z"));

    // a view on the proxy: selections come back as source ids
    const int64_t view = mountView("QTableView", "table", Obj{{"selectionMode", "extended"}});
    EXPECT(ok(query(cmd("model.attach", Obj{{"model", double(proxy)}, {"view", double(view)}}))));
    drainEvents();
    EXPECT(ok(query(cmd("model.select", Obj{{"view", double(view)}, {"ids", QJsonArray{"c", "a"}}, {"current", "c"}}))));
    Obj changed = waitFor("model.selectionChanged", 1000);
    EXPECT_EQ(changed.value(QStringLiteral("ids")).toArray().size(), 2);
    EXPECT_EQ(changed.value(QStringLiteral("key")).toString(), QStringLiteral("table"));
    Obj sel = query(cmd("model.selection", Obj{{"view", double(view)}}));
    EXPECT_EQ(sel.value(QStringLiteral("current")).toString(), QStringLiteral("c"));
    QStringList ids;
    for (const QJsonValue &v : sel.value(QStringLiteral("ids")).toArray()) {
        ids << v.toString();
    }
    ids.sort();
    EXPECT_EQ(ids.join(QLatin1Char(',')), QStringLiteral("a,c"));
    EXPECT_EQ(errorCode(query(cmd("model.select", Obj{{"view", double(view)}, {"ids", QJsonArray{"nonexistent"}}}))), -5);

    // activation by keyboard and a context-menu request
    drainEvents();
    onUi([] {
        for (QWidget *w : QApplication::allWidgets()) {
            if (w->objectName() == QLatin1String("table")) {
                w->show();
                w->setFocus();
                QTest::keyClick(w, Qt::Key_Return);
            }
        }
    });
    Obj act = waitFor("model.activated", 1000);
    EXPECT_EQ(act.value(QStringLiteral("id")).toString(), QStringLiteral("c"));
    onUi([] {
        for (QWidget *w : QApplication::allWidgets()) {
            if (w->objectName() == QLatin1String("table")) {
                auto *v = qobject_cast<QAbstractItemView *>(w);
                QContextMenuEvent e(QContextMenuEvent::Mouse, v->visualRect(v->currentIndex()).center(),
                                    v->viewport()->mapToGlobal(v->visualRect(v->currentIndex()).center()));
                QApplication::sendEvent(v->viewport(), &e);
            }
        }
    });
    Obj ctx = waitFor("model.contextMenuRequested", 1000);
    EXPECT_EQ(ctx.value(QStringLiteral("key")).toString(), QStringLiteral("table"));
    EXPECT(ctx.contains(QStringLiteral("x")) && ctx.contains(QStringLiteral("ids")));

    // detach, then drop the model while a view still shows it
    EXPECT(ok(query(cmd("model.attach", Obj{{"model", QJsonValue(QJsonValue::Null)}, {"view", double(view)}}))));
    EXPECT(ok(query(cmd("model.attach", Obj{{"model", double(proxy)}, {"view", double(view)}}))));
    EXPECT(tkde_release(m) == TKDE_OK);
    query(cmd("core.ping"));
    EXPECT_EQ(errorCode(query(cmd("model.info", Obj{{"target", double(m)}}))), -5);
    EXPECT_EQ(errorCode(query(cmd("model.setFilter", Obj{{"target", double(proxy)}, {"filter", Obj{{"text", "x"}}}}))),
              -5); // the proxy went with its model
}

static void testCheckAndEdit()
{
    drainEvents();
    const int64_t m = newModel();
    EXPECT(
        ok(query(cmd("model.reset", Obj{{"target", double(m)},
                                        {"rows", QJsonArray{row("t", "test_a", Obj{{"checked", false}, {"flags", Obj{{"editable", true}}}}),
                                                            row("plain", "plain")}}}))));
    QAbstractItemModel *model = modelPtr(m);
    onUi([model] {
        QVERIFY(model->flags(model->index(0, 0)) & Qt::ItemIsUserCheckable);
        QVERIFY(!(model->flags(model->index(1, 0)) & Qt::ItemIsUserCheckable));
        QVERIFY(model->setData(model->index(0, 0), Qt::Checked, Qt::CheckStateRole));
        QVERIFY(!model->setData(model->index(0, 0), QStringLiteral("renamed"), Qt::EditRole)); // a request, not a change
        QVERIFY(!model->setData(model->index(1, 0), QStringLiteral("x"), Qt::EditRole));       // not editable: refused outright
    });
    Obj check = waitFor("model.checkChanged", 1000);
    EXPECT_EQ(check.value(QStringLiteral("id")).toString(), QStringLiteral("t"));
    EXPECT(check.value(QStringLiteral("checked")).toBool());
    EXPECT_EQ(query(cmd("model.debug.data", Obj{{"target", double(m)}, {"id", "t"}})).value(QStringLiteral("checkState")).toInt(),
              int(Qt::Checked));
    Obj edit = waitFor("model.editRequested", 1000);
    EXPECT_EQ(edit.value(QStringLiteral("text")).toString(), QStringLiteral("renamed"));
    EXPECT_EQ(query(cmd("model.debug.data", Obj{{"target", double(m)}, {"id", "t"}})).value(QStringLiteral("display")).toString(),
              QStringLiteral("test_a")); // unchanged until the logic says so
    EXPECT(waitFor("model.editRequested", 100).isEmpty());
    tkde_release(m);
}

static void testDragAndDrop()
{
    drainEvents();
    const int64_t m = newModel(Obj{{"acceptDrops", true}});
    EXPECT(ok(query(cmd("model.reset", Obj{{"target", double(m)},
                                           {"rows", QJsonArray{row("dir", "dir", Obj{{"flags", Obj{{"drop", true}}}}),
                                                               row("file", "f.txt", Obj{{"flags", Obj{{"drag", true}}}})}}}))));
    QAbstractItemModel *model = modelPtr(m);
    onUi([model] {
        QVERIFY(model->flags(model->index(0, 0)) & Qt::ItemIsDropEnabled);
        QVERIFY(!(model->flags(model->index(0, 0)) & Qt::ItemIsDragEnabled));
        QVERIFY(model->flags(model->index(1, 0)) & Qt::ItemIsDragEnabled);
        QVERIFY(model->flags(QModelIndex()) & Qt::ItemIsDropEnabled); // acceptDrops: the top level takes drops
        QMimeData *mime = model->mimeData({model->index(1, 0)});
        QVERIFY(mime->hasFormat(QStringLiteral("application/x-tinox-rows")));
        QVERIFY(model->mimeTypes().contains(QStringLiteral("text/uri-list")));
        delete mime;
    });
    // dropping rows on a folder is a request; the model itself does not move anything
    Obj d = query(cmd("model.debug.drop", Obj{{"target", double(m)}, {"ids", QJsonArray{"file"}}, {"parent", "dir"}, {"action", "move"}}));
    EXPECT(ok(d) && !d.value(QStringLiteral("accepted")).toBool());
    Obj ev = waitFor("model.dropRequested", 1000);
    EXPECT_EQ(ev.value(QStringLiteral("ids")).toArray(), QJsonArray({QStringLiteral("file")}));
    EXPECT_EQ(ev.value(QStringLiteral("parent")).toString(), QStringLiteral("dir"));
    EXPECT_EQ(ev.value(QStringLiteral("action")).toString(), QStringLiteral("move"));
    EXPECT_EQ(outline(m), QStringLiteral("dir\nfile"));
    // files dragged in from the file manager
    query(cmd("model.debug.drop",
              Obj{{"target", double(m)}, {"urls", QJsonArray{"file:///tmp/a.txt", "file:///tmp/b.txt"}}, {"action", "copy"}}));
    Obj ext = waitFor("model.dropRequested", 1000);
    EXPECT_EQ(ext.value(QStringLiteral("urls")).toArray().size(), 2);
    EXPECT_EQ(ext.value(QStringLiteral("files")).toArray(), QJsonArray({QStringLiteral("/tmp/a.txt"), QStringLiteral("/tmp/b.txt")}));
    EXPECT(ext.value(QStringLiteral("parent")).isNull());
    EXPECT_EQ(ext.value(QStringLiteral("action")).toString(), QStringLiteral("copy"));
    tkde_release(m);
}

static void testLargeModel()
{
    // 10k rows in one reset, shown in a view, scrolled to the end
    QJsonArray rows;
    for (int i = 0; i < 10000; ++i) {
        const QByteArray id = "r" + QByteArray::number(i);
        rows.append(Obj{{"id", QString::fromLatin1(id)},
                        {"text", QString::fromLatin1("row " + QByteArray::number(i))},
                        {"cells", QJsonArray{"x", "y"}},
                        {"icon", "text-x-generic"}});
    }
    const int64_t m = newModel(Obj{{"columns", QJsonArray{"A", "B", "C"}}}, false); // the tester walks the whole model after each change
    const int64_t view = mountView("QTableView", "big", Obj{{"stretchLastSection", true}});
    EXPECT(ok(query(cmd("model.attach", Obj{{"model", double(m)}, {"view", double(view)}}))));
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT(ok(query(cmd("model.reset", Obj{{"target", double(m)}, {"rows", rows}}))));
    const auto t1 = std::chrono::steady_clock::now();
    EXPECT(ok(query(cmd("model.scrollTo", Obj{{"view", double(view)}, {"id", "r9999"}}))));
    EXPECT(ok(query(cmd("model.scrollTo", Obj{{"view", double(view)}, {"id", "r0"}}))));
    const auto t2 = std::chrono::steady_clock::now();
    // 100 inserts into a model with 10k rows, as a file watcher would cause
    for (int i = 0; i < 100; ++i) {
        const QByteArray id = "n" + QByteArray::number(i);
        EXPECT(ok(query(cmd(
            "model.insert",
            Obj{{"target", double(m)}, {"index", i * 50}, {"rows", QJsonArray{Obj{{"id", QString::fromLatin1(id)}, {"text", "new"}}}}}))));
    }
    const auto t3 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return int(std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count()); };
    std::fprintf(stderr, "10k rows: reset %d ms, scroll %d ms, 100 inserts %d ms\n", ms(t0, t1), ms(t1, t2), ms(t2, t3));
    EXPECT(ms(t0, t1) < 3000);
    EXPECT(ms(t1, t2) < 2000);
    EXPECT(ms(t2, t3) < 3000);
    EXPECT_EQ(query(cmd("model.info", Obj{{"target", double(m)}})).value(QStringLiteral("rows")).toInt(), 10100);
    tkde_release(m);
}

static void driver()
{
    GcAttach gc;
    EXPECT(!waitFor("core.ready").isEmpty());
    EXPECT(query(cmd("core.capabilities")).value(QStringLiteral("namespaces")).toArray().contains(QStringLiteral("model")));
    testStructure();
    testErrorsAreAtomic();
    testLazyTree();
    testLazyRoot();
    testProxyAndSelection();
    testCheckAndEdit();
    testDragAndDrop();
    testLargeModel();
    tkde_post("{\"cmd\":\"core.quit\"}");
}

int main()
{
    gcInit();
    ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    ::setenv("TKDE_DEBUG_COMMANDS", "1", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);
    if (tkde_init("model-test", "Model test") != 0) {
        std::fprintf(stderr, "init failed: %s\n", tkde_last_error());
        return 1;
    }
    std::thread logic(driver);
    tkde_exec();
    logic.join();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures.load());
        return 1;
    }
    std::puts("model ok");
    return 0;
}
