/* Compiles the public header as C11 and uses every declaration. */
#include "tinoxkde.h"

#include <stddef.h>

_Static_assert(TKDE_ABI_VERSION == 1, "the ABI version in the header and in docs/abi.md must agree");
_Static_assert(TKDE_E_FAILED < TKDE_OK, "error codes are negative");

int tkde_header_c_uses_everything(void)
{
    int64_t status = tkde_init("app", "App") + tkde_exec() + tkde_post("{}") + tkde_release(1) + tkde_abi_version() + tkde_now_ns();
    const char *s = tkde_query("{}");
    s = tkde_next_event(0);
    s = tkde_last_error();
    s = tkde_getenv("HOME");
    tkde_quit();
    return (int)status + (s != NULL);
}
