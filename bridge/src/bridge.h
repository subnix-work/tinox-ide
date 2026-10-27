// The core of the real bridge: application lifecycle, the command and event
// queues, the object registry and command dispatch (docs/abi.md).
//
// Threads: the process main thread owns Qt (init, exec). Commands always run on
// that UI thread. Everything marked "any thread" is safe to call from the Tinox
// logic task; nothing here ever calls back into Tinox.
#ifndef TINOXKDE_BRIDGE_H
#define TINOXKDE_BRIDGE_H

#include "event_queue.h"

#include <QByteArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QApplication;
class QObject;
class KDBusService;

Q_DECLARE_LOGGING_CATEGORY(lcBridge)

namespace tkde
{
/// Outcome of a command: `{"ok":true, ...fields}` or `{"ok":false,"error":{code,message}}`.
struct Reply {
    bool ok = true;
    int64_t code = 0;
    QString message;
    QJsonObject fields;

    static Reply success(QJsonObject fields = {})
    {
        Reply r;
        r.fields = std::move(fields);
        return r;
    }
    static Reply failure(int64_t code, const QString &message)
    {
        Reply r;
        r.ok = false;
        r.code = code;
        r.message = message;
        return r;
    }
    QJsonObject toJson() const;
};

/// A command handler. Runs on the UI thread; `cmd` is the whole command object.
using Handler = std::function<Reply(const QJsonObject &cmd)>;

class Bridge
{
public:
    static Bridge &instance();

    // ---- the ABI (abi.cpp wraps these) ----
    int64_t init(const QString &appName, const QString &displayName);
    int64_t exec();
    void requestQuit(int64_t code);                      // any thread
    int64_t post(const QByteArray &json);                // any thread
    QByteArray query(const QByteArray &json);            // not the UI thread
    bool nextEvent(int64_t timeoutMs, std::string &out); // any thread
    int64_t release(int64_t id);                         // any thread

    // ---- for feature modules ----
    /// Registers `name` ("namespace.verb"). UI thread, before exec.
    void registerCommand(const QString &name, Handler handler);
    /// Adds a namespace to the core.capabilities answer.
    void addNamespace(const QString &ns);
    /// Queues an event. Any thread.
    void emitEvent(const QString &type, const QJsonObject &fields = {}, int64_t target = 0);
    /// Gives `object` an id (UI thread). The registry holds a QPointer: an object destroyed
    /// from the Qt side simply stops resolving.
    int64_t registerObject(QObject *object);
    /// Runs on the UI thread right before a released object is scheduled for deletion, while it
    /// is still fully alive (modules free things the object's destructor can no longer reach).
    void addReleaseHook(std::function<void(QObject *)> hook);
    /// UI thread. nullptr when the id is unknown, released or the object is gone.
    QObject *object(int64_t id) const;
    bool isUiThread() const;
    size_t objectCount() const;
    int64_t droppedEvents() const { return m_events.dropped(); }
    /// True with TKDE_DEBUG_COMMANDS=1: test-only commands may be registered.
    bool debugCommandsEnabled() const { return m_debugCommands; }

private:
    Bridge();
    ~Bridge();

    Reply dispatch(const QJsonObject &cmd);
    void runOnUi(std::function<void()> fn);        // queued, any thread
    bool runOnUiAndWait(std::function<void()> fn); // false if the loop ended first
    void registerCoreCommands();
    int64_t releaseOnUi(int64_t id);
    void destroyAllObjects();
    void setupKde(const QString &appName, const QString &displayName);

    tkde::EventQueue m_events;
    QApplication *m_app = nullptr;
    QObject *m_context = nullptr; // lives in the UI thread; queued calls target it
    std::unique_ptr<KDBusService> m_service;

    std::atomic<bool> m_inited{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shutdown{false};
    std::atomic<bool> m_quitting{false};
    std::atomic<int64_t> m_uiThreadId{0};

    std::map<QString, Handler> m_handlers;
    std::vector<QString> m_namespaces;
    std::vector<std::function<void(QObject *)>> m_releaseHooks;
    std::map<int64_t, QPointer<QObject>> m_objects;
    int64_t m_nextId = 1;
    bool m_debugCommands = false;
};

/// Records `message` as the calling thread's last error and returns `code`.
int64_t setLastError(int64_t code, const QString &message);
const std::string &lastError();
} // namespace tkde

#endif // TINOXKDE_BRIDGE_H
