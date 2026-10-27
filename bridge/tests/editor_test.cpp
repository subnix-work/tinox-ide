// Tests the `editor` namespace of the real bridge through the C ABI (JSON commands/events).
// The main thread runs tkde_exec(); a driver thread plays the Tinox logic task. Offscreen.
#include "tinoxkde.h"

#include "gc_thread.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <type_traits>

static std::atomic<int> g_failures{0};
#define EXPECT(cond)                                                                                                                       \
    do {                                                                                                                                   \
        if (!(cond)) {                                                                                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                           \
            ++g_failures;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)
#define EXPECT_EQ(a, b)                                                                                                                    \
    do {                                                                                                                                   \
        if (!((a) == (b))) {                                                                                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s\n   got: %s\n  want: %s\n", __FILE__, __LINE__, #a, #b, show(a).c_str(),            \
                         show(b).c_str());                                                                                                 \
            ++g_failures;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

static std::string show(const QByteArray &b)
{
    return b.toStdString();
}
static std::string show(const QString &s)
{
    return s.toStdString();
}
static std::string show(const char *s)
{
    return s;
}
template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
static std::string show(T v)
{
    return std::to_string(v);
}
static std::string show(bool v)
{
    return v ? "true" : "false";
}
static std::string show(const QJsonValue &v)
{
    return QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact).toStdString();
}

using Obj = QJsonObject;

static Obj parse(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

static QString compact(const Obj &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

/// Runs a command synchronously and returns its reply.
static Obj query(Obj cmd)
{
    return parse(tkde_query(compact(cmd).toUtf8().constData()));
}

static Obj cmd(const char *name, Obj fields = {})
{
    fields.insert(QStringLiteral("cmd"), QString::fromUtf8(name));
    return fields;
}

static int64_t idOf(const Obj &reply)
{
    return int64_t(reply.value(QStringLiteral("id")).toDouble());
}

static Obj pos(int line, int character)
{
    return Obj{{"line", line}, {"character", character}};
}
static Obj range(int l1, int c1, int l2, int c2)
{
    return Obj{{"start", pos(l1, c1)}, {"end", pos(l2, c2)}};
}

/// The next event of `type` (others are skipped), or {} after the timeout.
static Obj waitFor(const char *type, int timeoutMs = 5000)
{
    for (;;) {
        std::string e = tkde_next_event(timeoutMs);
        if (e.empty()) {
            return Obj();
        }
        Obj o = parse(e.c_str());
        if (o.value(QStringLiteral("type")).toString() == QLatin1String(type)) {
            return o;
        }
    }
}

static void drainEvents()
{
    while (!std::string(tkde_next_event(0)).empty()) {
    }
}

static int64_t newDoc(const QString &text)
{
    Obj r = query(cmd("editor.createDocument", Obj{{"text", text}}));
    EXPECT(r.value(QStringLiteral("ok")).toBool());
    return idOf(r);
}

static int64_t newView(int64_t doc)
{
    Obj r = query(cmd("editor.createView", Obj{{"document", double(doc)}}));
    EXPECT(r.value(QStringLiteral("ok")).toBool());
    return idOf(r);
}

static QString textOf(int64_t doc)
{
    return query(cmd("editor.getText", Obj{{"target", double(doc)}})).value(QStringLiteral("text")).toString();
}

static int errorCode(const Obj &reply)
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt();
}

static Obj infoOf(int64_t doc)
{
    return query(cmd("editor.info", Obj{{"target", double(doc)}}));
}

static void testTextEditsAndUndo()
{
    int64_t doc = newDoc(QStringLiteral("héllo\nworld"));
    int64_t view = newView(doc);
    EXPECT_EQ(textOf(doc), QStringLiteral("héllo\nworld"));
    Obj info = infoOf(doc);
    EXPECT_EQ(info.value(QStringLiteral("lines")).toInt(), 2);
    EXPECT_EQ(info.value(QStringLiteral("version")).toInt(), 0);
    EXPECT(!info.value(QStringLiteral("modified")).toBool());
    drainEvents();

    // one atomic multi-edit: ranges refer to the ORIGINAL text, applied as one undo step
    Obj r = query(cmd("editor.edit", Obj{{"target", double(doc)},
                                         {"edits", QJsonArray{Obj{{"range", range(0, 0, 0, 1)}, {"newText", "H"}},
                                                              Obj{{"range", range(1, 0, 1, 5)}, {"newText", "WORLD"}},
                                                              Obj{{"range", range(1, 5, 1, 5)}, {"newText", "!"}}}}}));
    EXPECT(r.value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(textOf(doc), QStringLiteral("Héllo\nWORLD!"));
    Obj changed = waitFor("editor.changed");
    EXPECT_EQ(changed.value(QStringLiteral("origin")).toString(), QStringLiteral("command"));
    EXPECT_EQ(changed.value(QStringLiteral("version")).toInt(), 1); // ONE notification for the whole transaction
    EXPECT(changed.value(QStringLiteral("changes")).toArray().size() >= 3);
    EXPECT_EQ(int64_t(changed.value(QStringLiteral("target")).toDouble()), doc);

    // undo takes ALL of it back at once
    EXPECT(query(cmd("editor.undo", Obj{{"target", double(view)}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(textOf(doc), QStringLiteral("héllo\nworld"));
    EXPECT(query(cmd("editor.redo", Obj{{"target", double(view)}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(textOf(doc), QStringLiteral("Héllo\nWORLD!"));

    // a bad edit list changes nothing: overlap, bad range, bad character, missing newText
    const QString before = textOf(doc);
    EXPECT_EQ(errorCode(query(cmd("editor.edit", Obj{{"target", double(doc)},
                                                     {"edits", QJsonArray{Obj{{"range", range(0, 0, 0, 3)}, {"newText", "x"}},
                                                                          Obj{{"range", range(0, 2, 0, 4)}, {"newText", "y"}}}}}))),
              -1);
    EXPECT_EQ(errorCode(query(cmd("editor.edit", Obj{{"target", double(doc)},
                                                     {"edits", QJsonArray{Obj{{"range", range(0, 0, 0, 1)}, {"newText", "ok"}},
                                                                          Obj{{"range", range(9, 0, 9, 1)}, {"newText", "bad"}}}}}))),
              -1);
    EXPECT_EQ(errorCode(query(cmd("editor.edit", Obj{{"target", double(doc)},
                                                     {"edits", QJsonArray{Obj{{"range", range(0, 0, 0, 99)}, {"newText", "x"}}}}}))),
              -1);
    EXPECT_EQ(errorCode(query(cmd("editor.edit", Obj{{"target", double(doc)}, {"edits", QJsonArray{Obj{{"range", range(0, 0, 0, 1)}}}}}))),
              -1);
    EXPECT_EQ(errorCode(query(cmd("editor.edit", Obj{{"target", double(doc)}}))), -1);
    EXPECT_EQ(textOf(doc), before);

    // ranges of getText, setText as one step
    EXPECT_EQ(
        query(cmd("editor.getText", Obj{{"target", double(doc)}, {"range", range(0, 1, 1, 2)}})).value(QStringLiteral("text")).toString(),
        QStringLiteral("éllo\nWO"));
    EXPECT(query(cmd("editor.setText", Obj{{"target", double(doc)}, {"text", "replaced"}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(textOf(doc), QStringLiteral("replaced"));
    EXPECT(query(cmd("editor.undo", Obj{{"target", double(view)}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(textOf(doc).toUtf8().toHex(), QStringLiteral("H\u00e9llo\nWORLD!").toUtf8().toHex());

    // wrong target kinds and unknown ids
    EXPECT_EQ(errorCode(query(cmd("editor.getText", Obj{{"target", double(view)}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("editor.getCursor", Obj{{"target", double(doc)}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("editor.getText", Obj{{"target", 999999.0}}))), -5);
    EXPECT_EQ(errorCode(query(cmd("editor.getText"))), -1);
    EXPECT(tkde_release(doc) == TKDE_OK);
    EXPECT_EQ(errorCode(query(cmd("editor.getText", Obj{{"target", double(doc)}}))), -5);
}

static void testUserTypingHasUtf16Ranges()
{
    // an emoji is TWO UTF-16 code units: the column after it is 2, not 1
    int64_t doc = newDoc(QStringLiteral("\U0001F600"));
    int64_t view = newView(doc);
    EXPECT(query(cmd("editor.setCursor", Obj{{"target", double(view)}, {"position", pos(0, 2)}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(query(cmd("editor.getCursor", Obj{{"target", double(view)}}))
                  .value(QStringLiteral("position"))
                  .toObject()
                  .value(QStringLiteral("character"))
                  .toInt(),
              2);
    drainEvents();
    EXPECT(query(cmd("editor.debug.type", Obj{{"target", double(view)}, {"text", "hé"}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(textOf(doc), QStringLiteral("\U0001F600hé"));
    Obj e1 = waitFor("editor.changed");
    EXPECT_EQ(e1.value(QStringLiteral("origin")).toString(), QStringLiteral("user"));
    Obj c1 = e1.value(QStringLiteral("changes")).toArray().at(0).toObject();
    EXPECT_EQ(
        c1.value(QStringLiteral("range")).toObject().value(QStringLiteral("start")).toObject().value(QStringLiteral("character")).toInt(),
        2);
    EXPECT_EQ(c1.value(QStringLiteral("text")).toString(), QStringLiteral("h"));
    Obj e2 = waitFor("editor.changed");
    EXPECT_EQ(e2.value(QStringLiteral("changes"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("range"))
                  .toObject()
                  .value(QStringLiteral("start"))
                  .toObject()
                  .value(QStringLiteral("character"))
                  .toInt(),
              3);
    EXPECT_EQ(e2.value(QStringLiteral("version")).toInt(), e1.value(QStringLiteral("version")).toInt() + 1);

    // Backspace removes a character: the change is a range with empty text; Enter inserts a newline
    EXPECT(query(cmd("editor.debug.type", Obj{{"target", double(view)}, {"text", "\b\n"}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(textOf(doc), QStringLiteral("\U0001F600h\n"));
    Obj removed = waitFor("editor.changed");
    Obj rc = removed.value(QStringLiteral("changes")).toArray().at(0).toObject();
    EXPECT_EQ(rc.value(QStringLiteral("text")).toString(), QString());
    EXPECT_EQ(
        rc.value(QStringLiteral("range")).toObject().value(QStringLiteral("end")).toObject().value(QStringLiteral("character")).toInt(), 4);

    // cursor and selection events
    drainEvents();
    query(cmd("editor.setCursor", Obj{{"target", double(view)}, {"position", pos(0, 0)}}));
    Obj moved = waitFor("editor.cursorMoved");
    EXPECT_EQ(moved.value(QStringLiteral("position")).toObject().value(QStringLiteral("line")).toInt(), 0);
    EXPECT_EQ(int64_t(moved.value(QStringLiteral("target")).toDouble()), view);
    EXPECT(query(cmd("editor.setSelection", Obj{{"target", double(view)}, {"range", range(0, 0, 0, 3)}}))
               .value(QStringLiteral("ok"))
               .toBool());
    Obj sel = query(cmd("editor.getSelection", Obj{{"target", double(view)}}));
    EXPECT_EQ(sel.value(QStringLiteral("text")).toString(), QStringLiteral("\U0001F600h"));
    EXPECT(waitFor("editor.selectionChanged").contains(QStringLiteral("range")));
    EXPECT(query(cmd("editor.setSelection", Obj{{"target", double(view)}, {"range", QJsonValue(QJsonValue::Null)}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT(query(cmd("editor.getSelection", Obj{{"target", double(view)}})).value(QStringLiteral("range")).isNull());
    EXPECT_EQ(errorCode(query(cmd("editor.setCursor", Obj{{"target", double(view)}, {"position", pos(5, 0)}}))), -1);
    tkde_release(doc);
}

static void testFilesEncodingAndEndOfLine(const QString &dir)
{
    // open: a latin-1 file, decoded with the requested encoding, written back byte for byte
    const QString path = dir + QStringLiteral("/latin1.txt");
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("caf\xe9\nl\xe4ngs\n");
    }
    Obj opened = query(cmd("editor.openDocument", Obj{{"url", path}, {"encoding", "ISO-8859-1"}}));
    EXPECT(opened.value(QStringLiteral("ok")).toBool());
    int64_t doc = idOf(opened);
    EXPECT_EQ(textOf(doc), QStringLiteral("café\nlängs\n"));
    EXPECT_EQ(infoOf(doc).value(QStringLiteral("url")).toString(), QUrl::fromLocalFile(path).toString());
    EXPECT_EQ(infoOf(doc).value(QStringLiteral("endOfLine")).toString(), QStringLiteral("lf"));
    drainEvents();

    EXPECT(
        query(cmd("editor.edit", Obj{{"target", double(doc)}, {"edits", QJsonArray{Obj{{"range", range(0, 4, 0, 4)}, {"newText", "!"}}}}}))
            .value(QStringLiteral("ok"))
            .toBool());
    EXPECT(waitFor("editor.modifiedChanged").value(QStringLiteral("modified")).toBool());
    EXPECT(infoOf(doc).value(QStringLiteral("modified")).toBool());
    EXPECT(query(cmd("editor.saveDocument", Obj{{"target", double(doc)}})).value(QStringLiteral("ok")).toBool());
    EXPECT(waitFor("editor.saved").contains(QStringLiteral("url")));
    {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        EXPECT_EQ(QString::fromLatin1(f.readAll()), QStringLiteral("café!\nlängs\n")); // still latin-1
    }
    EXPECT(!infoOf(doc).value(QStringLiteral("modified")).toBool());

    // line endings and "save as"
    EXPECT(query(cmd("editor.setEndOfLine", Obj{{"target", double(doc)}, {"eol", "crlf"}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(infoOf(doc).value(QStringLiteral("endOfLine")).toString(), QStringLiteral("crlf"));
    EXPECT(query(cmd("editor.setEncoding", Obj{{"target", double(doc)}, {"encoding", "UTF-8"}})).value(QStringLiteral("ok")).toBool());
    const QString out = dir + QStringLiteral("/crlf.txt");
    EXPECT(query(cmd("editor.saveDocument", Obj{{"target", double(doc)}, {"url", out}})).value(QStringLiteral("ok")).toBool());
    {
        QFile f(out);
        f.open(QIODevice::ReadOnly);
        EXPECT_EQ(f.readAll(), QByteArray("caf\xc3\xa9!\r\nl\xc3\xa4ngs\r\n"));
    }
    EXPECT_EQ(errorCode(query(cmd("editor.setEndOfLine", Obj{{"target", double(doc)}, {"eol", "weird"}}))), -1);
    EXPECT_EQ(errorCode(query(cmd("editor.setEncoding", Obj{{"target", double(doc)}, {"encoding", "no-such-encoding"}}))), -1);

    // errors
    EXPECT_EQ(errorCode(query(cmd("editor.openDocument", Obj{{"url", QString(dir + QStringLiteral("/missing.txt"))}}))), -5);
    EXPECT_EQ(errorCode(query(cmd("editor.openDocument"))), -1);
    int64_t untitled = newDoc(QStringLiteral("x"));
    EXPECT_EQ(errorCode(query(cmd("editor.saveDocument", Obj{{"target", double(untitled)}}))), -1); // would open a dialog: refused
    EXPECT(query(cmd("editor.closeDocument", Obj{{"target", double(doc)}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(errorCode(query(cmd("editor.info", Obj{{"target", double(doc)}}))), -5);
    tkde_release(untitled);
}

static void testExternalModification(const QString &dir)
{
    const QString path = dir + QStringLiteral("/watched.txt");
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("one\n");
    }
    int64_t doc = idOf(query(cmd("editor.openDocument", Obj{{"url", path}})));
    drainEvents();
    // the file watch needs a moment to be armed: rewrite until the editor notices
    Obj e;
    for (int attempt = 0; attempt < 8 && e.isEmpty(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(QByteArray("changed behind the editor's back ") + QByteArray::number(attempt) + "\n");
        f.close();
        e = waitFor("editor.externallyModified", 2000);
    }
    EXPECT(!e.isEmpty());
    EXPECT_EQ(e.value(QStringLiteral("reason")).toString(), QStringLiteral("modified"));
    EXPECT_EQ(int64_t(e.value(QStringLiteral("target")).toDouble()), doc);
    tkde_release(doc);
}

static void testHighlightingAndThemes()
{
    int64_t doc = newDoc(QStringLiteral("int main() { return 0; }"));
    int64_t view = newView(doc);
    Obj modes = query(cmd("editor.highlightingModes", Obj{{"target", double(doc)}}));
    EXPECT(modes.value(QStringLiteral("modes")).toArray().size() > 20);
    drainEvents();
    EXPECT(query(cmd("editor.setHighlightingMode", Obj{{"target", double(doc)}, {"mode", "C++"}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(infoOf(doc).value(QStringLiteral("highlightingMode")).toString(), QStringLiteral("C++"));
    EXPECT_EQ(waitFor("editor.highlightingModeChanged").value(QStringLiteral("mode")).toString(), QStringLiteral("C++"));
    EXPECT_EQ(errorCode(query(cmd("editor.setHighlightingMode", Obj{{"target", double(doc)}, {"mode", "No Such Language"}}))), -5);

    Obj themes = query(cmd("editor.themes", Obj{{"target", double(doc)}}));
    EXPECT(themes.value(QStringLiteral("themes")).toArray().size() >= 2);
    const QString first = themes.value(QStringLiteral("themes")).toArray().at(0).toString();
    EXPECT(query(cmd("editor.setTheme", Obj{{"target", double(view)}, {"theme", first}})).value(QStringLiteral("ok")).toBool());
    EXPECT(query(cmd("editor.setTheme", Obj{{"target", double(view)}, {"theme", ""}}))
               .value(QStringLiteral("ok"))
               .toBool()); // back to automatic
    EXPECT_EQ(errorCode(query(cmd("editor.setTheme", Obj{{"target", double(view)}, {"theme", "No Such Theme"}}))), -5);
    tkde_release(doc);
}

static double luminance(const QColor &c)
{
    auto f = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * f(c.redF()) + 0.7152 * f(c.greenF()) + 0.0722 * f(c.blueF());
}

static double contrast(const QColor &a, const QColor &b)
{
    double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

static void testDiagnostics()
{
    int64_t doc = newDoc(QStringLiteral("let a = 1\nlet b = 2\nlet c = 3\nlet d = 4"));
    newView(doc);
    Obj r = query(
        cmd("editor.setDiagnostics",
            Obj{{"target", double(doc)},
                {"owner", "lsp"},
                {"items",
                 QJsonArray{
                     Obj{{"range", range(1, 4, 1, 5)}, {"severity", 1}, {"message", "unknown <name>"}, {"code", "E1"}, {"source", "tinox"}},
                     Obj{{"range", range(3, 0, 3, 3)}, {"severity", 2}, {"message", "unused"}}}}}));
    EXPECT(r.value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(r.value(QStringLiteral("count")).toInt(), 2);
    Obj marks = query(cmd("editor.debug.marks", Obj{{"target", double(doc)}})).value(QStringLiteral("marks")).toObject();
    EXPECT_EQ(marks.size(), 2);
    EXPECT(marks.contains(QStringLiteral("1")) && marks.contains(QStringLiteral("3")));

    // several owners coexist; replacing one leaves the other
    EXPECT(query(cmd("editor.setDiagnostics",
                     Obj{{"target", double(doc)},
                         {"owner", "build"},
                         {"items", QJsonArray{Obj{{"range", range(0, 0, 0, 3)}, {"severity", 3}, {"message", "note"}}}}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT_EQ(query(cmd("editor.getDiagnostics", Obj{{"target", double(doc)}})).value(QStringLiteral("items")).toArray().size(), 3);
    EXPECT(query(cmd("editor.setDiagnostics", Obj{{"target", double(doc)}, {"owner", "build"}, {"items", QJsonArray{}}}))
               .value(QStringLiteral("ok"))
               .toBool());
    EXPECT_EQ(query(cmd("editor.getDiagnostics", Obj{{"target", double(doc)}})).value(QStringLiteral("items")).toArray().size(), 2);

    // hover text covers the diagnostic under the position, escaped
    QString hint =
        query(cmd("editor.debug.hint", Obj{{"target", double(doc)}, {"position", pos(1, 4)}})).value(QStringLiteral("text")).toString();
    EXPECT(hint.contains(QStringLiteral("error")) && hint.contains(QStringLiteral("unknown &lt;name&gt;")) &&
           hint.contains(QStringLiteral("tinox E1")));
    EXPECT(query(cmd("editor.debug.hint", Obj{{"target", double(doc)}, {"position", pos(2, 0)}}))
               .value(QStringLiteral("text"))
               .toString()
               .isEmpty());

    // diagnostics follow the text: inserting two lines at the top moves them down
    query(cmd("editor.edit",
              Obj{{"target", double(doc)}, {"edits", QJsonArray{Obj{{"range", range(0, 0, 0, 0)}, {"newText", "// one\n// two\n"}}}}}));
    Obj moved = query(cmd("editor.getDiagnostics", Obj{{"target", double(doc)}, {"owner", "lsp"}}));
    EXPECT_EQ(moved.value(QStringLiteral("items"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("range"))
                  .toObject()
                  .value(QStringLiteral("start"))
                  .toObject()
                  .value(QStringLiteral("line"))
                  .toInt(),
              3);

    // all or nothing
    EXPECT_EQ(
        errorCode(query(cmd("editor.setDiagnostics", Obj{{"target", double(doc)},
                                                         {"owner", "lsp"},
                                                         {"items", QJsonArray{Obj{{"range", range(0, 0, 0, 1)}, {"message", "ok"}},
                                                                              Obj{{"range", range(99, 0, 99, 1)}, {"message", "bad"}}}}}))),
        -1);
    EXPECT_EQ(errorCode(query(cmd("editor.setDiagnostics",
                                  Obj{{"target", double(doc)},
                                      {"owner", "lsp"},
                                      {"items", QJsonArray{Obj{{"range", range(0, 0, 0, 1)}, {"severity", 9}, {"message", "x"}}}}}))),
              -1);
    EXPECT_EQ(
        query(cmd("editor.getDiagnostics", Obj{{"target", double(doc)}, {"owner", "lsp"}})).value(QStringLiteral("items")).toArray().size(),
        2); // untouched

    // legible on a light and on a dark colour scheme
    query(cmd("editor.debug.setPalette", Obj{{"dark", false}}));
    QCoreApplication::processEvents(); // (no-op off the UI thread; the palette change itself runs on the UI thread)
    QColor lightBg(0xff, 0xff, 0xff), darkBg(0x1b, 0x1e, 0x20);
    auto errorColor = [&] {
        Obj items = query(cmd("editor.getDiagnostics", Obj{{"target", double(doc)}, {"owner", "lsp"}}));
        return QColor(items.value(QStringLiteral("items")).toArray().at(0).toObject().value(QStringLiteral("color")).toString());
    };
    // the palette event is delivered asynchronously: poll until the colour settles
    QColor light, dark;
    for (int i = 0; i < 100; ++i) {
        light = errorColor();
        if (contrast(light, lightBg) >= 3.0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT(contrast(light, lightBg) >= 3.0);
    query(cmd("editor.debug.setPalette", Obj{{"dark", true}}));
    for (int i = 0; i < 100; ++i) {
        dark = errorColor();
        if (contrast(dark, darkBg) >= 3.0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT(contrast(dark, darkBg) >= 3.0);
    EXPECT(light != dark);
    query(cmd("editor.debug.setPalette", Obj{{"dark", false}}));

    // releasing a document that still has diagnostics must not crash (ranges are the caller's to delete)
    EXPECT(tkde_release(doc) == TKDE_OK);
    query(cmd("core.ping"));
}

static void testCompletionAndHover()
{
    int64_t doc = newDoc(QStringLiteral("let value = Fs\nlet other = 1"));
    int64_t view = newView(doc);
    EXPECT_EQ(errorCode(query(cmd("editor.requestCompletion", Obj{{"target", double(view)}}))), -7); // not enabled yet
    EXPECT_EQ(errorCode(query(cmd("editor.setCompletions", Obj{{"target", double(view)}, {"req", 1}, {"items", QJsonArray{}}}))), -7);
    EXPECT(query(cmd("editor.enableCompletion", Obj{{"target", double(view)}, {"triggers", QJsonArray{"."}}, {"minWordLength", 2}}))
               .value(QStringLiteral("ok"))
               .toBool());
    query(cmd("editor.setCursor", Obj{{"target", double(view)}, {"position", pos(0, 14)}}));
    drainEvents();

    EXPECT(query(cmd("editor.requestCompletion", Obj{{"target", double(view)}})).value(QStringLiteral("ok")).toBool());
    Obj asked = waitFor("editor.completionRequested");
    EXPECT(!asked.isEmpty());
    EXPECT_EQ(asked.value(QStringLiteral("wordText")).toString(), QStringLiteral("Fs"));
    EXPECT_EQ(
        asked.value(QStringLiteral("word")).toObject().value(QStringLiteral("start")).toObject().value(QStringLiteral("character")).toInt(),
        12);
    const int req = asked.value(QStringLiteral("req")).toInt();

    // an answer to an older request is ignored, not an error
    Obj stale =
        query(cmd("editor.setCompletions", Obj{{"target", double(view)}, {"req", req - 1}, {"items", QJsonArray{Obj{{"label", "x"}}}}}));
    EXPECT(stale.value(QStringLiteral("ok")).toBool() && stale.value(QStringLiteral("stale")).toBool());
    EXPECT_EQ(query(cmd("editor.debug.completionState", Obj{{"target", double(view)}})).value(QStringLiteral("items")).toInt(), 0);

    Obj answer = query(cmd(
        "editor.setCompletions",
        Obj{{"target", double(view)},
            {"req", req},
            {"items",
             QJsonArray{Obj{{"label", "Fs"},
                            {"kind", "class"},
                            {"detail", "tinox.core.fs"},
                            {"insertText", "Fs::readFile"},
                            {"additionalTextEdits", QJsonArray{Obj{{"range", range(0, 0, 0, 0)}, {"newText", "import tinox.core.fs\n"}}}}},
                        Obj{{"label", "Fx"}}}}}));
    EXPECT(answer.value(QStringLiteral("ok")).toBool() && !answer.value(QStringLiteral("stale")).toBool());
    EXPECT_EQ(query(cmd("editor.debug.completionState", Obj{{"target", double(view)}})).value(QStringLiteral("items")).toInt(), 2);
    EXPECT_EQ(errorCode(query(cmd("editor.setCompletions",
                                  Obj{{"target", double(view)}, {"req", req}, {"items", QJsonArray{Obj{{"detail", "no label"}}}}}))),
              -1);

    // accepting item 0 replaces the word, applies the extra edit in the same step, and puts the cursor behind the text
    drainEvents();
    EXPECT(query(cmd("editor.debug.acceptCompletion", Obj{{"target", double(view)}, {"index", 0}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(textOf(doc), QStringLiteral("import tinox.core.fs\nlet value = Fs::readFile\nlet other = 1"));
    EXPECT_EQ(waitFor("editor.completionAccepted").value(QStringLiteral("label")).toString(), QStringLiteral("Fs"));
    Obj cursor = query(cmd("editor.getCursor", Obj{{"target", double(view)}})).value(QStringLiteral("position")).toObject();
    EXPECT_EQ(cursor.value(QStringLiteral("line")).toInt(), 1);
    EXPECT_EQ(cursor.value(QStringLiteral("character")).toInt(), int(QStringLiteral("let value = Fs::readFile").size()));
    EXPECT(query(cmd("editor.undo", Obj{{"target", double(view)}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(textOf(doc), QStringLiteral("let value = Fs\nlet other = 1")); // one undo step for both edits

    EXPECT(query(cmd("editor.disableCompletion", Obj{{"target", double(view)}})).value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(errorCode(query(cmd("editor.requestCompletion", Obj{{"target", double(view)}}))), -7);

    // tooltips need a visible position
    EXPECT_EQ(errorCode(query(cmd("editor.showTooltip", Obj{{"target", double(view)}, {"position", pos(0, 1)}, {"markup", "<b>hi</b>"}}))),
              -7);
    tkde_release(doc);
}

static void testLifecycleCleanup()
{
    // many documents and views created and released: nothing is left registered
    const int before = query(cmd("core.debug.objects")).value(QStringLiteral("count")).toInt();
    for (int i = 0; i < 200; ++i) {
        int64_t doc = newDoc(QStringLiteral("text %1").arg(i));
        int64_t view = newView(doc);
        query(cmd("editor.setDiagnostics",
                  Obj{{"target", double(doc)}, {"items", QJsonArray{Obj{{"range", range(0, 0, 0, 2)}, {"message", "m"}}}}}));
        query(cmd("editor.enableCompletion", Obj{{"target", double(view)}}));
        if (i % 2) {
            EXPECT(tkde_release(view) == TKDE_OK);
        }
        EXPECT(tkde_release(doc) == TKDE_OK);
    }
    query(cmd("core.ping"));
    // views of a released document go with it and stop resolving
    for (int i = 0; i < 50 && query(cmd("core.debug.objects")).value(QStringLiteral("count")).toInt() != before; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_EQ(query(cmd("core.debug.objects")).value(QStringLiteral("count")).toInt(), before);
}

static void driver(QString dir)
{
    GcAttach gc;
    EXPECT(!waitFor("core.ready").isEmpty());
    EXPECT(query(cmd("core.capabilities")).value(QStringLiteral("namespaces")).toArray().contains(QStringLiteral("editor")));
    testTextEditsAndUndo();
    testUserTypingHasUtf16Ranges();
    testFilesEncodingAndEndOfLine(dir);
    testExternalModification(dir);
    testHighlightingAndThemes();
    testDiagnostics();
    testCompletionAndHover();
    testLifecycleCleanup();
    tkde_post("{\"cmd\":\"core.quit\"}");
}

int main()
{
    gcInit();
    ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    ::setenv("TKDE_DEBUG_COMMANDS", "1", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);
    QTemporaryDir dir;
    if (tkde_init("editor-test", "Editor test") != 0) {
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
    std::puts("editor ok");
    return 0;
}
