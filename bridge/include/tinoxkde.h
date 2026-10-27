/* libtinoxkde: C ABI between the Tinox IDE and Qt / KDE Frameworks.
 *
 * Placeholder until the ABI specification (docs/abi.md) lands: only the
 * version query exists so the Tinox side can prove that it links the bridge.
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

/** Version of the C ABI this library implements; 0 = pre-specification stub. */
#define TKDE_ABI_VERSION 0

TKDE_EXPORT int64_t tkde_abi_version(void);

/* ---- Spike API (issue #7), provisional: replaced by docs/abi.md (issue #6) ----
 *
 * Threading: tkde_init() and tkde_exec() are called on the process main thread
 * (Qt owns it). Tinox logic runs on another thread and uses only tkde_post(),
 * tkde_next_event() and tkde_quit(), which are safe from any thread.
 * Strings returned to Tinox are GC-allocated copies; strings passed in are
 * copied before the call returns.
 */

/** Creates the QApplication, the main window and the editor. 0 on success. */
TKDE_EXPORT int64_t tkde_init(void);
/** Shows the window and runs Qt's event loop until quit. Blocks the calling (main) thread. */
TKDE_EXPORT int64_t tkde_exec(void);
/** Queues a command (JSON object) for the UI thread. Any thread. */
TKDE_EXPORT void tkde_post(const char *json);
/** Next UI event as a JSON object, or "" after timeout_ms (< 0 waits forever). Any thread. */
TKDE_EXPORT const char *tkde_next_event(int64_t timeout_ms);
/** Value of an environment variable (GC-allocated copy), "" when unset. */
TKDE_EXPORT const char *tkde_getenv(const char *name);
/** Monotonic clock in nanoseconds (CLOCK_MONOTONIC), the clock event timestamps use. */
TKDE_EXPORT int64_t tkde_now_ns(void);
/** Asks the event loop to end. Any thread. */
TKDE_EXPORT void tkde_quit(void);

#ifdef __cplusplus
}
#endif

#endif /* TINOXKDE_H */
