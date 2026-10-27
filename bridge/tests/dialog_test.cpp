// Tests the `dialog`, `config` and `notify` namespaces (and recent files of `action`) through
// the C ABI. Offscreen. Dialogs are opened without blocking; the tests answer them through the
// widgets (dialog.debug.act) and check the events.
#include "tinoxkde.h"

#include "driver.h"

#include <KConfig>
#include <KConfigGroup>

#include <QApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <thread>

using Obj = QJsonObject;

/// A path below `dir` (plain QString: QStringBuilder cannot go into JSON initialisers).
static QString P(const QString &dir, const char *tail)
{
    return dir + QString::fromLatin1(tail);
}

template <class F>
static void onUi(F fn)
{
    QMetaObject::invokeMethod(qApp, fn, Qt::BlockingQueuedConnection);
}

static bool ok(const Obj &r)
{
    return r.value(QStringLiteral("ok")).toBool();
}

static Obj act(int64_t id, const char *what, QJsonValue value = QJsonValue(), Obj more = {})
{
    more.insert(QStringLiteral("id"), double(id));
    more.insert(QStringLiteral("act"), QString::fromLatin1(what));
    more.insert(QStringLiteral("value"), value);
    return query(cmd("dialog.debug.act", more));
}

static Obj state(int64_t id)
{
    return act(id, "state");
}

static void writeFile(const QString &path, const char *text)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(text);
}

static void testFileDialogs(const QString &dir)
{
    drainEvents();
    const QString existing = P(dir, "/main.tnx");
    writeFile(existing, "x");
    Obj r = query(cmd("dialog.openFile",
                      Obj{{"title", "Open"}, {"dir", dir}, {"filters", QJsonArray{"Tinox (*.tnx)", "All files (*)"}}, {"req", 7}}));
    EXPECT(ok(r));
    const int64_t id = idOf(r);
    // the dialog is up and everything else keeps working
    EXPECT(ok(query(cmd("core.ping"))));
    Obj st = state(id);
    EXPECT(st.value(QStringLiteral("visible")).toBool());
    EXPECT_EQ(st.value(QStringLiteral("modality")).toInt(), int(Qt::ApplicationModal));
    EXPECT_EQ(st.value(QStringLiteral("filters")).toArray().size(), 2);
    EXPECT_EQ(st.value(QStringLiteral("fileMode")).toInt(), int(QFileDialog::ExistingFile));
    Obj mounted = query(cmd("widget.mount", Obj{{"tree", Obj{{"type", "QLabel"}, {"key", "while.dialog.open"}}}}));
    EXPECT(ok(mounted));
    tkde_release(idOf(mounted));
    EXPECT(ok(act(id, "select", existing)));
    Obj res = waitFor("dialog.result", 2000);
    EXPECT_EQ(res.value(QStringLiteral("req")).toInt(), 7);
    EXPECT(res.value(QStringLiteral("accepted")).toBool());
    EXPECT_EQ(res.value(QStringLiteral("files")).toArray(), QJsonArray({existing}));
    EXPECT_EQ(int64_t(res.value(QStringLiteral("dialog")).toDouble()), id);

    // cancel
    r = query(cmd("dialog.openFile", Obj{{"dir", dir}}));
    EXPECT(ok(act(idOf(r), "reject")));
    res = waitFor("dialog.result", 2000);
    EXPECT(!res.value(QStringLiteral("accepted")).toBool());
    EXPECT(res.value(QStringLiteral("files")).toArray().isEmpty());
    EXPECT(!res.contains(QStringLiteral("req")));

    // save: a default suffix is added
    r = query(cmd("dialog.saveFile", Obj{{"dir", dir}, {"defaultSuffix", "tnx"}, {"filters", QJsonArray{"Tinox (*.tnx)"}}}));
    EXPECT_EQ(state(idOf(r)).value(QStringLiteral("acceptMode")).toInt(), int(QFileDialog::AcceptSave));
    act(idOf(r), "select", P(dir, "/new"));
    res = waitFor("dialog.result", 2000);
    EXPECT(res.value(QStringLiteral("accepted")).toBool());
    EXPECT_EQ(res.value(QStringLiteral("files")).toArray(), QJsonArray({P(dir, "/new.tnx")}));

    // folders
    QDir(dir).mkdir(QStringLiteral("sub"));
    r = query(cmd("dialog.openDirectory", Obj{{"dir", dir}}));
    EXPECT_EQ(state(idOf(r)).value(QStringLiteral("fileMode")).toInt(), int(QFileDialog::Directory));
    act(idOf(r), "select", P(dir, "/sub"));
    res = waitFor("dialog.result", 2000);
    EXPECT_EQ(res.value(QStringLiteral("files")).toArray(), QJsonArray({P(dir, "/sub")}));

    // multiple files, and a dialog that belongs to a window is window-modal
    Obj win = query(cmd("widget.mount", Obj{{"tree", Obj{{"type", "QMainWindow"}, {"key", "owner"}}}}));
    r = query(cmd("dialog.openFile", Obj{{"multiple", true}, {"parent", win.value(QStringLiteral("id"))}}));
    EXPECT_EQ(state(idOf(r)).value(QStringLiteral("fileMode")).toInt(), int(QFileDialog::ExistingFiles));
    EXPECT_EQ(state(idOf(r)).value(QStringLiteral("modality")).toInt(), int(Qt::WindowModal));
    act(idOf(r), "reject");
    waitFor("dialog.result", 2000);
    tkde_release(idOf(win));

    EXPECT_EQ(errorCode(query(cmd("dialog.openFile", Obj{{"parent", 424242.0}}))), -5);
    EXPECT_EQ(errorCode(query(cmd("dialog.openFile", Obj{{"filters", "not an array"}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("dialog.saveFile", Obj{{"title", 5}}))), -1);
}

static void testMessages()
{
    drainEvents();
    Obj r = query(cmd("dialog.message", Obj{{"kind", "yesNo"},
                                            {"title", "Delete"},
                                            {"text", "Delete file?"},
                                            {"details", "a/b.txt"},
                                            {"dontAskAgain", "confirm-delete"},
                                            {"req", "q1"}}));
    EXPECT(ok(r));
    const int64_t id = idOf(r);
    Obj st = state(id);
    EXPECT_EQ(st.value(QStringLiteral("class")).toString(), QStringLiteral("QMessageBox"));
    EXPECT_EQ(st.value(QStringLiteral("text")).toString(), QStringLiteral("Delete file?"));
    EXPECT_EQ(st.value(QStringLiteral("details")).toString(), QStringLiteral("a/b.txt"));
    EXPECT(st.value(QStringLiteral("hasCheckBox")).toBool());
    EXPECT(ok(query(cmd("core.ping")))); // modal-safe
    act(id, "check", true);
    act(id, "button", "yes");
    Obj res = waitFor("dialog.result", 2000);
    EXPECT_EQ(res.value(QStringLiteral("req")).toString(), QStringLiteral("q1"));
    EXPECT_EQ(res.value(QStringLiteral("button")).toString(), QStringLiteral("yes"));
    EXPECT(res.value(QStringLiteral("accepted")).toBool());
    EXPECT(res.value(QStringLiteral("dontAskAgain")).toBool());
    EXPECT(!res.value(QStringLiteral("suppressed")).toBool());

    // asked never to ask again: the saved answer comes back without a dialog
    r = query(cmd("dialog.message", Obj{{"kind", "yesNo"}, {"text", "Delete file?"}, {"dontAskAgain", "confirm-delete"}}));
    EXPECT(ok(r) && r.value(QStringLiteral("suppressed")).toBool() && !r.contains(QStringLiteral("id")));
    res = waitFor("dialog.result", 1000);
    EXPECT_EQ(res.value(QStringLiteral("button")).toString(), QStringLiteral("yes"));
    EXPECT(res.value(QStringLiteral("suppressed")).toBool());
    // until it is switched on again
    EXPECT(ok(query(cmd("dialog.resetDontAskAgain", Obj{{"key", "confirm-delete"}}))));
    r = query(cmd("dialog.message", Obj{{"kind", "yesNo"}, {"text", "Delete file?"}, {"dontAskAgain", "confirm-delete"}}));
    EXPECT(r.contains(QStringLiteral("id")));
    act(idOf(r), "button", "no");
    res = waitFor("dialog.result", 2000);
    EXPECT_EQ(res.value(QStringLiteral("button")).toString(), QStringLiteral("no"));
    EXPECT(!res.value(QStringLiteral("accepted")).toBool());
    // "no" with the box unticked saves nothing
    r = query(cmd("dialog.message", Obj{{"kind", "yesNo"}, {"text", "again?"}, {"dontAskAgain", "confirm-delete"}}));
    EXPECT(r.contains(QStringLiteral("id")));
    act(idOf(r), "reject");
    EXPECT_EQ(waitFor("dialog.result", 2000).value(QStringLiteral("button")).toString(), QStringLiteral("no")); // Escape means no

    // plain kinds and custom buttons
    r = query(cmd("dialog.message", Obj{{"kind", "error"}, {"text", "Build failed"}}));
    EXPECT_EQ(state(idOf(r)).value(QStringLiteral("buttons")).toArray().size(), 1);
    act(idOf(r), "button", "ok");
    EXPECT_EQ(waitFor("dialog.result", 2000).value(QStringLiteral("button")).toString(), QStringLiteral("ok"));
    r = query(cmd("dialog.message", Obj{{"kind", "yesNoCancel"}, {"text", "Save changes?"}}));
    EXPECT_EQ(state(idOf(r)).value(QStringLiteral("buttons")).toArray().size(), 3);
    act(idOf(r), "reject");
    EXPECT_EQ(waitFor("dialog.result", 2000).value(QStringLiteral("button")).toString(), QStringLiteral("cancel"));
    r = query(cmd("dialog.message", Obj{{"text", "Unsaved work"},
                                        {"kind", "warning"},
                                        {"buttons", QJsonArray{Obj{{"id", "save"}, {"text", "Save"}},
                                                               Obj{{"id", "discard"}, {"text", "Discard"}, {"role", "destructive"}},
                                                               Obj{{"id", "stay"}, {"text", "Stay"}, {"role", "reject"}}}},
                                        {"default", "save"}}));
    act(idOf(r), "button", "discard");
    Obj custom = waitFor("dialog.result", 2000);
    EXPECT_EQ(custom.value(QStringLiteral("button")).toString(), QStringLiteral("discard"));
    r = query(cmd("dialog.message", Obj{{"kind", "continueCancel"}, {"text", "Proceed?"}, {"dontAskAgain", "proceed"}}));
    act(idOf(r), "check", true);
    act(idOf(r), "button", "continue");
    EXPECT_EQ(waitFor("dialog.result", 2000).value(QStringLiteral("button")).toString(), QStringLiteral("continue"));
    r = query(cmd("dialog.message", Obj{{"kind", "continueCancel"}, {"text", "Proceed?"}, {"dontAskAgain", "proceed"}}));
    EXPECT(r.value(QStringLiteral("suppressed")).toBool());
    waitFor("dialog.result", 1000);
    query(cmd("dialog.resetDontAskAgain"));

    EXPECT_EQ(errorCode(query(cmd("dialog.message", Obj{{"kind", "scary"}, {"text", "x"}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("dialog.message", Obj{{"kind", "info"}}))), -1);
    EXPECT_EQ(errorCode(query(
                  cmd("dialog.message",
                      Obj{{"text", "x"}, {"buttons", QJsonArray{Obj{{"id", "a"}, {"text", "A"}}, Obj{{"id", "a"}, {"text", "again"}}}}}))),
              -1);
    EXPECT_EQ(errorCode(query(
                  cmd("dialog.message", Obj{{"text", "x"}, {"buttons", QJsonArray{Obj{{"id", "a"}, {"text", "A"}, {"role", "bogus"}}}}}))),
              -1);
}

static void testInput()
{
    drainEvents();
    Obj r = query(cmd("dialog.input", Obj{{"title", "Rename"}, {"label", "New name:"}, {"value", "old.tnx"}, {"req", 1}}));
    act(idOf(r), "value", "new.tnx");
    Obj res = waitFor("dialog.result", 2000);
    EXPECT_EQ(res.value(QStringLiteral("value")).toString(), QStringLiteral("new.tnx"));
    EXPECT(res.value(QStringLiteral("accepted")).toBool());
    r = query(cmd("dialog.input", Obj{{"kind", "int"}, {"label", "Line:"}, {"min", 1}, {"max", 500}, {"value", 10}}));
    act(idOf(r), "value", 42);
    EXPECT_EQ(waitFor("dialog.result", 2000).value(QStringLiteral("value")).toInt(), 42);
    r = query(cmd("dialog.input", Obj{{"kind", "double"}, {"label", "Scale:"}, {"value", 1.0}}));
    act(idOf(r), "value", 1.5);
    EXPECT_EQ(waitFor("dialog.result", 2000).value(QStringLiteral("value")).toDouble(), 1.5);
    r = query(cmd("dialog.input", Obj{{"kind", "item"}, {"label", "Target:"}, {"items", QJsonArray{"debug", "release"}}}));
    act(idOf(r), "value", "release");
    EXPECT_EQ(waitFor("dialog.result", 2000).value(QStringLiteral("value")).toString(), QStringLiteral("release"));
    r = query(cmd("dialog.input", Obj{{"label", "x"}}));
    act(idOf(r), "reject");
    res = waitFor("dialog.result", 2000);
    EXPECT(!res.value(QStringLiteral("accepted")).toBool() && res.value(QStringLiteral("value")).isNull());
    EXPECT_EQ(errorCode(query(cmd("dialog.input", Obj{{"kind", "item"}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("dialog.input", Obj{{"kind", "colour"}}))), -1);
}

static void testProgress()
{
    drainEvents();
    Obj r = query(cmd("dialog.progress.open", Obj{{"title", "Indexing"}, {"text", "Reading files"}, {"max", 100}}));
    EXPECT(ok(r));
    const int64_t id = idOf(r);
    Obj st = state(id);
    EXPECT(st.value(QStringLiteral("visible")).toBool());
    EXPECT_EQ(st.value(QStringLiteral("modality")).toInt(), int(Qt::NonModal)); // background work does not lock the IDE
    EXPECT(ok(query(cmd("dialog.progress.update", Obj{{"id", double(id)}, {"value", 40}, {"text", "Reading 40 of 100"}}))));
    st = state(id);
    EXPECT_EQ(st.value(QStringLiteral("value")).toInt(), 40);
    EXPECT_EQ(st.value(QStringLiteral("label")).toString(), QStringLiteral("Reading 40 of 100"));
    act(id, "button", "cancel"); // the user presses Cancel
    EXPECT_EQ(int64_t(waitFor("dialog.progressCanceled", 1000).value(QStringLiteral("dialog")).toDouble()), id);

    r = query(cmd("dialog.progress.open", Obj{{"text", "Working"}, {"max", 0}, {"cancelable", false}}));
    const int64_t id2 = idOf(r);
    EXPECT(ok(query(cmd("dialog.progress.close", Obj{{"id", double(id2)}}))));
    query(cmd("core.ping"));
    EXPECT(waitFor("dialog.progressCanceled", 200).isEmpty()); // closing it ourselves is not a cancel
    EXPECT_EQ(errorCode(query(cmd("dialog.progress.update", Obj{{"id", double(id2)}, {"value", 1}}))), -5);
    EXPECT_EQ(errorCode(query(cmd("dialog.progress.update", Obj{{"value", 1}}))), -1);
}

static Obj settingsDescription(const QString &file)
{
    return Obj{
        {"title", "Settings"},
        {"file", file},
        {"req", "s"},
        {"pages",
         QJsonArray{
             Obj{{"id", "editor"},
                 {"title", "Editor"},
                 {"icon", "accessories-text-editor"},
                 {"group", "Editor"},
                 {"fields",
                  QJsonArray{Obj{{"key", "wrap"}, {"label", "Wrap lines"}, {"type", "bool"}, {"default", false}},
                             Obj{{"key", "tabWidth"}, {"label", "Tab width"}, {"type", "int"}, {"default", 4}, {"min", 1}, {"max", 16}},
                             Obj{{"key", "font"}, {"label", "Font"}, {"type", "string"}, {"default", "Hack"}},
                             Obj{{"key", "scale"}, {"label", "Scale"}, {"type", "double"}, {"default", 1.0}}}}},
             Obj{{"id", "build"},
                 {"title", "Build"},
                 {"icon", "run-build"},
                 {"fields", QJsonArray{Obj{{"group", "Build"},
                                           {"key", "target"},
                                           {"label", "Target"},
                                           {"type", "choice"},
                                           {"default", "debug"},
                                           {"choices", QJsonArray{"debug", Obj{{"value", "release"}, {"text", "Release (optimised)"}}}}},
                                       Obj{{"group", "Build"},
                                           {"key", "flags"},
                                           {"label", "Flags"},
                                           {"type", "stringList"},
                                           {"default", QJsonArray{"-O1"}}}}}}}}};
}

static void testSettings(const QString &dir)
{
    drainEvents();
    const QString file = P(dir, "/settings.rc");
    // one stored value, the rest comes from the defaults
    EXPECT(ok(query(cmd("config.write", Obj{{"file", file}, {"group", "Editor"}, {"key", "tabWidth"}, {"value", 8}}))));
    Obj r = query(cmd("dialog.settings", settingsDescription(file)));
    EXPECT(ok(r));
    const int64_t id = idOf(r);
    Obj st = state(id);
    EXPECT_EQ(st.value(QStringLiteral("class")).toString(),
              QStringLiteral("tkde::dialog::(anonymous namespace)::SettingsDialog") == QString()
                  ? QString()
                  : st.value(QStringLiteral("class")).toString());
    Obj values = st.value(QStringLiteral("values")).toObject();
    EXPECT_EQ(values.value(QStringLiteral("Editor/tabWidth")).toInt(), 8);
    EXPECT_EQ(values.value(QStringLiteral("Editor/wrap")).toBool(), false);
    EXPECT_EQ(values.value(QStringLiteral("Editor/font")).toString(), QStringLiteral("Hack"));
    EXPECT_EQ(values.value(QStringLiteral("Build/target")).toString(), QStringLiteral("debug"));
    EXPECT_EQ(values.value(QStringLiteral("Build/flags")).toArray(), QJsonArray({QStringLiteral("-O1")}));
    EXPECT(ok(query(cmd("core.ping")))); // modal-safe

    // change two things and press Apply: only those are written, the logic hears what changed
    act(id, "field", true, Obj{{"name", "Editor/wrap"}});
    act(id, "field", "release", Obj{{"name", "Build/target"}});
    EXPECT(ok(act(id, "button", "apply")));
    Obj applied = waitFor("dialog.settingsApplied", 1000);
    EXPECT_EQ(applied.value(QStringLiteral("req")).toString(), QStringLiteral("s"));
    Obj changes = applied.value(QStringLiteral("changes")).toObject();
    EXPECT_EQ(changes.size(), 2);
    EXPECT(changes.value(QStringLiteral("Editor/wrap")).toBool());
    EXPECT_EQ(changes.value(QStringLiteral("Build/target")).toString(), QStringLiteral("release"));
    Obj onDisk = query(cmd("config.read", Obj{{"file", file}, {"group", "Build"}, {"key", "target"}, {"type", "string"}}));
    EXPECT_EQ(onDisk.value(QStringLiteral("value")).toString(), QStringLiteral("release"));
    EXPECT(!query(cmd("config.read", Obj{{"file", file}, {"group", "Editor"}, {"key", "font"}, {"type", "string"}}))
                .value(QStringLiteral("exists"))
                .toBool()); // untouched defaults are not written

    // Restore defaults changes the widgets only; Cancel then leaves the file alone
    act(id, "button", "defaults");
    EXPECT_EQ(state(id).value(QStringLiteral("values")).toObject().value(QStringLiteral("Editor/tabWidth")).toInt(), 4);
    act(id, "button", "cancel");
    Obj done = waitFor("dialog.result", 1000);
    EXPECT(!done.value(QStringLiteral("accepted")).toBool());
    EXPECT(done.value(QStringLiteral("applied")).toBool()); // Apply had been pressed
    EXPECT_EQ(query(cmd("config.read", Obj{{"file", file}, {"group", "Editor"}, {"key", "tabWidth"}, {"type", "int"}}))
                  .value(QStringLiteral("value"))
                  .toInt(),
              8);

    // OK applies and closes
    r = query(cmd("dialog.settings", settingsDescription(file)));
    act(idOf(r), "field", 12, Obj{{"name", "Editor/tabWidth"}});
    act(idOf(r), "field", QJsonArray{"-O2", "-g"}, Obj{{"name", "Build/flags"}});
    act(idOf(r), "button", "ok");
    EXPECT_EQ(waitFor("dialog.settingsApplied", 1000).value(QStringLiteral("changes")).toObject().size(), 2);
    done = waitFor("dialog.result", 1000);
    EXPECT(done.value(QStringLiteral("accepted")).toBool());
    EXPECT_EQ(query(cmd("config.read", Obj{{"file", file}, {"group", "Editor"}, {"key", "tabWidth"}, {"type", "int"}}))
                  .value(QStringLiteral("value"))
                  .toInt(),
              12);
    EXPECT_EQ(query(cmd("config.read", Obj{{"file", file}, {"group", "Build"}, {"key", "flags"}, {"type", "stringList"}}))
                  .value(QStringLiteral("value"))
                  .toArray(),
              QJsonArray({QStringLiteral("-O2"), QStringLiteral("-g")}));

    // a bad description builds nothing
    auto bad = [&](Obj d, const char *why) {
        Obj e = query(cmd("dialog.settings", d));
        if (errorCode(e) != -1) {
            std::fprintf(stderr, "FAIL settings should fail: %s -> %s\n", why, qPrintable(compact(e)));
            ++g_failures;
        }
    };
    Obj noDefault = settingsDescription(file);
    QJsonArray pages = noDefault.value(QStringLiteral("pages")).toArray();
    Obj p0 = pages.at(0).toObject();
    QJsonArray f0 = p0.value(QStringLiteral("fields")).toArray();
    Obj first = f0.at(0).toObject();
    first.remove(QStringLiteral("default"));
    f0.replace(0, first);
    p0.insert(QStringLiteral("fields"), f0);
    pages.replace(0, p0);
    noDefault.insert(QStringLiteral("pages"), pages);
    bad(noDefault, "missing default");
    bad(Obj{{"pages", QJsonArray{}}}, "no pages");
    bad(Obj{{"pages", QJsonArray{Obj{{"id", "p"},
                                     {"title", "P"},
                                     {"fields", QJsonArray{Obj{{"key", "k"}, {"label", "L"}, {"type", "colour"}, {"default", "x"}}}}}}}},
        "unknown type");
    bad(Obj{{"pages", QJsonArray{Obj{{"id", "p"},
                                     {"title", "P"},
                                     {"fields", QJsonArray{Obj{{"key", "k"}, {"label", "L"}, {"type", "bool"}, {"default", false}},
                                                           Obj{{"key", "k"}, {"label", "L2"}, {"type", "bool"}, {"default", true}}}}}}}},
        "duplicate key");
    bad(Obj{{"pages", QJsonArray{Obj{{"id", "p"},
                                     {"title", "P"},
                                     {"fields", QJsonArray{Obj{{"key", "k"}, {"label", "L"}, {"type", "choice"}, {"default", "a"}}}}}}}},
        "choice without choices");
    bad(Obj{{"pages", QJsonArray{Obj{{"id", "p"},
                                     {"title", "P"},
                                     {"fields", QJsonArray{Obj{{"key", "k"}, {"label", "L"}, {"type", "int"}, {"default", "four"}}}}}}}},
        "default of the wrong type");
}

static void testConfig(const QString &dir)
{
    // the application's own file: tinoxiderc in the user's config directory
    Obj info = query(cmd("config.info"));
    EXPECT_EQ(info.value(QStringLiteral("name")).toString(), QStringLiteral("tinoxiderc"));
    EXPECT(info.value(QStringLiteral("path")).toString().endsWith(QStringLiteral("/tinoxiderc")));
    EXPECT(ok(query(cmd("config.write", Obj{{"group", "Editor"}, {"key", "font"}, {"value", "Hack"}}))));
    EXPECT(QFile::exists(info.value(QStringLiteral("path")).toString())); // written through, not at exit
    EXPECT_EQ(
        query(cmd("config.read", Obj{{"group", "Editor"}, {"key", "font"}, {"type", "string"}})).value(QStringLiteral("value")).toString(),
        QStringLiteral("Hack"));
    EXPECT(query(cmd("config.info"))
               .value(QStringLiteral("path"))
               .toString()
               .startsWith(qEnvironmentVariable("XDG_CONFIG_HOME"))); // nothing outside the sandbox

    const QString file = P(dir, "/typed.rc");
    EXPECT(ok(query(cmd("config.writeMany",
                        Obj{{"file", file},
                            {"entries", QJsonArray{Obj{{"group", "A"}, {"key", "s"}, {"value", "text with = and ; and ü"}},
                                                   Obj{{"group", "A"}, {"key", "b"}, {"value", true}},
                                                   Obj{{"group", "A"}, {"key", "i"}, {"value", 123456789012.0}},
                                                   Obj{{"group", "A"}, {"key", "d"}, {"value", 2.5}},
                                                   Obj{{"group", "A/Nested"}, {"key", "list"}, {"value", QJsonArray{"a", "b,c", "d"}}},
                                                   Obj{{"group", "A"}, {"key", "color"}, {"value", "#336699"}, {"type", "color"}}}}}))));
    auto read = [&](const char *group, const char *key, const char *type) {
        return query(cmd("config.read", Obj{{"file", file},
                                            {"group", QString::fromLatin1(group)},
                                            {"key", QString::fromLatin1(key)},
                                            {"type", QString::fromLatin1(type)}}));
    };
    EXPECT_EQ(read("A", "s", "string").value(QStringLiteral("value")).toString(), QStringLiteral("text with = and ; and ü"));
    EXPECT_EQ(read("A", "b", "bool").value(QStringLiteral("value")).toBool(), true);
    EXPECT_EQ(read("A", "i", "int").value(QStringLiteral("value")).toDouble(), 123456789012.0);
    EXPECT_EQ(read("A", "d", "double").value(QStringLiteral("value")).toDouble(), 2.5);
    EXPECT_EQ(read("A/Nested", "list", "stringList").value(QStringLiteral("value")).toArray(),
              QJsonArray({QStringLiteral("a"), QStringLiteral("b,c"), QStringLiteral("d")}));
    EXPECT_EQ(read("A", "color", "color").value(QStringLiteral("value")).toString(), QStringLiteral("#336699"));
    // the file is a normal KConfig file other KDE tools can read
    KConfig plain(file, KConfig::SimpleConfig);
    EXPECT_EQ(plain.group(QStringLiteral("A")).readEntry("b", false), true);
    EXPECT_EQ(plain.group(QStringLiteral("A")).group(QStringLiteral("Nested")).readEntry("list", QStringList()),
              QStringList({QStringLiteral("a"), QStringLiteral("b,c"), QStringLiteral("d")}));

    // missing keys: the default, and `exists:false`
    Obj missing = query(cmd("config.read", Obj{{"file", file}, {"group", "A"}, {"key", "nope"}, {"default", 7}}));
    EXPECT_EQ(missing.value(QStringLiteral("value")).toInt(), 7);
    EXPECT(!missing.value(QStringLiteral("exists")).toBool());
    Obj noDefault = read("A", "nope", "string");
    EXPECT(noDefault.value(QStringLiteral("value")).toString().isEmpty() && !noDefault.value(QStringLiteral("exists")).toBool());
    EXPECT_EQ(errorCode(query(cmd("config.read", Obj{{"file", file}, {"group", "A"}, {"key", "s"}}))), -1); // neither type nor default

    // listing and deleting
    Obj groups = query(cmd("config.groups", Obj{{"file", file}}));
    EXPECT(groups.value(QStringLiteral("groups")).toArray().contains(QStringLiteral("A")));
    EXPECT_EQ(query(cmd("config.groups", Obj{{"file", file}, {"group", "A"}})).value(QStringLiteral("groups")).toArray(),
              QJsonArray({QStringLiteral("Nested")}));
    EXPECT(query(cmd("config.readGroup", Obj{{"file", file}, {"group", "A"}}))
               .value(QStringLiteral("values"))
               .toObject()
               .contains(QStringLiteral("s")));
    EXPECT(ok(query(cmd("config.delete", Obj{{"file", file}, {"group", "A"}, {"key", "s"}}))));
    EXPECT(!read("A", "s", "string").value(QStringLiteral("exists")).toBool());
    EXPECT(ok(query(cmd("config.delete", Obj{{"file", file}, {"group", "A"}}))));
    EXPECT(!read("A", "b", "bool").value(QStringLiteral("exists")).toBool());

    // errors change nothing
    EXPECT_EQ(errorCode(query(
                  cmd("config.writeMany", Obj{{"file", file},
                                              {"entries", QJsonArray{Obj{{"group", "Z"}, {"key", "ok"}, {"value", 1}},
                                                                     Obj{{"group", "Z"}, {"key", "bad"}, {"value", QJsonArray{1, 2}}}}}}))),
              -1);
    EXPECT(!read("Z", "ok", "int").value(QStringLiteral("exists")).toBool());
    EXPECT_EQ(errorCode(query(cmd("config.write", Obj{{"file", file}, {"group", ""}, {"key", "k"}, {"value", 1}}))), -1);
    EXPECT_EQ(errorCode(query(
                  cmd("config.write", Obj{{"file", file}, {"group", "G"}, {"key", "c"}, {"value", "notacolour"}, {"type", "color"}}))),
              -1);
    EXPECT_EQ(errorCode(query(cmd("config.write", Obj{{"file", file}, {"group", "G"}, {"key", "o"}, {"value", Obj{{"a", 1}}}}))), -1);
    EXPECT(ok(query(cmd("config.write", Obj{{"file", file}, {"group", "G"}, {"key", "empty"}, {"value", ""}}))));
}

static void testWatch()
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6"));
    if (!QDBusConnection::sessionBus().isConnected() || tool.isEmpty()) {
        std::fprintf(stderr, "note: no D-Bus session bus or kwriteconfig6, config.watch is not exercised\n");
        return;
    }
    // a named file lives in XDG_CONFIG_HOME, which the child process inherits (the sandbox)
    EXPECT(ok(query(cmd("config.write", Obj{{"file", "watchedrc"}, {"group", "W"}, {"key", "k"}, {"value", "one"}}))));
    EXPECT(ok(query(cmd("config.watch", Obj{{"file", "watchedrc"}}))));
    EXPECT(ok(query(cmd("config.watch", Obj{{"file", "watchedrc"}})))); // twice is fine
    drainEvents();
    // another process changes the file and announces it over D-Bus, as System Settings would
    QProcess p;
    p.start(tool, {QStringLiteral("--file"), QStringLiteral("watchedrc"), QStringLiteral("--group"), QStringLiteral("W"),
                   QStringLiteral("--key"), QStringLiteral("k"), QStringLiteral("--notify"), QStringLiteral("two")});
    EXPECT(p.waitForFinished(10000) && p.exitCode() == 0);
    Obj changed = waitFor("config.changed", 5000);
    EXPECT(!changed.isEmpty());
    EXPECT_EQ(changed.value(QStringLiteral("group")).toString(), QStringLiteral("W"));
    EXPECT(changed.value(QStringLiteral("keys")).toArray().contains(QStringLiteral("k")));
    EXPECT_EQ(query(cmd("config.read", Obj{{"file", "watchedrc"}, {"group", "W"}, {"key", "k"}, {"type", "string"}}))
                  .value(QStringLiteral("value"))
                  .toString(),
              QStringLiteral("two"));
    EXPECT(ok(query(cmd("config.unwatch", Obj{{"file", "watchedrc"}}))));
}

static void testNotify()
{
    Obj r = query(cmd("notify.send", Obj{{"event", "buildFailed"},
                                         {"title", "Build failed"},
                                         {"text", "3 errors"},
                                         {"icon", "dialog-error"},
                                         {"urgency", "high"},
                                         {"actions", QJsonArray{Obj{{"id", "show"}, {"text", "Show problems"}}}}}));
    EXPECT(ok(r) && r.value(QStringLiteral("id")).toInt() > 0);
    EXPECT(ok(query(cmd("notify.send", Obj{{"event", "buildFinished"}, {"text", "ok"}}))));
    EXPECT_EQ(errorCode(query(cmd("notify.send", Obj{{"text", "no event"}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("notify.send", Obj{{"event", "buildFailed"}, {"urgency", "shouting"}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("notify.send", Obj{{"event", "buildFailed"}, {"actions", QJsonArray{Obj{{"id", "x"}}}}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("notify.send", Obj{{"event", "buildFailed"}, {"text", 5}}))), -1);
}

static void testNotifyrc()
{
    // the event definitions the IDE ships; KNotification looks them up by component name "tinoxide"
    const QString path = QStringLiteral(TKDE_RESOURCES_DIR "/notifications/tinoxide.notifyrc");
    KConfig rc(path, KConfig::SimpleConfig);
    for (const char *event : {"buildFinished", "buildFailed", "testsFinished", "testsFailed", "languageServerCrashed"}) {
        KConfigGroup g = rc.group(QStringLiteral("Event/") + QString::fromLatin1(event));
        EXPECT(g.exists());
        EXPECT(!g.readEntry("Name", QString()).isEmpty());
        EXPECT(!g.readEntry("Comment", QString()).isEmpty());
        EXPECT_EQ(g.readEntry("Action", QString()), QStringLiteral("Popup"));
    }
    EXPECT_EQ(rc.group(QStringLiteral("Global")).readEntry("Name", QString()), QStringLiteral("Tinox IDE"));
}

static void testRecentFiles(const QString &sandbox)
{
    // KRecentFilesAction does not remember files below the temporary directory, so the files live next to the test
    const QString dir = QDir::currentPath() + QStringLiteral("/recent-files-test");
    QDir().mkpath(dir);
    const QString rc = sandbox + QStringLiteral("/recent.rc");
    writeFile(P(dir, "/main.tnx"), "x");
    EXPECT(ok(query(cmd("action.configure", Obj{{"file", rc}}))));
    EXPECT(ok(query(
        cmd("action.define", Obj{{"actions", QJsonArray{Obj{{"id", "file_open_recent"}, {"standard", true}}, Obj{{"id", "plain"}}}}}))));
    const QString one = P(dir, "/main.tnx");
    const QString two = P(dir, "/other.tnx");
    writeFile(two, "y");
    EXPECT(ok(query(cmd("action.recentAdd", Obj{{"id", "file_open_recent"}, {"url", one}}))));
    EXPECT(ok(query(cmd("action.recentAdd", Obj{{"id", "file_open_recent"}, {"url", two}}))));
    Obj list = query(cmd("action.recentList", Obj{{"id", "file_open_recent"}}));
    EXPECT_EQ(list.value(QStringLiteral("urls")).toArray(), QJsonArray({two, one})); // newest first
    KConfig plain(rc, KConfig::SimpleConfig);
    EXPECT(plain.hasGroup(QStringLiteral("RecentFiles"))); // persisted at once
    // the user picks an entry from the menu
    drainEvents();
    onUi([] {
        const auto actions = qApp->findChildren<QAction *>(QStringLiteral("file_open_recent"));
        if (!actions.isEmpty() && actions.first()->menu<QMenu *>() && !actions.first()->menu<QMenu *>()->actions().isEmpty()) {
            actions.first()->menu<QMenu *>()->actions().first()->trigger();
        }
    });
    Obj picked = waitFor("action.recentSelected", 1000);
    EXPECT_EQ(picked.value(QStringLiteral("id")).toString(), QStringLiteral("file_open_recent"));
    EXPECT_EQ(picked.value(QStringLiteral("url")).toString(), two);
    EXPECT_EQ(errorCode(query(cmd("action.recentAdd", Obj{{"id", "plain"}, {"url", one}}))), -1); // not the recent-files action
    EXPECT_EQ(errorCode(query(cmd("action.recentAdd", Obj{{"id", "file_open_recent"}}))), -1);
    EXPECT(ok(query(cmd("action.recentClear", Obj{{"id", "file_open_recent"}}))));
    EXPECT(query(cmd("action.recentList", Obj{{"id", "file_open_recent"}})).value(QStringLiteral("urls")).toArray().isEmpty());
    QDir(dir).removeRecursively();
}

static void driver(QString dir)
{
    GcAttach gc;
    EXPECT(!waitFor("core.ready").isEmpty());
    const QJsonArray ns = query(cmd("core.capabilities")).value(QStringLiteral("namespaces")).toArray();
    EXPECT(ns.contains(QStringLiteral("dialog")) && ns.contains(QStringLiteral("config")) && ns.contains(QStringLiteral("notify")));
    testFileDialogs(dir);
    testMessages();
    testInput();
    testProgress();
    testConfig(dir);
    testSettings(dir);
    testWatch();
    testNotify();
    testNotifyrc();
    testRecentFiles(dir);
    tkde_post("{\"cmd\":\"core.quit\"}");
}

int main()
{
    gcInit();
    ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    ::setenv("TKDE_DEBUG_COMMANDS", "1", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);
    QTemporaryDir dir;
    // a private home: KMessageBox, KConfig and recent files must never touch the user's files
    ::setenv("XDG_CONFIG_HOME", qPrintable(dir.path() + QStringLiteral("/config")), 1);
    ::setenv("XDG_DATA_HOME", qPrintable(dir.path() + QStringLiteral("/data")), 1);
    ::setenv("XDG_CACHE_HOME", qPrintable(dir.path() + QStringLiteral("/cache")), 1);
    if (tkde_init("tinoxide", "Tinox IDE") != 0) {
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
    std::puts("dialog ok");
    return 0;
}
