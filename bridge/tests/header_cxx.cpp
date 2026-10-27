// Compiles the public header as C++ and checks its linkage and types.
#include "tinoxkde.h"

#include <type_traits>

static_assert(std::is_same_v<decltype(tkde_post("{}")), int64_t>);
static_assert(std::is_same_v<decltype(tkde_next_event(0)), const char *>);
static_assert(TKDE_ABI_VERSION == 1);

int main()
{
    return 0;
}
