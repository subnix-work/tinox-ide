// Spike for issue #7: the whole bridge architecture in one file.
//   - Qt's event loop runs on the main thread (tkde_exec)
//   - Tinox logic runs on another thread and talks through two queues
//   - the editor is KTextEditor (the Kate component)
// Replaced by the real bridge core (issue #8) once the architecture is confirmed.

#include "tinoxkde.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QQueue>
#include <QStatusBar>
#include <QToolBar>
#include <QWaitCondition>

#include <gc.h>

#include <cstring>
#include <ctime>
#include <fstream>
#include <string>

namespace
{
int g_argc = 1;
char g_arg0[] = "tinox-ide";
char *g_argv[] = {g_arg0, nullptr};

QApplication *g_app = nullptr;
QMainWindow *g_window = nullptr;
QPushButton *g_button = nullptr;
KTextEditor::Document *g_document = nullptr;

struct EventQueue {
    QMutex mutex;
    QWaitCondition ready;
    QQueue<QByteArray> items;
} g_events;

qint64 steadyNanos()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts); // the clock tinox_clock_nanos() uses
    return qint64(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void pushEvent(QJsonObject event)
{
    event.insert(QStringLiteral("t_ns"), steadyNanos());
    const QByteArray bytes = QJsonDocument(event).toJson(QJsonDocument::Compact);
    QMutexLocker lock(&g_events.mutex);
    g_events.items.enqueue(bytes);
    g_events.ready.wakeOne();
}

qint64 residentKilobytes()
{
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            return std::stoll(line.substr(6));
        }
    }
    return -1;
}

// Runs on the UI thread.
void handleCommand(const QByteArray &json)
{
    const QJsonObject cmd = QJsonDocument::fromJson(json).object();
    const QString name = cmd.value(QStringLiteral("cmd")).toString();
    if (name == QLatin1String("click")) {
        g_button->click();
    } else if (name == QLatin1String("setText")) {
        g_document->setText(cmd.value(QStringLiteral("text")).toString());
    } else if (name == QLatin1String("memory")) {
        pushEvent({{QStringLiteral("type"), QStringLiteral("memory")}, {QStringLiteral("rss_kb"), residentKilobytes()}});
    } else if (name == QLatin1String("platform")) {
        pushEvent({{QStringLiteral("type"), QStringLiteral("platform")}, {QStringLiteral("name"), QGuiApplication::platformName()}});
    } else if (name == QLatin1String("mode")) {
        pushEvent({{QStringLiteral("type"), QStringLiteral("mode")}, {QStringLiteral("name"), g_document->highlightingMode()}});
    } else if (name == QLatin1String("quit")) {
        g_app->quit();
    } else {
        pushEvent({{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("message"), QString(QStringLiteral("unknown command: ") + name)}});
    }
}

const char *gcCopy(const QByteArray &bytes)
{
    char *out = static_cast<char *>(GC_malloc_atomic(size_t(bytes.size()) + 1));
    if (!out) {
        return "";
    }
    std::memcpy(out, bytes.constData(), size_t(bytes.size()));
    out[bytes.size()] = '\0';
    return out;
}
} // namespace

extern "C" {

const char *tkde_getenv(const char *name)
{
    return gcCopy(qgetenv(name));
}

int64_t tkde_now_ns(void)
{
    return steadyNanos();
}

int64_t tkde_init(void)
{
    if (g_app) {
        return 0;
    }
    g_app = new QApplication(g_argc, g_argv);
    QApplication::setApplicationName(QStringLiteral("tinox-ide"));

    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    g_document = editor->createDocument(g_app);
    g_document->setHighlightingMode(QStringLiteral("Tinox"));
    KTextEditor::View *view = g_document->createView(nullptr);

    g_window = new QMainWindow;
    g_window->setWindowTitle(QStringLiteral("Tinox IDE (spike)"));
    g_window->setCentralWidget(view);
    g_button = new QPushButton(QStringLiteral("Click me"));
    QToolBar *bar = g_window->addToolBar(QStringLiteral("main"));
    bar->addWidget(g_button);
    g_window->statusBar()->showMessage(QStringLiteral("ready"));
    g_window->resize(900, 600);

    QObject::connect(g_button, &QPushButton::clicked, g_app, [] { pushEvent({{QStringLiteral("type"), QStringLiteral("clicked")}}); });
    QObject::connect(g_document, &KTextEditor::Document::textChanged, g_app, [](KTextEditor::Document *doc) {
        pushEvent({{QStringLiteral("type"), QStringLiteral("textChanged")}, {QStringLiteral("chars"), doc->totalCharacters()}});
    });
    return 0;
}

int64_t tkde_exec(void)
{
    if (!g_app) {
        return -1;
    }
    g_window->show();
    // "ready" is queued after the first pass of the event loop: the window is shown by then
    QMetaObject::invokeMethod(g_app, [] { pushEvent({{QStringLiteral("type"), QStringLiteral("ready")}}); }, Qt::QueuedConnection);
    return g_app->exec();
}

void tkde_post(const char *json)
{
    if (!g_app || !json) {
        return;
    }
    QMetaObject::invokeMethod(g_app, [bytes = QByteArray(json)] { handleCommand(bytes); }, Qt::QueuedConnection);
}

const char *tkde_next_event(int64_t timeout_ms)
{
    QMutexLocker lock(&g_events.mutex);
    if (g_events.items.isEmpty()) {
        g_events.ready.wait(&g_events.mutex, timeout_ms < 0 ? QDeadlineTimer(QDeadlineTimer::Forever) : QDeadlineTimer(timeout_ms));
    }
    if (g_events.items.isEmpty()) {
        return "";
    }
    return gcCopy(g_events.items.dequeue());
}

void tkde_quit(void)
{
    if (g_app) {
        QMetaObject::invokeMethod(g_app, [] { g_app->quit(); }, Qt::QueuedConnection);
    }
}

} // extern "C"
