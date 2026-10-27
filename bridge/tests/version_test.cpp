#include "tinoxkde.h"

#include <cstdio>

int main()
{
    if (tkde_abi_version() != TKDE_ABI_VERSION) {
        std::fprintf(stderr, "ABI version mismatch: library %lld, header %d\n", static_cast<long long>(tkde_abi_version()),
                     TKDE_ABI_VERSION);
        return 1;
    }
    return 0;
}
