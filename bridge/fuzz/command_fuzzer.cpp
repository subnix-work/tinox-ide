// libFuzzer target: feeds arbitrary bytes to the command parser and every registered command.
//
// The real bridge runs offscreen in this process. There is no event loop thread: each input is
// posted with tkde_post (the same entry point Tinox uses) and the queued command is executed by
// pumping the Qt event loop on the main thread. The events the command produced are drained so
// the queue never fills up.
//
// Inputs reach file system paths (editor.saveDocument, config.write, dialog.*): run it in a
// disposable environment. The harness points HOME and the XDG directories into a temporary
// directory and changes into it, but a hostile input can still name an absolute path.
#include "tinoxkde.h"

#include "../tests/gc_thread.h"

#include <QApplication>
#include <QPointer>
#include <QWidget>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
QWidget *g_keeper = nullptr; // one window that is always visible: closing others never means "last window closed"
uint64_t g_runs = 0;
int64_t g_nextRelease = 1;
} // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    char dir[] = "/tmp/tkde-fuzz-XXXXXX";
    if (!mkdtemp(dir)) {
        return 1;
    }
    const std::string root(dir);
    ::setenv("HOME", root.c_str(), 1);
    ::setenv("XDG_CONFIG_HOME", (root + "/config").c_str(), 1);
    ::setenv("XDG_DATA_HOME", (root + "/data").c_str(), 1);
    ::setenv("XDG_CACHE_HOME", (root + "/cache").c_str(), 1);
    ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
    ::setenv("QT_QPA_PLATFORM", "offscreen", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", "disabled:", 1); // no notifications or config broadcasts out of the fuzzer
    if (chdir(root.c_str()) != 0) {
        return 1;
    }
    gcInit();
    if (tkde_init("fuzz", "Fuzz") != TKDE_OK) {
        return 1;
    }
    // libFuzzer ends with exit(), which would destroy whatever the inputs left behind after the
    // frameworks' static destructors have run. That is a property of ending a process without a
    // normal tkde_exec (which deletes its objects first: Bridge::destroyAllObjects), not something
    // a command can cause, so the harness leaves the process instead.
    std::atexit([] { _exit(0); });
    g_keeper = new QWidget;
    g_keeper->setAttribute(Qt::WA_QuitOnClose, false);
    g_keeper->show();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 1 << 16) {
        return 0;
    }
    // the ABI takes a C string: an embedded NUL just ends it, as it would for any caller
    const std::string input(reinterpret_cast<const char *>(data), size);
    tkde_post(input.c_str());
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    while (tkde_next_event(0)[0] != '\0') {
    }
    // dialogs and windows the input opened go; the registry only holds QPointers
    // (a dialog takes its own pop-ups with it: look at them through guarded pointers)
    QList<QPointer<QWidget>> windows;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        windows.append(w);
    }
    for (const QPointer<QWidget> &w : windows) {
        if (w && w != g_keeper) {
            delete w.data();
        }
    }
    if (++g_runs % 256 == 0) {
        for (int i = 0; i < 512; ++i) {
            tkde_release(g_nextRelease++);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        while (tkde_next_event(0)[0] != '\0') {
        }
    }
    return 0;
}
