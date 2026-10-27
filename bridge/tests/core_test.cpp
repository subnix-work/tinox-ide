// Tests the real bridge core against docs/abi.md through the C ABI only.
// The main thread runs tkde_exec(); a driver thread plays the Tinox logic task.
// Runs on Qt's offscreen platform (set by CTest); TKDE_DEBUG_COMMANDS=1 enables the test hooks.
#include "tinoxkde.h"

#include "gc_thread.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

static std::atomic<int> g_failures{0};
#define EXPECT(cond)                                                                                                                       \
    do {                                                                                                                                   \
        if (!(cond)) {                                                                                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                           \
            ++g_failures;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)
#define EXPECT_CONTAINS(text, part)                                                                                                        \
    do {                                                                                                                                   \
        std::string t_ = (text);                                                                                                           \
        if (t_.find(part) == std::string::npos) {                                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: \"%s\" not in %s\n", __FILE__, __LINE__, part, t_.c_str());                                  \
            ++g_failures;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

static std::string event(int64_t timeout = 5000)
{
    return tkde_next_event(timeout);
}

static int64_t seqOf(const std::string &json)
{
    size_t at = json.find("\"seq\":");
    return at == std::string::npos ? -1 : std::strtoll(json.c_str() + at + 6, nullptr, 10);
}

// The integer after `"key":` in a flat JSON event, or -1.
static int64_t intAfter(const std::string &json, const char *key)
{
    std::string marker = std::string("\"") + key + "\":";
    size_t at = json.find(marker);
    return at == std::string::npos ? -1 : std::strtoll(json.c_str() + at + marker.size(), nullptr, 10);
}

static void driver()
{
    GcAttach gc;
    // ---- startup
    std::string ready = event();
    EXPECT_CONTAINS(ready, "\"type\":\"core.ready\"");
    EXPECT_CONTAINS(ready, "\"t_ns\":");
    EXPECT(seqOf(ready) == 1);

    // ---- thread rules
    EXPECT(tkde_exec() == TKDE_E_WRONG_THREAD); // not the main thread
    EXPECT(tkde_init("again", "Again") == TKDE_E_WRONG_THREAD);
    EXPECT(std::string(tkde_last_error()).find("main thread") != std::string::npos);
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.debug.selfquery\"}"), "\\\"code\\\":-2"); // tkde_query on the UI thread is refused

    // ---- core commands: reply by event (req) and by query
    EXPECT(tkde_post("{\"cmd\":\"core.ping\",\"req\":7,\"payload\":{\"s\":\"a\\\"b\\u00e4\\n\",\"n\":[1,2.5,null,true]}}") == TKDE_OK);
    std::string reply = event();
    EXPECT_CONTAINS(reply, "\"type\":\"core.reply\"");
    EXPECT_CONTAINS(reply, "\"req\":7");
    EXPECT_CONTAINS(reply, "\"ok\":true");
    EXPECT_CONTAINS(reply, "\"payload\":{\"n\":[1,2.5,null,true],\"s\":\"a\\\"b\xc3\xa4\\n\"}");
    EXPECT(event(0).empty()); // nothing else was produced

    std::string caps = tkde_query("{\"cmd\":\"core.capabilities\"}");
    EXPECT_CONTAINS(caps, "\"abi\":1");
    EXPECT_CONTAINS(caps, "\"namespaces\":[\"core\"]");
    EXPECT_CONTAINS(caps, "\"qt\":\"6.");
    EXPECT_CONTAINS(caps, "\"kf\":\"6.");

    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"nope.nothing\"}"), "\"code\":-6");
    EXPECT_CONTAINS(tkde_query("{\"nocmd\":1}"), "\"code\":-1");
    EXPECT_CONTAINS(tkde_query("{broken"), "\"code\":-1");
    EXPECT(tkde_post("{\"cmd\":\"nope.nothing\"}") == TKDE_OK);
    std::string err = event();
    EXPECT_CONTAINS(err, "\"type\":\"core.error\"");
    EXPECT_CONTAINS(err, "\"cmd\":\"nope.nothing\"");
    EXPECT(tkde_post("{\"cmd\":\"nope.nothing\",\"req\":9}") == TKDE_OK);
    std::string failed = event();
    EXPECT_CONTAINS(failed, "\"core.reply\"");
    EXPECT_CONTAINS(failed, "\"ok\":false");
    EXPECT(tkde_post(nullptr) == TKDE_E_INVALID_ARG);
    EXPECT(tkde_post("{not json") == TKDE_E_INVALID_ARG);
    EXPECT(tkde_post("{\"cmd\":5}") == TKDE_E_INVALID_ARG);
    EXPECT_CONTAINS(tkde_query(nullptr), "\"code\":-1");

    // ---- logging
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.log\",\"level\":\"info\",\"message\":\"hello from the test\"}"), "\"ok\":true");
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.log\",\"level\":\"loud\",\"message\":\"x\"}"), "\"code\":-1");

    // ---- ordering: replies come back in posting order, seq strictly increases
    for (int i = 0; i < 1000; ++i) {
        std::string c = "{\"cmd\":\"core.ping\",\"req\":" + std::to_string(i) + ",\"payload\":" + std::to_string(i * 3) + "}";
        EXPECT(tkde_post(c.c_str()) == TKDE_OK);
    }
    int64_t last = 0;
    for (int i = 0; i < 1000; ++i) {
        std::string ev = event();
        EXPECT(intAfter(ev, "req") == i);
        EXPECT(intAfter(ev, "payload") == i * 3);
        int64_t seq = seqOf(ev);
        EXPECT(seq > last);
        last = seq;
    }

    // ---- object registry: ids, release, objects destroyed from the Qt side
    std::string made = tkde_query("{\"cmd\":\"core.debug.create\",\"count\":5000}");
    EXPECT_CONTAINS(made, "\"count\":5000");
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.debug.objects\"}"), "\"count\":5000");
    size_t at = made.find("\"first\":");
    int64_t first = std::strtoll(made.c_str() + at + 8, nullptr, 10);
    EXPECT(first > 0);
    for (int i = 0; i < 4999; ++i) {
        if (tkde_release(first + i) != TKDE_OK) {
            EXPECT(false);
            break;
        }
    }
    EXPECT(tkde_release(first) == TKDE_E_NOT_FOUND); // released twice
    EXPECT(std::string(tkde_last_error()).find("no object") != std::string::npos);
    // the last one disappears on the Qt side first: release must say NOT_FOUND, not crash
    EXPECT_CONTAINS(tkde_query(("{\"cmd\":\"core.debug.destroy\",\"target\":" + std::to_string(first + 4999) + "}").c_str()),
                    "\"ok\":true");
    EXPECT(tkde_release(first + 4999) == TKDE_E_NOT_FOUND);
    EXPECT(tkde_release(0) == TKDE_E_INVALID_ARG);
    EXPECT(tkde_release(-3) == TKDE_E_INVALID_ARG);
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.debug.objects\"}"), "\"count\":0");
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.release\",\"target\":424242}"), "\"code\":-5");

    // ---- the event queue is bounded and the loss is visible
    while (!event(0).empty()) {
    }
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.debug.emit\",\"type\":\"flood\",\"count\":70000}"), "\"ok\":true");
    EXPECT(tkde_post("{\"cmd\":\"core.ping\",\"req\":99999}") == TKDE_OK);
    tkde_query("{\"cmd\":\"core.ping\"}"); // commands run in order: everything above has been produced now
    std::string marker = event();
    EXPECT_CONTAINS(marker, "\"type\":\"core.overflow\"");
    int64_t dropped = std::strtoll(marker.c_str() + marker.find("\"dropped\":") + 10, nullptr, 10);
    int64_t lastSeq = std::strtoll(marker.c_str() + marker.find("\"last_seq\":") + 11, nullptr, 10);
    int64_t firstSeq = seqOf(marker);
    EXPECT(dropped > 0 && lastSeq - firstSeq + 1 == dropped);
    EXPECT(seqOf(event()) == lastSeq + 1);
    size_t remaining = 2;
    while (!event(0).empty()) {
        ++remaining;
    }
    EXPECT(remaining <= 65536);

    // ---- shutdown: core.quit ends tkde_exec with the code, then the stream ends
    EXPECT(tkde_post("{\"cmd\":\"core.quit\",\"code\":4}") == TKDE_OK);
    std::string quitEvent = event();
    EXPECT_CONTAINS(quitEvent, "\"type\":\"core.quit\"");
    EXPECT_CONTAINS(quitEvent, "\"code\":4");
    EXPECT_CONTAINS(event(), "\"type\":\"core.shutdown\"");
}

int main()
{
    gcInit();
    ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    ::setenv("TKDE_DEBUG_COMMANDS", "1", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);

    // before init
    EXPECT(tkde_post("{\"cmd\":\"core.ping\"}") == TKDE_E_NOT_INITIALIZED);
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.ping\"}"), "\"code\":-3");
    EXPECT(tkde_exec() == TKDE_E_NOT_INITIALIZED);
    EXPECT(tkde_release(1) == TKDE_E_NOT_INITIALIZED);
    EXPECT(tkde_init(nullptr, "x") == TKDE_E_INVALID_ARG);
    EXPECT(tkde_init("", "x") == TKDE_E_INVALID_ARG);

    EXPECT(tkde_init("tinox-ide-test", "Tinox IDE test") == TKDE_OK);
    EXPECT(tkde_init("tinox-ide-test", "Tinox IDE test") == TKDE_E_ALREADY_INITIALIZED);

    std::thread logic(driver);
    const int64_t rc = tkde_exec(); // blocks until core.quit
    EXPECT(rc == 4);
    logic.join();

    // after shutdown: the stream ended, everything is refused
    EXPECT(std::string(event(-1)).empty()); // returns at once even with an infinite timeout
    EXPECT(tkde_post("{\"cmd\":\"core.ping\"}") == TKDE_E_NOT_INITIALIZED);
    EXPECT_CONTAINS(tkde_query("{\"cmd\":\"core.ping\"}"), "\"code\":-3");
    EXPECT(tkde_exec() == TKDE_E_FAILED);

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures.load());
        return 1;
    }
    std::puts("core ok");
    return 0;
}
