/* ABI contract test, written the way Tinox uses the library: plain C against tinoxkde.h only.
 *
 * The same source is linked once with the real bridge (libtinoxkde, offscreen) and once with
 * the mock (libtinoxkde-mock). Both must behave identically for everything docs/abi.md
 * promises for the core namespace, because the Tinox side is developed against the mock and
 * shipped against the real one. Threading follows the documented pattern: the main thread
 * calls tkde_init and tkde_exec, a second thread plays the Tinox task.
 */
#include "tinoxkde.h"

#define GC_THREADS
#include <gc.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                         \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_HAS(text, part)                                                                      \
    do {                                                                                           \
        const char *t_ = (text);                                                                   \
        if (!t_ || !strstr(t_, (part))) {                                                          \
            fprintf(stderr, "FAIL %s:%d: \"%s\" not in %s\n", __FILE__, __LINE__, (part), t_ ? t_ : "(null)"); \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static long long seq_of(const char *json)
{
    const char *at = strstr(json, "\"seq\":");
    return at ? strtoll(at + 6, NULL, 10) : -1;
}

static long long int_after(const char *json, const char *key)
{
    char marker[64];
    snprintf(marker, sizeof marker, "\"%s\":", key);
    const char *at = strstr(json, marker);
    return at ? strtoll(at + strlen(marker), NULL, 10) : -1;
}

/* A fresh copy: returned strings are only valid until the next call that returns one. */
static char *keep(const char *s)
{
    return strdup(s ? s : "");
}

static void *logic(void *unused)
{
    (void)unused;
    struct GC_stack_base base;
    int attached = GC_get_stack_base(&base) == GC_SUCCESS && GC_register_my_thread(&base) == GC_SUCCESS;

    /* startup: the first event is core.ready, seq 1, stamped with the bridge clock */
    char *ready = keep(tkde_next_event(5000));
    CHECK_HAS(ready, "\"type\":\"core.ready\"");
    CHECK_HAS(ready, "\"t_ns\":");
    CHECK(seq_of(ready) == 1);
    free(ready);

    /* thread rules: the main-thread functions refuse other threads, they do not crash */
    CHECK(tkde_exec() == TKDE_E_WRONG_THREAD);
    CHECK(tkde_init("again", "Again") == TKDE_E_WRONG_THREAD);
    CHECK(strlen(tkde_last_error()) > 0);

    /* last_error is per thread: this thread's message is not the main thread's */
    CHECK(tkde_release(0) == TKDE_E_INVALID_ARG);
    char *mine = keep(tkde_last_error());
    CHECK(strlen(mine) > 0);
    free(mine);

    /* a command by event and by query; payloads come back as JSON, strings escaped */
    CHECK(tkde_post("{\"cmd\":\"core.ping\",\"req\":7,\"payload\":{\"s\":\"a\\\"b\\u00e4\\n\",\"n\":[1,2.5,null,true]}}") == TKDE_OK);
    char *reply = keep(tkde_next_event(5000));
    CHECK_HAS(reply, "\"type\":\"core.reply\"");
    CHECK_HAS(reply, "\"req\":7");
    CHECK_HAS(reply, "\"ok\":true");
    CHECK_HAS(reply, "\"n\":[1,2.5,null,true]");
    CHECK_HAS(reply, "\\\"b");
    free(reply);
    CHECK(tkde_next_event(0)[0] == '\0'); /* nothing else was produced; "" and never NULL */

    char *caps = keep(tkde_query("{\"cmd\":\"core.capabilities\"}"));
    CHECK_HAS(caps, "\"ok\":true");
    CHECK_HAS(caps, "\"abi\":1");
    CHECK_HAS(caps, "\"core\"");
    CHECK_HAS(caps, "\"namespaces\":[");
    free(caps);
    CHECK_HAS(tkde_query("{\"cmd\":\"core.ping\"}"), "\"ok\":true");

    /* errors have codes; nothing throws */
    CHECK_HAS(tkde_query("{\"cmd\":\"nope.nothing\"}"), "\"code\":-6");
    CHECK_HAS(tkde_query("{\"nocmd\":1}"), "\"code\":-1");
    CHECK_HAS(tkde_query("{broken"), "\"code\":-1");
    CHECK_HAS(tkde_query(NULL), "\"code\":-1");
    CHECK(tkde_post(NULL) == TKDE_E_INVALID_ARG);
    CHECK(tkde_post("{not json") == TKDE_E_INVALID_ARG);
    CHECK(tkde_post("{\"cmd\":5}") == TKDE_E_INVALID_ARG);
    CHECK(tkde_post("[]") == TKDE_E_INVALID_ARG);
    CHECK(tkde_post("") == TKDE_E_INVALID_ARG);

    /* an unknown command that was posted is reported later, as an event */
    CHECK(tkde_post("{\"cmd\":\"nope.nothing\"}") == TKDE_OK);
    char *err = keep(tkde_next_event(5000));
    CHECK_HAS(err, "\"type\":\"core.error\"");
    CHECK_HAS(err, "\"cmd\":\"nope.nothing\"");
    free(err);
    CHECK(tkde_post("{\"cmd\":\"nope.nothing\",\"req\":9}") == TKDE_OK);
    char *failed = keep(tkde_next_event(5000));
    CHECK_HAS(failed, "\"core.reply\"");
    CHECK_HAS(failed, "\"req\":9");
    CHECK_HAS(failed, "\"ok\":false");
    free(failed);

    /* ids: positive, assigned by the bridge; unknown or invalid ids are errors, not crashes */
    CHECK(tkde_release(424242) == TKDE_E_NOT_FOUND);
    CHECK(tkde_release(0) == TKDE_E_INVALID_ARG);
    CHECK(tkde_release(-3) == TKDE_E_INVALID_ARG);
    CHECK_HAS(tkde_query("{\"cmd\":\"core.release\",\"target\":424242}"), "\"code\":-5");

    /* ordering: replies in posting order, seq strictly increasing */
    enum { N = 500 };
    for (int i = 0; i < N; ++i) {
        char cmd[128];
        snprintf(cmd, sizeof cmd, "{\"cmd\":\"core.ping\",\"req\":%d,\"payload\":%d}", i, i * 3);
        CHECK(tkde_post(cmd) == TKDE_OK);
    }
    long long last = 0;
    for (int i = 0; i < N; ++i) {
        char *ev = keep(tkde_next_event(5000));
        CHECK(int_after(ev, "req") == i);
        CHECK(int_after(ev, "payload") == i * 3);
        long long seq = seq_of(ev);
        CHECK(seq > last);
        last = seq;
        free(ev);
    }

    /* the clock is monotonic and the environment is readable */
    long long t1 = tkde_now_ns();
    long long t2 = tkde_now_ns();
    CHECK(t1 > 0 && t2 >= t1);
    CHECK(strcmp(tkde_getenv("TKDE_CONTRACT_DOES_NOT_EXIST"), "") == 0);
    setenv("TKDE_CONTRACT_VAR", "value", 1);
    CHECK(strcmp(tkde_getenv("TKDE_CONTRACT_VAR"), "value") == 0);

    /* shutdown: core.quit ends tkde_exec with the code, then core.quit and core.shutdown arrive */
    CHECK(tkde_post("{\"cmd\":\"core.quit\",\"code\":4}") == TKDE_OK);
    char *quit = keep(tkde_next_event(5000));
    CHECK_HAS(quit, "\"type\":\"core.quit\"");
    CHECK_HAS(quit, "\"code\":4");
    free(quit);
    char *down = keep(tkde_next_event(5000));
    CHECK_HAS(down, "\"type\":\"core.shutdown\"");
    free(down);

    if (attached) {
        GC_unregister_my_thread();
    }
    return NULL;
}

int main(void)
{
    GC_INIT();
    GC_allow_register_threads();
    setenv("QT_QPA_PLATFORM", "offscreen", 0); /* the real bridge needs a platform; the mock ignores it */
    setenv("TKDE_NO_KCRASH", "1", 1);

    CHECK(tkde_abi_version() == TKDE_ABI_VERSION);
    CHECK(TKDE_ABI_VERSION == 1);

    /* before tkde_init everything is refused with TKDE_E_NOT_INITIALIZED */
    CHECK(tkde_post("{\"cmd\":\"core.ping\"}") == TKDE_E_NOT_INITIALIZED);
    CHECK_HAS(tkde_query("{\"cmd\":\"core.ping\"}"), "\"code\":-3");
    CHECK(tkde_exec() == TKDE_E_NOT_INITIALIZED);
    CHECK(tkde_release(1) == TKDE_E_NOT_INITIALIZED);
    CHECK(tkde_init(NULL, "x") == TKDE_E_INVALID_ARG);
    CHECK(tkde_init("", "x") == TKDE_E_INVALID_ARG);
    CHECK(tkde_next_event(0) != NULL);

    CHECK(tkde_init("contract-test", "Contract test") == TKDE_OK);
    CHECK(tkde_init("contract-test", "Contract test") == TKDE_E_ALREADY_INITIALIZED);
    CHECK(strlen(tkde_last_error()) > 0);

    pthread_t task;
    if (pthread_create(&task, NULL, logic, NULL) != 0) {
        fprintf(stderr, "cannot start the task thread\n");
        return 2;
    }
    long long rc = tkde_exec(); /* blocks until core.quit */
    CHECK(rc == 4);
    pthread_join(task, NULL);

    /* after shutdown the stream has ended and the library refuses everything */
    CHECK(tkde_next_event(-1)[0] == '\0'); /* returns at once even with an infinite timeout */
    CHECK(tkde_post("{\"cmd\":\"core.ping\"}") == TKDE_E_NOT_INITIALIZED);
    CHECK_HAS(tkde_query("{\"cmd\":\"core.ping\"}"), "\"code\":-3");
    CHECK(tkde_exec() == TKDE_E_FAILED);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("contract ok");
    return 0;
}
