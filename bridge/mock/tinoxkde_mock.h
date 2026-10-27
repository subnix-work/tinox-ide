/* Scripting interface of libtinoxkde-mock: the headless stand-in for the bridge.
 *
 * The mock exports the whole ABI of tinoxkde.h (same names, same behaviour for
 * the core namespace) without Qt, KDE or a display, so the Tinox side can be
 * developed and tested anywhere. Tests drive it with the functions below:
 * inject events, read back the commands the code under test posted, and script
 * replies. Not part of the ABI; the real bridge does not export these.
 *
 * Recording and replay (environment variables, read at tkde_init):
 *   TKDE_MOCK_RECORD=file   write every command, query and event as JSON lines
 *   TKDE_MOCK_REPLAY=file   deliver the "event" lines of such a file instead of generating events
 */
#ifndef TINOXKDE_MOCK_H
#define TINOXKDE_MOCK_H

#include "tinoxkde.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Forgets everything: queues, scripts, objects, initialisation. Call between tests. */
TKDE_EXPORT void mock_reset(void);

/** Injects an event as if the UI had produced it. "seq" and "t_ns" are added when absent. */
TKDE_EXPORT int64_t mock_push_event(const char *json);

/** The next command recorded from tkde_post / tkde_query, in call order; "" after timeout_ms. */
TKDE_EXPORT const char *mock_next_command(int64_t timeout_ms);

/** Number of recorded commands not yet read with mock_next_command. */
TKDE_EXPORT int64_t mock_pending_commands(void);

/** Scripts what the mock does when a command named `cmd` arrives: `reply_json` is merged into the
 *  reply object ("" = just {"ok":true}), `event_json` ("" = none) is pushed as an event afterwards. */
TKDE_EXPORT int64_t mock_on_command(const char *cmd, const char *reply_json, const char *event_json);

/** Registers an object id as existing (so core.release / targets resolve). Returns the new id. */
TKDE_EXPORT int64_t mock_create_object(void);

/** Adds a namespace to the core.capabilities answer. */
TKDE_EXPORT int64_t mock_add_namespace(const char *name);

/** Number of events dropped by queue overflow so far. */
TKDE_EXPORT int64_t mock_dropped_events(void);

/** Makes tkde_exec return as if the last window had been closed. Any thread. */
TKDE_EXPORT void mock_close_last_window(void);

#ifdef __cplusplus
}
#endif

#endif /* TINOXKDE_MOCK_H */
