// Tests the translation of the strings Tinox hands to the bridge (docs/i18n.md), with a small German catalog
// compiled by msgfmt into a private share directory. Offscreen.
#include "tinoxkde.h"

#include "driver.h"

#include <QDir>
#include <QTemporaryDir>

#include <thread>

#ifndef TKDE_I18N_SHARE
#error "TKDE_I18N_SHARE must point at the share directory with locale/de/LC_MESSAGES/tinox-ide.mo"
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

static QString translate(const Obj &fields)
{
    Obj r = query(cmd("i18n.translate", fields));
    return r.value(QStringLiteral("ok")).toBool() ? r.value(QStringLiteral("text")).toString() : QStringLiteral("<error>");
}

static void testWidgets()
{
    Obj m =
        query(cmd("widget.mount",
                  Obj{{"tree", tree("QWidget", "root", {},
                                    QJsonArray{tree("QLabel", "label", Obj{{"text", "Zzz Label"}, {"toolTip", "Zzz Tip"}}),
                                               // what the user types or a file name is data: it is never looked up
                                               tree("QLineEdit", "input", Obj{{"text", "Zzz Label"}, {"placeholder", "Zzz Placeholder"}}),
                                               tree("QPushButton", "file", Obj{{"text", "Main.tnx"}}),
                                               tree("QComboBox", "choice", Obj{{"items", QJsonArray{"Zzz Choice", "Other"}}}),
                                               tree("QPlainTextEdit", "log", Obj{{"text", "Zzz Label"}})})}}));
    EXPECT(m.value(QStringLiteral("ok")).toBool());
    const int64_t id = int64_t(m.value(QStringLiteral("id")).toDouble());
    Obj d = query(cmd("widget.debug.dump", Obj{{"target", double(id)}})).value(QStringLiteral("tree")).toObject();
    const QJsonArray kids = d.value(QStringLiteral("children")).toArray();
    EXPECT_EQ(kids.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Zzz Beschriftung"));
    EXPECT_EQ(kids.at(1).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Zzz Label"));
    EXPECT_EQ(kids.at(1).toObject().value(QStringLiteral("placeholder")).toString(), QStringLiteral("Zzz Platzhalter"));
    EXPECT_EQ(kids.at(2).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Main.tnx"));
    EXPECT_EQ(kids.at(3).toObject().value(QStringLiteral("items")).toArray().at(0).toString(), QStringLiteral("Zzz Auswahl"));
    EXPECT_EQ(kids.at(3).toObject().value(QStringLiteral("items")).toArray().at(1).toString(), QStringLiteral("Other"));
    EXPECT_EQ(kids.at(4).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Zzz Label"));
    tkde_release(id);
}

static void testActionsAndMenus()
{
    Obj r =
        query(cmd("action.define", Obj{{"actions", QJsonArray{Obj{{"id", "zzz.act"}, {"text", "Zzz &Action"}, {"toolTip", "Zzz Tip"}}}}}));
    EXPECT(r.value(QStringLiteral("ok")).toBool());
    bool found = false;
    for (const QJsonValue &a : query(cmd("action.list")).value(QStringLiteral("actions")).toArray()) {
        if (a.toObject().value(QStringLiteral("id")).toString() == QLatin1String("zzz.act")) {
            found = true;
            EXPECT_EQ(a.toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Zzz &Aktion"));
            EXPECT_EQ(a.toObject().value(QStringLiteral("label")).toString(), QStringLiteral("Zzz Aktion"));
        }
    }
    EXPECT(found);
}

static void testTabs()
{
    Obj m = query(
        cmd("widget.mount", Obj{{"tree", tree("QTabWidget", "tabs", {},
                                              QJsonArray{tree("QWidget", "page", {}, {}, Obj{{"tab", Obj{{"title", "Zzz Tab"}}}})})}}));
    EXPECT(m.value(QStringLiteral("ok")).toBool());
    Obj d = query(cmd("widget.debug.dump", Obj{{"target", m.value(QStringLiteral("id"))}})).value(QStringLiteral("tree")).toObject();
    EXPECT(QJsonDocument(d).toJson().contains("Zzz Reiter"));
    tkde_release(int64_t(m.value(QStringLiteral("id")).toDouble()));
}

static void testTranslateCommand()
{
    EXPECT_EQ(translate(Obj{{"text", "Zzz Same"}}), QStringLiteral("Zzz Gleich"));
    EXPECT_EQ(translate(Obj{{"text", "Zzz Same"}, {"context", "menu"}}), QStringLiteral("Zzz Gleich (Menü)"));
    // arguments fill %1, %2 in the order the translation puts them
    EXPECT_EQ(translate(Obj{{"text", "Zzz saved %1 of %2"}, {"args", QJsonArray{"3", "main.tnx"}}}),
              QStringLiteral("Zzz main.tnx: 3 gespeichert"));
    // plurals follow the language's rule; %1 is the count
    EXPECT_EQ(translate(Obj{{"text", "Zzz %1 file"}, {"plural", "Zzz %1 files"}, {"n", 1}}), QStringLiteral("Zzz 1 Datei"));
    EXPECT_EQ(translate(Obj{{"text", "Zzz %1 file"}, {"plural", "Zzz %1 files"}, {"n", 5}}), QStringLiteral("Zzz 5 Dateien"));
    // no entry: the English text, with the arguments filled in
    EXPECT_EQ(translate(Obj{{"text", "No entry for %1"}, {"args", QJsonArray{"x"}}}), QStringLiteral("No entry for x"));
    // bad requests are rejected, not guessed at
    EXPECT_EQ(translate(Obj{{"text", ""}}), QStringLiteral("<error>"));
    EXPECT_EQ(translate(Obj{{"text", "a"}, {"plural", "b"}}), QStringLiteral("<error>"));
}

static void driver()
{
    GcAttach gc;
    EXPECT(!waitFor("core.ready").isEmpty());
    EXPECT(query(cmd("core.capabilities")).value(QStringLiteral("namespaces")).toArray().contains(QStringLiteral("i18n")));
    testWidgets();
    testActionsAndMenus();
    testTabs();
    testTranslateCommand();
    tkde_post("{\"cmd\":\"core.quit\"}");
}

int main()
{
    gcInit();
    ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    ::setenv("TKDE_DEBUG_COMMANDS", "1", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);
    // the catalog is found through the data directories, the language through LANGUAGE
    ::setenv("XDG_DATA_DIRS", TKDE_I18N_SHARE, 1);
    ::setenv("LANGUAGE", "de", 1);
    ::setenv("LC_ALL", "de_DE.UTF-8", 1);
    QTemporaryDir dir;
    ::setenv("XDG_CONFIG_HOME", qPrintable(dir.path()), 1);
    if (tkde_init("i18n-test", "i18n test") != 0) {
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
    std::puts("i18n ok");
    return 0;
}
