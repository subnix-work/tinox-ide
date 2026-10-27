// Tests the headless mock against the contract in docs/abi.md, through the C ABI only.
#include "tinoxkde_mock.h"

#include "../../tests/gc_thread.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

static int g_failures = 0;
#define EXPECT(cond)                                                                                                   \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                        \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)
#define EXPECT_CONTAINS(text, part)                                                                                    \
    do {                                                                                                               \
        std::string t_ = (text);                                                                                       \
        if (t_.find(part) == std::string::npos) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: \"%s\" not in %s\n", __FILE__, __LINE__, part, t_.c_str());              \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static std::string event(int64_t timeout = 2000)
{
    return tkde_next_event(timeout);
}

// Starts tkde_exec on a thread (it blocks) and waits for core.ready.
struct Running {
    std::thread ui;
    int64_t exitCode = -1000;
    std::string readyEvent;
    Running()
    {
        EXPECT(tkde_init("test", "Test") == TKDE_OK);
        ui = std::thread([this] { exitCode = tkde_exec(); });
        readyEvent = event();
        EXPECT_CONTAINS(readyEvent, "\"core.ready\"");
    }
    int64_t finish()
    {
        ui.join();
        return exitCode;
    }
};

static void testLifecycleErrors()
{
    mock_reset();
    EXPECT(tkde_post("{\"cmd\":\"core.ping\"}") == TKDE_E_NOT_INITIALIZED);
    EXPECT(tkde_exec() == TKDE_E_NOT_INITIALIZED);
    EXPECT(tkde_init(nullptr, "x") == TKDE_E_INVALID_ARG);
    EXPECT(tkde_init("a", "A") == TKDE_OK);
    EXPECT(tkde_init("a", "A") == TKDE_E_ALREADY_INITIALIZED);
    EXPECT(std::string(tkde_last_error()).find("already") != std::string::npos);
    EXPECT(tkde_abi_version() == TKDE_ABI_VERSION);
    EXPECT(tkde_post(nullptr) == TKDE_E_INVALID_ARG);
    EXPECT(tkde_post("{not json") == TKDE_E_INVALID_ARG);
    EXPECT(tkde_now_ns() > 0);
    ::setenv("TKDE_MOCK_ENV_TEST", "yes", 1);
    EXPECT(std::string(tkde_getenv("TKDE_MOCK_ENV_TEST")) == "yes");
    EXPECT(std::string(tkde_getenv("TKDE_MOCK_UNSET_VARIABLE")).empty());
    EXPECT(std::string(event(10)).empty()); // timeout yields ""
}

static void testCoreCommandsAndShutdown()
{
    mock_reset();
    Running r;
    // posted with req: the reply is an event and echoes unicode and escapes untouched
    EXPECT(tkde_post("{\"cmd\":\"core.ping\",\"req\":7,\"payload\":{\"s\":\"a\\\"b\\u00e4\\n\",\"n\":[1,2.5,null,true]}}") == TKDE_OK);
    std::string reply = event();
    EXPECT_CONTAINS(reply, "\"type\":\"core.reply\"");
    EXPECT_CONTAINS(reply, "\"req\":7");
    EXPECT_CONTAINS(reply, "\"ok\":true");
    // escapes are normalised (\u00e4 comes back as the UTF-8 character), the value is unchanged
    EXPECT_CONTAINS(reply, "\"payload\":{\"s\":\"a\\\"b\xc3\xa4\\n\"");
    EXPECT_CONTAINS(reply, "[1,2.5,null,true]");
    // query returns directly
    std::string q = tkde_query("{\"cmd\":\"core.capabilities\"}");
    EXPECT_CONTAINS(q, "\"abi\":1");
    EXPECT_CONTAINS(q, "\"namespaces\":[\"core\"]");
    // unknown command: query gets the error, a post without req gets a core.error event
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"nope.nothing\"}"), "\"code\":-6");
    EXPECT(tkde_post("{\"cmd\":\"nope.nothing\"}") == TKDE_OK);
    std::string err = event();
    EXPECT_CONTAINS(err, "\"type\":\"core.error\"");
    EXPECT_CONTAINS(err, "\"cmd\":\"nope.nothing\"");
    // a post with req and a failing command still answers through core.reply
    EXPECT(tkde_post("{\"cmd\":\"nope.nothing\",\"req\":9}") == TKDE_OK);
    std::string failed = event();
    EXPECT_CONTAINS(failed, "\"core.reply\"");
    EXPECT_CONTAINS(failed, "\"ok\":false");

    EXPECT(tkde_post("{\"cmd\":\"core.quit\",\"code\":3}") == TKDE_OK);
    EXPECT_CONTAINS(event(), "\"core.quit\"");
    EXPECT_CONTAINS(event(), "\"core.shutdown\"");
    EXPECT(r.finish() == 3);
    EXPECT(std::string(event(-1)).empty()); // after shutdown: "" at once, even with an infinite timeout
    EXPECT(tkde_post("{\"cmd\":\"core.ping\"}") == TKDE_E_NOT_INITIALIZED);
}

static void testOrderingAndSequence()
{
    mock_reset();
    Running r;
    for (int i = 0; i < 100; ++i) {
        std::string c = "{\"cmd\":\"core.ping\",\"payload\":" + std::to_string(i) + "}";
        EXPECT(tkde_post(c.c_str()) == TKDE_OK);
    }
    // commands are recorded in posting order
    for (int i = 0; i < 100; ++i) {
        std::string expected = "\"payload\":" + std::to_string(i) + "}";
        std::string got = mock_next_command(100);
        EXPECT_CONTAINS(got, expected.c_str());
    }
    EXPECT(mock_pending_commands() == 0);
    // injected events keep their order and get strictly increasing seq numbers
    int64_t last = 0;
    for (int i = 0; i < 50; ++i) {
        std::string e = "{\"type\":\"widget.clicked\",\"target\":" + std::to_string(i + 1) + "}";
        EXPECT(mock_push_event(e.c_str()) == TKDE_OK);
    }
    for (int i = 0; i < 50; ++i) {
        std::string ev = event();
        EXPECT_CONTAINS(ev, ("\"target\":" + std::to_string(i + 1)).c_str());
        size_t at = ev.find("\"seq\":");
        EXPECT(at != std::string::npos);
        int64_t seq = std::strtoll(ev.c_str() + at + 6, nullptr, 10);
        EXPECT(seq > last);
        last = seq;
        EXPECT_CONTAINS(ev, "\"t_ns\":");
    }
    tkde_quit();
    r.finish();
}

static void testObjectsAndScripts()
{
    mock_reset();
    Running r;
    int64_t id = mock_create_object();
    EXPECT(id > 0);
    EXPECT(mock_create_object() != id);
    EXPECT(tkde_release(id) == TKDE_OK);
    EXPECT(tkde_release(id) == TKDE_E_NOT_FOUND);
    EXPECT(tkde_release(0) == TKDE_E_INVALID_ARG);
    EXPECT(tkde_release(-4) == TKDE_E_INVALID_ARG);
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.release\",\"target\":99999}"), "\"code\":-5");

    EXPECT(mock_add_namespace("widget") == TKDE_OK);
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.capabilities\"}"), "\"core\",\"widget\"");

    // scripted: a reply merged into {"ok":true} plus a follow-up event
    EXPECT(mock_on_command("widget.apply", "{\"id\":17}", "{\"type\":\"widget.created\",\"target\":17}") == TKDE_OK);
    std::string q = tkde_query("{\"cmd\":\"widget.apply\",\"tree\":{}}");
    EXPECT_CONTAINS(q, "\"ok\":true");
    EXPECT_CONTAINS(q, "\"id\":17");
    EXPECT_CONTAINS(event(), "\"widget.created\"");
    EXPECT(mock_on_command("", "", "") == TKDE_E_INVALID_ARG);
    EXPECT(mock_on_command("x.y", "[1]", "") == TKDE_E_INVALID_ARG);
    tkde_quit();
    r.finish();
}

static void testOverflowIsVisible()
{
    mock_reset();
    Running r;
    const int total = 65536 + 500;
    for (int i = 0; i < total; ++i) {
        mock_push_event("{\"type\":\"widget.clicked\"}");
    }
    EXPECT(mock_dropped_events() > 0);
    std::string first = event();
    EXPECT_CONTAINS(first, "\"type\":\"core.overflow\"");
    EXPECT_CONTAINS(first, "\"dropped\":");
    EXPECT_CONTAINS(first, "\"last_seq\":");
    // the marker's range is exactly what is missing: next seq = last_seq + 1
    int64_t lastSeq = std::strtoll(first.c_str() + first.find("\"last_seq\":") + 11, nullptr, 10);
    int64_t dropped = std::strtoll(first.c_str() + first.find("\"dropped\":") + 10, nullptr, 10);
    int64_t firstSeq = std::strtoll(first.c_str() + first.find("\"seq\":") + 6, nullptr, 10);
    EXPECT(lastSeq - firstSeq + 1 == dropped);
    std::string next = event();
    int64_t nextSeq = std::strtoll(next.c_str() + next.find("\"seq\":") + 6, nullptr, 10);
    EXPECT(nextSeq == lastSeq + 1);
    // everything still queued is bounded by the capacity
    int remaining = 2;
    while (!std::string(event(0)).empty()) {
        ++remaining;
    }
    EXPECT(remaining <= 65536);
    EXPECT(dropped + (remaining - 1) >= total); // nothing vanished without being counted (the ready event shares the range)
    tkde_quit();
    r.finish();
}

static std::string readFile(const std::string &path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void testRecordAndReplay()
{
    std::string path = std::string("/tmp/tinox_mock_transcript_") + std::to_string(::getpid()) + ".jsonl";
    ::setenv("TKDE_MOCK_RECORD", path.c_str(), 1);
    ::unsetenv("TKDE_MOCK_REPLAY");
    std::vector<std::string> recorded;
    {
        mock_reset();
        Running r;
        recorded.push_back(r.readyEvent);
        mock_push_event("{\"type\":\"widget.clicked\",\"target\":5}");
        tkde_post("{\"cmd\":\"core.ping\",\"req\":1,\"payload\":\"hi\"}");
        tkde_post("{\"cmd\":\"core.quit\"}");
        for (;;) {
            std::string e = event();
            if (e.empty()) {
                break;
            }
            recorded.push_back(e);
            if (e.find("\"core.shutdown\"") != std::string::npos) {
                break;
            }
        }
        r.finish();
    }
    ::unsetenv("TKDE_MOCK_RECORD");
    std::string transcript = readFile(path);
    EXPECT_CONTAINS(transcript, "\"dir\":\"command\"");
    EXPECT_CONTAINS(transcript, "\"dir\":\"event\"");
    EXPECT_CONTAINS(transcript, "\"payload\":\"hi\"");

    // replay: the recorded events come back verbatim and nothing else is generated
    ::setenv("TKDE_MOCK_REPLAY", path.c_str(), 1);
    mock_reset();
    EXPECT(tkde_init("replay", "Replay") == TKDE_OK);
    std::thread ui([] { tkde_exec(); });
    std::vector<std::string> replayed;
    for (;;) {
        std::string e = event();
        if (e.empty()) {
            break;
        }
        replayed.push_back(e);
        if (e.find("\"core.shutdown\"") != std::string::npos) {
            break;
        }
    }
    ui.join();
    ::unsetenv("TKDE_MOCK_REPLAY");
    EXPECT(replayed.size() == recorded.size());
    for (size_t i = 0; i < recorded.size() && i < replayed.size(); ++i) {
        EXPECT(replayed[i] == recorded[i]);
    }
    std::remove(path.c_str());
}

int main()
{
    gcInit();
    testLifecycleErrors();
    testCoreCommandsAndShutdown();
    testOrderingAndSequence();
    testObjectsAndScripts();
    testOverflowIsVisible();
    testRecordAndReplay();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("mock ok");
    return 0;
}
