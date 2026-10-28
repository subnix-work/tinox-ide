#include "tinoxkde.h"

#include "driver.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <thread>

static void driver(const QString &directory, bool withoutKonsole)
{
    GcAttach gc;
    EXPECT(!waitFor("core.ready").isEmpty());
    const auto baseline = query(cmd("core.debug.objects"))["count"].toInteger();
    EXPECT(query(cmd("core.capabilities"))["namespaces"].toArray().contains("terminal"));
    EXPECT_EQ(errorCode(query(cmd("terminal.create", {{"directory", "/no-such-tinox-directory"}}))), int(TKDE_E_INVALID_ARG));
    EXPECT_EQ(errorCode(query(cmd("terminal.create", {{"directory", directory}, {"backend", "bad"}}))), int(TKDE_E_INVALID_ARG));
    const Obj created = query(cmd("terminal.create", {{"directory", directory}, {"backend", "console"}}));
    const auto id = idOf(created);
    EXPECT(id > 0);
    EXPECT_EQ(created["backend"].toString(), QString("console"));
    auto call = [id](const char *name, Obj fields = {}) {
        fields.insert("target", double(id));
        return query(cmd(name, fields));
    };
    // Real dock embedding and destruction use the same path as the IDE.
    const QString tree =
        QString(
            R"({"type":"QMainWindow","key":"main","children":[{"type":"QDockWidget","key":"dock","props":{"title":"Terminal"},"children":[{"type":"Embed","key":"terminal","object":%1}]}]})")
            .arg(id);
    const Obj mounted = query(cmd("widget.mount", {{"tree", QJsonDocument::fromJson(tree.toUtf8()).object()}}));
    EXPECT(mounted["ok"].toBool());
    EXPECT(call("terminal.sendText", {{"text", "plain \033[3"}})["ok"].toBool());
    EXPECT(call("terminal.sendText", {{"text", "1mred\033[0m normal\n"}})["ok"].toBool());
    const Obj dump = call("terminal.debug.dump");
    EXPECT_EQ(dump["text"].toString(), QString("plain red normal\n"));
    EXPECT(dump["readOnly"].toBool());
    bool red = false;
    for (const auto &run : dump["runs"].toArray()) {
        if (run.toObject()["text"] == "red") {
            red = run.toObject()["color"] == "#aa0000";
        }
    }
    EXPECT(red);
    EXPECT(call("terminal.setTitle", {{"title", "Build output"}})["ok"].toBool());
    EXPECT_EQ(call("terminal.info")["title"].toString(), QString("Build output"));
    EXPECT_EQ(errorCode(call("terminal.setDirectory", {{"directory", "relative"}})), int(TKDE_E_INVALID_ARG));
    const QString controlPath = directory + QStringLiteral("/control") + QChar(3);
    EXPECT(QDir().mkpath(controlPath));
    EXPECT_EQ(errorCode(call("terminal.setDirectory", {{"directory", controlPath}})), int(TKDE_E_INVALID_ARG));
    EXPECT_EQ(errorCode(query(cmd("terminal.create", {{"directory", controlPath}}))), int(TKDE_E_INVALID_ARG));
    EXPECT_EQ(call("terminal.info")["directory"].toString(), directory);
    EXPECT(call("terminal.setDirectory", {{"directory", "/"}})["ok"].toBool());
    EXPECT_EQ(call("terminal.info")["directory"].toString(), QString("/"));
    EXPECT_EQ(errorCode(call("terminal.sendText", {{"text", 7}})), int(TKDE_E_INVALID_ARG));
    EXPECT_EQ(errorCode(query(cmd("terminal.info", {{"target", 1.5}}))), int(TKDE_E_INVALID_ARG));
    tkde_release(idOf(mounted));
    tkde_release(id);
    EXPECT_EQ(errorCode(call("terminal.info")), int(TKDE_E_NOT_FOUND));

    // Auto mode exercises plugin discovery; on machines without Konsole it must still succeed.
    const Obj automatic = query(cmd("terminal.create", {{"directory", directory}}));
    std::fprintf(stderr, "terminal auto backend: %s (%s)\n", qPrintable(automatic["backend"].toString()),
                 qPrintable(automatic["reason"].toString()));
    const auto shell = idOf(automatic);
    EXPECT(shell > 0);
    if (withoutKonsole) {
        EXPECT_EQ(automatic["backend"].toString(), QString("console"));
    }
    if (automatic["backend"] == "konsole") {
        const QString project = directory + QStringLiteral("/project 'quoted' $name");
        EXPECT(QDir().mkpath(project));
        EXPECT(query(cmd("terminal.setDirectory", {{"target", double(shell)}, {"directory", project}}))["ok"].toBool());
        const QString output = directory + QStringLiteral("/pwd.txt");
        const QString input = QStringLiteral("pwd > '") + output + QStringLiteral("'; exit\n");
        query(cmd("terminal.sendText", {{"target", double(shell)}, {"text", input}}));
        const Obj finished = waitFor("terminal.finished");
        EXPECT_EQ(finished["target"].toInteger(), shell);
        QFile outputFile(output);
        EXPECT(outputFile.open(QIODevice::ReadOnly));
        EXPECT_EQ(outputFile.readAll(), (project + QLatin1Char('\n')).toUtf8());
        EXPECT(query(cmd("terminal.info", {{"target", double(shell)}}))["finished"].toBool());
        EXPECT_EQ(errorCode(query(cmd("terminal.sendText", {{"target", double(shell)}, {"text", "echo late\n"}}))), int(TKDE_E_FAILED));
    } else {
        EXPECT_EQ(automatic["backend"].toString(), QString("console"));
    }
    tkde_release(shell);
    drainEvents();
    EXPECT_EQ(query(cmd("core.debug.objects"))["count"].toInteger(), baseline);
    tkde_post("{\"cmd\":\"core.quit\"}");
}

int main(int argc, char **)
{
    gcInit();
    ::setenv("QT_QPA_PLATFORM", "offscreen", 1);
    ::setenv("TKDE_DEBUG_COMMANDS", "1", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);
    QTemporaryDir directory;
    ::setenv("XDG_CONFIG_HOME", qPrintable(directory.path()), 1);
    ::setenv("XDG_DATA_HOME", qPrintable(directory.path()), 1);
    if (tkde_init("terminal-test", "Terminal Test") != 0) {
        return 1;
    }
    const bool withoutKonsole = argc > 1;
    if (withoutKonsole) {
        QCoreApplication::setLibraryPaths({directory.path()});
    }
    std::thread logic(driver, directory.path(), withoutKonsole);
    tkde_exec();
    logic.join();
    return g_failures ? 1 : 0;
}
