// Throughput of the bridge queues. Not a test: run `make bench` and read the numbers.
#include "tinoxkde.h"

#include "gc_thread.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using Clock = std::chrono::steady_clock;

static double seconds(Clock::time_point since)
{
    return std::chrono::duration<double>(Clock::now() - since).count();
}

static void driver()
{
    GcAttach gc;
    while (std::string(tkde_next_event(5000)).find("core.ready") == std::string::npos) {
    }

    // 1. sequential round trips: post a ping, wait for its reply event
    const int roundTrips = 100000;
    auto t0 = Clock::now();
    for (int i = 0; i < roundTrips; ++i) {
        tkde_post("{\"cmd\":\"core.ping\",\"req\":1}");
        while (std::string(tkde_next_event(5000)).find("core.reply") == std::string::npos) {
        }
    }
    double s = seconds(t0);
    std::printf("post + reply event round trips : %8.0f /s  (%.1f us each)\n", roundTrips / s, s / roundTrips * 1e6);

    // 2. synchronous queries
    const int queries = 100000;
    t0 = Clock::now();
    for (int i = 0; i < queries; ++i) {
        tkde_query("{\"cmd\":\"core.ping\"}");
    }
    s = seconds(t0);
    std::printf("tkde_query round trips         : %8.0f /s  (%.1f us each)\n", queries / s, s / queries * 1e6);

    // 3. posted commands without waiting for answers (fire and forget)
    const int posts = 1000000;
    t0 = Clock::now();
    for (int i = 0; i < posts; ++i) {
        tkde_post("{\"cmd\":\"core.ping\"}");
    }
    tkde_query("{\"cmd\":\"core.ping\"}"); // the UI has processed everything before this returns
    s = seconds(t0);
    std::printf("posted commands (no reply)     : %8.0f /s\n", posts / s);

    // 4. event throughput with a concurrent consumer
    const int events = 1000000;
    t0 = Clock::now();
    std::thread producer([&] { GcAttach gcp; tkde_query("{\"cmd\":\"core.debug.emit\",\"type\":\"x\",\"count\":1000000}"); });
    long received = 0;
    long overflow = 0;
    while (received < events) {
        std::string e = tkde_next_event(2000);
        if (e.empty()) {
            break;
        }
        if (e.find("core.overflow") != std::string::npos) {
            overflow += std::strtol(e.c_str() + e.find("\"dropped\":") + 10, nullptr, 10);
            received += std::strtol(e.c_str() + e.find("\"dropped\":") + 10, nullptr, 10);
        } else {
            ++received;
        }
    }
    producer.join();
    s = seconds(t0);
    std::printf("events UI -> consumer          : %8.0f /s  (%ld delivered, %ld lost to overflow)\n", events / s, received - overflow, overflow);

    tkde_post("{\"cmd\":\"core.quit\"}");
}

int main()
{
    gcInit();
    ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    ::setenv("TKDE_DEBUG_COMMANDS", "1", 1);
    ::setenv("TKDE_NO_KCRASH", "1", 1);
    if (tkde_init("bench", "Bench") != 0) {
        std::fprintf(stderr, "init failed: %s\n", tkde_last_error());
        return 1;
    }
    std::thread logic(driver);
    tkde_exec();
    logic.join();
    return 0;
}
