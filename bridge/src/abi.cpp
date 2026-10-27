// The C ABI of tinoxkde.h on top of tkde::Bridge. Nothing but argument checking
// and string ownership happens here; see docs/abi.md for the contract.

#include "tinoxkde.h"

#include "bridge.h"

#include <QByteArray>
#include <QString>

#include <gc.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
// Strings handed to Tinox are GC allocations: the collector owns their lifetime.
const char *gcCopy(const char *data, size_t size)
{
    char *out = static_cast<char *>(GC_malloc_atomic(size + 1));
    if (!out) {
        return "";
    }
    std::memcpy(out, data, size);
    out[size] = '\0';
    return out;
}

const char *gcCopy(const std::string &s)
{
    return gcCopy(s.data(), s.size());
}

const char *gcCopy(const QByteArray &s)
{
    return gcCopy(s.constData(), size_t(s.size()));
}

const char *errorReply(int64_t code, const char *message)
{
    tkde::setLastError(code, QString::fromUtf8(message));
    return gcCopy(QByteArray("{\"ok\":false,\"error\":{\"code\":") + QByteArray::number(qlonglong(code)) + ",\"message\":\"" + message +
                  "\"}}");
}
} // namespace

extern "C" {

int64_t tkde_abi_version(void)
{
    return TKDE_ABI_VERSION;
}

int64_t tkde_init(const char *app_name, const char *display_name)
{
    if (!app_name || !display_name) {
        return tkde::setLastError(TKDE_E_INVALID_ARG, QStringLiteral("tkde_init needs an application and a display name"));
    }
    return tkde::Bridge::instance().init(QString::fromUtf8(app_name), QString::fromUtf8(display_name));
}

int64_t tkde_exec(void)
{
    return tkde::Bridge::instance().exec();
}

void tkde_quit(void)
{
    tkde::Bridge::instance().requestQuit(0);
}

int64_t tkde_post(const char *json)
{
    if (!json) {
        return tkde::setLastError(TKDE_E_INVALID_ARG, QStringLiteral("tkde_post(NULL)"));
    }
    return tkde::Bridge::instance().post(QByteArray(json));
}

const char *tkde_query(const char *json)
{
    if (!json) {
        return errorReply(TKDE_E_INVALID_ARG, "tkde_query(NULL)");
    }
    return gcCopy(tkde::Bridge::instance().query(QByteArray(json)));
}

const char *tkde_next_event(int64_t timeout_ms)
{
    std::string out;
    if (!tkde::Bridge::instance().nextEvent(timeout_ms, out)) {
        return "";
    }
    return gcCopy(out);
}

int64_t tkde_release(int64_t id)
{
    return tkde::Bridge::instance().release(id);
}

const char *tkde_last_error(void)
{
    return gcCopy(tkde::lastError());
}

int64_t tkde_now_ns(void)
{
    return tkde::monotonicNanos();
}

const char *tkde_getenv(const char *name)
{
    const char *value = name ? std::getenv(name) : nullptr;
    return value ? gcCopy(value, std::strlen(value)) : "";
}

} // extern "C"
