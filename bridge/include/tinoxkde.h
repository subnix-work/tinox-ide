/* libtinoxkde: the C ABI between the Tinox IDE and Qt / KDE Frameworks.
 *
 * Version 1. The specification, with the message protocol, lifecycle and
 * versioning rules, is docs/abi.md; this header is its C surface.
 *
 * Only int64_t and UTF-8 NUL-terminated strings cross the boundary. Strings
 * passed in are borrowed for the call; strings returned are GC-allocated copies
 * (or static) that the caller keeps forever and never frees; they are never
 * NULL. Objects are named by int64_t ids assigned by the bridge; no pointer is
 * ever exchanged. Nothing calls back into the caller.
 */
#ifndef TINOXKDE_H
#define TINOXKDE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define TKDE_EXPORT
#else
#define TKDE_EXPORT __attribute__((visibility("default")))
#endif

/** The ABI version this header describes. Bumped only for an incompatible change. */
#define TKDE_ABI_VERSION 1

/** Status codes returned by the int64_t functions (negative on failure). */
enum {
    TKDE_OK = 0,
    TKDE_E_INVALID_ARG = -1,     /* NULL, malformed JSON, bad id */
    TKDE_E_WRONG_THREAD = -2,    /* main-thread function elsewhere, query on the UI thread */
    TKDE_E_NOT_INITIALIZED = -3, /* used before tkde_init or after shutdown */
    TKDE_E_ALREADY_INITIALIZED = -4,
    TKDE_E_NOT_FOUND = -5,   /* unknown or released object id */
    TKDE_E_UNSUPPORTED = -6, /* unknown command */
    TKDE_E_FAILED = -7       /* anything else; tkde_last_error() says what */
};

/** The ABI version the loaded library implements (compare with TKDE_ABI_VERSION). Any thread. */
TKDE_EXPORT int64_t tkde_abi_version(void);

/** Creates the application object. Main thread only. 0 or a TKDE_E_* code. */
TKDE_EXPORT int64_t tkde_init(const char *app_name, const char *display_name);

/** Shows the windows and runs the event loop until quit; blocks the main thread.
 *  Returns the exit code (>= 0) or a TKDE_E_* code. */
TKDE_EXPORT int64_t tkde_exec(void);

/** Asks the event loop to end (same as the core.quit command). Any thread. */
TKDE_EXPORT void tkde_quit(void);

/** Queues a command (a JSON object, see docs/abi.md) for the UI thread, in call order.
 *  0 when queued, else a TKDE_E_* code. Any thread. */
TKDE_EXPORT int64_t tkde_post(const char *json);

/** Runs a command on the UI thread and returns its reply JSON. Blocks until answered.
 *  Not callable from the UI thread (the reply would never be produced). */
TKDE_EXPORT const char *tkde_query(const char *json);

/** The next event as a JSON object, or "" after timeout_ms (< 0 waits forever). Any thread. */
TKDE_EXPORT const char *tkde_next_event(int64_t timeout_ms);

/** Drops the bridge's reference to an object id (same as core.release). Any thread. */
TKDE_EXPORT int64_t tkde_release(int64_t id);

/** Message of the calling thread's last failed call, "" if none. */
TKDE_EXPORT const char *tkde_last_error(void);

/** CLOCK_MONOTONIC in nanoseconds, the clock event "t_ns" fields are stamped with. */
TKDE_EXPORT int64_t tkde_now_ns(void);

/** Value of an environment variable, "" when unset. */
TKDE_EXPORT const char *tkde_getenv(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* TINOXKDE_H */
