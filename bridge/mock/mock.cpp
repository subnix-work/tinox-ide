// libtinoxkde-mock: a headless implementation of the whole bridge ABI (tinoxkde.h).
//
// No Qt, no KDE, no display: the Tinox side of the IDE can be built and tested
// anywhere. Behaviour for the core namespace, ordering, ids, queue bounds and
// shutdown follows docs/abi.md; everything else is scripted by the test through
// tinoxkde_mock.h. Commands run synchronously in the caller's thread, which is
// what a UI thread that is always idle would do.

#include "tinoxkde_mock.h"

#include <gc.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr size_t kEventCapacity = 65536;

// ---------------------------------------------------------------- tiny JSON

struct J {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type t = Null;
    std::string s; // string value, raw number text, "true"/"false"
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;

    static J str(std::string v)
    {
        J j;
        j.t = Str;
        j.s = std::move(v);
        return j;
    }
    static J num(int64_t v)
    {
        J j;
        j.t = Num;
        j.s = std::to_string(v);
        return j;
    }
    static J boolean(bool v)
    {
        J j;
        j.t = Bool;
        j.s = v ? "true" : "false";
        return j;
    }
    static J object()
    {
        J j;
        j.t = Obj;
        return j;
    }
    static J array()
    {
        J j;
        j.t = Arr;
        return j;
    }
    const J *get(const std::string &k) const
    {
        for (const auto &p : o) {
            if (p.first == k) {
                return &p.second;
            }
        }
        return nullptr;
    }
    void set(const std::string &k, J v)
    {
        for (auto &p : o) {
            if (p.first == k) {
                p.second = std::move(v);
                return;
            }
        }
        o.emplace_back(k, std::move(v));
    }
    bool isInt() const { return t == Num && s.find_first_of(".eE") == std::string::npos; }
    int64_t asInt() const { return std::strtoll(s.c_str(), nullptr, 10); }
};

class Parser
{
public:
    explicit Parser(const std::string &in) : m_in(in) {}
    bool parse(J &out)
    {
        skip();
        if (!value(out, 0)) {
            return false;
        }
        skip();
        return m_i == m_in.size();
    }

private:
    const std::string &m_in;
    size_t m_i = 0;

    void skip()
    {
        while (m_i < m_in.size() && (m_in[m_i] == ' ' || m_in[m_i] == '\t' || m_in[m_i] == '\n' || m_in[m_i] == '\r')) {
            ++m_i;
        }
    }
    bool lit(const char *word)
    {
        size_t n = std::strlen(word);
        if (m_in.compare(m_i, n, word) == 0) {
            m_i += n;
            return true;
        }
        return false;
    }
    static void utf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned &v)
    {
        if (m_i + 4 > m_in.size()) {
            return false;
        }
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = m_in[m_i++];
            v <<= 4;
            if (c >= '0' && c <= '9') {
                v |= unsigned(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                v |= unsigned(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                v |= unsigned(c - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }
    bool string(std::string &out)
    {
        if (m_i >= m_in.size() || m_in[m_i] != '"') {
            return false;
        }
        ++m_i;
        while (m_i < m_in.size()) {
            unsigned char c = static_cast<unsigned char>(m_in[m_i++]);
            if (c == '"') {
                return true;
            }
            if (c < 0x20) {
                return false;
            }
            if (c != '\\') {
                out += char(c);
                continue;
            }
            if (m_i >= m_in.size()) {
                return false;
            }
            char e = m_in[m_i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp;
                if (!hex4(cp)) {
                    return false;
                }
                if (cp >= 0xD800 && cp < 0xDC00) { // surrogate pair
                    unsigned lo;
                    if (m_in.compare(m_i, 2, "\\u") != 0) {
                        return false;
                    }
                    m_i += 2;
                    if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) {
                        return false;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool value(J &out, int depth)
    {
        if (depth > 64 || m_i >= m_in.size()) {
            return false;
        }
        char c = m_in[m_i];
        if (c == '{') {
            ++m_i;
            out = J::object();
            skip();
            if (m_i < m_in.size() && m_in[m_i] == '}') {
                ++m_i;
                return true;
            }
            for (;;) {
                skip();
                std::string key;
                if (!string(key)) {
                    return false;
                }
                skip();
                if (m_i >= m_in.size() || m_in[m_i++] != ':') {
                    return false;
                }
                skip();
                J v;
                if (!value(v, depth + 1)) {
                    return false;
                }
                out.set(key, std::move(v));
                skip();
                if (m_i >= m_in.size()) {
                    return false;
                }
                char d = m_in[m_i++];
                if (d == '}') {
                    return true;
                }
                if (d != ',') {
                    return false;
                }
            }
        }
        if (c == '[') {
            ++m_i;
            out = J::array();
            skip();
            if (m_i < m_in.size() && m_in[m_i] == ']') {
                ++m_i;
                return true;
            }
            for (;;) {
                skip();
                J v;
                if (!value(v, depth + 1)) {
                    return false;
                }
                out.a.push_back(std::move(v));
                skip();
                if (m_i >= m_in.size()) {
                    return false;
                }
                char d = m_in[m_i++];
                if (d == ']') {
                    return true;
                }
                if (d != ',') {
                    return false;
                }
            }
        }
        if (c == '"') {
            out.t = J::Str;
            return string(out.s);
        }
        if (lit("true")) {
            out = J::boolean(true);
            return true;
        }
        if (lit("false")) {
            out = J::boolean(false);
            return true;
        }
        if (lit("null")) {
            out = J();
            return true;
        }
        size_t start = m_i;
        if (m_in[m_i] == '-') {
            ++m_i;
        }
        while (m_i < m_in.size() && (std::isdigit(static_cast<unsigned char>(m_in[m_i])) || m_in[m_i] == '.' || m_in[m_i] == 'e' || m_in[m_i] == 'E' || m_in[m_i] == '+' || m_in[m_i] == '-')) {
            ++m_i;
        }
        if (m_i == start || (m_i == start + 1 && m_in[start] == '-')) {
            return false;
        }
        out.t = J::Num;
        out.s = m_in.substr(start, m_i - start);
        return true;
    }
};

void escapeInto(std::string &out, const std::string &s)
{
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    out += '"';
}

void dumpInto(std::string &out, const J &j)
{
    switch (j.t) {
    case J::Null: out += "null"; break;
    case J::Bool:
    case J::Num: out += j.s; break;
    case J::Str: escapeInto(out, j.s); break;
    case J::Arr: {
        out += '[';
        for (size_t i = 0; i < j.a.size(); ++i) {
            if (i) {
                out += ',';
            }
            dumpInto(out, j.a[i]);
        }
        out += ']';
        break;
    }
    case J::Obj: {
        out += '{';
        bool first = true;
        for (const auto &p : j.o) {
            if (!first) {
                out += ',';
            }
            first = false;
            escapeInto(out, p.first);
            out += ':';
            dumpInto(out, p.second);
        }
        out += '}';
        break;
    }
    }
}

std::string dump(const J &j)
{
    std::string out;
    dumpInto(out, j);
    return out;
}

bool parseJson(const char *text, J &out)
{
    if (!text) {
        return false;
    }
    std::string s(text);
    return Parser(s).parse(out);
}

// ---------------------------------------------------------------- state

int64_t nowNs()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

struct Event {
    int64_t seq = 0;
    std::string json;
    // an overflow marker stands for the dropped events seq..lastSeq
    bool overflow = false;
    int64_t dropped = 0;
    int64_t lastSeq = 0;
};

struct Script {
    J reply = J::object();
    std::string event;
};

struct State {
    std::mutex m;
    std::condition_variable eventCv;
    std::condition_variable quitCv;
    std::condition_variable commandCv;

    bool inited = false;
    bool running = false;
    bool quit = false;
    bool shutdown = false;
    int64_t exitCode = 0;
    std::thread::id uiThread;

    std::deque<Event> events;
    int64_t nextSeq = 1;
    int64_t droppedTotal = 0;

    std::deque<std::string> commands;
    std::map<std::string, Script> scripts;
    std::set<int64_t> objects;
    int64_t nextId = 1;
    std::vector<std::string> namespaces{"core"};

    bool replaying = false;
    std::ofstream record;
};

std::mutex g_stateMutex;
std::shared_ptr<State> g_state = std::make_shared<State>();

std::shared_ptr<State> state()
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_state;
}

thread_local std::string t_lastError;

const char *gcCopy(const std::string &s)
{
    char *out = static_cast<char *>(GC_malloc_atomic(s.size() + 1));
    if (!out) {
        return "";
    }
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

int64_t fail(int64_t code, const std::string &message)
{
    t_lastError = message;
    return code;
}

J errorReply(int64_t code, const std::string &message)
{
    J err = J::object();
    err.set("code", J::num(code));
    err.set("message", J::str(message));
    J r = J::object();
    r.set("ok", J::boolean(false));
    r.set("error", std::move(err));
    return r;
}

void recordLine(State &st, const char *dir, const std::string &json)
{
    if (st.record.is_open()) {
        st.record << "{\"dir\":\"" << dir << "\",\"json\":" << json << "}\n";
        st.record.flush();
    }
}

// Caller holds st.m. Adds seq / t_ns when missing, enforces the bound.
void pushEventLocked(State &st, J ev, bool fromReplay = false)
{
    if (st.replaying && !fromReplay) {
        return; // a replay delivers exactly what was recorded
    }
    int64_t seq;
    if (const J *s = ev.get("seq"); s && s->isInt()) {
        seq = s->asInt();
        st.nextSeq = std::max(st.nextSeq, seq + 1);
    } else {
        seq = st.nextSeq++;
        ev.set("seq", J::num(seq));
    }
    if (!ev.get("t_ns")) {
        ev.set("t_ns", J::num(nowNs()));
    }
    std::string json = dump(ev);
    if (!fromReplay) {
        recordLine(st, "event", json);
    }
    if (st.events.size() >= kEventCapacity) {
        // Drop from the oldest end; the loss stays visible as ONE core.overflow
        // marker at the front that stands for the whole dropped seq range.
        if (!st.events.front().overflow) {
            Event marker;
            marker.overflow = true;
            marker.seq = marker.lastSeq = st.events.front().seq;
            marker.dropped = 1;
            st.events.front() = marker;
            ++st.droppedTotal;
        }
        if (st.events.size() > 1) {
            int64_t droppedSeq = st.events[1].seq;
            st.events.erase(st.events.begin() + 1); // invalidates references: re-read the front
            st.events.front().dropped += 1;
            st.events.front().lastSeq = droppedSeq;
            ++st.droppedTotal;
        }
    }
    Event e;
    e.seq = seq;
    e.json = std::move(json);
    st.events.push_back(std::move(e));
    st.eventCv.notify_all();
}

std::string overflowJson(const Event &e)
{
    J j = J::object();
    j.set("type", J::str("core.overflow"));
    j.set("seq", J::num(e.seq));
    j.set("t_ns", J::num(nowNs()));
    j.set("dropped", J::num(e.dropped));
    j.set("last_seq", J::num(e.lastSeq));
    return dump(j);
}

void pushType(State &st, const char *type, J extra = J::object())
{
    extra.set("type", J::str(type));
    pushEventLocked(st, std::move(extra));
}

void requestQuitLocked(State &st, int64_t code)
{
    if (!st.running || st.quit) {
        return;
    }
    st.quit = true;
    st.exitCode = code;
    J q = J::object();
    q.set("code", J::num(code));
    pushType(st, "core.quit", std::move(q));
    st.quitCv.notify_all();
}

// Runs one command. Caller holds st.m. `viaQuery`: the reply goes straight back.
J runCommandLocked(State &st, const J &cmd, bool viaQuery)
{
    const J *name = cmd.get("cmd");
    if (cmd.t != J::Obj || !name || name->t != J::Str) {
        return errorReply(TKDE_E_INVALID_ARG, "a command is a JSON object with a string \"cmd\"");
    }
    const std::string &n = name->s;
    J reply = J::object();
    reply.set("ok", J::boolean(true));
    std::string scriptedEvent;

    if (n == "core.ping") {
        const J *p = cmd.get("payload");
        reply.set("payload", p ? *p : J());
    } else if (n == "core.capabilities") {
        reply.set("abi", J::num(TKDE_ABI_VERSION));
        J ns = J::array();
        for (const auto &x : st.namespaces) {
            ns.a.push_back(J::str(x));
        }
        reply.set("namespaces", std::move(ns));
        reply.set("qt", J::str("mock"));
        reply.set("kf", J::str("mock"));
    } else if (n == "core.release") {
        const J *t = cmd.get("target");
        if (!t || !t->isInt()) {
            return errorReply(TKDE_E_INVALID_ARG, "core.release needs an integer \"target\"");
        }
        if (st.objects.erase(t->asInt()) == 0) {
            return errorReply(TKDE_E_NOT_FOUND, "no object with id " + t->s);
        }
    } else if (n == "core.quit") {
        const J *c = cmd.get("code");
        requestQuitLocked(st, c && c->isInt() ? c->asInt() : 0);
    } else if (n == "core.log") {
        // accepted; the mock has no log sink
    } else if (auto it = st.scripts.find(n); it != st.scripts.end()) {
        for (const auto &p : it->second.reply.o) {
            reply.set(p.first, p.second);
        }
        scriptedEvent = it->second.event;
    } else {
        return errorReply(TKDE_E_UNSUPPORTED, "unknown command: " + n);
    }
    (void)viaQuery;
    if (!scriptedEvent.empty()) {
        J ev;
        if (parseJson(scriptedEvent.c_str(), ev)) {
            pushEventLocked(st, std::move(ev));
        }
    }
    return reply;
}

// tkde_post / tkde_query share this: record, run, deliver the reply as asked.
J handle(State &st, const char *json, bool viaQuery)
{
    std::unique_lock<std::mutex> lock(st.m);
    J cmd;
    if (!parseJson(json, cmd)) {
        return errorReply(TKDE_E_INVALID_ARG, "malformed JSON command");
    }
    std::string raw(json);
    recordLine(st, viaQuery ? "query" : "command", raw);
    st.commands.push_back(raw);
    st.commandCv.notify_all();
    J reply = runCommandLocked(st, cmd, viaQuery);
    if (!viaQuery) {
        const J *req = cmd.get("req");
        const J *ok = reply.get("ok");
        bool failed = ok && ok->s == "false";
        if (req) {
            J ev = reply;
            ev.set("req", *req);
            ev.set("type", J::str("core.reply"));
            pushEventLocked(st, std::move(ev));
        } else if (failed) {
            J ev = J::object();
            ev.set("type", J::str("core.error"));
            if (const J *e = reply.get("error")) {
                ev.set("error", *e);
            }
            if (const J *c = cmd.get("cmd")) {
                ev.set("cmd", *c);
            }
            pushEventLocked(st, std::move(ev));
        }
    } else {
        recordLine(st, "reply", dump(reply));
    }
    return reply;
}

std::string wrongThreadReply()
{
    return dump(errorReply(TKDE_E_WRONG_THREAD, "tkde_query was called from the UI thread"));
}
} // namespace

// ---------------------------------------------------------------- ABI

extern "C" {

int64_t tkde_abi_version(void)
{
    return TKDE_ABI_VERSION;
}

int64_t tkde_init(const char *app_name, const char *display_name)
{
    if (!app_name || !display_name) {
        return fail(TKDE_E_INVALID_ARG, "tkde_init needs an application and a display name");
    }
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    if (st->inited) {
        return fail(TKDE_E_ALREADY_INITIALIZED, "tkde_init was already called");
    }
    st->inited = true;
    if (const char *rec = std::getenv("TKDE_MOCK_RECORD"); rec && *rec) {
        st->record.open(rec, std::ios::trunc);
    }
    if (const char *rep = std::getenv("TKDE_MOCK_REPLAY"); rep && *rep) {
        std::ifstream in(rep);
        std::string line;
        st->replaying = true;
        while (std::getline(in, line)) {
            J rec;
            if (!parseJson(line.c_str(), rec)) {
                continue;
            }
            const J *dir = rec.get("dir");
            const J *ev = rec.get("json");
            if (dir && dir->s == "event" && ev) {
                pushEventLocked(*st, *ev, true);
            }
        }
    }
    return TKDE_OK;
}

int64_t tkde_exec(void)
{
    auto st = state();
    std::unique_lock<std::mutex> lock(st->m);
    if (!st->inited) {
        return fail(TKDE_E_NOT_INITIALIZED, "tkde_exec before tkde_init");
    }
    if (st->running || st->shutdown) {
        return fail(TKDE_E_FAILED, "tkde_exec is already running or finished");
    }
    st->running = true;
    st->uiThread = std::this_thread::get_id();
    pushType(*st, "core.ready");
    st->quitCv.wait(lock, [&] { return st->quit; });
    pushType(*st, "core.shutdown");
    st->shutdown = true;
    st->running = false;
    return st->exitCode;
}

void tkde_quit(void)
{
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    requestQuitLocked(*st, 0);
}

int64_t tkde_post(const char *json)
{
    auto st = state();
    {
        std::lock_guard<std::mutex> lock(st->m);
        if (!st->inited || st->shutdown) {
            return fail(TKDE_E_NOT_INITIALIZED, "tkde_post outside the bridge's lifetime");
        }
    }
    if (!json) {
        return fail(TKDE_E_INVALID_ARG, "tkde_post(NULL)");
    }
    J reply = handle(*st, json, false);
    const J *ok = reply.get("ok");
    if (ok && ok->s == "false") {
        const J *err = reply.get("error");
        const J *code = err ? err->get("code") : nullptr;
        // a malformed command is rejected on the spot; any other failure arrives as an event
        if (code && code->isInt() && code->asInt() == TKDE_E_INVALID_ARG) {
            const J *msg = err->get("message");
            return fail(TKDE_E_INVALID_ARG, msg ? msg->s : "invalid command");
        }
    }
    return TKDE_OK;
}

const char *tkde_query(const char *json)
{
    auto st = state();
    {
        std::lock_guard<std::mutex> lock(st->m);
        if (!st->inited || st->shutdown) {
            return gcCopy(dump(errorReply(TKDE_E_NOT_INITIALIZED, "tkde_query outside the bridge's lifetime")));
        }
        if (st->running && st->uiThread == std::this_thread::get_id()) {
            return gcCopy(wrongThreadReply());
        }
    }
    if (!json) {
        return gcCopy(dump(errorReply(TKDE_E_INVALID_ARG, "tkde_query(NULL)")));
    }
    return gcCopy(dump(handle(*st, json, true)));
}

const char *tkde_next_event(int64_t timeout_ms)
{
    auto st = state();
    std::unique_lock<std::mutex> lock(st->m);
    auto ready = [&] { return !st->events.empty() || st->shutdown; };
    if (timeout_ms < 0) {
        st->eventCv.wait(lock, ready);
    } else {
        st->eventCv.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready);
    }
    if (st->events.empty()) {
        return "";
    }
    Event e = std::move(st->events.front());
    st->events.pop_front();
    std::string json = e.overflow ? overflowJson(e) : e.json;
    if (st->replaying && json.find("\"core.shutdown\"") != std::string::npos) {
        // a replayed run ends where the recorded one did
        st->shutdown = true;
        st->quit = true;
        st->quitCv.notify_all();
    }
    return gcCopy(json);
}

int64_t tkde_release(int64_t id)
{
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    if (!st->inited) {
        return fail(TKDE_E_NOT_INITIALIZED, "tkde_release before tkde_init");
    }
    if (id <= 0) {
        return fail(TKDE_E_INVALID_ARG, "object ids are positive");
    }
    recordLine(*st, "command", "{\"cmd\":\"core.release\",\"target\":" + std::to_string(id) + "}");
    st->commands.push_back("{\"cmd\":\"core.release\",\"target\":" + std::to_string(id) + "}");
    st->commandCv.notify_all();
    if (st->objects.erase(id) == 0) {
        return fail(TKDE_E_NOT_FOUND, "no object with id " + std::to_string(id));
    }
    return TKDE_OK;
}

const char *tkde_last_error(void)
{
    return gcCopy(t_lastError);
}

int64_t tkde_now_ns(void)
{
    return nowNs();
}

const char *tkde_getenv(const char *name)
{
    const char *v = name ? std::getenv(name) : nullptr;
    return gcCopy(v ? v : "");
}

// ---------------------------------------------------------------- scripting

void mock_reset(void)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_state = std::make_shared<State>();
    t_lastError.clear();
}

int64_t mock_push_event(const char *json)
{
    J ev;
    if (!parseJson(json, ev) || ev.t != J::Obj) {
        return fail(TKDE_E_INVALID_ARG, "mock_push_event needs a JSON object");
    }
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    pushEventLocked(*st, std::move(ev));
    return TKDE_OK;
}

const char *mock_next_command(int64_t timeout_ms)
{
    auto st = state();
    std::unique_lock<std::mutex> lock(st->m);
    auto ready = [&] { return !st->commands.empty(); };
    if (timeout_ms < 0) {
        st->commandCv.wait(lock, ready);
    } else {
        st->commandCv.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready);
    }
    if (st->commands.empty()) {
        return "";
    }
    std::string c = std::move(st->commands.front());
    st->commands.pop_front();
    return gcCopy(c);
}

int64_t mock_pending_commands(void)
{
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    return int64_t(st->commands.size());
}

int64_t mock_on_command(const char *cmd, const char *reply_json, const char *event_json)
{
    if (!cmd || !*cmd) {
        return fail(TKDE_E_INVALID_ARG, "mock_on_command needs a command name");
    }
    Script s;
    if (reply_json && *reply_json) {
        J r;
        if (!parseJson(reply_json, r) || r.t != J::Obj) {
            return fail(TKDE_E_INVALID_ARG, "the scripted reply must be a JSON object");
        }
        s.reply = std::move(r);
    }
    if (event_json && *event_json) {
        J e;
        if (!parseJson(event_json, e) || e.t != J::Obj) {
            return fail(TKDE_E_INVALID_ARG, "the scripted event must be a JSON object");
        }
        s.event = event_json;
    }
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    st->scripts[cmd] = std::move(s);
    return TKDE_OK;
}

int64_t mock_create_object(void)
{
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    int64_t id = st->nextId++;
    st->objects.insert(id);
    return id;
}

int64_t mock_add_namespace(const char *name)
{
    if (!name || !*name) {
        return fail(TKDE_E_INVALID_ARG, "namespace name required");
    }
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    st->namespaces.emplace_back(name);
    return TKDE_OK;
}

int64_t mock_dropped_events(void)
{
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    return st->droppedTotal;
}

void mock_close_last_window(void)
{
    auto st = state();
    std::lock_guard<std::mutex> lock(st->m);
    requestQuitLocked(*st, 0);
}

} // extern "C"
