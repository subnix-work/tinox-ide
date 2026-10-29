#include "bridge.h"

#include "tinoxkde.h"
#include "version.h"

#include "action/action.h"
#include "dialog/dialog.h"
#include "editor/editor.h"
#include "model/model.h"
#include "terminal/terminal.h"
#include "widget/widget.h"

#include "i18n.h"
#include <KAboutData>
#include <KCoreAddons>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>

#include <QApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QThread>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <sys/syscall.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcBridge, "org.subnix.tinoxide.bridge")

namespace
{
int g_argc = 1;
char g_arg0[] = "tinox-ide";
char *g_argv[] = {g_arg0, nullptr};

thread_local std::string t_lastError;

int64_t currentTid()
{
    return int64_t(::syscall(SYS_gettid));
}

// On Linux the main thread's tid equals the process id.
bool onProcessMainThread()
{
    return currentTid() == int64_t(::getpid());
}

QString jsonString(const QString &s)
{
    const QByteArray a = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact); // ["..."]
    return QString::fromUtf8(a.mid(1, a.size() - 2));
}

// The event envelope is written by hand so `seq` and `t_ns` are exact integers
// (a QJsonObject stores numbers as doubles).
std::string makeEvent(int64_t seq, const QString &type, QJsonObject fields, int64_t target)
{
    fields.remove(QStringLiteral("type"));
    fields.remove(QStringLiteral("seq"));
    fields.remove(QStringLiteral("t_ns"));
    fields.remove(QStringLiteral("target"));
    std::string out = "{\"type\":" + jsonString(type).toStdString() + ",\"seq\":" + std::to_string(seq) +
                      ",\"t_ns\":" + std::to_string(tkde::monotonicNanos());
    if (target > 0) {
        out += ",\"target\":" + std::to_string(target);
    }
    const QByteArray body = QJsonDocument(fields).toJson(QJsonDocument::Compact);
    if (body.size() > 2) {
        out += ',';
        out.append(body.constData() + 1, size_t(body.size() - 1));
    } else {
        out += '}';
    }
    return out;
}

QByteArray toJsonBytes(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
} // namespace

namespace tkde
{
int64_t setLastError(int64_t code, const QString &message)
{
    t_lastError = message.toStdString();
    return code;
}

const std::string &lastError()
{
    return t_lastError;
}

QJsonObject Reply::toJson() const
{
    QJsonObject o = fields;
    o.insert(QStringLiteral("ok"), ok);
    if (!ok) {
        QJsonObject err;
        err.insert(QStringLiteral("code"), double(code));
        err.insert(QStringLiteral("message"), message);
        o.insert(QStringLiteral("error"), err);
    }
    return o;
}

Bridge &Bridge::instance()
{
    // Never destroyed, like the QApplication it owns: Qt deletes pending objects (a document with
    // deleteLater) while the process exits, after function-local statics are gone, and their
    // handlers still emit events. Found by the command fuzzer.
    static Bridge *bridge = new Bridge;
    return *bridge;
}

Bridge::Bridge() = default;
Bridge::~Bridge() = default;

bool Bridge::isUiThread() const
{
    return m_inited && currentTid() == m_uiThreadId.load();
}

size_t Bridge::objectCount() const
{
    return m_objects.size();
}

// ------------------------------------------------------------------ lifecycle

void Bridge::setupKde(const QString &appName, const QString &displayName)
{
    KLocalizedString::setApplicationDomain("tinox-ide");
    KAboutData about(appName, displayName, QStringLiteral(TINOX_IDE_VERSION), i18n("Native IDE for the Tinox language"), KAboutLicense::MIT,
                     i18n("(c) 2026 Tino Gohl"));
    about.setDesktopFileName(QStringLiteral("org.subnix.tinoxide"));
    KAboutData::setApplicationData(about);
    m_app->setWindowIcon(QIcon::fromTheme(QStringLiteral("org.subnix.tinoxide")));
    if (!qEnvironmentVariableIsSet("TKDE_NO_KCRASH")) {
        KCrash::initialize();
    }
}

int64_t Bridge::init(const QString &appName, const QString &displayName)
{
    if (!onProcessMainThread()) {
        return setLastError(TKDE_E_WRONG_THREAD, QStringLiteral("tkde_init must be called on the process main thread"));
    }
    if (m_inited) {
        return setLastError(TKDE_E_ALREADY_INITIALIZED, QStringLiteral("tkde_init was already called"));
    }
    if (appName.isEmpty() || displayName.isEmpty()) {
        return setLastError(TKDE_E_INVALID_ARG, QStringLiteral("tkde_init needs an application and a display name"));
    }
    // Qt aborts the whole process when it cannot reach a display; report it instead.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && !qEnvironmentVariableIsSet("WAYLAND_DISPLAY") &&
        !qEnvironmentVariableIsSet("DISPLAY")) {
        return setLastError(
            TKDE_E_FAILED,
            QStringLiteral("no display available: set WAYLAND_DISPLAY or DISPLAY, or QT_QPA_PLATFORM=offscreen for headless runs"));
    }

    m_app = new QApplication(g_argc, g_argv); // lives for the whole process
    setupKde(appName, displayName);
    m_context = new QObject(m_app);
    m_debugCommands = qEnvironmentVariable("TKDE_DEBUG_COMMANDS") == QLatin1String("1");
    m_namespaces = {QStringLiteral("core")};
    registerCoreCommands();
    tkde::translation::registerCommands(*this);
    tkde::editor::registerCommands(*this);
    tkde::widget::registerCommands(*this);
    tkde::action::registerCommands(*this);
    tkde::model::registerCommands(*this);
    tkde::dialog::registerCommands(*this);
    tkde::config::registerCommands(*this);
    tkde::notify::registerCommands(*this);
    tkde::terminal::registerCommands(*this);

    QObject::connect(m_app, &QGuiApplication::lastWindowClosed, m_context, [this] { requestQuit(0); });

    m_uiThreadId = currentTid();
    m_inited = true;
    qCInfo(lcBridge) << "initialised" << appName << "on" << QGuiApplication::platformName();
    return TKDE_OK;
}

int64_t Bridge::exec()
{
    if (!m_inited) {
        return setLastError(TKDE_E_NOT_INITIALIZED, QStringLiteral("tkde_exec before tkde_init"));
    }
    if (!isUiThread()) {
        return setLastError(TKDE_E_WRONG_THREAD, QStringLiteral("tkde_exec must be called on the thread that called tkde_init"));
    }
    if (m_running || m_shutdown) {
        return setLastError(TKDE_E_FAILED, QStringLiteral("tkde_exec is already running or has finished"));
    }
    m_running = true;
    // queued, so it is delivered once the loop runs and the windows are shown
    runOnUi([this] { emitEvent(QStringLiteral("core.ready")); });
    const int rc = m_app->exec();
    emitEvent(QStringLiteral("core.shutdown"));
    m_shutdown = true;
    m_running = false;
    destroyAllObjects();
    m_service.reset();
    m_events.close();
    return rc;
}

void Bridge::destroyAllObjects()
{
    // While the libraries are still alive: an object left for process exit is deleted after
    // the static destructors of KDE Frameworks and Qt have run (found by the command fuzzer).
    std::vector<QPointer<QObject>> remaining;
    for (const auto &entry : m_objects) {
        remaining.push_back(entry.second);
    }
    for (const QPointer<QObject> &o : remaining) {
        if (o) {
            for (const auto &hook : m_releaseHooks) {
                hook(o);
            }
            delete o.data();
        }
    }
    m_objects.clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

void Bridge::requestQuit(int64_t code)
{
    bool expected = false;
    if (!m_running || !m_quitting.compare_exchange_strong(expected, true)) {
        return;
    }
    runOnUi([this, code] {
        QJsonObject f;
        f.insert(QStringLiteral("code"), double(code));
        emitEvent(QStringLiteral("core.quit"), f);
        m_app->exit(int(code));
    });
}

// ------------------------------------------------------------------ queues

void Bridge::runOnUi(std::function<void()> fn)
{
    QMetaObject::invokeMethod(m_context, std::move(fn), Qt::QueuedConnection);
}

bool Bridge::runOnUiAndWait(std::function<void()> fn)
{
    if (isUiThread()) {
        fn();
        return true;
    }
    struct Waiter {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
    };
    auto waiter = std::make_shared<Waiter>();
    runOnUi([fn = std::move(fn), waiter] {
        fn();
        std::lock_guard<std::mutex> lock(waiter->m);
        waiter->done = true;
        waiter->cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(waiter->m);
    while (!waiter->done) {
        waiter->cv.wait_for(lock, std::chrono::milliseconds(50));
        if (!waiter->done && m_shutdown) {
            return false; // the loop ended before it got to our call
        }
    }
    return true;
}

int64_t Bridge::post(const QByteArray &json)
{
    if (!m_inited || m_shutdown) {
        return setLastError(TKDE_E_NOT_INITIALIZED, QStringLiteral("tkde_post outside the bridge's lifetime"));
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value(QStringLiteral("cmd")).isString()) {
        return setLastError(TKDE_E_INVALID_ARG, QStringLiteral("a command is a JSON object with a string \"cmd\""));
    }
    const QJsonObject cmd = doc.object();
    runOnUi([this, cmd] {
        const Reply reply = dispatch(cmd);
        if (cmd.contains(QStringLiteral("req"))) {
            QJsonObject fields = reply.toJson();
            fields.insert(QStringLiteral("req"), cmd.value(QStringLiteral("req")));
            emitEvent(QStringLiteral("core.reply"), fields);
        } else if (!reply.ok) {
            QJsonObject fields;
            fields.insert(QStringLiteral("error"), reply.toJson().value(QStringLiteral("error")));
            fields.insert(QStringLiteral("cmd"), cmd.value(QStringLiteral("cmd")));
            emitEvent(QStringLiteral("core.error"), fields);
        }
    });
    return TKDE_OK;
}

QByteArray Bridge::query(const QByteArray &json)
{
    auto fail = [](int64_t code, const QString &message) {
        setLastError(code, message);
        return toJsonBytes(Reply::failure(code, message).toJson());
    };
    if (!m_inited || m_shutdown) {
        return fail(TKDE_E_NOT_INITIALIZED, QStringLiteral("tkde_query outside the bridge's lifetime"));
    }
    if (isUiThread()) {
        return fail(TKDE_E_WRONG_THREAD, QStringLiteral("tkde_query was called from the UI thread"));
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value(QStringLiteral("cmd")).isString()) {
        return fail(TKDE_E_INVALID_ARG, QStringLiteral("a command is a JSON object with a string \"cmd\""));
    }
    const QJsonObject cmd = doc.object();
    auto result = std::make_shared<QByteArray>();
    const bool answered = runOnUiAndWait([this, cmd, result] { *result = toJsonBytes(dispatch(cmd).toJson()); });
    if (!answered) {
        return fail(TKDE_E_NOT_INITIALIZED, QStringLiteral("the event loop ended before the query was answered"));
    }
    return *result;
}

bool Bridge::nextEvent(int64_t timeoutMs, std::string &out)
{
    return m_events.pop(timeoutMs, out);
}

void Bridge::emitEvent(const QString &type, const QJsonObject &fields, int64_t target)
{
    m_events.push([&](int64_t seq) { return makeEvent(seq, type, fields, target); });
}

// ------------------------------------------------------------------ dispatch and registry

void Bridge::registerCommand(const QString &name, Handler handler)
{
    m_handlers[name] = std::move(handler);
}

void Bridge::addNamespace(const QString &ns)
{
    if (std::find(m_namespaces.begin(), m_namespaces.end(), ns) == m_namespaces.end()) {
        m_namespaces.push_back(ns);
    }
}

Reply Bridge::dispatch(const QJsonObject &cmd)
{
    const QString name = cmd.value(QStringLiteral("cmd")).toString();
    const auto it = m_handlers.find(name);
    if (it == m_handlers.end()) {
        return Reply::failure(TKDE_E_UNSUPPORTED, QStringLiteral("unknown command: ") + name);
    }
    return it->second(cmd);
}

int64_t Bridge::registerObject(QObject *object)
{
    if (!object || !isUiThread()) {
        return 0;
    }
    const int64_t id = m_nextId++;
    m_objects[id] = object;
    QObject::connect(object, &QObject::destroyed, m_context, [this, id] { m_objects.erase(id); });
    return id;
}

QObject *Bridge::object(int64_t id) const
{
    const auto it = m_objects.find(id);
    return it == m_objects.end() ? nullptr : it->second.data();
}

void Bridge::addReleaseHook(std::function<void(QObject *)> hook)
{
    m_releaseHooks.push_back(std::move(hook));
}

int64_t Bridge::releaseOnUi(int64_t id)
{
    if (id <= 0) {
        return setLastError(TKDE_E_INVALID_ARG, QStringLiteral("object ids are positive"));
    }
    const auto it = m_objects.find(id);
    if (it == m_objects.end() || it->second.isNull()) {
        m_objects.erase(id);
        return setLastError(TKDE_E_NOT_FOUND, QStringLiteral("no object with id %1").arg(id));
    }
    QObject *o = it->second.data();
    m_objects.erase(it);
    for (const auto &hook : m_releaseHooks) {
        hook(o);
    }
    o->deleteLater();
    return TKDE_OK;
}

int64_t Bridge::release(int64_t id)
{
    if (!m_inited || m_shutdown) {
        return setLastError(TKDE_E_NOT_INITIALIZED, QStringLiteral("tkde_release outside the bridge's lifetime"));
    }
    auto result = std::make_shared<int64_t>(TKDE_E_FAILED);
    auto message = std::make_shared<std::string>();
    const bool done = runOnUiAndWait([this, id, result, message] {
        *result = releaseOnUi(id);
        *message = lastError(); // the UI thread's message, handed to the caller below
    });
    if (!done) {
        return setLastError(TKDE_E_NOT_INITIALIZED, QStringLiteral("the event loop ended before the call was processed"));
    }
    if (*result != TKDE_OK) {
        t_lastError = *message;
    }
    return *result;
}

// ------------------------------------------------------------------ core namespace

void Bridge::registerCoreCommands()
{
    registerCommand(QStringLiteral("core.ping"), [](const QJsonObject &cmd) {
        QJsonObject f;
        f.insert(QStringLiteral("payload"),
                 cmd.contains(QStringLiteral("payload")) ? cmd.value(QStringLiteral("payload")) : QJsonValue(QJsonValue::Null));
        return Reply::success(f);
    });

    registerCommand(QStringLiteral("core.capabilities"), [this](const QJsonObject &) {
        QJsonArray ns;
        for (const QString &n : m_namespaces) {
            ns.append(n);
        }
        QJsonObject f;
        f.insert(QStringLiteral("abi"), TKDE_ABI_VERSION);
        f.insert(QStringLiteral("namespaces"), ns);
        f.insert(QStringLiteral("qt"), QString::fromUtf8(qVersion()));
        f.insert(QStringLiteral("kf"), KCoreAddons::versionString());
        return Reply::success(f);
    });

    registerCommand(QStringLiteral("core.release"), [this](const QJsonObject &cmd) {
        const QJsonValue target = cmd.value(QStringLiteral("target"));
        if (!target.isDouble()) {
            return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("core.release needs an integer \"target\""));
        }
        const int64_t code = releaseOnUi(int64_t(target.toDouble()));
        return code == TKDE_OK ? Reply::success() : Reply::failure(code, QString::fromStdString(lastError()));
    });

    registerCommand(QStringLiteral("core.quit"), [this](const QJsonObject &cmd) {
        requestQuit(int64_t(cmd.value(QStringLiteral("code")).toDouble(0)));
        return Reply::success();
    });

    registerCommand(QStringLiteral("core.log"), [](const QJsonObject &cmd) {
        const QString level = cmd.value(QStringLiteral("level")).toString();
        const QString message = cmd.value(QStringLiteral("message")).toString();
        if (level == QLatin1String("debug")) {
            qCDebug(lcBridge).noquote() << message;
        } else if (level == QLatin1String("info")) {
            qCInfo(lcBridge).noquote() << message;
        } else if (level == QLatin1String("warning")) {
            qCWarning(lcBridge).noquote() << message;
        } else if (level == QLatin1String("error")) {
            qCCritical(lcBridge).noquote() << message;
        } else {
            return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("core.log level must be debug, info, warning or error"));
        }
        return Reply::success();
    });

    // Lets any thread put a message into the event stream, e.g. a background task reporting to the
    // logic task (tkde_post is thread safe and wakes tkde_next_event). Answers as `core.message`.
    registerCommand(QStringLiteral("core.message"), [this](const QJsonObject &cmd) {
        const QJsonValue topic = cmd.value(QStringLiteral("topic"));
        if (!topic.isString() || topic.toString().isEmpty()) {
            return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("core.message needs a non-empty string \"topic\""));
        }
        QJsonObject f;
        f.insert(QStringLiteral("topic"), topic);
        f.insert(QStringLiteral("payload"),
                 cmd.contains(QStringLiteral("payload")) ? cmd.value(QStringLiteral("payload")) : QJsonValue(QJsonValue::Null));
        emitEvent(QStringLiteral("core.message"), f);
        return Reply::success();
    });

    // One running IDE per user: a second start hands its arguments to the first.
    registerCommand(QStringLiteral("core.singleInstance"), [this](const QJsonObject &cmd) {
        if (m_running) {
            return Reply::failure(TKDE_E_FAILED, QStringLiteral("core.singleInstance must be sent before tkde_exec"));
        }
        if (!cmd.value(QStringLiteral("enabled")).toBool(true)) {
            m_service.reset();
            return Reply::success();
        }
        if (!m_service) {
            m_service = std::make_unique<KDBusService>(KDBusService::Unique | KDBusService::NoExitOnFailure, m_app);
            QObject::connect(m_service.get(), &KDBusService::activateRequested, m_context,
                             [this](const QStringList &arguments, const QString &workingDirectory) {
                                 QJsonObject f;
                                 f.insert(QStringLiteral("arguments"), QJsonArray::fromStringList(arguments));
                                 f.insert(QStringLiteral("workingDirectory"), workingDirectory);
                                 emitEvent(QStringLiteral("core.activated"), f);
                             });
        }
        QJsonObject f;
        f.insert(QStringLiteral("registered"), m_service->isRegistered());
        return Reply::success(f);
    });

    if (!m_debugCommands) {
        return;
    }
    // Test hooks, only with TKDE_DEBUG_COMMANDS=1; not part of core.capabilities.
    registerCommand(QStringLiteral("core.debug.emit"), [this](const QJsonObject &cmd) {
        const QString type = cmd.value(QStringLiteral("type")).toString(QStringLiteral("debug.event"));
        const int count = cmd.value(QStringLiteral("count")).toInt(1);
        for (int i = 0; i < count; ++i) {
            QJsonObject f;
            f.insert(QStringLiteral("n"), i);
            emitEvent(type, f);
        }
        return Reply::success();
    });
    registerCommand(QStringLiteral("core.debug.create"), [this](const QJsonObject &cmd) {
        const int count = cmd.value(QStringLiteral("count")).toInt(1);
        int64_t first = 0;
        for (int i = 0; i < count; ++i) {
            const int64_t id = registerObject(new QObject);
            if (i == 0) {
                first = id;
            }
        }
        QJsonObject f;
        f.insert(QStringLiteral("first"), double(first));
        f.insert(QStringLiteral("count"), count);
        return Reply::success(f);
    });
    registerCommand(QStringLiteral("core.debug.destroy"), [this](const QJsonObject &cmd) {
        QObject *o = object(int64_t(cmd.value(QStringLiteral("target")).toDouble()));
        delete o; // as if Qt itself had destroyed it
        return Reply::success();
    });
    registerCommand(QStringLiteral("core.debug.objects"), [this](const QJsonObject &) {
        QJsonObject f;
        f.insert(QStringLiteral("count"), double(objectCount()));
        return Reply::success(f);
    });
    registerCommand(QStringLiteral("core.debug.selfquery"), [](const QJsonObject &) {
        // tkde_query on the UI thread must be refused, not deadlock
        const char *inner = tkde_query("{\"cmd\":\"core.ping\"}");
        QJsonObject f;
        f.insert(QStringLiteral("inner"), QString::fromUtf8(inner));
        return Reply::success(f);
    });
}
} // namespace tkde
