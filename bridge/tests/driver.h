// Shared by the bridge tests that drive the real library through the C ABI: JSON
// commands, events and the EXPECT macros. Include after tinoxkde.h.
#ifndef TINOXKDE_TEST_DRIVER_H
#define TINOXKDE_TEST_DRIVER_H

#include "gc_thread.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <cstdio>
#include <string>
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

static int errorCode(const Obj &reply)
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt();
}

#endif // TINOXKDE_TEST_DRIVER_H
